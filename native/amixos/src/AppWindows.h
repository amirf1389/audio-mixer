// PROGRAM PICTURES for APP CAPTURE: the picker shows every program that plays sound with its icon and a picture of its window (as the
// "share a window" dialog of a call program does), and a captured program can keep a live picture of its window inside the console
// beside its sound. Windows only: the picture is asked from Windows for that one window (PrintWindow), a few times a second, on a
// thread of its own, and only for programs the user picked. A window that protects its content (some video players) stays black;
// nothing is worked around. The pictures are never saved or sent anywhere: they live in memory while they are on screen.
//
// QML reads them as images:  image://appwin/icon/<pid>   image://appwin/window/<pid>?<tick>
#pragma once
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QSet>
#include <QTimer>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

class AppWindows : public QObject {
  Q_OBJECT
  Q_PROPERTY(int tick READ tick NOTIFY tickChanged)                // goes up when a new picture is in: put it in the image's source
  Q_PROPERTY(bool available READ available CONSTANT)
  Q_PROPERTY(QVariantList watched READ watched NOTIFY watchedChanged)   // the pids with a live picture
 public:
  explicit AppWindows(QObject* parent = nullptr);
  ~AppWindows() override;
  bool available() const;
  int tick() const { return tick_; }
  QVariantList watched() const;
  Q_INVOKABLE QString titleOf(quint32 pid);                        // the title of the program's main window ("" = none found)
  Q_INVOKABLE void snapshot(quint32 pid);                          // one picture now (the picker's tiles)
  Q_INVOKABLE void watch(quint32 pid, bool on);                    // a live picture, about 8 a second, until switched off
  Q_INVOKABLE bool isWatched(quint32 pid) const { return watch_.contains(pid); }
  Q_INVOKABLE void forget(quint32 pid);                            // the program is no longer captured: its picture goes
  QImage icon(quint32 pid); QImage picture(quint32 pid);
  // tests and the screenshots: pictures made here instead of asked from Windows
  std::function<QImage(quint32 pid, bool icon)> testSource; std::function<QString(quint32 pid)> testTitle;
 signals:
  void tickChanged(); void watchedChanged();
 private:
  void run(); void grab(quint32 pid);
  QSet<quint32> watch_; QTimer timer_; int tick_ = 0;
  std::mutex mu_; QHash<quint32, QImage> pics_, icons_; QList<quint32> jobs_;   // mu_ guards these three
  std::thread thread_; std::atomic<bool> stop_{false}, busy_{false};
};

class AppWindowImages : public QQuickImageProvider {
 public:
  explicit AppWindowImages(AppWindows* w) : QQuickImageProvider(QQuickImageProvider::Image), w_(w) {}
  QImage requestImage(const QString& id, QSize* size, const QSize& requested) override;
 private:
  AppWindows* w_;
};
