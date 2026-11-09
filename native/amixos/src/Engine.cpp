#include "Engine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

using am::dsp::FilterType;
using am::dsp::XoverType;

static double toDb(double lin) { return lin > 1e-9 ? 20.0 * std::log10(lin) : -120.0; }

Engine::Engine(QObject* parent) : QObject(parent) {
  strips_.resize(kStrips);
  for (int i = 0; i < kStrips; ++i) strips_[static_cast<std::size_t>(i)].name = QStringLiteral("CH %1").arg(i + 1, 2, 10, QLatin1Char('0'));
  load();
  refreshDevices();
  timer_.setInterval(33);
  connect(&timer_, &QTimer::timeout, this, &Engine::tick);
  tick();
}
Engine::~Engine() { save(); stop(); }

QStringList Engine::backends() const { QStringList l; for (const auto& b : am::availableBackends()) l << QString::fromStdString(b); return l; }

void Engine::refreshDevices() {
  devices_.clear();
  for (const am::DeviceInfo& d : am::listDevices()) {
    QVariantMap m; m["backend"] = QString::fromStdString(d.backend); m["id"] = QString::fromStdString(d.id); m["name"] = QString::fromStdString(d.name);
    m["inChannels"] = d.inChannels; m["outChannels"] = d.outChannels; m["defaultRate"] = d.defaultRate; m["defaultIn"] = d.defaultIn; m["defaultOut"] = d.defaultOut;
    QVariantList rates; for (double r : d.sampleRates) rates << r; m["sampleRates"] = rates;
    devices_ << m;
  }
  emit devicesChanged();
}

bool Engine::open(const QString& backend, const QString& inDevice, const QString& outDevice, double sampleRate, int frames, int inChannels, int outChannels) {
  stop();
  rt_ = std::make_unique<am::Runtime>(kStrips);
  am::DeviceConfig cfg; cfg.backend = backend.isEmpty() ? "auto" : backend.toStdString(); cfg.inDevice = inDevice.toStdString(); cfg.outDevice = outDevice.toStdString();
  cfg.sampleRate = sampleRate > 0 ? sampleRate : 48000; cfg.frames = static_cast<unsigned>(frames > 0 ? frames : 256); cfg.inChannels = inChannels; cfg.outChannels = outChannels;
  std::string err;
  if (!rt_->open(cfg, err) || !rt_->start(err)) { status_ = QStringLiteral("Could not open the sound card: %1").arg(QString::fromStdString(err)); rt_.reset(); emit stateChanged(); return false; }
  for (auto& s : strips_) s.dsp.setSampleRate(cfg.sampleRate);
  pushAll();
  status_ = QStringLiteral("%1 • %2 Hz • %3 frames • %4 in / %5 out").arg(QString::fromStdString(rt_->backendName())).arg(cfg.sampleRate).arg(cfg.frames).arg(inChannels).arg(outChannels);
  timer_.start(); emit stateChanged(); return true;
}
void Engine::stop() { if (rt_) { rt_->stop(); rt_.reset(); status_ = QStringLiteral("Stopped"); timer_.stop(); emit stateChanged(); } }

void Engine::pushAll() {
  if (!rt_) return;
  for (int i = 0; i < kStrips; ++i) {
    const StripUi& s = strips_[static_cast<std::size_t>(i)];
    rt_->setGainDb(i, s.db); rt_->setPan(i, s.pan); rt_->setMute(i, s.mute); rt_->setSolo(i, s.solo);
    const am::dsp::ChannelParams& p = s.dsp.params();
    for (int b = 0; b < am::dsp::kEqBands; ++b) { const auto& e = p.eq[static_cast<std::size_t>(b)]; rt_->setEqBand(i, b, e.type, e.freq, e.q, e.gainDb); }
    rt_->setDelayMs(i, p.delayMs); rt_->setPolarity(i, p.polarityInvert); rt_->setCrossover(i, p.xoverType, p.hpOrder, p.hpFreq, p.lpOrder, p.lpFreq); rt_->setChannelLimiter(i, p.limiterOn, p.limiterCeilDb, p.limiterReleaseMs);
    rt_->setCompressor(i, p.compOn, p.compThresholdDb, p.compRatio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, p.compMakeupDb); rt_->setGate(i, p.gateOn, p.gateThresholdDb, p.gateRangeDb, p.gateAttackMs, p.gateHoldMs, p.gateReleaseMs);
  }
  rt_->setMasterDb(masterDb_); rt_->setLimiter(masterLim_, masterCeil_);
}

QVariantList Engine::stripState() const {
  QVariantList l;
  for (int i = 0; i < kStrips; ++i) { const StripUi& s = strips_[static_cast<std::size_t>(i)]; QVariantMap m; m["index"] = i; m["name"] = s.name; m["db"] = s.db; m["pan"] = s.pan; m["mute"] = s.mute; m["solo"] = s.solo; m["dsp"] = s.dsp.active(); l << m; }
  return l;
}
void Engine::setGain(int i, double db) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].db = std::clamp(db, -60.0, 10.0); if (rt_) rt_->setGainDb(i, strips_[static_cast<std::size_t>(i)].db); emit stripsChanged(); }
void Engine::setPan(int i, double p) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].pan = std::clamp(p, -1.0, 1.0); if (rt_) rt_->setPan(i, strips_[static_cast<std::size_t>(i)].pan); emit stripsChanged(); }
void Engine::setMute(int i, bool on) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].mute = on; if (rt_) rt_->setMute(i, on); emit stripsChanged(); }
void Engine::setSolo(int i, bool on) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].solo = on; if (rt_) rt_->setSolo(i, on); emit stripsChanged(); }
void Engine::setName(int i, const QString& n) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].name = n.left(24); emit stripsChanged(); }
void Engine::setMaster(double db) { masterDb_ = std::clamp(db, -60.0, 10.0); if (rt_) rt_->setMasterDb(masterDb_); emit stripsChanged(); }
void Engine::setMasterLimiter(bool on, double c) { masterLim_ = on; masterCeil_ = c; if (rt_) rt_->setLimiter(on, c); emit stripsChanged(); }
void Engine::clearClips() { if (rt_) rt_->clearClips(); }

void Engine::setEq(int i, int band, int type, double f, double q, double g) {
  if (i < 0 || i >= kStrips || band < 0 || band >= am::dsp::kEqBands) return;
  const am::dsp::EqBand b{static_cast<FilterType>(std::clamp(type, 0, 8)), std::clamp(f, 10.0, 24000.0), std::clamp(q, 0.1, 36.0), std::clamp(g, -24.0, 24.0)};
  strips_[static_cast<std::size_t>(i)].dsp.setEqBand(band, b);
  if (rt_) rt_->setEqBand(i, band, b.type, b.freq, b.q, b.gainDb);
  emit stripsChanged();
}
QVariantMap Engine::eqBand(int i, int band) const {
  QVariantMap m; if (i < 0 || i >= kStrips || band < 0 || band >= am::dsp::kEqBands) return m;
  const auto& b = strips_[static_cast<std::size_t>(i)].dsp.params().eq[static_cast<std::size_t>(band)];
  m["type"] = static_cast<int>(b.type); m["freq"] = b.freq; m["q"] = b.q; m["gainDb"] = b.gainDb; return m;
}
void Engine::setDelay(int i, double ms) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dsp.setDelayMs(ms); if (rt_) rt_->setDelayMs(i, strips_[static_cast<std::size_t>(i)].dsp.params().delayMs); emit stripsChanged(); }
void Engine::setPolarity(int i, bool inv) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dsp.setPolarity(inv); if (rt_) rt_->setPolarity(i, inv); emit stripsChanged(); }
void Engine::setCrossover(int i, int type, int hpO, double hpF, int lpO, double lpF) {
  if (i < 0 || i >= kStrips) return; const XoverType t = static_cast<XoverType>(std::clamp(type, 0, 2));
  strips_[static_cast<std::size_t>(i)].dsp.setCrossover(t, std::clamp(hpO, 0, 8), std::clamp(hpF, 10.0, 24000.0), std::clamp(lpO, 0, 8), std::clamp(lpF, 10.0, 24000.0));
  if (rt_) { const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params(); rt_->setCrossover(i, p.xoverType, p.hpOrder, p.hpFreq, p.lpOrder, p.lpFreq); }
  emit stripsChanged();
}
void Engine::setChannelLimiter(int i, bool on, double c, double r) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dsp.setLimiter(on, std::clamp(c, -30.0, 0.0), std::clamp(r, 1.0, 2000.0)); if (rt_) rt_->setChannelLimiter(i, on, c, r); emit stripsChanged(); }
void Engine::setCompressor(int i, bool on, double thr, double ratio, double knee, double att, double rel, double makeup) {
  if (i < 0 || i >= kStrips) return;
  strips_[static_cast<std::size_t>(i)].dsp.setCompressor(on, std::clamp(thr, -60.0, 0.0), std::clamp(ratio, 1.0, 20.0), std::clamp(knee, 0.0, 24.0), std::clamp(att, 0.1, 200.0), std::clamp(rel, 5.0, 2000.0), std::clamp(makeup, 0.0, 24.0));
  if (rt_) { const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params(); rt_->setCompressor(i, p.compOn, p.compThresholdDb, p.compRatio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, p.compMakeupDb); }
  emit stripsChanged();
}
void Engine::setGate(int i, bool on, double thr, double range, double att, double hold, double rel) {
  if (i < 0 || i >= kStrips) return;
  strips_[static_cast<std::size_t>(i)].dsp.setGate(on, std::clamp(thr, -90.0, 0.0), std::clamp(range, -90.0, 0.0), std::clamp(att, 0.1, 100.0), std::clamp(hold, 0.0, 2000.0), std::clamp(rel, 5.0, 3000.0));
  if (rt_) { const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params(); rt_->setGate(i, p.gateOn, p.gateThresholdDb, p.gateRangeDb, p.gateAttackMs, p.gateHoldMs, p.gateReleaseMs); }
  emit stripsChanged();
}
QVariantList Engine::compressorCurve(int i, int points) const {
  QVariantList l; if (i < 0 || i >= kStrips) return l; points = std::clamp(points, 8, 256); const auto& c = strips_[static_cast<std::size_t>(i)].dsp.compressor();
  for (int k = 0; k < points; ++k) { const double in = -60.0 + 60.0 * k / (points - 1); QVariantMap m; m["in"] = in; m["out"] = c.computeDb(in); l << m; }
  return l;
}
QVariantList Engine::gateCurve(int i, int points) const {
  QVariantList l; if (i < 0 || i >= kStrips) return l; points = std::clamp(points, 8, 256); const auto& g = strips_[static_cast<std::size_t>(i)].dsp.gate();
  for (int k = 0; k < points; ++k) { const double in = -90.0 + 90.0 * k / (points - 1); QVariantMap m; m["in"] = in; m["out"] = g.computeDb(in); l << m; }
  return l;
}
QVariantMap Engine::channelParams(int i) const {
  QVariantMap m; if (i < 0 || i >= kStrips) return m; const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params();
  m["compOn"] = p.compOn; m["compThresholdDb"] = p.compThresholdDb; m["compRatio"] = p.compRatio; m["compKneeDb"] = p.compKneeDb; m["compAttackMs"] = p.compAttackMs; m["compReleaseMs"] = p.compReleaseMs; m["compMakeupDb"] = p.compMakeupDb;
  m["gateOn"] = p.gateOn; m["gateThresholdDb"] = p.gateThresholdDb; m["gateRangeDb"] = p.gateRangeDb; m["gateAttackMs"] = p.gateAttackMs; m["gateHoldMs"] = p.gateHoldMs; m["gateReleaseMs"] = p.gateReleaseMs;
  m["polarityInvert"] = p.polarityInvert; m["delayMs"] = p.delayMs; m["xoverType"] = static_cast<int>(p.xoverType); m["hpOrder"] = p.hpOrder; m["hpFreq"] = p.hpFreq; m["lpOrder"] = p.lpOrder; m["lpFreq"] = p.lpFreq;
  m["limiterOn"] = p.limiterOn; m["limiterCeilDb"] = p.limiterCeilDb; m["limiterReleaseMs"] = p.limiterReleaseMs; return m;
}
QVariantList Engine::responseCurve(int i, int points) const {
  QVariantList l; if (i < 0 || i >= kStrips) return l; points = std::clamp(points, 8, 512);
  for (int k = 0; k < points; ++k) { const double f = 20.0 * std::pow(1000.0, static_cast<double>(k) / (points - 1)); QVariantMap m; m["f"] = f; m["db"] = strips_[static_cast<std::size_t>(i)].dsp.gainDbAt(f); l << m; }
  return l;
}

QVariantList Engine::pages() const {
  // the pages of the console, grouped as the AmF X1 interface was (same words, same order); `ready` = carried over in this build
  struct P { const char* group; const char* id; const char* text; const char* icon; bool ready; };
  static const P k[] = {
    {"START", "home", "HOME", "⌂", true},
    {"MIX", "mixer", "MIXER", "☰", true}, {"MIX", "arrange", "ARRANGE", "≡", false}, {"MIX", "dsp", "TONE (EQ)", "∿", true}, {"MIX", "dyn", "COMPRESSOR", "⩏", false}, {"MIX", "gate", "NOISE GATE", "▣", false},
    {"MIX", "fx", "EFFECTS", "✨", false}, {"MIX", "bus", "SEND TO", "⇉", false}, {"MIX", "dca", "GROUP FADERS", "☷", false}, {"MIX", "stereopan", "SURROUND", "⦿", false},
    {"ROUTE", "io", "CONNECTIONS", "⚡", true}, {"ROUTE", "routing", "ROUTING", "⎇", false},
    {"METER", "meter", "LEVELS", "▓", true}, {"METER", "lufs", "LOUDNESS", "◎", true}, {"METER", "osc", "WAVEFORM", "∽", false}, {"METER", "phase", "PHASE", "☯", false},
    {"SOURCE", "media", "STREAM & MEDIA", "▶", false}, {"SOURCE", "live", "LIVE SOURCES", "☉", false}, {"SOURCE", "miceq", "MIC EQ", "♪", false}, {"SOURCE", "ai", "AI MIX", "⚙", false}, {"SOURCE", "tools", "TOOLS", "⚒", false}, {"SOURCE", "plugins", "PLUGINS", "⬡", false},
    {"DEVICE", "asio", "SOUND CARD", "◉", false}, {"DEVICE", "bluetooth", "BLUETOOTH", "⮁", false}, {"DEVICE", "network", "NETWORK", "⮞", false}, {"DEVICE", "cloud", "CLOUD", "☁", false}, {"DEVICE", "cascade", "CASCADE", "⚭", false},
    {"SYSTEM", "presets", "PRESETS", "⬇", false}, {"SYSTEM", "sys", "SETTINGS", "⚙", true}, {"SYSTEM", "acct", "ACCOUNT", "☺", false}, {"SYSTEM", "access", "ACCESS", "⚿", false}, {"SYSTEM", "renew", "LICENSE", "★", false}, {"SYSTEM", "admin", "ADMIN", "⚑", false}, {"SYSTEM", "calib", "CALIBRATE", "⌖", false},
    {"HELP", "help", "HELP", "?", false}, {"HELP", "scenes", "SCENES", "⚐", false}, {"SYSTEM", "boot", "BOOT / RESET", "⏻", false},
  };
  QVariantList l; for (const P& p : k) { QVariantMap m; m["group"] = p.group; m["id"] = p.id; m["text"] = p.text; m["icon"] = QString::fromUtf8(p.icon); m["ready"] = p.ready; l << m; }
  return l;
}

void Engine::tick() {
  QVariantList m; QVariantMap t;
  if (rt_) {
    const am::Snapshot s = rt_->snapshot();
    for (int i = 0; i < kStrips; ++i) { QVariantMap x; x["peakDb"] = toDb(s.strips[i].pk); x["rmsDb"] = toDb(s.strips[i].rms); x["holdDb"] = toDb(s.strips[i].hold); x["clip"] = s.strips[i].clipN > 0; x["compGrDb"] = s.strips[i].compGrDb; x["gateDb"] = s.strips[i].gateDb; m << x; }
    t["masterPeakDb"] = toDb(std::max(s.master[0].pk, s.master[1].pk)); t["masterLDb"] = toDb(s.master[0].pk); t["masterRDb"] = toDb(s.master[1].pk); t["masterRmsDb"] = toDb(std::max(s.master[0].rms, s.master[1].rms));
    t["masterClip"] = s.master[0].clipN + s.master[1].clipN > 0; t["momentary"] = s.momentary; t["shortTerm"] = s.shortTerm; t["integrated"] = s.integrated; t["lra"] = s.lra; t["truePeak"] = s.truePeak; t["correlation"] = s.correlation;
    t["blocks"] = static_cast<qulonglong>(s.blocks); t["frames"] = static_cast<qulonglong>(s.frames); t["xruns"] = static_cast<qulonglong>(s.xruns);
  } else {
    for (int i = 0; i < kStrips; ++i) { QVariantMap x; x["peakDb"] = -120.0; x["rmsDb"] = -120.0; x["holdDb"] = -120.0; x["clip"] = false; x["compGrDb"] = 0.0; x["gateDb"] = 0.0; m << x; }
    t["masterPeakDb"] = -120.0; t["masterLDb"] = -120.0; t["masterRDb"] = -120.0; t["masterRmsDb"] = -120.0; t["masterClip"] = false; t["momentary"] = -120.0; t["shortTerm"] = -120.0; t["integrated"] = -120.0; t["lra"] = 0.0; t["truePeak"] = -120.0; t["correlation"] = 0.0; t["blocks"] = 0; t["frames"] = 0; t["xruns"] = 0;
  }
  meters_ = m; telemetry_ = t; emit metersChanged();
}

// ── persistence: one JSON document in QSettings (strip names, levels, pan, mute, solo, channel DSP, master) ──
void Engine::save() {
  QJsonArray arr;
  for (const StripUi& s : strips_) {
    const auto& p = s.dsp.params(); QJsonObject o; o["name"] = s.name; o["db"] = s.db; o["pan"] = s.pan; o["mute"] = s.mute; o["solo"] = s.solo;
    o["polarity"] = p.polarityInvert; o["delayMs"] = p.delayMs; o["xoverType"] = static_cast<int>(p.xoverType); o["hpOrder"] = p.hpOrder; o["hpFreq"] = p.hpFreq; o["lpOrder"] = p.lpOrder; o["lpFreq"] = p.lpFreq;
    o["limOn"] = p.limiterOn; o["limCeil"] = p.limiterCeilDb; o["limRel"] = p.limiterReleaseMs;
    o["compOn"] = p.compOn; o["compThr"] = p.compThresholdDb; o["compRatio"] = p.compRatio; o["compKnee"] = p.compKneeDb; o["compAtt"] = p.compAttackMs; o["compRel"] = p.compReleaseMs; o["compMakeup"] = p.compMakeupDb;
    o["gateOn"] = p.gateOn; o["gateThr"] = p.gateThresholdDb; o["gateRange"] = p.gateRangeDb; o["gateAtt"] = p.gateAttackMs; o["gateHold"] = p.gateHoldMs; o["gateRel"] = p.gateReleaseMs;
    QJsonArray eq; for (const auto& b : p.eq) { QJsonObject e; e["t"] = static_cast<int>(b.type); e["f"] = b.freq; e["q"] = b.q; e["g"] = b.gainDb; eq << e; } o["eq"] = eq;
    arr << o;
  }
  QJsonObject root; root["strips"] = arr; root["masterDb"] = masterDb_; root["masterLim"] = masterLim_; root["masterCeil"] = masterCeil_; root["v"] = 1;
  settings_.setValue("console/state", QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}
void Engine::load() {
  const QJsonObject root = QJsonDocument::fromJson(settings_.value("console/state").toString().toUtf8()).object();
  if (root.isEmpty()) return;
  masterDb_ = std::clamp(root.value("masterDb").toDouble(0), -60.0, 10.0); masterLim_ = root.value("masterLim").toBool(true); masterCeil_ = std::clamp(root.value("masterCeil").toDouble(-0.5), -12.0, 0.0);
  const QJsonArray arr = root.value("strips").toArray();
  for (int i = 0; i < kStrips && i < arr.size(); ++i) {
    const QJsonObject o = arr[i].toObject(); StripUi& s = strips_[static_cast<std::size_t>(i)];
    if (o.contains("name")) s.name = o.value("name").toString().left(24); s.db = std::clamp(o.value("db").toDouble(0), -60.0, 10.0); s.pan = std::clamp(o.value("pan").toDouble(0), -1.0, 1.0); s.mute = o.value("mute").toBool(); s.solo = o.value("solo").toBool();
    am::dsp::ChannelParams p; p.polarityInvert = o.value("polarity").toBool(); p.delayMs = std::clamp(o.value("delayMs").toDouble(0), 0.0, 50.0); p.xoverType = static_cast<XoverType>(std::clamp(o.value("xoverType").toInt(1), 0, 2));
    p.hpOrder = std::clamp(o.value("hpOrder").toInt(0), 0, 8); p.hpFreq = std::clamp(o.value("hpFreq").toDouble(80), 10.0, 24000.0); p.lpOrder = std::clamp(o.value("lpOrder").toInt(0), 0, 8); p.lpFreq = std::clamp(o.value("lpFreq").toDouble(4000), 10.0, 24000.0);
    p.limiterOn = o.value("limOn").toBool(); p.limiterCeilDb = std::clamp(o.value("limCeil").toDouble(-1), -30.0, 0.0); p.limiterReleaseMs = std::clamp(o.value("limRel").toDouble(80), 1.0, 2000.0);
    p.compOn = o.value("compOn").toBool(); p.compThresholdDb = std::clamp(o.value("compThr").toDouble(-20), -60.0, 0.0); p.compRatio = std::clamp(o.value("compRatio").toDouble(4), 1.0, 20.0); p.compKneeDb = std::clamp(o.value("compKnee").toDouble(6), 0.0, 24.0);
    p.compAttackMs = std::clamp(o.value("compAtt").toDouble(10), 0.1, 200.0); p.compReleaseMs = std::clamp(o.value("compRel").toDouble(100), 5.0, 2000.0); p.compMakeupDb = std::clamp(o.value("compMakeup").toDouble(0), 0.0, 24.0);
    p.gateOn = o.value("gateOn").toBool(); p.gateThresholdDb = std::clamp(o.value("gateThr").toDouble(-50), -90.0, 0.0); p.gateRangeDb = std::clamp(o.value("gateRange").toDouble(-80), -90.0, 0.0);
    p.gateAttackMs = std::clamp(o.value("gateAtt").toDouble(1), 0.1, 100.0); p.gateHoldMs = std::clamp(o.value("gateHold").toDouble(50), 0.0, 2000.0); p.gateReleaseMs = std::clamp(o.value("gateRel").toDouble(100), 5.0, 3000.0);
    const QJsonArray eq = o.value("eq").toArray();
    for (int b = 0; b < am::dsp::kEqBands && b < eq.size(); ++b) { const QJsonObject e = eq[b].toObject(); p.eq[static_cast<std::size_t>(b)] = {static_cast<FilterType>(std::clamp(e.value("t").toInt(0), 0, 8)), std::clamp(e.value("f").toDouble(1000), 10.0, 24000.0), std::clamp(e.value("q").toDouble(1), 0.1, 36.0), std::clamp(e.value("g").toDouble(0), -24.0, 24.0)}; }
    s.dsp.set(p);
  }
}
