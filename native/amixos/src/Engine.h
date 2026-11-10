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
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "audiomixer/dsp.hpp"
#include "audiomixer/runtime.hpp"
#include "vst3/vst3_host.hpp"
#include "vst2/vst2_host.hpp"
#include "record/recorder.hpp"
#include "fx/fx.hpp"
#include "Scope.h"
#include "Generator.h"

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
  Q_PROPERTY(QString inputDeviceName READ inputDeviceName NOTIFY stateChanged)    // the open input device, as the driver names it ("" when none)
  Q_PROPERTY(QString outputDeviceName READ outputDeviceName NOTIFY stateChanged)

  Q_PROPERTY(int deviceOutputs READ deviceOutputs NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap monitor READ monitor NOTIFY monitorChanged)                 // control room: source (0 master, 1..16 bus), db, dim, mono, mute
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
  Q_INVOKABLE void tick();   // one meter poll now (the timer does this every 33 ms; the smoke test calls it directly)
  Q_INVOKABLE void refreshLicense();            // re-reads the stored key / edition (also after the date changes)
  // for the Settings system
  void setClipThreshold(double db);             // the meters' clip point in dBFS (default -0.1)
  void setPeakHold(int ms);
  QJsonObject consoleExport() const { return consoleJson(); }
  QJsonArray scenesExport() const; QJsonArray presetsExport() const;
  void consoleImport(const QJsonObject& console, const QJsonArray& scenes, const QJsonArray& presets);
  void factoryReset();                          // the console, scenes, user presets, selection back to a new desk (saved)
  Q_INVOKABLE QVariantMap checkKey(const QString& key) const;   // genuine? which plan? upgrade / same / downgrade; nothing is stored
  Q_INVOKABLE QVariantMap activateKey(const QString& key);      // verifies and stores; {ok, reason, message}
  Q_INVOKABLE bool deactivateKey();
  Q_INVOKABLE QString licenseRequest(const QString& plan, const QString& name, const QString& email) const;   // JSON to send the vendor
  Q_INVOKABLE QString vendorKeyFingerprint() const;
  // AUTO LICENCE: the console writes its key request to <data>/key-request.json on its own and watches the data folder and ~/Downloads for a
  // key file (*.amixkey / *.key / key.txt holding an AMIX1 key) and activates it on sight. Keys are never made here: only the vendor's
  // Key Generator signs them; the console only asks and listens.
  Q_PROPERTY(bool autoLicense READ autoLicense WRITE setAutoLicense NOTIFY licenseChanged)
  Q_PROPERTY(QString autoLicenseStatus READ autoLicenseStatus NOTIFY licenseChanged)
  Q_PROPERTY(QString autoLicenseRequestPath READ autoLicenseRequestPath CONSTANT)
  bool autoLicense() const; void setAutoLicense(bool on);
  QString autoLicenseStatus() const { return autoStatus_; }
  QString autoLicenseRequestPath() const;
  Q_INVOKABLE QString writeLicenseRequest(const QString& plan, const QString& name, const QString& email);   // writes key-request.json; returns the path ("" + status on failure)
  Q_INVOKABLE int scanForKeys();                                                                              // looks now; returns how many files were tried

  Q_INVOKABLE void refreshDevices();
  Q_INVOKABLE bool open(const QString& backend, const QString& inDevice, const QString& outDevice, double sampleRate, int frames, int inChannels, int outChannels);
  // DRIVER INTELLIGENCE (engine/cpp/include/audiomixer/driver_intel.hpp): the ladder for an ASIO device, the Realtek pin and the locks learned
  Q_INVOKABLE bool openSmart(const QString& backend, const QString& inDevice, const QString& outDevice, double sampleRate, int frames, int inChannels, int outChannels);   // walks the plan until a step opens
  Q_INVOKABLE QVariantList driverPlan(const QString& backend, const QString& inDevice, const QString& outDevice) const;   // [{mode, backend, in, out, exclusive, why, fromLock, pinned}]
  Q_INVOKABLE QVariantList driverLocks() const;
  Q_INVOKABLE QVariantList driverPins() const;          // Realtek ASIO drivers seen, with their pin state
  Q_INVOKABLE void setDriverPin(const QString& name, bool on);
  Q_INVOKABLE int forgetDriverLocks(const QString& name);   // "*" = all
  Q_PROPERTY(QString driverFallback READ driverFallback WRITE setDriverFallback NOTIFY devicesChanged)   // after ASIO: "all" (WASAPI exclusive, shared, DirectSound), "dsound" (DirectSound only; WASAPI never a lock), "none"
  QString driverFallback() const; void setDriverFallback(const QString& f);
  Q_INVOKABLE QVariantList bluetoothDevices() const;        // the devices whose name says Bluetooth (headset, hands-free, A2DP, AirPods...), DirectSound first
  Q_PROPERTY(QString lastRoute READ lastRoute NOTIFY stateChanged)   // what the last start did, in plain words
  QString lastRoute() const { return lastRoute_; }
  Q_INVOKABLE void stop();
  // RECORD (engine/cpp/app/record): the master and armed strips to WAV while the engine runs
  Q_PROPERTY(bool recording READ recording NOTIFY recordChanged)
  Q_PROPERTY(double recordSeconds READ recordSeconds NOTIFY metersChanged)
  Q_PROPERTY(QStringList recordFiles READ recordFiles NOTIFY recordChanged)
  Q_PROPERTY(QString recordFolder READ recordFolder WRITE setRecordFolder NOTIFY recordChanged)
  Q_PROPERTY(bool recordFloat READ recordFloat WRITE setRecordFloat NOTIFY recordChanged)
  Q_PROPERTY(bool recordMaster READ recordMaster WRITE setRecordMaster NOTIFY recordChanged)
  Q_PROPERTY(QString recordMessage READ recordMessage NOTIFY recordChanged)
  bool recording() const { return recorder_.recording(); }
  double recordSeconds() const { return recorder_.seconds(); }
  QStringList recordFiles() const { QStringList l; for (const auto& f : recorder_.files()) l << QString::fromStdString(f); return l; }
  QString recordFolder() const; void setRecordFolder(const QString& f) { settings_.setValue("record/folder", f); emit recordChanged(); }
  bool recordFloat() const { return settings_.value("record/float32", false).toBool(); } void setRecordFloat(bool v) { settings_.setValue("record/float32", v); emit recordChanged(); }
  bool recordMaster() const { return settings_.value("record/master", true).toBool(); } void setRecordMaster(bool v) { settings_.setValue("record/master", v); emit recordChanged(); }
  QString recordMessage() const { return recMsg_; }
  Q_INVOKABLE bool startRecording();
  Q_INVOKABLE void stopRecording();
  Q_INVOKABLE void setArmed(int strip, bool on);
  Q_INVOKABLE bool armed(int strip) const { return strip >= 0 && strip < kStrips && ((armed_ >> strip) & 1u); }
  Q_INVOKABLE int armedCount() const { int n = 0; for (int i = 0; i < kStrips; ++i) if ((armed_ >> i) & 1u) ++n; return n; }
  // EFFECTS: one built-in effect per bus (reverb / delay / chorus / drive), before the bus returns to the master
  Q_INVOKABLE QVariantList fxCatalog() const;                 // [{kind, name}]
  Q_INVOKABLE QVariantMap busFx(int bus) const;               // {kind, name, bypass, mix, params: [{id, title, units, value, text, default}]}
  Q_INVOKABLE void setBusFx(int bus, const QString& kind);    // "" removes
  Q_INVOKABLE void setBusFxParam(int bus, const QString& id, double value);
  Q_INVOKABLE void setBusFxBypass(int bus, bool on);
  Q_INVOKABLE void setBusFxMix(int bus, double wet);
  Q_INVOKABLE QVariantList fxPresets() const;                 // the 300 presets: [{id, name, kind, category, mix}]
  Q_INVOKABLE bool applyFxPreset(int bus, int presetId);      // loads the kind on the bus (if different) and sets every value
  Q_INVOKABLE int busFxPreset(int bus) const { return bus >= 0 && bus < kBuses ? buses_[bus].fxPreset : 0; }
  // WAVEFORM / PHASE: the scope tap
  Q_PROPERTY(double width READ width WRITE setWidth NOTIFY stripsChanged)   // master stereo width 0..2
  double width() const { return width_; } void setWidth(double w);
  // AI MIX: gain-sharing automixer membership per strip and the master AUTO LEVEL (short-term loudness toward a target under the limiter)
  Q_INVOKABLE void setAutomix(int strip, bool on, double weight);
  Q_PROPERTY(bool autoLevelOn READ autoLevelOn WRITE setAutoLevelOn NOTIFY stripsChanged)
  Q_PROPERTY(double autoLevelTarget READ autoLevelTarget WRITE setAutoLevelTarget NOTIFY stripsChanged)   // LUFS, -30 .. -6
  Q_PROPERTY(double autoLevelMax READ autoLevelMax WRITE setAutoLevelMax NOTIFY stripsChanged)         // dB of lift at most, 0 .. 24
  bool autoLevelOn() const { return autoOn_; } void setAutoLevelOn(bool v) { autoOn_ = v; pushAutoLevel(); emit stripsChanged(); }
  double autoLevelTarget() const { return autoTarget_; } void setAutoLevelTarget(double v) { autoTarget_ = std::clamp(v, -30.0, -6.0); pushAutoLevel(); emit stripsChanged(); }
  double autoLevelMax() const { return autoMax_; } void setAutoLevelMax(double v) { autoMax_ = std::clamp(v, 0.0, 24.0); pushAutoLevel(); emit stripsChanged(); }
  void pushAutoLevel() { if (rt_) rt_->setAutoLevel(autoOn_, autoTarget_, autoMax_); }
  Q_INVOKABLE QVariantMap scopeFrame(int channel, double windowMs, int points, bool trigger);   // {samples: [..], vpp, vrms, freq, crest}
  Q_INVOKABLE QVariantList lissajous(int points);                                                // [l0, r0, l1, r1, ...]
  Q_INVOKABLE void setScopeStrip(int strip) { scope_.setStrip(strip); }
  // AUTO DETECT: the best device to start with, without a remembered one: a native ASIO driver (not a wrapper, Realtek pinned) >
  // ASIO4ALL-style wrapper > WASAPI / Core Audio / ALSA default endpoints > nothing (null is never auto-picked)
  Q_INVOKABLE QVariantMap autoDetect() const;      // {backend, in, out, name, why, inChannels, outChannels, rate} or empty
  Q_INVOKABLE bool autoStart(int frames);           // detect and SMART START it
  // an extra stereo source summed into the master (the ARRANGE player): attached to every runtime this Engine opens
  void setExtraSource(am::Runtime::ExtraSource* s) { extra_ = s; sources_.a.store(s); }
  void setMediaSource(am::Runtime::ExtraSource* s) { sources_.c.store(s); }
  void setSampleSource(am::Runtime::ExtraSource* s) { sources_.d.store(s); }
  // OUTPUT STAGE + PROFILER (SOUND CARD page): DC removal before the limiter, TPDF dither for 16 / 24-bit devices, block-time figures in telemetry
  Q_PROPERTY(bool dcBlock READ dcBlock WRITE setDcBlock NOTIFY stateChanged)
  Q_PROPERTY(int outputBits READ outputBits WRITE setOutputBits NOTIFY stateChanged)   // 0 float, 16, 24
  bool dcBlock() const { return settings_.value("audio/dcBlock", true).toBool(); } void setDcBlock(bool v) { settings_.setValue("audio/dcBlock", v); if (rt_) rt_->setDcBlock(v); emit stateChanged(); }
  int outputBits() const { return settings_.value("audio/outputBits", 0).toInt(); } void setOutputBits(int b) { b = b == 16 || b == 24 ? b : 0; settings_.setValue("audio/outputBits", b); if (rt_) rt_->setOutputBits(b); emit stateChanged(); }
  Q_INVOKABLE void resetProfiler() { if (rt_) rt_->resetProfiler(); }
  Q_INVOKABLE void setNullTestInput(bool on) { nullTone_ = on; }   // the null device delivers a -12 dBFS test signal on inputs 1-2 (sine 440 / 660 Hz) instead of silence: meters and faders show something (--shots, demos)
  bool nullTestInput() const { return nullTone_; }
  Q_INVOKABLE QVariantList spectrum(int channel);
  Q_INVOKABLE QVariantMap roundTrip() const { double ms = 0, db = -120; QVariantMap m; m["ok"] = scope_.roundTrip(ms, db); m["ms"] = ms; m["returnDb"] = db; m["frames"] = blockFrames(); m["rate"] = sampleRate(); return m; }
  Q_PROPERTY(double sampleRate READ sampleRate NOTIFY stateChanged)
  Q_PROPERTY(int blockFrames READ blockFrames NOTIFY stateChanged)
  double sampleRate() const { return rt_ ? rt_->sampleRate() : 0.0; } int blockFrames() const { return rt_ ? blockFrames_ : 0; }    // 31 third-octave bands in dBFS, 20 Hz .. 20 kHz; channel 0 master L, 1 R, 2 the scope strip
  // TOOLS: test generator and metronome, summed into the master (pre master fader) with the ARRANGE player
  Q_PROPERTY(bool testOn READ testOn WRITE setTestOn NOTIFY genChanged)
  Q_PROPERTY(int testType READ testType WRITE setTestType NOTIFY genChanged)        // 0 sine, 1 pink, 2 white, 3 polarity pulse
  Q_PROPERTY(double testFreq READ testFreq WRITE setTestFreq NOTIFY genChanged)
  Q_PROPERTY(double testDb READ testDb WRITE setTestDb NOTIFY genChanged)
  Q_PROPERTY(bool clickOn READ clickOn WRITE setClickOn NOTIFY genChanged)
  Q_PROPERTY(double clickBpm READ clickBpm WRITE setClickBpm NOTIFY genChanged)
  Q_PROPERTY(int clickBeats READ clickBeats WRITE setClickBeats NOTIFY genChanged)
  Q_PROPERTY(double clickDb READ clickDb WRITE setClickDb NOTIFY genChanged)
  Q_PROPERTY(int clickBeat READ clickBeat NOTIFY metersChanged)
  bool testOn() const { return gen_.testOn.load(); } void setTestOn(bool v) { gen_.testOn.store(v); emit genChanged(); }
  int testType() const { return gen_.testType.load(); } void setTestType(int v) { gen_.testType.store(std::clamp(v, 0, 3)); emit genChanged(); }
  double testFreq() const { return gen_.testFreq.load(); } void setTestFreq(double v) { gen_.testFreq.store(std::clamp(v, 10.0, 20000.0)); emit genChanged(); }
  double testDb() const { return gen_.testDb.load(); } void setTestDb(double v) { gen_.testDb.store(std::clamp(v, -60.0, 0.0)); emit genChanged(); }
  bool clickOn() const { return gen_.clickOn.load(); } void setClickOn(bool v) { gen_.clickOn.store(v); emit genChanged(); }
  double clickBpm() const { return gen_.bpm.load(); } void setClickBpm(double v) { gen_.bpm.store(std::clamp(v, 20.0, 300.0)); emit genChanged(); }
  int clickBeats() const { return gen_.beatsPerBar.load(); } void setClickBeats(int v) { gen_.beatsPerBar.store(std::clamp(v, 1, 12)); emit genChanged(); }
  double clickDb() const { return gen_.clickDb.load(); } void setClickDb(double v) { gen_.clickDb.store(std::clamp(v, -60.0, 0.0)); emit genChanged(); }
  int clickBeat() const { return gen_.beat.load(); }
  // MIC EQ: the 10 bands of a strip as a graphic equaliser on the ISO octave centres (31.5 Hz .. 16 kHz)
  Q_INVOKABLE void setGraphicEq(int strip, const QVariantList& gainsDb, bool on);   // 10 gains; on = false keeps them but switches the bands off
  Q_INVOKABLE QVariantList graphicEq(int strip) const;                             // the 10 gains (0 for a band that is not on an octave centre)
  Q_INVOKABLE void copyEqToAll(int strip);
  // EQ PRESETS: slots 1-10 are the factory set (locked: AmiX W Shape, AmiX Golden EQ, ...), slots 11-20 are the user's own (SAVE from a strip, CLEAR).
  // A preset holds the 10 parametric bands and the high-pass / low-pass of the crossover; it is applied to one strip.
  Q_INVOKABLE QVariantList eqPresets() const;                        // [{slot, name, locked, empty, bands: n}]
  Q_INVOKABLE bool applyEqPreset(int slot, int strip);
  Q_INVOKABLE bool saveEqPreset(int slot, const QString& name, int strip);   // user slots only
  Q_INVOKABLE bool clearEqPreset(int slot);
  Q_INVOKABLE int stripEqPreset(int strip) const { return strip >= 0 && strip < kStrips ? strips_[static_cast<std::size_t>(strip)].eqPreset : 0; }   // the last preset applied (0 after an edit)
  Q_INVOKABLE QVariantList eqPresetCurve(int slot, int points) const;        // [{f, db}] the preset's response, for the picker
  // RTA EQ (the beta's "10-band program bus EQ & diamond RTA"): ten custom graphic-EQ slots, each a name + 10 ISO band gains
  Q_INVOKABLE QVariantList geqSlots() const;                                 // [{slot 1..10, name, gains: [10], empty}]
  Q_INVOKABLE bool saveGeqSlot(int slot, const QString& name, const QVariantList& gains);
  Q_INVOKABLE bool clearGeqSlot(int slot);                                          // EQ bands + high-pass of this strip onto every other strip
  am::Runtime* runtime() { return rt_.get(); }
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
  Q_INVOKABLE QString inputLabel(int input) const;   // "UM2 · IN 3": the short device name + the channel, for a strip's INPUT chip
  QString inputDeviceName() const { return inDevName_; } QString outputDeviceName() const { return outDevName_; }
  Q_INVOKABLE void setInputRoute(int strip, int input);
  Q_INVOKABLE void setOutputRoute(int out, int source);
  Q_INVOKABLE void resetRouting();                      // input s -> strip s, monitor on 1-2, bus k on 3+2k / 4+2k
  Q_INVOKABLE void setMonitor(int source, double db, bool dim, bool mono, bool mute);
  int vuRefDb() const { return vuRef_; }
  void setVuRefDb(int db) { vuRef_ = std::clamp(db, -24, -8); settings_.setValue("ui/vuRef", vuRef_); emit monitorChanged(); }
  // MORE menu on a strip
  Q_INVOKABLE void resetStrip(int strip);            // fader 0, pan centre, unmuted, DSP flat, sends off, no groups; the name and colour stay
  Q_INVOKABLE void resetStripDsp(int strip);         // only the channel DSP
  // UNDO / REDO of console changes (a snapshot after each burst of changes, 100 deep) and A / B compare
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
  Q_PROPERTY(QString abSlot READ abSlot NOTIFY historyChanged)   // "A" or "B"
  bool canUndo() const { return histPos_ > 0; } bool canRedo() const { return histPos_ + 1 < static_cast<int>(history_.size()); } QString abSlot() const { return abSlot_; }
  Q_INVOKABLE void recordHistory();                   // snapshot now (the timer does it 400 ms after a change)
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void toggleAB();                        // swap the console with the other slot (the first toggle copies the current console to B)
  // stereo LINK: strips 2k and 2k+1 follow each other (gain, mute, solo; pan mirrored)
  Q_INVOKABLE void setLink(int strip, bool on);
  Q_INVOKABLE bool linked(int strip) const;
  // input TRIM before the channel DSP (-24 .. +24 dB)
  Q_INVOKABLE void setTrim(int strip, double db);
  Q_INVOKABLE double trim(int strip) const;
  // global mixer actions (the MIXER toolbar)
  Q_INVOKABLE void muteAll(bool on);
  Q_INVOKABLE void clearSolo();
  Q_INVOKABLE void resetFaders();                     // every strip and the master to 0 dB, pans centre
  Q_INVOKABLE void flattenAllDsp();
  // PLUGIN slots: 10 per strip. A slot holds a built-in stage (gate | eq | xover | comp | limiter | delay | polarity: inserting it switches that
  // stage on, removing it switches it off) or a VST3 reference ("vst3:<path>"), kept for the VST3 host that is not built yet (shown as pending).
  static constexpr int kPluginSlots = 10;
  Q_INVOKABLE QVariantList plugins(int strip) const;  // [{slot, kind, name, bypass, builtIn, pending}]
  Q_INVOKABLE void setPlugin(int strip, int slot, const QString& kind);   // "" clears the slot
  Q_INVOKABLE void setPluginBypass(int strip, int slot, bool bypass);
  Q_INVOKABLE QVariantList pluginCatalog() const;     // the built-in stages, for the chooser
  Q_INVOKABLE void movePlugin(int strip, int from, int to);   // reorder the rack (swap two slots)
  // VST3 (engine/cpp/app/vst3): plug-ins found in the VST3 folders (Settings adds more), loaded into a slot as "vst3:<module path>|<class id>"
  Q_INVOKABLE QVariantList vstCatalog() const;        // [{kind, name, vendor, module, cid, format}] from the last scan: VST3 and VST2
  Q_INVOKABLE int rescanVst(const QStringList& extraDirs);   // scans the platform folders + extraDirs; returns the plug-in count
  Q_INVOKABLE QVariantList pluginParams(int strip, int slot) const;   // [{id, title, units, value (normalized), text, stepCount, bypass, hidden, readOnly}]
  Q_INVOKABLE void setPluginParam(int strip, int slot, int id, double normalized);
  Q_INVOKABLE QString pluginError(int strip, int slot) const;
  Q_INVOKABLE int pluginCount(int strip) const;
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
  void historyChanged();
  void recordChanged();
  void genChanged();
  void clipboardChanged();

 private:
  static constexpr int kStrips = 32;
  static constexpr int kBuses = 16, kGroups = 8;   // buses 0-7 AUX, 8-15 FX
  struct SendUi { double db = -60; bool pre = false; };
  struct PluginUi { QString kind; bool bypass = false; std::shared_ptr<am::host::Plugin> vst; QString state, error; };   // vst: loaded instance (null = pending with error)
  std::vector<QVariantMap> vstCatalog_; std::vector<std::shared_ptr<am::host::Plugin>> graveyard_; QTimer graveTimer_;
  bool loadVst(int strip, int slot);                      // from kind + state at the runtime's rate; false -> error set
  void rebuildInserts(int strip);                         // the strip's VST chain into the runtime
  void reloadAllVst();                                    // after open (new rate / block)
  struct StripUi { QString name; double db = 0, pan = 0; bool mute = false, solo = false; am::dsp::Channel dsp{48000}; SendUi sends[kBuses]; int dca = 0; unsigned muteGroups = 0; int color = 0; PluginUi plugins[kPluginSlots]; bool automix = false; double amWeight = 1; bool link = false; double trimDb = 0; int eqPreset = 0; };
  std::vector<QJsonObject> history_; int histPos_ = -1; bool restoring_ = false; QTimer histTimer_; QJsonObject abOther_; QString abSlot_ = "A";
  int partner(int i) const { return (i ^ 1) < kStrips ? (i ^ 1) : -1; }
  void applyPluginStage(int strip, const QString& kind, bool on);   // a built-in stage follows its slot
  struct BusUi { QString name; double db = 0, returnDb = -60; bool mute = false; QString fxKind; std::unique_ptr<am::fx::Effect> fx; double fxMix = 1; bool fxBypass = false; std::map<QString, double> fxValues; int fxPreset = 0; };
  std::vector<std::unique_ptr<am::fx::Effect>> fxGraveyard_; double width_ = 1.0; ScopeTap scope_; TapFanout taps_; Generator gen_; SourceFanout sources_; int blockFrames_ = 0; bool autoOn_ = false; double autoTarget_ = -16, autoMax_ = 12;
  void makeBusFx(int k);   // (re)creates the bus effect at the runtime's rate from kind + values and attaches it
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
  int inRoute_[kStrips] = {}; int outRoute_[kOutputs] = {}; int devIn_ = 0, devOut_ = 0; QString inDevName_, outDevName_; void rememberDeviceNames(const std::string& backend, const std::string& in, const std::string& out);
  QVariantMap license_; int licensedChannels_ = 8;
  struct EqPresetUi { QString name; am::dsp::EqBand bands[10]; int hpOrder = 0; double hpFreq = 80; int lpOrder = 0; double lpFreq = 16000; bool used = false; };
  static const EqPresetUi* factoryEqPreset(int slot); EqPresetUi userEq_[10]; void loadUserEq(); void storeUserEq() const;
  bool nullTone_ = false; void nullInputFor(am::DeviceConfig& cfg) const;
  QString autoStatus_ = "off"; class QFileSystemWatcher* keyWatch_ = nullptr; QTimer keyScan_;
  QString lastRoute_; am::Runtime::ExtraSource* extra_ = nullptr; am::rec::Recorder recorder_; unsigned armed_ = 0; QString recMsg_; double clipDb_ = -0.1; int peakHoldMs_ = 1400;
  struct MonitorUi { int source = 0; double db = 0; bool dim = false, mono = false, mute = false; } mon_;
  int vuRef_ = -18; double vu_[kStrips] = {}; double vuMaster_ = 0;                     // VU ballistics (300 ms to 99 %), run on the UI timer
  std::optional<QJsonObject> clipboard_;
  QTimer timer_;
  mutable QSettings settings_;
};
