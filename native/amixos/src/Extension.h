// An AmF X1 extension: a library next to the console (AmF X1 Network.dll, AmF X1 Hybrid.dll ...) that runs one
// of the product's programs inside the console's process, through the amfb_ext_* calls (engine/cpp/app/ext/ext.h).
// The console loads it only when that program is started, so a missing or broken library never stops the console.
#pragma once
#include <QLibrary>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class Extension : public QObject {
  Q_OBJECT
 public:
  explicit Extension(const QString& baseName, QObject* parent = nullptr);   // e.g. "AmF X1 Network"
  ~Extension() override;
  static QString path(const QString& baseName);          // the library's full path next to the console ("" when not there)
  bool available() const { return !path(base_).isEmpty(); }
  bool loaded() const { return lib_.isLoaded(); }
  bool running() const { return running_ && running_() != 0; }
  QString name() const { return name_ ? QString::fromUtf8(name_()) : base_; }
  QString status() const { return status_ ? QString::fromUtf8(status_()).left(200) : QString(); }
  QString takeOutput() { return output_ ? QString::fromUtf8(output_()) : QString(); }
  // 0 = running on its thread, >0 the program's exit code (it ended at once), -1 the library could not be loaded
  int start(const QStringList& args);
  void stop();
 signals:
  void changed();
  void stopped();
 private:
  bool load();
  QString base_; QLibrary lib_; QTimer poll_; bool wasRunning_ = false;
  const char* (*name_)() = nullptr; int (*start_)(int, const char* const*) = nullptr; int (*running_)() = nullptr;
  const char* (*status_)() = nullptr; const char* (*output_)() = nullptr; void (*stop_)() = nullptr;
};
