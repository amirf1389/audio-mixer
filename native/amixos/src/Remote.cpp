#include "Remote.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QVariantMap>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>

#include "Engine.h"
#include "paths/paths.hpp"

Remote::Remote(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  proc_.setProcessChannelMode(QProcess::MergedChannels);
  connect(&proc_, &QProcess::readyRead, this, [this] { const QString out = QString::fromLocal8Bit(proc_.readAll()).trimmed(); if (!out.isEmpty()) { status_ = out.section('\n', -1).left(200); emit changed(); } });
  connect(&proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) { status_ = code == 0 ? QStringLiteral("off") : QStringLiteral("stopped (%1): %2").arg(code).arg(status_); emit changed(); emit stopped(); });
  connect(&ext_, &Extension::changed, this, [this] { const QString out = ext_.takeOutput().trimmed(); if (!out.isEmpty()) status_ = out.section('\n', -1).left(200); emit changed(); });
  connect(&ext_, &Extension::stopped, this, [this] { if (status_.isEmpty() || status_ == "off") status_ = QStringLiteral("off"); emit changed(); emit stopped(); });
  connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) { if (e == QProcess::FailedToStart) { status_ = QStringLiteral("the remote service could not be started: ") + program(); emit changed(); } });
}
Remote::~Remote() { if (running()) { proc_.terminate(); if (!proc_.waitForFinished(1500)) proc_.kill(); } }
// The service program: "AmF X1 Network" next to the console (its name in the release); the build's own name is the fallback.
QString Remote::program() const {
  const QString d = QCoreApplication::applicationDirPath();
#ifdef _WIN32
  const QStringList names{"/AmF X1 Network.exe", "/amixos-daemon.exe"};
#else
  const QStringList names{"/AmF X1 Network", "/amixos-daemon", "/engine/amixos-daemon"};
#endif
  for (const QString& n : names) if (QFileInfo::exists(d + n)) return d + n;
  return d + names.first();
}
// THE NAMED TOKENS (one per phone or operator): the service program keeps them (hashes only); the console asks it to list, make and revoke.
QString Remote::runTool(const QStringList& args, bool* ok) const {
  QProcess p; p.start(program(), args); const bool fin = p.waitForStarted(3000) && p.waitForFinished(8000); if (!fin) p.kill();
  if (ok) *ok = fin && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
  return QString::fromUtf8(fin ? (p.exitCode() == 0 ? p.readAllStandardOutput() : p.readAllStandardError()) : QByteArray("the service program did not answer"));
}
QString Remote::pairPin() const {
  const QString t = token(); if (t.isEmpty()) return {};
  const QByteArray h = QCryptographicHash::hash((t + "|pair|" + QString::number(QDateTime::currentSecsSinceEpoch() / 60)).toUtf8(), QCryptographicHash::Sha256).toHex();
  return QString::number(h.left(8).toULong(nullptr, 16) % 1000000UL).rightJustified(6, '0');
}
int Remote::pairSecondsLeft() const { return 60 - static_cast<int>(QDateTime::currentSecsSinceEpoch() % 60); }
void Remote::refreshTokens() {
  tokens_.clear(); if (!available()) { emit changed(); return; }
  bool ok = false; const QString out = runTool({"--list-tokens"}, &ok);
  if (ok) for (const QString& line : out.split('\n', Qt::SkipEmptyParts)) {   // id  role  state  name
    const QStringList f = line.simplified().split(' '); if (f.size() < 4 || f[0].size() != 8) continue;
    QVariantMap m; m["id"] = f[0]; m["role"] = f[1]; m["state"] = f[2]; m["name"] = QStringList(f.mid(3)).join(' '); tokens_ << m; }
  emit changed();
}
void Remote::restartIfRunning() { if (!running()) return; const bool w = wifi_; stop(); start(w); }   // the service reads its tokens when it starts
QString Remote::newToken(const QString& name, const QString& role, int days) {
  if (!available()) { tokenNote_ = QStringLiteral("the service program is not installed next to the console"); emit changed(); return {}; }
  QString n; for (const QChar c : name.trimmed()) if (c.isPrint() && c != '"' && n.size() < 40) n += c;
  if (n.isEmpty()) { tokenNote_ = QStringLiteral("Give the token a name first (whose phone it is)."); emit changed(); return {}; }
  bool ok = false; const QString out = runTool({"--new-token", n, "--role", role == "view" ? "view" : "control", "--days", QString::number(qBound(0, days, 3650))}, &ok);
  QString tok; if (ok) for (const QString& line : out.split('\n', Qt::SkipEmptyParts)) if (line.trimmed().startsWith("amx_")) tok = line.trimmed();
  tokenNote_ = tok.isEmpty() ? out.trimmed().left(160) : QStringLiteral("Made. It is shown once, for 10 minutes: type it into the phone, then press nothing else here. The first phone that uses it owns it.");
  refreshTokens(); if (!tok.isEmpty()) restartIfRunning(); return tok;   // the token itself is returned to the page only: never stored or logged here
}
bool Remote::revokeToken(const QString& id) {
  bool ok = false; const QString out = runTool({"--revoke-token", id}, &ok); tokenNote_ = ok ? QStringLiteral("Revoked: that phone is out.") : out.trimmed().left(160);
  refreshTokens(); if (ok) restartIfRunning(); return ok;
}
bool Remote::available() const { return ext_.available() || QFileInfo::exists(program()); }
QString Remote::tokenFile() const { return QString::fromStdString(am::paths::file("daemon.token")); }
QString Remote::token() const { QFile f(tokenFile()); if (!f.open(QIODevice::ReadOnly) || f.size() > 256) return {}; return QString::fromLatin1(f.readAll()).trimmed(); }
QStringList Remote::addresses() const {
  QStringList l;
  for (const QHostAddress& a : QNetworkInterface::allAddresses()) { if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLoopback()) continue; const quint32 ip = a.toIPv4Address(); const unsigned x = ip >> 24, y = (ip >> 16) & 255;
    if (x == 10 || (x == 172 && y >= 16 && y <= 31) || (x == 192 && y == 168)) l << QStringLiteral("http://%1:%2").arg(a.toString()).arg(port()); }
  return l;
}
bool Remote::start(bool wifi) {
  if (running()) return true;
  if (!available()) { status_ = QStringLiteral("the remote service program is not installed next to the console"); emit changed(); return false; }
  engine_->stop();   // the service opens the sound card itself
  wifi_ = wifi;
  QStringList args{"--port", QString::number(port()), "--open", "auto", "--version", QCoreApplication::applicationVersion()}; if (wifi) args << "--allow-lan";   // local policy stays on: private addresses only
  if (ext_.available()) {                       // the extension runs inside the console: same process, same rights on the sound card
    const int rc = ext_.start(args);
    if (rc == 0 && ext_.running()) { status_ = wifi ? QStringLiteral("starting for the phones on this network (extension)") : QStringLiteral("starting for this computer only (extension)"); emit changed(); return true; }
    status_ = QStringLiteral("the network extension did not start (%1): %2").arg(rc).arg(ext_.takeOutput().trimmed().section('\n', -1).left(160)); emit changed();   // then the program next to it, if there is one
  }
  proc_.start(program(), args); if (!proc_.waitForStarted(3000)) { if (status_.isEmpty() || status_ == "off") status_ = QStringLiteral("the remote service did not start"); emit changed(); return false; }
  status_ = wifi ? QStringLiteral("starting for the phones on this network") : QStringLiteral("starting for this computer only"); emit changed(); return true;
}
void Remote::stop() { if (!running()) return; if (ext_.running()) { ext_.stop(); return; } proc_.terminate(); if (!proc_.waitForFinished(2000)) proc_.kill(); }
