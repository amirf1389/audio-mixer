#include "Net.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QThread>

#include "Engine.h"

using namespace am::net;
static constexpr int kNetStrips = am::kMaxStrips;   // the console runs every strip the engine has

Net::Net(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  map_.load(s_.value("net/midiMap").toString().toStdString());
  connect(engine_, &Engine::stateChanged, this, &Net::attach);
  timer_.setInterval(500); connect(&timer_, &QTimer::timeout, this, [this] { emit statsChanged(); }); timer_.start();
}

Net::~Net() {
  patch(false);
  if (am::Runtime* rt = engine_->runtime()) { rt->setNetTap(nullptr); rt->setNetInput(nullptr); QThread::msleep(40); }
  tx_.stop(); rx_.stop(); disc_.stop(); rtpMidi_.stop(); ports_.close();
}

QStringList Net::cards() const { QStringList l; for (const std::string& a : localAddresses()) l << QString::fromStdString(a); return l; }

// The engine makes a new am::Runtime at every device open and drops it at stop. The sender's rate must be the runtime's: restart it when it changed.
void Net::attach() {
  am::Runtime* rt = engine_->runtime(); const double rate = rt ? rt->sampleRate() : 0;
  if (rt == rt_ && rate == rate_) return;
  rt_ = rt; rate_ = rate;
  if (!rt) return;
  if (txWanted_ && (!tx_.running() || tx_.desc().rate != static_cast<int>(rate + 0.5))) { tx_.stop(); startSend(); }
  else if (tx_.running()) rt->setNetTap(&tx_);
  if (rx_.running()) rt->setNetInput(&rx_);
}

bool Net::startSend() {
  stopSend(); txWanted_ = true;
  am::Runtime* rt = engine_->runtime(); if (!rt) { fail(tr("Start the sound card first: the stream carries the mixer's master.")); return false; }
  StreamDesc d; d.name = txName().toStdString(); d.address = txAddress().toStdString(); d.port = txPort(); d.channels = txChannels(); d.bits = txBits(); d.rate = static_cast<int>(rt->sampleRate() + 0.5);
  d.sessionId = static_cast<std::uint32_t>(QDateTime::currentSecsSinceEpoch());
  std::string err; if (!tx_.start(d, card().toStdString(), true, err)) { txWanted_ = false; fail(QString::fromStdString(err)); return false; }
  rt->setNetTap(&tx_); rt_ = rt; rate_ = rt->sampleRate(); msg_.clear(); emit changed(); return true;
}
void Net::stopSend() { txWanted_ = false; if (!tx_.running()) return; if (am::Runtime* rt = engine_->runtime()) { rt->setNetTap(nullptr); QThread::msleep(30); } tx_.stop(); emit changed(); }

bool Net::startReceive() {
  stopReceive();
  StreamDesc d; d.address = rxAddress().toStdString(); d.port = rxPort(); d.channels = rxChannels(); d.bits = rxBits(); d.rate = rxRate();
  std::string err; if (!rx_.start(d, card().toStdString(), rxLatency(), err)) { fail(QString::fromStdString(err)); return false; }
  if (am::Runtime* rt = engine_->runtime()) rt->setNetInput(&rx_);
  patch(true); msg_.clear(); emit changed(); return true;
}
void Net::stopReceive() { if (!rx_.running()) return; patch(false); if (am::Runtime* rt = engine_->runtime()) { rt->setNetInput(nullptr); QThread::msleep(30); } rx_.stop(); emit changed(); }

// network channel k -> strip rxFirstStrip + k; switching the receiver off gives those strips their own device input back
void Net::patch(bool on) {
  for (int strip : patched_) engine_->setInputRoute(strip, strip);
  patched_.clear();
  const int first = rxFirstStrip(); if (!on || first < 1 || !rx_.running()) return;
  for (int k = 0; k < std::min(rxChannels(), am::Runtime::kNetInputs); ++k) { const int strip = first - 1 + k; if (strip >= kNetStrips) break; engine_->setInputRoute(strip, am::Runtime::kNetInBase + k); patched_ << strip; }
}
void Net::setRxFirstStrip(int v) { s_.setValue("net/rxFirstStrip", std::clamp(v, 0, kNetStrips)); patch(true); emit changed(); }

QVariantMap Net::stats() const {
  QVariantMap m; const SendStats t = tx_.stats(); const RecvStats r = rx_.stats();
  m["txPackets"] = static_cast<qulonglong>(t.packets); m["txDropped"] = static_cast<qulonglong>(t.droppedFrames); m["txMismatch"] = t.rateMismatch; m["txRate"] = tx_.desc().rate;
  m["rxPackets"] = static_cast<qulonglong>(r.packets); m["rxLost"] = static_cast<qulonglong>(r.lost); m["rxLate"] = static_cast<qulonglong>(r.late); m["rxUnderruns"] = static_cast<qulonglong>(r.underruns);
  m["rxFillMs"] = r.fillMs; m["rxIdleMs"] = r.idleMs; m["rxFrom"] = QString::fromStdString(r.from); m["rxLive"] = r.running && r.idleMs >= 0 && r.idleMs < 1000;
  const RtpMidiStats ms = rtpMidi_.stats(); m["midiPeers"] = ms.peers; m["midiPeer"] = QString::fromStdString(ms.lastPeer);
  return m;
}

bool Net::setDiscover(bool on) {
  if (!on) { disc_.stop(); emit changed(); return true; }
  std::string err; if (!disc_.start(card().toStdString(), err)) { fail(QString::fromStdString(err)); return false; }
  msg_.clear(); emit changed(); return true;
}
QVariantList Net::streams() const {
  QVariantList l;
  for (const Found& f : disc_.list()) { QVariantMap m; m["name"] = QString::fromStdString(f.d.name); m["origin"] = QString::fromStdString(f.d.origin); m["address"] = QString::fromStdString(f.d.address); m["port"] = f.d.port;
    m["channels"] = f.d.channels; m["bits"] = f.d.bits; m["rate"] = f.d.rate; m["clock"] = QString::fromStdString(f.d.refClock); m["age"] = f.ageMs / 1000; l << m; }
  return l;
}
void Net::useStream(int index) {
  const std::vector<Found> l = disc_.list(); if (index < 0 || index >= static_cast<int>(l.size())) return;
  const StreamDesc& d = l[static_cast<std::size_t>(index)].d;
  s_.setValue("net/rxAddress", QString::fromStdString(d.address)); s_.setValue("net/rxPort", d.port); s_.setValue("net/rxChannels", std::clamp(d.channels, 1, 16)); s_.setValue("net/rxBits", d.bits == 16 ? 16 : 24); s_.setValue("net/rxRate", d.rate);
  emit changed();
}
QVariantList Net::transports() const { return QJsonDocument::fromJson(QByteArray::fromStdString(transportsJson())).array().toVariantList(); }

// ---- MIDI
am::net::MidiSink Net::sink() {   // called on a network or driver thread: hand the message to the interface thread
  return [this](const MidiMsg& m) { const int st = m.status, a = m.d1, b = m.d2; QMetaObject::invokeMethod(this, [this, st, a, b] { midiIn(st, a, b); }, Qt::QueuedConnection); };
}
void Net::setMidiLan(bool v) { s_.setValue("net/midiLan", v); if (rtpMidi_.running()) setMidiNet(true); emit changed(); }
bool Net::setMidiNet(bool on) {
  rtpMidi_.stop(); if (!on) { emit changed(); return true; }
  std::string err; if (!rtpMidi_.start(txName().toStdString(), midiLan() ? std::string() : std::string("127.0.0.1"), midiNetPort(), sink(), err)) { fail(QString::fromStdString(err)); return false; }
  msg_.clear(); emit changed(); return true;
}
QVariantList Net::midiInputs() const { QVariantList l; for (const MidiPortInfo& p : am::net::midiInputs()) { QVariantMap m; m["id"] = QString::fromStdString(p.id); m["name"] = QString::fromStdString(p.name); l << m; } return l; }
bool Net::openMidi(const QString& id) {
  ports_.close(); if (id.isEmpty()) { emit changed(); return true; }
  std::string err; if (!ports_.open(id.toStdString(), sink(), err)) { fail(QString::fromStdString(err)); return false; }
  msg_.clear(); emit changed(); return true;
}
void Net::learn(const QString& kind, int strip) {
  const MidiAction::Kind k = kind == "fader" ? MidiAction::Fader : kind == "pan" ? MidiAction::Pan : kind == "mute" ? MidiAction::Mute : kind == "solo" ? MidiAction::Solo : kind == "master" ? MidiAction::Master : MidiAction::None;
  map_.learn(k, std::clamp(strip, 0, kNetStrips - 1)); emit statsChanged();
}
void Net::clearLearned() { map_.clear(); s_.setValue("net/midiMap", QString()); emit statsChanged(); }

void Net::midiIn(int status, int d1, int d2) {
  const MidiMsg m{static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(d1 & 0x7f), static_cast<std::uint8_t>(d2 & 0x7f)};
  ++midiCount_;
  const int type = m.type(); const char* tn = type == 0xb0 ? "CC" : type == 0x90 ? "NOTE ON" : type == 0x80 ? "NOTE OFF" : type == 0xe0 ? "BEND" : type == 0xc0 ? "PROGRAM" : "MIDI";
  midiLast_ = QStringLiteral("%1 %2 = %3 · ch %4").arg(QLatin1String(tn)).arg(m.d1).arg(m.d2).arg(m.channel() + 1);
  if (map_.offer(m)) { s_.setValue("net/midiMap", QString::fromStdString(map_.save())); emit statsChanged(); return; }
  const MidiAction a = map_.apply(m);
  if (a.strip < 0 || a.strip >= kNetStrips) return;
  switch (a.kind) {
    case MidiAction::Fader: engine_->setGain(a.strip, a.value); break;
    case MidiAction::Pan: engine_->setPan(a.strip, a.value); break;
    case MidiAction::Master: engine_->setMaster(a.value); break;
    case MidiAction::Mute: engine_->setMute(a.strip, !engine_->stripState()[a.strip].toMap()["mute"].toBool()); break;
    case MidiAction::Solo: engine_->setSolo(a.strip, !engine_->stripState()[a.strip].toMap()["solo"].toBool()); break;
    default: break;
  }
}
