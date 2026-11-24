#include "Journal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QJsonObject>
#include <algorithm>

Journal::Journal(const QString& path, Provider provider, int debounceMs, int maxWaitMs, QObject* parent)
    : QObject(parent), path_(path), provider_(std::move(provider)), debounce_(debounceMs), maxWait_(maxWaitMs) {
  timer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, [this] { if (pending_) saveNow(); });
}
Journal::~Journal() = default;

void Journal::touch() {
  if (!enabled_) return;
  if (!pending_) { pending_ = true; first_.start(); }
  const qint64 left = maxWait_ - first_.elapsed();
  timer_.start(static_cast<int>(std::max<qint64>(0, std::min<qint64>(debounce_, left))));   // restarted by every change, but never past the longest wait
}

bool Journal::flush() { return !pending_ || saveNow(); }

bool Journal::saveNow() {
  timer_.stop(); pending_ = false;
  if (!provider_) return false;
  const QByteArray data = provider_();
  if (data.isEmpty() || !writeAtomic(path_, data)) return false;
  ++saves_; emit saved(); return true;
}

bool Journal::parses(const QByteArray& data) {
  QJsonParseError e{}; const QJsonDocument d = QJsonDocument::fromJson(data, &e);
  return e.error == QJsonParseError::NoError && d.isObject() && !d.object().isEmpty();
}

bool Journal::writeAtomic(const QString& path, const QByteArray& data, QString* error) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  // ONE BACKUP: the file being replaced, when it is a good one (a torn or empty file never overwrites the good backup)
  QFile cur(path);
  if (cur.exists() && cur.open(QIODevice::ReadOnly)) {
    const QByteArray old = cur.readAll(); cur.close();
    if (parses(old)) { QSaveFile b(path + QStringLiteral(".bak")); if (b.open(QIODevice::WriteOnly)) { b.write(old); b.commit(); } }
  }
  QSaveFile f(path);   // writes <dir>/<name>.XXXX, then renames it over the file in one step
  if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
  if (f.write(data) != data.size()) { if (error) *error = f.errorString(); f.cancelWriting(); return false; }
  if (!f.commit()) { if (error) *error = f.errorString(); return false; }
  return true;
}

QByteArray Journal::loadGood(QString* from) const {
  for (const QString& p : {path_, backupPath()}) {
    QFile f(p); if (!f.open(QIODevice::ReadOnly)) continue;
    const QByteArray d = f.readAll();
    if (parses(d)) { if (from) *from = p; return d; }
  }
  return {};
}

bool Journal::begin() {
  const QString marker = path_ + QStringLiteral(".running");
  const bool crashed = QFileInfo::exists(marker);
  QDir().mkpath(QFileInfo(marker).absolutePath());
  QFile m(marker); if (m.open(QIODevice::WriteOnly | QIODevice::Truncate)) m.write("running\n");
  return crashed;
}
void Journal::end() { flush(); QFile::remove(path_ + QStringLiteral(".running")); }
