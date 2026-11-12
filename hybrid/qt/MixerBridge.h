// Qt side of the hybrid mixer: attaches to the engine's shared memory, publishes the meters thirty
// times a second and writes clamped parameters. It holds no audio and never blocks the engine.
#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include "../core/shared.hpp"

class MixerBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
  Q_PROPERTY(QVariantList levels READ levels NOTIFY levelsChanged)      // 17 entries: strips 1-16, then the master
  Q_PROPERTY(int stripCount READ stripCount CONSTANT)
  Q_PROPERTY(double sampleRate READ sampleRate NOTIFY connectedChanged)
  Q_PROPERTY(double masterGainDb READ masterGainDb WRITE setMasterGainDb NOTIFY paramsChanged)
  Q_PROPERTY(int underruns READ underruns NOTIFY levelsChanged)

 public:
  explicit MixerBridge(QObject* parent = nullptr);
  bool connected() const { return alive_; }
  QVariantList levels() const { return levels_; }
  int stripCount() const { return amfb::kStrips; }
  double sampleRate() const { return alive_ ? double(b_->sampleRate.load()) : 0.0; }
  double masterGainDb() const { return alive_ ? double(b_->masterGainDb.load()) : 0.0; }
  int underruns() const { return alive_ ? int(b_->underruns.load()) : 0; }
  void setMasterGainDb(double db);

  Q_INVOKABLE QVariantMap strip(int index) const;                        // source, gainDb, pan, width, mute, solo, eqOn, eq[7]
  Q_INVOKABLE void setGainDb(int index, double db);
  Q_INVOKABLE void setPan(int index, double pan);
  Q_INVOKABLE void setWidth(int index, double width);
  Q_INVOKABLE void setMute(int index, bool on);
  Q_INVOKABLE void setSolo(int index, bool on);
  Q_INVOKABLE void setEqOn(int index, bool on);
  Q_INVOKABLE void setEqBand(int index, int band, double freq, double gainDb, double q);

 signals:
  void connectedChanged();
  void levelsChanged();
  void paramsChanged();

 private:
  void poll();
  bool ok(int index) const { return alive_ && index >= 0 && index < amfb::kStrips; }
  void bump() { b_->paramGen.fetch_add(1, std::memory_order_release); emit paramsChanged(); }

  amfb::SharedMem mem_;
  amfb::SharedBlock* b_ = nullptr;
  QTimer timer_;
  QVariantList levels_;
  bool alive_ = false;
  quint64 lastBeat_ = 0;
  int still_ = 0, retry_ = 0;
};
