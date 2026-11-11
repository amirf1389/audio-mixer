#include "Remote.h"

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
  connect(&proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) { if (e == QProcess::FailedToStart) { status_ = QStringLiteral("the remote service could not be started: ") + program(); emit changed(); } });
}
Remote::~Remote() { if (running()) { proc_.terminate(); if (!proc_.waitForFinished(1500)) proc_.kill(); } }
QString Remote::program() const {
#ifdef _WIN32
  return QCoreApplication::applicationDirPath() + "/amixos-daemon.exe";
#else
  return QCoreApplication::applicationDirPath() + "/amixos-daemon";
#endif
}
bool Remote::available() const { return QFileInfo::exists(program()); }
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
  QStringList args{"--port", QString::number(port()), "--open", "auto", "--version", QCoreApplication::applicationVersion()}; if (wifi) args << "--allow-lan";   // local policy stays on: private addresses only
  proc_.start(program(), args); if (!proc_.waitForStarted(3000)) { status_ = QStringLiteral("the remote service did not start"); emit changed(); return false; }
  status_ = wifi ? QStringLiteral("starting for the phones on this network") : QStringLiteral("starting for this computer only"); emit changed(); return true;
}
void Remote::stop() { if (!running()) return; proc_.terminate(); if (!proc_.waitForFinished(2000)) proc_.kill(); }
