// Engine: the one object the QML interface talks to. Wraps am::Runtime (engine/cpp): devices, open / start / stop, strip and master control,
// channel DSP (parametric EQ, crossover, delay, polarity, limiter), and the meters published 30 times a second as plain properties.
// Everything here runs on the GUI thread; the audio thread is inside am::Runtime and is never blocked by this object (control goes through
// the runtime's lock-free ring, meters come out of its sequence-counted snapshot). Settings (strip names, levels, DSP) persist with QSettings.
#pragma once
#include <algorithm>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <memory>
#include <optional>
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
  Q_PROPERTY(QString edition READ edition NOTIFY licenseChanged)
  Q_PROPERTY(QVariantMap license READ license NOTIFY licenseChanged)          // state, plan, planName, channels, source, id, name, serial, expires, machine, message
  Q_PROPERTY(int licensedChannels READ licensedChannels NOTIFY licenseChanged)   // strips beyond this are locked (muted in the engine)
  Q_PROPERTY(QVariantList busState READ busState NOTIFY busesChanged)
  Q_PROPERTY(QVariantList dcaState READ dcaState NOTIFY groupsChanged)
  Q_PROPERTY(QVariantList muteGroupState READ muteGroupState NOTIFY groupsChanged)
  Q_PROPERTY(QVariantList busMeters READ busMeters NOTIFY metersChanged)
  Q_PROPERTY(QVariantList scenes READ scenes NOTIFY scenesChanged)
  Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
  Q_PROPERTY(int selectedStrip READ selectedStrip WRITE setSelectedStrip NOTIFY selectedStripChanged)
  Q_PROPERTY(QVariantList inputRoutes READ inputRoutes NOTIFY routingChanged)     // per strip: the device input it reads (-1 none)
  Q_PROPERTY(QVariantList outputRoutes READ outputRoutes NOTIFY routingChanged)   // per device output: the source index (see outputSources)
  Q_PROPERTY(QVariantList outputSources READ outputSources NOTIFY busesChanged)              // [{index, name, group}] off, master L/R, monitor L/R, bus k L/R
  Q_PROPERTY(int deviceInputs READ deviceInputs NOTIFY stateChanged)
  Q_PROPERTY(int deviceOutputs READ deviceOutputs NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap monitor READ monitor NOTIFY monitorChanged)                 // control room: source (0 master, 1..8 bus), db, dim, mono, mute
  Q_PROPERTY(bool channelClipboard READ channelClipboard NOTIFY clipboardChanged)   // a channel has been copied (MORE > COPY)
  Q_PROPERTY(int vuRefDb READ vuRefDb WRITE setVuRefDb NOTIFY monitorChanged)         // 0 VU = this many dBFS (-18 EBU, -20 SMPTE, -14 loud)

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
  QString edition() const;                      // BASIC / PRO / STUDIO / MASTER, from the licence check
  QVariantMap license() const { return license_; }
  int licensedChannels() const { return licensedChannels_; }
  Q_INVOKABLE void refreshLicense();            // re-reads the stored key / edition (also after the date changes)
  Q_INVOKABLE QVariantMap checkKey(const QString& key) const;   // genuine? which plan? upgrade / same / downgrade; nothing is stored
  Q_INVOKABLE QVariantMap activateKey(const QString& key);      // verifies and stores; {ok, reason, message}
  Q_INVOKABLE bool deactivateKey();
  Q_INVOKABLE QString licenseRequest(const QString& plan, const QString& name, const QString& email) const;   // JSON to send the vendor
  Q_INVOKABLE QString vendorKeyFingerprint() const;

  Q_INVOKABLE void refreshDevices();
  Q_INVOKABLE bool open(const QString& backend, const QString& inDevice, const QString& outDevice, double sampleRate, int frames, int inChannels, int outChannels);
  // DRIVER INTELLIGENCE (engine/cpp/include/audiomixer/driver_intel.hpp): the ladder for an ASIO device, the Realtek pin and the locks learned
  Q_INVOKABLE bool openSmart(const QString& backend, const QString& inDevice, const QString& outDevice, double sampleRate, int frames, int inChannels, int outChannels);   // walks the plan until a step opens
  Q_INVOKABLE QVariantList driverPlan(const QString& backend, const QString& inDevice, const QString& outDevice) const;   // [{mode, backend, in, out, exclusive, why, fromLock, pinned}]
  Q_INVOKABLE QVariantList driverLocks() const;
  Q_INVOKABLE QVariantList driverPins() const;          // Realtek ASIO drivers seen, with their pin state
  Q_INVOKABLE void setDriverPin(const QString& name, bool on);
  Q_INVOKABLE int forgetDriverLocks(const QString& name);   // "*" = all
  Q_PROPERTY(QString lastRoute READ lastRoute NOTIFY stateChanged)   // what the last start did, in plain words
  QString lastRoute() const { return lastRoute_; }
  Q_INVOKABLE void stop();
  // strips
  Q_INVOKABLE void setGain(int strip, double db);
  Q_INVOKABLE void setPan(int strip, double pan);
  Q_INVOKABLE void setMute(int strip, bool on);
  Q_INVOKABLE void setSolo(int strip, bool on);
  Q_INVOKABLE void setName(int strip, const QString& name);
  Q_INVOKABLE void setMaster(double db);
  Q_INVOKABLE void setColor(int strip, int color);           // 0..7 (Theme.stripColors)
  int selectedStrip() const { return selected_; }
  void setSelectedStrip(int i) { if (i < 0 || i >= kStrips || i == selected_) return; selected_ = i; emit selectedStripChanged(); }
  Q_INVOKABLE void setMasterLimiter(bool on, double ceilingDb);
  QVariantMap monitor() const;
  QVariantList inputRoutes() const; QVariantList outputRoutes() const; QVariantList outputSources() const;
  int deviceInputs() const { return devIn_; } int deviceOutputs() const { return devOut_; }
  Q_INVOKABLE void setInputRoute(int strip, int input);
  Q_INVOKABLE void setOutputRoute(int out, int source);
  Q_INVOKABLE void resetRouting();                      // input s -> strip s, monitor on 1-2, bus k on 3+2k / 4+2k
  Q_INVOKABLE void setMonitor(int source, double db, bool dim, bool mono, bool mute);
  int vuRefDb() const { return vuRef_; }
  void setVuRefDb(int db) { vuRef_ = std::clamp(db, -24, -8); settings_.setValue("ui/vuRef", vuRef_); emit monitorChanged(); }
  // MORE menu on a strip
  Q_INVOKABLE void resetStrip(int strip);            // fader 0, pan centre, unmuted, DSP flat, sends off, no groups; the name and colour stay
  Q_INVOKABLE void resetStripDsp(int strip);         // only the channel DSP
  Q_INVOKABLE void copyChannel(int strip);
  Q_INVOKABLE bool pasteChannel(int strip);          // the copied DSP + sends + pan onto this strip (not the name, colour, fader or groups)
  bool channelClipboard() const { return clipboard_.has_value(); }
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
  // SCENES: 16 snapshots of the whole console; RECALL SAFE strips are left alone by a recall
  QVariantList scenes() const;
  Q_INVOKABLE void storeScene(int i, const QString& name);
  Q_INVOKABLE bool recallScene(int i);
  Q_INVOKABLE void clearScene(int i);
  Q_INVOKABLE void renameScene(int i, const QString& name);
  Q_INVOKABLE void setRecallSafe(int strip, bool on);
  Q_INVOKABLE bool recallSafe(int strip) const;
  // PRESETS: a channel's DSP chain by name (factory + user)
  QVariantList presets() const;
  Q_INVOKABLE bool applyPreset(const QString& name, int strip);
  Q_INVOKABLE bool savePreset(const QString& name, int strip);
  Q_INVOKABLE bool deletePreset(const QString& name);
  Q_INVOKABLE void save();

 signals:
  void stateChanged();
  void devicesChanged();
  void stripsChanged();
  void metersChanged();
  void busesChanged();
  void groupsChanged();
  void scenesChanged();
  void selectedStripChanged();
  void presetsChanged();
  void monitorChanged();
  void routingChanged();
  void licenseChanged();
  void clipboardChanged();

 private:
  static constexpr int kStrips = 32;
  static constexpr int kBuses = 8, kGroups = 8;
  struct SendUi { double db = -60; bool pre = false; };
  struct StripUi { QString name; double db = 0, pan = 0; bool mute = false, solo = false; am::dsp::Channel dsp{48000}; SendUi sends[kBuses]; int dca = 0; unsigned muteGroups = 0; int color = 0; };
  struct BusUi { QString name; double db = 0, returnDb = -60; bool mute = false; };
  struct DcaUi { QString name; double db = 0; bool mute = false; };
  struct MuteGroupUi { QString name; bool active = false; };
  void pushStrip(int i);   // the strip's effective gain and mute (its own + DCA + mute groups) into the runtime
  static constexpr int kScenes = 16;
  struct SceneUi { QString name, at; QJsonObject data; };
  struct PresetUi { QString name, category; QJsonObject dsp; };
  QJsonObject stripJson(const StripUi& s) const;
  static QJsonObject dspJson(const am::dsp::ChannelParams& p);
  static am::dsp::ChannelParams dspFromJson(const QJsonObject& o);
  static void stripFromJson(StripUi& s, const QJsonObject& o);
  QJsonObject consoleJson() const;
  void applyConsoleJson(const QJsonObject& root, bool respectSafe);
  std::vector<SceneUi> scenes_; int currentScene_ = -1; int selected_ = 0; unsigned recallSafe_ = 0; std::vector<PresetUi> userPresets_;
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
  static constexpr int kOutputs = 32;
  int inRoute_[kStrips] = {}; int outRoute_[kOutputs] = {}; int devIn_ = 0, devOut_ = 0;
  QVariantMap license_; int licensedChannels_ = 8;
  QString lastRoute_;
  struct MonitorUi { int source = 0; double db = 0; bool dim = false, mono = false, mute = false; } mon_;
  int vuRef_ = -18; double vu_[kStrips] = {}; double vuMaster_ = 0;                     // VU ballistics (300 ms to 99 %), run on the UI timer
  std::optional<QJsonObject> clipboard_;
  QTimer timer_;
  QSettings settings_;
};
