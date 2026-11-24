// GRAPHICS: which drawing back end the console's window uses, and how smooth it runs on this computer.
//
//   choose()   before the window exists (after QGuiApplication): the saved GRAPHICS choice (AUTO / DIRECT3D 11 / DIRECT3D 12 when this Qt
//              has it / VULKAN when this Qt has it and the loader is installed / OPENGL / SOFTWARE SAFE MODE) becomes QQuickWindow::setGraphicsApi.
//              AUTO on Windows: the graphics card is looked up (DXGI: name, maker, driver version), a Direct3D 11 device is tried (feature
//              level 10.0 or better), else an OpenGL context, else the software renderer. A card on the short known-bad list (no real
//              driver installed: "Microsoft Basic Render Driver"; Direct3D below 10.0) goes straight to the next one. The back end that
//              reached its first picture is remembered and tried first next time.
//   guard      a start writes "gfx/pending" before the window is made and clears it at the first frame shown. When the previous start never
//              got there (a driver crash, a black window), this start uses SOFTWARE SAFE MODE, says so, and AUTO leaves that back end out
//              from then on (choosing it again by hand clears that). A scene-graph error at start restarts the console in safe mode.
//   perf       PERFORMANCE: AUTO / SMOOTH / BATTERY. The meter rate follows the display (60 Hz at most, capped at the monitor's refresh);
//              BATTERY, and AUTO on a weak processor (4 threads or fewer) or on battery power, run the meters at 30 fps and switch the
//              moving backgrounds off. Re-checked every 30 s and when the window moves to another monitor.
//   stats      frames drawn per second and the frame time (mean, 95th percentile, worst) over the last second, for the GRAPHICS panel.
// Test runs (--smoke, --bench, --shots) skip the guard and keep Qt's own choice (the offscreen platform).
#pragma once
#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <vector>

class QQuickWindow;
class QScreen;

class Graphics : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString choice READ choice WRITE setChoice NOTIFY changed)          // saved: auto d3d11 d3d12 vulkan opengl software
  Q_PROPERTY(QVariantList choices READ choices CONSTANT)                         // [{v, t}] what this build offers
  Q_PROPERTY(QString inUse READ inUse NOTIFY changed)                            // the back end the window really runs on
  Q_PROPERTY(QString startedWith READ startedWith CONSTANT)                      // what choose() asked for at this start
  Q_PROPERTY(bool safeMode READ safeMode NOTIFY changed)
  Q_PROPERTY(QString note READ note NOTIFY changed)                              // why this back end (plain words)
  Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)
  Q_PROPERTY(QString gpu READ gpu NOTIFY changed)
  Q_PROPERTY(QString driver READ driver NOTIFY changed)
  Q_PROPERTY(double refreshHz READ refreshHz NOTIFY changed)
  Q_PROPERTY(double dpr READ dpr NOTIFY changed)                                 // device pixels per logical pixel on the window's monitor
  Q_PROPERTY(QString renderLoop READ renderLoop CONSTANT)
  Q_PROPERTY(QString perfMode READ perfMode WRITE setPerfMode NOTIFY changed)    // auto smooth battery
  Q_PROPERTY(QString perfInUse READ perfInUse NOTIFY changed)                    // smooth or battery
  Q_PROPERTY(QString perfNote READ perfNote NOTIFY changed)
  Q_PROPERTY(bool heavyEffects READ heavyEffects NOTIFY changed)                 // moving backgrounds, pulsing lamps
  Q_PROPERTY(int meterHz READ meterHz NOTIFY changed)
  Q_PROPERTY(bool lowPower READ lowPower NOTIFY changed)                         // the low-power profile is in use (BATTERY, or AUTO on a weak / laptop processor)
  Q_PROPERTY(bool saver READ saver NOTIFY changed)                               // Windows battery saver is on: nothing is built ahead
  Q_PROPERTY(int frameInterval READ frameInterval NOTIFY changed)                // ms: the fastest a page timer should poll for drawing
  Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
  Q_PROPERTY(double frameMs READ frameMs NOTIFY statsChanged)
  Q_PROPERTY(double frameP95 READ frameP95 NOTIFY statsChanged)
  Q_PROPERTY(double frameMax READ frameMax NOTIFY statsChanged)
 public:
  explicit Graphics(QObject* parent = nullptr);
  // before the first window: picks and sets the back end. testRun: no guard, Qt's own choice.
  static void choose(bool testRun, const QStringList& args);
  // the console's window: frame statistics, the first-frame guard, the monitor's refresh rate
  void attach(QQuickWindow* w);
  // the meter rate changes: Engine::setMeterHz is connected to this
  std::function<void(int)> onMeterHz;

  QString choice() const;
  void setChoice(const QString& c);
  QVariantList choices() const;
  QString inUse() const;
  QString startedWith() const { return s_started; }
  bool safeMode() const { return s_safe; }
  QString note() const { return s_note; }
  bool restartNeeded() const { return choice() != s_choice; }
  QString gpu() const { return s_gpu; }
  QString driver() const { return driver_.isEmpty() ? s_driver : driver_; }
  double refreshHz() const { return refresh_; }
  double dpr() const { return dpr_; }
  QString renderLoop() const;
  QString perfMode() const;
  void setPerfMode(const QString& m);
  QString perfInUse() const { return battery_ ? QStringLiteral("battery") : QStringLiteral("smooth"); }
  QString perfNote() const { return perfNote_; }
  bool lowPower() const { return battery_; }
  bool saver() const { return saver_; }
  bool heavyEffects() const { return !battery_ && inUse() != QLatin1String("software"); }
  int meterHz() const { return meterHz_; }
  int frameInterval() const { return meterHz_ > 0 ? 1000 / meterHz_ : 33; }
  double fps() const { return fps_; }
  double frameMs() const { return frameMs_; }
  double frameP95() const { return p95_; }
  double frameMax() const { return max_; }

  Q_INVOKABLE void safeModeNextStart();   // SOFTWARE SAFE MODE from the next start on
  Q_INVOKABLE void restartNow();          // starts the console again (the new back end) and quits this one
  Q_INVOKABLE QVariantMap report() const; // everything above as one map (DIAGNOSTICS, the bench)
  // frame statistics since resetFrames(): {frames, meanMs, p95Ms, maxMs, workMeanMs, workMaxMs}
  Q_INVOKABLE void resetFrames();
  Q_INVOKABLE QVariantMap frameStats() const;
  static QString apiName(const QString& v);   // "d3d11" -> "DIRECT3D 11"

 signals:
  void changed();
  void statsChanged();

 private:
  void recompute();
  void relaunch();
  void sample();
  static inline QString s_choice, s_started, s_note, s_gpu, s_driver; static inline bool s_safe = false, s_test = false;
  QPointer<QQuickWindow> win_; QTimer stats_, power_;
  mutable QMutex mu_; QElapsedTimer clock_; qint64 last_ = 0, syncAt_ = 0; std::vector<double> gaps_, work_;   // render thread -> GUI
  std::vector<double> allGaps_, allWork_;
  double refresh_ = 60, dpr_ = 1, fps_ = 0, frameMs_ = 0, p95_ = 0, max_ = 0; int meterHz_ = 30; bool battery_ = false, first_ = false, lowTdp_ = false, saver_ = false;
  QString perfNote_, driver_;
};
