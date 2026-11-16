#include "Osc.h"
#include "Frame.h"

#include <QDateTime>
#include <QtEndian>
#include <cmath>

#include "Engine.h"

namespace {
// OSC 1.0 encoding: address (null-padded to 4), type tags starting with ',' (padded), then big-endian arguments
QByteArray pad4(QByteArray b) { b.append('\0'); while (b.size() % 4) b.append('\0'); return b; }
QByteArray oscFloat(const QString& address, float v) { QByteArray out = pad4(address.toUtf8()) + pad4(QByteArray(",f")); quint32 bits; std::memcpy(&bits, &v, 4); char be[4]; qToBigEndian(bits, be); out.append(be, 4); return out; }
bool readString(const QByteArray& d, int& pos, QString& out) { const int end = d.indexOf('\0', pos); if (end < 0) return false; out = QString::fromUtf8(d.constData() + pos, end - pos); pos = end + 1; while (pos % 4) ++pos; return pos <= d.size(); }
}  // namespace

Osc::Osc(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  meterTimer_.setInterval(100); connect(&meterTimer_, &QTimer::timeout, this, &Osc::sendMeters);
  if (s_.value("osc/enabled", false).toBool()) setEnabled(true);
}
Osc::~Osc() { if (sock_) { sock_->close(); delete sock_; } }
QStringList Osc::addresses() const {
  return {"/ch/{1..32}/fader f  (dB, -60..10)", "/ch/{n}/mute i", "/ch/{n}/solo i", "/ch/{n}/pan f  (-1..1)", "/master/fader f  (dB)", "/bus/{1..16}/level f  (dB; 1-8 AUX, 9-16 FX)", "/ping  -> /pong", "meters out: /meter/ch/{n} f, /meter/master f  (dBFS peak, 10 Hz, to the last sender on the TX port)"};
}
#include <QNetworkInterface>
QVariantList Osc::interfaces() const {
  QVariantList l;
  for (const QNetworkInterface& ni : QNetworkInterface::allInterfaces()) {
    if (!(ni.flags() & QNetworkInterface::IsUp)) continue;
    for (const QNetworkAddressEntry& e : ni.addressEntries()) { if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
      QVariantMap m; m["name"] = ni.humanReadableName(); m["ip"] = e.ip().toString(); m["mask"] = e.netmask().toString(); m["up"] = bool(ni.flags() & QNetworkInterface::IsRunning);
      m["kind"] = ((ni.flags() & QNetworkInterface::IsLoopBack) || e.ip().isLoopback()) ? "loopback"   /* 127.x counts even when the system does not flag the adapter */ : ni.type() == QNetworkInterface::Wifi ? "wifi" : ni.type() == QNetworkInterface::Ethernet ? "ethernet" : ni.type() == QNetworkInterface::Virtual ? "virtual" : "other"; l << m; } }
  return l;
}
void Osc::setBindAddress(const QString& ip) { s_.setValue("osc/bind", ip.trimmed()); if (sock_) { setEnabled(false); setEnabled(true); } emit changed(); }
void Osc::setAllowedIps(const QString& list) { s_.setValue("osc/allow", list.trimmed()); emit changed(); }
QString Osc::remoteAddress() const {
  if (!allowLan()) return "127.0.0.1:" + QString::number(port()); if (!bindAddress().isEmpty()) return bindAddress() + ":" + QString::number(port());
  for (const QVariant& v : interfaces()) { const QVariantMap m = v.toMap(); if (m["kind"] != "loopback" && m["up"].toBool()) return m["ip"].toString() + ":" + QString::number(port()); } return "0.0.0.0:" + QString::number(port());
}
bool Osc::ipAllowed(const QHostAddress& a) const {
  const QString list = allowedIps(); if (a.isLoopback()) return true; QHostAddress v4(a.toIPv4Address());
  if (list.isEmpty()) { const quint32 ip = a.toIPv4Address(); const unsigned x = ip >> 24, y = (ip >> 16) & 255; return x == 10 || (x == 172 && y >= 16 && y <= 31) || (x == 192 && y == 168) || (x == 169 && y == 254); }   // LOCAL POLICY: with no list, only private-network senders; a public address must be listed by hand
  for (const QString& ip : list.split(',', Qt::SkipEmptyParts)) { const QString t = ip.trimmed(); if (t.isEmpty()) continue; if (QHostAddress(t) == v4 || QHostAddress(t) == a) return true;
    if (t.contains('/')) { auto sub = QHostAddress::parseSubnet(t); if (sub.second >= 0 && (v4.isInSubnet(sub) || a.isInSubnet(sub))) return true; } }
  return false;
}
void Osc::setAllowLan(bool v) { s_.setValue("osc/lan", v); if (sock_) { setEnabled(false); setEnabled(true); } emit changed(); }
void Osc::setPort(int p) { s_.setValue("osc/port", std::clamp(p, 1, 65535)); if (sock_) { setEnabled(false); setEnabled(true); } emit changed(); }
void Osc::setEnabled(bool on) {
  if (on == (sock_ != nullptr)) { s_.setValue("osc/enabled", on); return; }
  if (!on) { meterTimer_.stop(); sock_->close(); delete sock_; sock_ = nullptr; status_ = "off"; s_.setValue("osc/enabled", false); note("OSC server stopped"); emit changed(); return; }
  sock_ = new QUdpSocket(this); QHostAddress bind = allowLan() ? QHostAddress::AnyIPv4 : QHostAddress::LocalHost; if (allowLan() && !bindAddress().isEmpty()) { QHostAddress one(bindAddress()); if (!one.isNull()) bind = one; }
  if (!sock_->bind(bind, static_cast<quint16>(port()))) { status_ = "cannot bind UDP " + bind.toString() + ":" + QString::number(port()) + " (" + sock_->errorString() + ")"; delete sock_; sock_ = nullptr; note(status_); emit changed(); return; }
  connect(sock_, &QUdpSocket::readyRead, this, &Osc::onReadyRead); meterTimer_.start();
  status_ = QStringLiteral("listening on %1:%2 (%3)").arg(bind.toString()).arg(port()).arg(allowLan() ? "LAN, unauthenticated UDP" : "this computer only");
  s_.setValue("osc/enabled", true); note(status_); emit changed();
}
void Osc::onReadyRead() {
  while (sock_ && sock_->hasPendingDatagrams()) {
    QByteArray d; d.resize(static_cast<int>(sock_->pendingDatagramSize())); QHostAddress from; quint16 port = 0; sock_->readDatagram(d.data(), d.size(), &from, &port);
    if (!ipAllowed(from)) { note("refused " + from.toString() + " (not in the allowed list)"); continue; }
    client_ = from; clientPort_ = port; handle(d);
  }
}
bool Osc::handle(const QByteArray& d) {
  int pos = 0; QString addr, tags; if (!readString(d, pos, addr) || !readString(d, pos, tags) || !tags.startsWith(',')) return false;
  QVariantList args; for (int t = 1; t < tags.size(); ++t) {
    if (tags[t] == 'f') { if (pos + 4 > d.size()) return false; quint32 bits = qFromBigEndian<quint32>(d.constData() + pos); float f; std::memcpy(&f, &bits, 4); args << f; pos += 4; }
    else if (tags[t] == 'i') { if (pos + 4 > d.size()) return false; args << qFromBigEndian<qint32>(d.constData() + pos); pos += 4; }
    else if (tags[t] == 's') { QString s; if (!readString(d, pos, s)) return false; args << s; }
    else return false;
  }
  ++rx_; const double a0 = args.isEmpty() ? 0.0 : args[0].toDouble(); const QStringList p = addr.split('/', Qt::SkipEmptyParts); bool ok = false;
  if (frame_ && frame_->handleOsc(addr, a0)) ok = true;   // the I/O FRAME: /gpio/in/N, /locate
  else if (p.size() == 3 && p[0] == "ch") { const int n = p[1].toInt() - 1; if (n >= 0 && n < 32) { ok = true; if (p[2] == "fader") engine_->setGain(n, a0); else if (p[2] == "mute") engine_->setMute(n, a0 != 0); else if (p[2] == "solo") engine_->setSolo(n, a0 != 0); else if (p[2] == "pan") engine_->setPan(n, a0); else ok = false; } }
  else if (p.size() == 2 && p[0] == "master" && p[1] == "fader") { engine_->setMaster(a0); ok = true; }
  else if (p.size() == 3 && p[0] == "bus" && p[2] == "level") { const int k = p[1].toInt() - 1; if (k >= 0 && k < 16) { engine_->setBusLevel(k, a0); ok = true; } }
  else if (p.size() == 1 && p[0] == "ping") { if (sock_ && !client_.isNull()) { sock_->writeDatagram(pad4(QByteArray("/pong")) + pad4(QByteArray(",")), client_, static_cast<quint16>(txPort())); ++tx_; } ok = true; }
  note((ok ? "RX " : "RX (unknown) ") + addr + " " + (args.isEmpty() ? QString() : args[0].toString()));
  return ok;
}
void Osc::sendFloat(const QString& address, float v) { if (!sock_ || client_.isNull()) return; sock_->writeDatagram(oscFloat(address, v), client_, static_cast<quint16>(txPort())); ++tx_; emit statsChanged(); }
void Osc::sendMeters() {
  if (!sock_ || !meters() || client_.isNull() || !engine_->running()) return;
  const QVariantList m = engine_->meters(); const quint16 tp = static_cast<quint16>(txPort());
  for (int i = 0; i < m.size() && i < 32; ++i) { sock_->writeDatagram(oscFloat(QStringLiteral("/meter/ch/%1").arg(i + 1), static_cast<float>(m[i].toMap()["peakDb"].toDouble())), client_, tp); ++tx_; }
  sock_->writeDatagram(oscFloat("/meter/master", static_cast<float>(engine_->telemetry()["masterPeakDb"].toDouble())), client_, tp); ++tx_;
  emit statsChanged();
}
void Osc::note(const QString& m) { log_ << QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + m; while (log_.size() > 40) log_.removeFirst(); emit statsChanged(); }
