// WATERFALL 3D: the spectrum's history as a surface receding in depth (log frequency across, level up, time going back), coloured by level.
// The analyser ring of the scope tap is read through the shared FFT (no second FFT, nothing allocated per frame), folded into log bands
// and kept in a fixed ring of rows. Drawn on the graphics card as one coloured triangle mesh (a surface with a bright ridge line per row);
// on the software back end, as lines (3D) or cells (from above) painted into an image. New rows come at the chosen speed (never faster
// than the display) and only while the item is shown, not frozen and the window not minimised.
#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <complex>
#include <vector>

#include "measure/waterfall.hpp"

class Engine;

class Waterfall3D : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QObject* engine READ engineObj WRITE setEngineObj NOTIFY configChanged)
  Q_PROPERTY(int source READ source WRITE setSource NOTIFY configChanged)            // -1 master, 0.. strip, 1000 + k bus k, 2000 monitor
  Q_PROPERTY(bool frozen READ frozen WRITE setFrozen NOTIFY configChanged)
  Q_PROPERTY(int history READ history WRITE setHistory NOTIFY configChanged)         // rows kept (16 .. 256)
  Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY configChanged)            // rows per second (2 .. 60)
  Q_PROPERTY(double dbMin READ dbMin WRITE setDbMin NOTIFY viewChanged)
  Q_PROPERTY(double dbMax READ dbMax WRITE setDbMax NOTIFY viewChanged)
  Q_PROPERTY(double yaw READ yaw WRITE setYaw NOTIFY viewChanged)                    // degrees about the vertical
  Q_PROPERTY(double pitch READ pitch WRITE setPitch NOTIFY viewChanged)              // 0 = from the front, 90 = from above
  Q_PROPERTY(int viewTick READ viewTick NOTIFY viewChanged)                          // for labels placed with project()
  Q_PROPERTY(double cpuMs READ cpuMs NOTIFY statsChanged)                            // the item's own work, ms per second
  Q_PROPERTY(int rows READ rows NOTIFY statsChanged)                                 // rows received so far
  Q_PROPERTY(bool live READ live NOTIFY statsChanged)                                // taking rows now
 public:
  explicit Waterfall3D(QQuickItem* parent = nullptr);
  QObject* engineObj() const;
  void setEngineObj(QObject* e);
  int source() const { return source_; }
  void setSource(int s);
  bool frozen() const { return frozen_; }
  void setFrozen(bool f);
  int history() const { return history_; }
  void setHistory(int h);
  double speed() const { return speed_; }
  void setSpeed(double s);
  double dbMin() const { return dbMin_; }
  void setDbMin(double v);
  double dbMax() const { return dbMax_; }
  void setDbMax(double v);
  double yaw() const { return view_.yawDeg; }
  void setYaw(double v);
  double pitch() const { return view_.pitchDeg; }
  void setPitch(double v);
  int viewTick() const { return viewTick_; }
  double cpuMs() const { return cpuMs_; }
  int rows() const { return rowsIn_; }
  bool live() const { return timer_.isActive(); }
  // where a frequency (Hz), a level (dB) and an age (0 newest .. 1 oldest) land in the item (for the axis labels)
  Q_INVOKABLE QPointF project(double freq, double db, double age) const;
  Q_INVOKABLE void clear();
 signals:
  void configChanged();
  void viewChanged();
  void statsChanged();
 protected:
  QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
  void geometryChange(const QRectF& n, const QRectF& o) override;
  void itemChange(ItemChange c, const ItemChangeData& d) override;
 private:
  void tick();
  void reconsider();            // the timer runs only when the item is shown, the window is up and not frozen
  void touchView() { ++viewTick_; emit viewChanged(); update(); }
  void paintSoftware(QImage& img);
  static constexpr int kBins = 128;
  static constexpr std::size_t kFft = 4096;
  QPointer<Engine> engine_; int source_ = -1; bool frozen_ = false; int history_ = 80; double speed_ = 30, dbMin_ = -96, dbMax_ = 0;
  am::measure::wf::View view_; am::measure::wf::Bands bands_; am::measure::wf::RowRing ring_; am::measure::wf::ColorMap cmap_;
  std::vector<std::complex<double>> work_; std::vector<float> power_;   // sized once
  double bandsFs_ = 0; QTimer timer_; int viewTick_ = 0, rowsIn_ = 0; QElapsedTimer statClock_; qint64 statNs_ = 0; double cpuMs_ = 0;
  QImage soft_[2]; int softAt_ = 0;   // the software back end's pictures, painted in turn (the one on screen is never painted into: no copy)
};
