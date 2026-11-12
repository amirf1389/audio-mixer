#include "Extension.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <vector>

Extension::Extension(const QString& baseName, QObject* parent) : QObject(parent), base_(baseName) {
  poll_.setInterval(250);
  connect(&poll_, &QTimer::timeout, this, [this] {
    const bool r = running();
    if (r != wasRunning_) { wasRunning_ = r; emit changed(); if (!r) { poll_.stop(); emit stopped(); } }
    else emit changed();
  });
}
Extension::~Extension() { stop(); }

QString Extension::path(const QString& baseName) {
  const QString d = QCoreApplication::applicationDirPath() + "/" + baseName;
#ifdef _WIN32
  const QStringList ends{".dll"};
#elif defined(__APPLE__)
  const QStringList ends{".dylib", ".so"};
#else
  const QStringList ends{".so"};
#endif
  for (const QString& e : ends) if (QFileInfo::exists(d + e)) return d + e;
  return {};
}

bool Extension::load() {
  if (lib_.isLoaded()) return true;
  const QString p = path(base_); if (p.isEmpty()) return false;
  lib_.setFileName(p);
  if (!lib_.load()) return false;
  name_ = reinterpret_cast<const char* (*)()>(lib_.resolve("amfb_ext_name"));
  start_ = reinterpret_cast<int (*)(int, const char* const*)>(lib_.resolve("amfb_ext_start"));
  running_ = reinterpret_cast<int (*)()>(lib_.resolve("amfb_ext_running"));
  status_ = reinterpret_cast<const char* (*)()>(lib_.resolve("amfb_ext_status"));
  output_ = reinterpret_cast<const char* (*)()>(lib_.resolve("amfb_ext_output"));
  stop_ = reinterpret_cast<void (*)()>(lib_.resolve("amfb_ext_stop"));
  if (!name_ || !start_ || !running_ || !status_ || !output_ || !stop_) { lib_.unload(); name_ = nullptr; return false; }
  return true;
}

int Extension::start(const QStringList& args) {
  if (!load()) return -1;
  std::vector<QByteArray> keep; keep.reserve(args.size() + 1);
  keep.push_back(base_.toUtf8()); for (const QString& a : args) keep.push_back(a.toUtf8());
  std::vector<const char*> argv; for (const QByteArray& b : keep) argv.push_back(b.constData());
  const int rc = start_(int(argv.size()), argv.data());
  wasRunning_ = running();
  if (wasRunning_) poll_.start();
  emit changed();
  return rc;
}

void Extension::stop() {
  if (stop_) stop_();
  poll_.stop();
  if (wasRunning_) { wasRunning_ = false; emit changed(); emit stopped(); }
}
