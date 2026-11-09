// Engine: the one object the QML interface talks to. Wraps am::Runtime (engine/cpp): devices, open / start / stop, strip and master control,
// channel DSP (parametric EQ, crossover, delay, polarity, limiter), and the meters published 30 times a second as plain properties.
// Everything here runs on the GUI thread; the audio thread is inside am::Runtime and is never blocked by this object (control goes through
// the runtime's lock-free ring, meters come out of its sequence-counted snapshot). Settings (strip names, levels, DSP) persist with QSettings.
#pragma once
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <vector>

#include "audiomixer/dsp.hpp"
#include "audiomixer/runtime.hpp"

class Engine : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)
  Q_PROPERTY(QString backend READ backend NOTIFY stateChanged)
  Q_PROPERTY(int strips READ strips CONSTANT)
  Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
  Q_PROPERTY(QStringList backends READ backends CONSTANT)
  Q_PROPERTY(QVariantList stripState READ stripState NOTIFY stripsChanged)
  Q_PROPERTY(QVariantList meters READ meters NOTIFY metersChanged)
  Q_PROPERTY(QVariantMap telemetry READ telemetry NOTIFY metersChanged)
  Q_PROPERTY(double masterDb READ masterDb WRITE setMaster NOTIFY stripsChanged)
  Q_PROPERTY(QVariantList pages READ pages CONSTANT)
  Q_PROPERTY(QString edition READ edition CONSTANT)
  Q_PROPERTY(QVariantList busState READ busState NOTIFY busesChanged)
  Q_PROPERTY(QVariantList dcaState READ dcaState NOTIFY groupsChanged)
  Q_PROPERTY(QVariantList muteGroupState READ muteGroupState NOTIFY groupsChanged)
  Q_PROPERTY(QVariantList busMeters READ busMeters NOTIFY metersChanged)

 public:
  explicit Engine(QObject* parent = nullptr);
  ~Engine() override;

  bool running() const { return rt_ && rt_->running(); }
  QString status() const { return status_; }
  QString backend() const { return rt_ ? QString::fromStdString(rt_->backendName()) : QString(); }
  int strips() const { return kStrips; }
  QVariantList devices() const { return devices_; }
  QStringList backends() const;
  QVariantList stripState() const;
  QVariantList meters() const { return meters_; }
  QVariantMap telemetry() const { return telemetry_; }
  double masterDb() const { return masterDb_; }
  QVariantList pages() const;
  int pageCount() const { return pages().size(); }
  QString edition() const { return QStringLiteral("Lite"); }   // the Pro tier (editions, licence) is ported after the interface

  Q_INVOKABLE void refreshDevices();
  Q_INVOKABLE bool open(const QString& backend, const QString& inDevice, const QString& outDevice, double sampleRate, int frames, int inChannels, int outChannels);
  Q_INVOKABLE void stop();
  // strips
  Q_INVOKABLE void setGain(int strip, double db);
  Q_INVOKABLE void setPan(int strip, double pan);
  Q_INVOKABLE void setMute(int strip, bool on);
  Q_INVOKABLE void setSolo(int strip, bool on);
  Q_INVOKABLE void setName(int strip, const QString& name);
  Q_INVOKABLE void setMaster(double db);
  Q_INVOKABLE void setMasterLimiter(bool on, double ceilingDb);
  Q_INVOKABLE void clearClips();
  // channel DSP; type: 0 off, 1 peaking, 2 low shelf, 3 high shelf, 4 low pass, 5 high pass, 6 notch, 7 all-pass 1st, 8 all-pass 2nd
  Q_INVOKABLE void setEq(int strip, int band, int type, double freq, double q, double gainDb);
  Q_INVOKABLE QVariantMap eqBand(int strip, int band) const;
  Q_INVOKABLE void setDelay(int strip, double ms);
  Q_INVOKABLE void setPolarity(int strip, bool invert);
  Q_INVOKABLE void setCrossover(int strip, int type, int hpOrder, double hpFreq, int lpOrder, double lpFreq);
  Q_INVOKABLE void setChannelLimiter(int strip, bool on, double ceilingDb, double releaseMs);
  Q_INVOKABLE void setCompressor(int strip, bool on, double thresholdDb, double ratio, double kneeDb, double attackMs, double releaseMs, double makeupDb);
  Q_INVOKABLE void setGate(int strip, bool on, double thresholdDb, double rangeDb, double attackMs, double holdMs, double releaseMs);
  Q_INVOKABLE QVariantList compressorCurve(int strip, int points) const;   // [{in, out}] dB, -60..0, the static transfer without make-up
  Q_INVOKABLE QVariantList gateCurve(int strip, int points) const;
  Q_INVOKABLE QVariantMap channelParams(int strip) const;
  Q_INVOKABLE QVariantList responseCurve(int strip, int points) const;   // [{f, db}] 20 Hz .. 20 kHz, log spaced: EQ + crossover of that strip
  // AUX SENDS: 8 stereo buses; each strip sends pre or post fader; a bus has level, mute, RETURN into the master and its own output channels
  QVariantList busState() const;
  QVariantList busMeters() const { return busMeters_; }
  Q_INVOKABLE void setSend(int strip, int bus, double db, bool pre);
  Q_INVOKABLE QVariantMap send(int strip, int bus) const;
  Q_INVOKABLE void setBusLevel(int bus, double db);
  Q_INVOKABLE void setBusMute(int bus, bool on);
  Q_INVOKABLE void setBusReturn(int bus, double db);
  Q_INVOKABLE void setBusName(int bus, const QString& name);
  // GROUP FADERS: 8 DCA groups (a group fader adds to its members' faders, a group mute mutes them) and 8 mute groups
  QVariantList dcaState() const;
  QVariantList muteGroupState() const;
  Q_INVOKABLE void setDca(int strip, int group);               // 0 = none, 1..8
  Q_INVOKABLE void setDcaLevel(int group, double db);
  Q_INVOKABLE void setDcaMute(int group, bool on);
  Q_INVOKABLE void setDcaName(int group, const QString& name);
  Q_INVOKABLE void setMuteGroupMember(int strip, int group, bool member);
  Q_INVOKABLE void setMuteGroupActive(int group, bool on);
  Q_INVOKABLE double effectiveDb(int strip) const;
  Q_INVOKABLE bool effectiveMute(int strip) const;
  Q_INVOKABLE void save();

 signals:
  void stateChanged();
  void devicesChanged();
  void stripsChanged();
  void metersChanged();
  void busesChanged();
  void groupsChanged();

 private:
  static constexpr int kStrips = 32;
  static constexpr int kBuses = 8, kGroups = 8;
  struct SendUi { double db = -60; bool pre = false; };
  struct StripUi { QString name; double db = 0, pan = 0; bool mute = false, solo = false; am::dsp::Channel dsp{48000}; SendUi sends[kBuses]; int dca = 0; unsigned muteGroups = 0; };
  struct BusUi { QString name; double db = 0, returnDb = -60; bool mute = false; };
  struct DcaUi { QString name; double db = 0; bool mute = false; };
  struct MuteGroupUi { QString name; bool active = false; };
  void pushStrip(int i);   // the strip's effective gain and mute (its own + DCA + mute groups) into the runtime
  void tick();
  void load();
  void pushAll();   // every setting into a freshly opened runtime

  std::unique_ptr<am::Runtime> rt_;
  QString status_ = QStringLiteral("Not connected: choose a sound card on CONNECTIONS");
  QVariantList devices_, meters_, busMeters_;
  BusUi buses_[kBuses]; DcaUi dcas_[kGroups]; MuteGroupUi muteGroups_[kGroups];
  QVariantMap telemetry_;
  std::vector<StripUi> strips_;
  double masterDb_ = 0; bool masterLim_ = true; double masterCeil_ = -0.5;
  QTimer timer_;
  QSettings settings_;
};
