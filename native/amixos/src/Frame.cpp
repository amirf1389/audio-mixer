#include "Frame.h"

#include <QDateTime>
#include <QDir>
#include <QStringList>

#include "Engine.h"
#include "Osc.h"
#include "audiomixer/frame.hpp"
#include "audiomixer/runtime.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

// A serial line at 115200 8N1, polled (never blocks the interface): the USB GPIO box. Windows COM ports and POSIX ttys.
class SerialLine {
 public:
  ~SerialLine() { close(); }
  bool open(const QString& name, QString& err) {
#ifdef _WIN32
    const QString path = QStringLiteral("\\\\.\\") + name;
    h_ = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h_ == INVALID_HANDLE_VALUE) { err = QStringLiteral("cannot open %1 (in use, or not there)").arg(name); return false; }
    DCB dcb{}; dcb.DCBlength = sizeof dcb; GetCommState(h_, &dcb); dcb.BaudRate = 115200; dcb.ByteSize = 8; dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT; dcb.fDtrControl = DTR_CONTROL_ENABLE;
    SetCommState(h_, &dcb); COMMTIMEOUTS t{}; t.ReadIntervalTimeout = MAXDWORD; SetCommTimeouts(h_, &t);   // reads return at once with what is there
    return true;
#else
    fd_ = ::open(name.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) { err = QStringLiteral("cannot open %1 (in use, no permission, or not there)").arg(name); return false; }
    termios t{}; tcgetattr(fd_, &t); cfmakeraw(&t); cfsetispeed(&t, B115200); cfsetospeed(&t, B115200); t.c_cflag |= CLOCAL | CREAD; tcsetattr(fd_, TCSANOW, &t);
    return true;
#endif
  }
  void close() {
#ifdef _WIN32
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); h_ = INVALID_HANDLE_VALUE;
#else
    if (fd_ >= 0) ::close(fd_); fd_ = -1;
#endif
  }
  QByteArray read() {
    char b[256]; QByteArray out;
#ifdef _WIN32
    DWORD got = 0; while (h_ != INVALID_HANDLE_VALUE && ReadFile(h_, b, sizeof b, &got, nullptr) && got > 0) out.append(b, static_cast<int>(got));
#else
    ssize_t got; while (fd_ >= 0 && (got = ::read(fd_, b, sizeof b)) > 0) out.append(b, static_cast<int>(got));
#endif
    return out;
  }
  void write(const QByteArray& d) {
#ifdef _WIN32
    DWORD put = 0; if (h_ != INVALID_HANDLE_VALUE) WriteFile(h_, d.constData(), static_cast<DWORD>(d.size()), &put, nullptr);
#else
    if (fd_ >= 0) { const ssize_t r = ::write(fd_, d.constData(), static_cast<size_t>(d.size())); (void)r; }
#endif
  }
  QByteArray pending;   // a line not yet complete
 private:
#ifdef _WIN32
  HANDLE h_ = INVALID_HANDLE_VALUE;
#else
  int fd_ = -1;
#endif
};

namespace {
// The card types. channels: how many the card carries; dir: in (device -> strips), out (console -> device), both (the hybrid: caller in,
// send out); fx: the processing object a card puts into each of its strips' racks.
QVariantList makeTypes() {
  QVariantList l; auto add = [&](const char* t, const char* n, const char* dir, int ch, bool phantom, const char* fx, const char* note) {
    QVariantMap m; m["type"] = t; m["name"] = n; m["dir"] = dir; m["channels"] = ch; m["phantom"] = phantom; m["fx"] = fx; m["note"] = note; l << m; };
  add("analog-in", "ANALOG IN", "in", 4, true, "", "Four mic / line inputs with 48 V per channel.");
  add("analog-out", "ANALOG OUT", "out", 4, false, "", "Four line outputs.");
  add("digital-in", "DIGITAL IN (AES/EBU · S/PDIF)", "in", 4, false, "", "Two AES/EBU and two S/PDIF channels (the interface's digital inputs).");
  add("digital-out", "DIGITAL OUT (AES/EBU · S/PDIF)", "out", 4, false, "", "Two AES/EBU and two S/PDIF channels (the interface's digital outputs).");
  add("aec-in", "AEC IN", "in", 4, true, "fx:aec", "Four mic inputs with 48 V and an echo canceller each (the console's output is the reference).");
  add("hybrid", "TELEPHONE HYBRID", "both", 2, false, "fx:hybrid", "Channel 1: the caller (in), with the line echo canceller. Channel 2: what the caller hears (out).");
  return l;
}
const QVariantList& types() { static const QVariantList t = makeTypes(); return t; }
}

Frame::Frame(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  load(); rescanSerial();
  poll_.setInterval(50); connect(&poll_, &QTimer::timeout, this, &Frame::pollLogic); poll_.start();
}
Frame::~Frame() = default;

const QVariantMap* Frame::typeOf(const QString& t) const { for (const QVariant& v : types()) if (v.toMap().value("type").toString() == t) { static thread_local QVariantMap m; m = v.toMap(); return &m; } return nullptr; }
QVariantList Frame::cardTypes() const { return types(); }

QVariantList Frame::cards() const {
  QVariantList l; int bus = 1;
  for (int s = 0; s < kSlots; ++s) {
    const Card& c = cards_[s]; QVariantMap m; m["slot"] = s; m["type"] = c.type; const QVariantMap* t = typeOf(c.type);
    m["name"] = t ? t->value("name") : QStringLiteral("EMPTY"); m["note"] = t ? t->value("note") : QString(); m["phantomCard"] = t && t->value("phantom").toBool();
    QVariantList chs; const int n = t ? t->value("channels").toInt() : 0; bus = s * 64 + 1;
    for (int k = 0; k < n; ++k) {
      const Ch& h = c.ch[k]; QVariantMap x; x["n"] = k; x["device"] = h.device; x["strip"] = h.strip; x["source"] = h.source; x["phantom"] = h.phantom; x["bus"] = bus + k;
      const QString dir = t->value("dir").toString(); x["dir"] = dir == "both" ? (k == 0 ? "in" : "out") : dir;
      static const char* dig[] = {"AES 1", "AES 2", "S/PDIF L", "S/PDIF R"};
      x["label"] = c.type.startsWith("digital") ? QString::fromLatin1(dig[k]) : c.type == "hybrid" ? (k == 0 ? QStringLiteral("CALLER") : QStringLiteral("TO CALLER")) : QStringLiteral("CH %1").arg(k + 1);
      chs << x;
    }
    m["channels"] = chs; l << m;
  }
  return l;
}
int Frame::busUsed() const { int n = 0; for (const Card& c : cards_) if (const QVariantMap* t = typeOf(c.type)) n += t->value("channels").toInt(); return n; }

void Frame::setCardType(int slot, const QString& type) {
  if (slot < 0 || slot >= kSlots) return; if (!type.isEmpty() && !typeOf(type)) return;
  Card& c = cards_[slot]; c.type = type;
  // sensible defaults: the channels of this slot on the device (slot 1 = 1..4, slot 2 = 5..8 ...), the strips in the same order
  const QVariantMap* t = typeOf(type); const int n = t ? t->value("channels").toInt() : 0;
  for (int k = 0; k < 8; ++k) { c.ch[k] = Ch{}; if (k < n) { c.ch[k].device = slot * 4 + k; c.ch[k].strip = slot * 4 + k; c.ch[k].source = am::Runtime::kSrcMasterL + (k % 2); } }
  save(); note(QStringLiteral("SLOT %1: %2").arg(slot + 1).arg(t ? t->value("name").toString() : QStringLiteral("EMPTY"))); emit changed();
}
void Frame::setChannelDevice(int slot, int ch, int device) { if (slot < 0 || slot >= kSlots || ch < 0 || ch >= 8) return; cards_[slot].ch[ch].device = device; save(); emit changed(); }
void Frame::setChannelStrip(int slot, int ch, int strip) { if (slot < 0 || slot >= kSlots || ch < 0 || ch >= 8) return; cards_[slot].ch[ch].strip = strip; save(); emit changed(); }
void Frame::setChannelSource(int slot, int ch, int source) { if (slot < 0 || slot >= kSlots || ch < 0 || ch >= 8) return; cards_[slot].ch[ch].source = source; save(); emit changed(); }
void Frame::setPhantom(int slot, int ch, bool on) {
  if (slot < 0 || slot >= kSlots || ch < 0 || ch >= 8) return; cards_[slot].ch[ch].phantom = on; save();
  note(QStringLiteral("SLOT %1 CH %2: 48 V %3 (switch it on the interface too)").arg(slot + 1).arg(ch + 1).arg(on ? "ON" : "OFF")); emit changed();
}

void Frame::apply() {
  int routed = 0;
  for (int s = 0; s < kSlots; ++s) {
    const Card& c = cards_[s]; const QVariantMap* t = typeOf(c.type); if (!t) continue;
    const QString dir = t->value("dir").toString(), fx = t->value("fx").toString(); const int n = t->value("channels").toInt();
    for (int k = 0; k < n; ++k) {
      const Ch& h = c.ch[k]; const bool in = dir == "in" || (dir == "both" && k == 0);
      if (in && h.strip >= 0 && h.device >= 0) {
        engine_->setInputRoute(h.strip, h.device); ++routed;
        if (!fx.isEmpty()) {   // the processing object in the strip's rack: the first free slot, unless it is there already
          const QVariantList ps = engine_->plugins(h.strip); bool has = false; int free = -1;
          for (const QVariant& p : ps) { const QVariantMap m = p.toMap(); if (m.value("kind").toString() == fx) has = true; else if (free < 0 && m.value("kind").toString().isEmpty()) free = m.value("slot").toInt(); }
          if (!has && free >= 0) engine_->setPlugin(h.strip, free, fx);
        }
      } else if (!in && h.device >= 0) { engine_->setOutputRoute(h.device, h.source); ++routed; }
    }
  }
  note(QStringLiteral("APPLY: %1 channels routed").arg(routed));
}

QVariantList Frame::logicIn() const { QVariantList l; for (int i = 0; i < am::frame::kLogicIn; ++i) l << am::frame::logic().in[static_cast<std::size_t>(i)].load(); return l; }
QVariantList Frame::logicOut() const { QVariantList l; for (int i = 0; i < am::frame::kLogicOut; ++i) l << am::frame::logic().out[static_cast<std::size_t>(i)].load(); return l; }
void Frame::setLogicIn(int i, bool on, const QString& from) {
  if (i < 0 || i >= am::frame::kLogicIn) return;
  if (am::frame::logic().in[static_cast<std::size_t>(i)].exchange(on) == on) return;
  note(QStringLiteral("IN %1 %2 (%3)").arg(i + 1).arg(on ? "CLOSED" : "OPEN", from)); emit logicChanged();
}

void Frame::locate(bool on, bool send) {
  const bool was = locating(); locateLeft_ = on ? 15000 / 50 : 0;
  if (was != locating()) emit locateChanged();
  if (on) note(send ? QStringLiteral("LOCATE (sent to OSC / GPIO box)") : QStringLiteral("LOCATE received"));
  if (on && send) { if (osc_) osc_->sendFloat(QStringLiteral("/locate"), 1.f); if (serial_) serial_->write("LOCATE\n"); }
}

void Frame::pollLogic() {
  if (locateLeft_ > 0 && --locateLeft_ == 0) emit locateChanged();
  auto& lg = am::frame::logic(); const unsigned seq = lg.outSeq.load();
  if (seq != lastSeq_) {   // a logic output changed (a LOGIC / GPIO object in a rack): tell the screen, OSC and the GPIO box
    lastSeq_ = seq;
    for (int i = 0; i < am::frame::kLogicOut; ++i) { const bool v = lg.out[static_cast<std::size_t>(i)].load(); if (v == lastOut_[i]) continue; lastOut_[i] = v;
      if (osc_) osc_->sendFloat(QStringLiteral("/gpio/out/%1").arg(i + 1), v ? 1.f : 0.f);
      if (serial_) serial_->write(QStringLiteral("O%1=%2\n").arg(i + 1).arg(v ? 1 : 0).toLatin1()); }
    emit logicChanged();
  }
  if (serial_) { serial_->pending += serial_->read(); int nl;
    while ((nl = serial_->pending.indexOf('\n')) >= 0) { const QByteArray line = serial_->pending.left(nl).trimmed(); serial_->pending.remove(0, nl + 1); if (!line.isEmpty()) serialLine(line); }
    if (serial_->pending.size() > 4096) serial_->pending.clear(); }
}
void Frame::serialLine(const QByteArray& line) {
  if (line.toUpper() == "LOCATE") { locate(true, false); return; }
  // "I3=1": control input 3 closed
  if (line.size() >= 4 && (line[0] == 'I' || line[0] == 'i')) { const int eq = line.indexOf('='); if (eq > 1) { bool ok = false; const int n = line.mid(1, eq - 1).toInt(&ok); if (ok) setLogicIn(n - 1, line.mid(eq + 1).trimmed() == "1", QStringLiteral("GPIO BOX")); } }
}

bool Frame::handleOsc(const QString& addr, double value) {
  const QStringList p = addr.split('/', Qt::SkipEmptyParts);
  if (p.size() == 1 && p[0] == "locate") { locate(value > 0.5, false); return true; }
  if (p.size() == 3 && p[0] == "gpio" && p[1] == "in") { bool ok = false; const int n = p[2].toInt(&ok); if (ok && n >= 1 && n <= am::frame::kLogicIn) { setLogicIn(n - 1, value > 0.5, QStringLiteral("OSC")); return true; } }
  return false;
}
bool Frame::handleMidi(int status, int d1, int d2) {
  const int type = status & 0xf0; if (type != 0x90 && type != 0x80) return false;
  const bool on = type == 0x90 && d2 > 0;
  if (d1 >= 36 && d1 < 36 + am::frame::kLogicIn) { setLogicIn(d1 - 36, on, QStringLiteral("MIDI")); return true; }
  if (d1 == 48 && on) { locate(true, false); return true; }
  return false;
}

QStringList Frame::serialPorts() const { return ports_; }
void Frame::rescanSerial() {
  ports_.clear();
#ifdef _WIN32
  QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM"), QSettings::NativeFormat);
  for (const QString& k : reg.allKeys()) ports_ << reg.value(k).toString();
#else
  for (const char* pat : {"ttyUSB*", "ttyACM*", "cu.usb*", "tty.usb*"}) for (const QString& f : QDir(QStringLiteral("/dev")).entryList({QString::fromLatin1(pat)}, QDir::System)) ports_ << QStringLiteral("/dev/") + f;
#endif
  ports_.sort(); emit serialChanged();
}
bool Frame::openSerial(const QString& port) {
  serial_.reset(); serialName_.clear();
  if (port.isEmpty()) { serialNote_ = QStringLiteral("GPIO box closed"); s_.setValue("frame/serial", QString()); emit serialChanged(); return true; }
  auto line = std::make_unique<SerialLine>(); QString err;
  if (!line->open(port, err)) { serialNote_ = err; emit serialChanged(); return false; }
  serial_ = std::move(line); serialName_ = port; serialNote_ = QStringLiteral("GPIO box on %1 (115200 8N1)").arg(port); s_.setValue("frame/serial", port);
  for (int i = 0; i < am::frame::kLogicOut; ++i) serial_->write(QStringLiteral("O%1=%2\n").arg(i + 1).arg(lastOut_[i] ? 1 : 0).toLatin1());   // the box starts in step
  note(serialNote_); emit serialChanged(); return true;
}

void Frame::save() {
  QVariantList l; for (const Card& c : cards_) { QVariantMap m; m["type"] = c.type; QVariantList ch;
    for (const Ch& h : c.ch) { QVariantMap x; x["device"] = h.device; x["strip"] = h.strip; x["source"] = h.source; x["phantom"] = h.phantom; ch << x; } m["ch"] = ch; l << m; }
  s_.setValue("frame/cards", l);
}
void Frame::load() {
  const QVariantList l = s_.value("frame/cards").toList();
  for (int s = 0; s < kSlots && s < l.size(); ++s) { const QVariantMap m = l[s].toMap(); cards_[s].type = typeOf(m.value("type").toString()) ? m.value("type").toString() : QString();
    const QVariantList ch = m.value("ch").toList();
    for (int k = 0; k < 8 && k < ch.size(); ++k) { const QVariantMap x = ch[k].toMap(); cards_[s].ch[k] = Ch{x.value("device", -1).toInt(), x.value("strip", -1).toInt(), x.value("source", 0).toInt(), x.value("phantom").toBool()}; } }
  const QString sp = s_.value("frame/serial").toString(); if (!sp.isEmpty()) QTimer::singleShot(0, this, [this, sp] { openSerial(sp); });
}
void Frame::note(const QString& m) { log_.prepend(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + m); while (log_.size() > 30) log_.removeLast(); emit logicChanged(); }
