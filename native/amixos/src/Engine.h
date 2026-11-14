// Engine: the one object the QML interface talks to. Wraps am::Runtime (engine/cpp): devices, open / start / stop, strip and master control,
// channel DSP (parametric EQ, crossover, delay, polarity, limiter), and the meters published 30 times a second as plain properties.
// Everything here runs on the GUI thread; the audio thread is inside am::Runtime and is never blocked by this object (control goes through
// the runtime's lock-free ring, meters come out of its sequence-counted snapshot). Settings (strip names, levels, DSP) persist with QSettings.
#pragma once
#include <algorithm>
#include <QObject>
#include <functional>
#include <QSettings>
#include <QTimer>
#include <QWindow>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <map>
#include <memory>
#include <optional>
#include "audiomixer/dynamics.hpp"
#include <vector>

#include "audiomixer/dsp.hpp"
#include "audiomixer/runtime.hpp"
#include "audiomixer/master_chain.hpp"
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
  Q_PROPERTY(int meterTick READ meterTick NOTIFY metersChanged)
  Q_PROPERTY(int stripTick READ stripTick NOTIFY stripsChanged)   // counts strip changes: bind "engine.stripAt(i, engine.stripTick)" to follow one strip
  Q_PROPERTY(bool anyMute READ anyMute NOTIFY stripsChanged)
  Q_PROPERTY(bool anySolo READ anySolo NOTIFY stripsChanged)
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
  // ONE strip's meters: a delegate binding to engine.meters[i] converts all 32 strips to JavaScript on every tick (32 delegates x 32 maps,
  // 30 times a second); bind to "engine.meterTick, engine.meterAt(i)" instead, which converts one map.
  Q_INVOKABLE QVariantMap meterAt(int i) const { return i >= 0 && i < meters_.size() ? meters_[i].toMap() : QVariantMap{}; }
  Q_INVOKABLE int clippedStrips() const { int n = 0; for (const QVariant& v : meters_) if (v.toMap().value("clip").toBool()) ++n; return n; }
  int meterTick() const { return meterTick_; }
  // FAST READS for the interface: engine.stripState[i] builds and converts all 32 strips (about 40 values each) on every read, and every
  // engine.telemetry.x converts the whole telemetry map; a mixer with 32 strips did that thousands of times per fader move. These read one item
  // from a cache that is rebuilt only after a change (the tick argument makes the binding follow the change).
  Q_INVOKABLE QVariantMap stripAt(int i, int tick = 0) const { Q_UNUSED(tick); const QVariantList& l = stripCache(); return i >= 0 && i < l.size() ? l[i].toMap() : QVariantMap{}; }
  Q_INVOKABLE QVariant telv(const QString& key, int tick = 0) const { Q_UNUSED(tick); return telemetry_.value(key); }
  int stripTick() const { return stripTick_; }
  // ONE STRIP REDRAWS, NOT 32: "property int ver: engine.stripVer(index, engine.stripTick)" changes only when that strip changed, and
  // "property var st: { ver; return engine.stripLite(index) }" then converts one small map. QML does not re-run a binding whose input kept its value.
  Q_INVOKABLE int stripVer(int i, int tick = 0) const { Q_UNUSED(tick); stripCache(); return i >= 0 && i < static_cast<int>(stripVer_.size()) ? stripVer_[static_cast<std::size_t>(i)] : 0; }
  Q_INVOKABLE QVariantMap stripLite(int i) const { stripCache(); return i >= 0 && i < liteCache_.size() ? liteCache_[i].toMap() : QVariantMap{}; }
  bool anyMute() const; bool anySolo() const;
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
  Q_INVOKABLE QVariantMap storeActivation(const QString& activation);   // verifies it for this computer, stores it; {ok, reason, message}
  Q_INVOKABLE bool storeRevocations(const QString& list);               // keeps the vendor's revocation list when genuine and newer
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
  // HOT-PLUG: a cheap fingerprint of what is plugged in is polled every 1.5 s (no driver is loaded); when it changes the list is rescanned and
  // hotplug(added) fires; main() then auto-starts when the engine is idle and AUTO START is on. The running device vanishing stops the engine with a note.
  Q_PROPERTY(bool hotplugWatch READ hotplugWatch WRITE setHotplugWatch NOTIFY stateChanged)
  bool hotplugWatch() const { return hotplug_.isActive(); } void setHotplugWatch(bool on);
  // NEVER STUCK: device scans and driver starts run on a worker while the interface keeps painting; a driver that does not answer in time is
  // left behind (its thread is abandoned) and the console carries on: a scan falls back to the list without loading ASIO drivers, a start reports it.
  bool guarded(std::function<bool()> work, int timeoutMs);
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  bool busy() const { return scanning_ || opening_; }
  Q_INVOKABLE QString busyText() const { return opening_ ? QStringLiteral("starting the sound card") : scanning_ ? QStringLiteral("looking for sound cards") : QString(); }
  Q_SIGNAL void busyChanged();
  Q_INVOKABLE QVariantMap ioInfo() const;   // the running device: {backend, inName, inKind, inputs, outName, outKind, outputs, blockMs}; kinds: headphones, headset, speakers, microphone, line, digital, interface, bluetooth ...
  Q_INVOKABLE void simulateDevices(const QVariantList& devices);   // tests: replace the scanned list with this one (devicesChanged fires; the fingerprint poll is paused)
  Q_INVOKABLE void checkHotplug();                                 // one poll now
  // BLUETOOTH START that does not give up at the first refusal: the endpoint's own sample rate, then without the hands-free microphone (opening
  // it drops a headset to phone quality or fails outright), then the same device on the other Windows path (DirectSound <-> WASAPI shared).
  Q_INVOKABLE bool openBluetooth(const QString& backend, const QString& inId, const QString& outId, int frames);
  Q_INVOKABLE QString bluetoothNote() const { return btNote_; }
  Q_INVOKABLE bool open(const QString& backend, const QString& inDevice, const QString& outDevice, double sampleRate, int frames, int inChannels, int outChannels);
  // DRIVER INTELLIGENCE (engine/cpp/include/audiomixer/driver_intel.hpp): the ladder for an ASIO device, the Realtek pin and the locks learned
  // A MIXER OF ANOTHER MAKER connected through its ASIO driver, used as a multichannel sound card (am::drv::mixerProfile)
  // INPUT / OUTPUT CHECK of the running sound card (an audio interface or a mixer on its ASIO driver): every input channel with the strip that
  // listens to it and the level arriving now, every output channel with what it carries and its level, and whether the engine keeps up in real
  // time (dropouts, load, block time). {running, backend, device, inputs: [{ch, strip, name, db, live}], outputs: [{ch, source, db, live}],
  // liveInputs, sameChannel, xruns, loadPct, blockMs, latencyMs, realtime, verdict}
  Q_INVOKABLE QVariantMap ioCheck() const;
  Q_INVOKABLE QVariantList mixersFound() const;          // [{id, name, maker, model, prefix, note, inChannels, outChannels, running}]
  Q_INVOKABLE bool connectMixer(const QString& id, int frames);   // opens ALL its channels (32 at most) through ASIO and assigns them
  Q_INVOKABLE int assignMixerChannels(const QString& prefix, bool nameStrips);   // desk channel n -> strip n for every input the running device has; the other strips get no input; returns how many
  Q_INVOKABLE bool openSmart(const QString& backend, const QString& inDevice, const QString& outDevice, double sampleRate, int frames, int inChannels, int outChannels);   // walks the plan until a step opens
  Q_INVOKABLE QVariantList driverPlan(const QString& backend, const QString& inDevice, const QString& outDevice) const;   // [{mode, backend, in, out, exclusive, why, fromLock, pinned}]
  Q_INVOKABLE QVariantList driverLocks() const;
  Q_INVOKABLE QVariantList driverPins() const;          // Realtek ASIO drivers seen, with their pin state
  Q_INVOKABLE void setDriverPin(const QString& name, bool on);
  Q_INVOKABLE int forgetDriverLocks(const QString& name);   // "*" = all
  Q_PROPERTY(QString driverFallback READ driverFallback WRITE setDriverFallback NOTIFY devicesChanged)   // after ASIO: "all" (WASAPI exclusive, shared, DirectSound), "dsound" (DirectSound only; WASAPI never a lock), "none"
  QString driverFallback() const; void setDriverFallback(const QString& f);
  Q_PROPERTY(bool openRouting READ openRouting WRITE setOpenRouting NOTIFY devicesChanged)   // the ASIO driver that was picked is opened first: ASIO4ALL although a manufacturer driver is there, the Realtek driver instead of its pin (default on)
  bool openRouting() const; void setOpenRouting(bool on);
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
  Q_PROPERTY(int recordFormat READ recordFormat WRITE setRecordFormat NOTIFY recordChanged)
  Q_PROPERTY(QString deviceFormat READ deviceFormat NOTIFY stateChanged)
  Q_PROPERTY(bool recordMaster READ recordMaster WRITE setRecordMaster NOTIFY recordChanged)
  Q_PROPERTY(QString recordMessage READ recordMessage NOTIFY recordChanged)
  bool recording() const { return recorder_.recording(); }
  double recordSeconds() const { return recorder_.seconds(); }
  QStringList recordFiles() const { QStringList l; for (const auto& f : recorder_.files()) l << QString::fromStdString(f); return l; }
  QString recordFolder() const; void setRecordFolder(const QString& f) { settings_.setValue("record/folder", f); emit recordChanged(); }
  // record format: 0 = 24-bit PCM, 1 = 32-bit float, 2 = 64-bit float, 3 = 32-bit PCM (the old float32 switch still reads and writes 0 / 1)
  int recordFormat() const { const int f = settings_.value("record/format", -1).toInt(); if (f >= 0 && f <= 3) return f; return settings_.contains("record/float32") ? (settings_.value("record/float32").toBool() ? 1 : 0) : 2; }   // new installs: 64-bit float
  void setRecordFormat(int f) { f = std::clamp(f, 0, 3); settings_.setValue("record/format", f); settings_.setValue("record/float32", f == 1 || f == 2); emit recordChanged(); }
  bool recordFloat() const { const int f = recordFormat(); return f == 1 || f == 2; } void setRecordFloat(bool v) { setRecordFormat(v ? 1 : 0); }
  QString deviceFormat() const;
  // OUTPUT word length (WASAPI exclusive): 32-bit integer first (the device's own word length) or 32-bit float first
  Q_PROPERTY(bool preferInt32 READ preferInt32 WRITE setPreferInt32 NOTIFY recordChanged)
  bool preferInt32() const { return settings_.value("audio/preferInt32", true).toBool(); } void setPreferInt32(bool v) { settings_.setValue("audio/preferInt32", v); emit recordChanged(); }
  // BLUETOOTH AUDIO in the device lists: on (default) or off (hidden from the lists and AUTO DETECT; a device already open stays open)
  // HEADSET MICROPHONE OFF (WASAPI): when on, a headset's own microphone is not opened with headphones / headset / Bluetooth output. Off by default: inputs 1-2 open.
  Q_PROPERTY(bool headsetMicOff READ headsetMicOff WRITE setHeadsetMicOff NOTIFY recordChanged)
  // MIC ACCESS OFF: on Windows' own audio (WASAPI, DirectSound, MME, Bluetooth) no capture device of any kind is opened: built-in microphone
  // array, headset, Bluetooth hands-free, webcam, line-in. Outputs keep playing; Windows shows no "microphone in use". ASIO interfaces are not
  // touched (their inputs are the studio's, chosen on purpose). Takes effect at the next open (the switch re-opens at once).
  Q_PROPERTY(bool micAccessOff READ micAccessOff WRITE setMicAccessOff NOTIFY recordChanged)
  bool micAccessOff() const { return settings_.value("audio/micAccessOff", false).toBool(); } void setMicAccessOff(bool v) { settings_.setValue("audio/micAccessOff", v); emit recordChanged(); }
  bool headsetMicOff() const { return settings_.value("audio/headsetMicOff", false).toBool(); } void setHeadsetMicOff(bool v) { settings_.setValue("audio/headsetMicOff", v); emit recordChanged(); }
  Q_PROPERTY(bool bluetoothAudio READ bluetoothAudio WRITE setBluetoothAudio NOTIFY devicesChanged)
  bool bluetoothAudio() const { return settings_.value("audio/bluetooth", true).toBool(); } void setBluetoothAudio(bool v) { settings_.setValue("audio/bluetooth", v); refreshDevices(); }
  // LOCAL STORE (CLOUD page): what is installed on this computer and can be switched on or off — the extension libraries next to the console and the
  // built-in rack effects. A switched-off effect leaves the rack chooser (a slot that already holds it keeps working); nothing is downloaded or installed here.
  Q_INVOKABLE QVariantList storeItems() const;   // [{id, name, kind: "extension" | "effect", installed, on, note}]
  Q_INVOKABLE void setStoreEnabled(const QString& id, bool on);
  Q_INVOKABLE QVariantList ratesFor(const QString& backend, const QString& outDevice) const;   // the rates that device takes (44.1 kHz .. 768 kHz), [] when unknown   // what the open device itself carries (16 / 24 / 32-bit integer, 32 / 64-bit float); the engine mixes in 64-bit float
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
  // AUTO SELECT: which audio system AUTO DETECT may pick from: "best" (ASIO, then Windows audio, then DirectSound), or only "asio", "wasapi",
  // "dsound" or "bluetooth". When the chosen system has no device, the best of the others is taken and the note says so.
  Q_PROPERTY(QString autoPrefer READ autoPrefer WRITE setAutoPrefer NOTIFY devicesChanged)
  QString autoPrefer() const { return settings_.value("audio/autoPrefer", "best").toString(); } void setAutoPrefer(const QString& p);
  QVariantMap autoDetectFor(const QString& prefer) const;
  // STEREO PAIR: strips 1+2, 3+4 ... as one stereo channel. matchStereoPair() gives the partner the same channel processing (EQ, gate, compressor,
  // limiter, crossover, delay) and the same rack (every plug-in with its settings and bypass), panned to the other side. While the two are
  // LINKED, a plug-in put in, taken out, bypassed or adjusted on one is done on the other too. Each side runs its own copy of a plug-in
  // (two mono instances): the two sides are not processed together by one stereo plug-in.
  Q_INVOKABLE bool matchStereoPair(int strip);
  Q_INVOKABLE bool testTone() { if (!rt_ || !rt_->running()) return false; rt_->testTone(1.0); return true; }   // one second of 1 kHz at -20 dBFS on outputs 1-2
  Q_INVOKABLE bool testToneActive() const { return rt_ && rt_->toneActive(); }
  Q_PROPERTY(QString micOffNote READ micOffNote NOTIFY stateChanged)   // set when the last open left a headset microphone off (WASAPI)
  QString micOffNote() const { return micOffNote_; }
  Q_INVOKABLE QVariantList dacsFound() const;                    // DAC detection: [{backend, id, name, outChannels, rate, maxRate, current}]
  // outputs only, exactly this DAC; rate 0 = its own rate (any up to 1100 kHz may be asked; the DAC says whether it takes it), bits 0 = automatic,
  // 16 / 24 / 32 integer, -32 float. A chosen rate or word length opens WASAPI exclusively (bit-perfect: Windows' mixer does not convert it).
  Q_INVOKABLE bool openDac(const QString& backend, const QString& id, int frames, double rate = 0, int bits = 0);
  Q_PROPERTY(double dacRate READ dacRate WRITE setDacRate NOTIFY recordChanged)
  double dacRate() const { return settings_.value("audio/dacRate", 0).toDouble(); } void setDacRate(double v) { settings_.setValue("audio/dacRate", std::clamp(v, 0.0, 1100000.0)); emit recordChanged(); }
  Q_PROPERTY(int dacBits READ dacBits WRITE setDacBits NOTIFY recordChanged)
  int dacBits() const { return settings_.value("audio/dacBits", 0).toInt(); } void setDacBits(int v) { settings_.setValue("audio/dacBits", v); emit recordChanged(); }
  // DSP MODE: "digital" = the mix leaves the master exactly as computed; "analog" = a gentle soft saturation on the master (unity gain for quiet
  // signals, peaks rounded off the way an analogue output stage does), independent of the MASTERING switch
  Q_PROPERTY(QString dspMode READ dspMode WRITE setDspMode NOTIFY masterChainChanged)
  QString dspMode() const { return mastering()["analog"].toBool() ? QStringLiteral("analog") : QStringLiteral("digital"); }
  void setDspMode(const QString& m) { QVariantMap v; v["analog"] = (m == "analog"); setMastering(v); }
  Q_INVOKABLE QVariantList asioDrivers() const;                 // discovery only: every ASIO driver found {id, name, inChannels, outChannels, rate, kind, current}
  Q_INVOKABLE bool openAsioDriver(const QString& id, int frames);
  Q_INVOKABLE int readAsioChannels(const QString& id);   // input n of the running device -> strip n; returns how many (done by itself after an ASIO open)
  Q_PROPERTY(QString asioNote READ asioNote NOTIFY stateChanged)   // what was read from the device last
  QString asioNote() const { return asioNote_; }
  Q_INVOKABLE int asioFrames(double rate) const;     // 10 ms at this rate
  Q_INVOKABLE QString bestAsioDriver() const;        // the id AUTO DETECT would pick ("" = none)
  Q_INVOKABLE bool autoOpenAsio();                   // picks the best ASIO driver and opens it on a 10 ms buffer   // the manual choice: exactly this driver, no fallback to another
  Q_INVOKABLE QVariantMap autoDetect() const;      // {backend, in, out, name, why, inChannels, outChannels, rate} or empty
  Q_INVOKABLE bool autoStart(int frames);           // detect and SMART START it
  // an extra stereo source summed into the master (the ARRANGE player): attached to every runtime this Engine opens
  void setExtraSource(am::Runtime::ExtraSource* s) { extra_ = s; sources_.a.store(s); }
  void setMediaSource(am::Runtime::ExtraSource* s) { sources_.c.store(s); }
  void setSampleSource(am::Runtime::ExtraSource* s) { sources_.d.store(s); }
  void setMeasureIo(am::Runtime::Tap* t, am::Runtime::ExtraSource* s) { taps_.c.store(t); sources_.e.store(s); }
  // 360 AUDIO: a strip around the head (headphones). az degrees (0 front, 90 right), el -90..90, dist metres, spread 0..1
  // MASTER CHAIN: the PROGRAM EQ (strip index -1 in setEq / eqBand / setGraphicEq: the RTA EQ page's own rack) and MASTERING after it
  // EQUALIZER APO: the filter text that Equalizer APO (the open-source system-wide equaliser for Windows) reads and writes: "Preamp: -3 dB",
  // "Filter 1: ON PK Fc 100 Hz Gain 3 dB Q 1.41" (PK, LSC, HSC, LPQ, HPQ, NO). strip -1 = the PROGRAM EQ. Empty path = equalizer-apo.txt in the data folder.
  Q_INVOKABLE QString exportApo(int strip, const QString& path);   // returns the file written ("" on failure)
  Q_INVOKABLE int importApo(int strip, const QString& path);       // returns how many filters were taken (0 = none / unreadable)
  Q_INVOKABLE QVariantMap mastering() const;                       // {eqOn, on, low, air, thr, ratio, attack, release, makeup, drive, gain, preset}
  Q_INVOKABLE void setMastering(const QVariantMap& m);             // any subset of those keys
  Q_INVOKABLE QStringList masteringPresets() const;
  Q_INVOKABLE void applyMasteringPreset(const QString& name);
  Q_INVOKABLE double masteringReductionDb() const { return master_.reductionDb(); }
  Q_INVOKABLE void setSpatial(int strip, bool on, double az, double el, double dist, double spread);
  Q_INVOKABLE QVariantMap spatial(int strip) const;
  // FRONT 180° · 10 SPEAKERS: ten real speakers in a dome in front (ear level -90 .. +90, three above, two below) on outputs 1-10. While on,
  // buses AUX 1-8 and FX 1-2 are the ten speaker feeds (their earlier sends, names, levels, returns and outputs 1-10 are kept and given back
  // when it is switched off); every strip feeds them by its 360 place (a strip without one sits in the centre), and the binaural stage rests.
  Q_PROPERTY(bool speakerArray READ speakerArray WRITE setSpeakerArray NOTIFY spatialMoved)
  bool speakerArray() const { return spkOn_; }
  void setSpeakerArray(bool on);
  Q_INVOKABLE QVariantList speakerLayout() const;   // [{name, az, el, out}]
  int iamfSlot(int strip) const;   // the rack slot of the strip's IAMF ELEMENT, -1 when it has none
  // 360 AUDIO, THE LISTENER AND A SCENE. HEAD TRACK: the listener's head direction (yaw to the right, pitch up, in degrees) is taken off every
  // object's direction, so the scene stays where it is in the room while the head turns. The angles come from the page (or anything that
  // sets these properties); this program reads no head-tracking sensor by itself.
  // CINEMA HALL: 20 objects on the first 20 strips. Ten FIXED POINTS, the loudspeaker places of a cinema (left, centre, right, two sides,
  // two rear, two overhead, one high centre), and ten MOVING objects that travel around the listener in the background, each on its own
  // path and speed, updated 30 times a second while the engine runs. A hall reverb on FX 8 gives the room.
  Q_PROPERTY(bool headTrack READ headTrack WRITE setHeadTrack NOTIFY spatialMoved)
  Q_PROPERTY(double headYaw READ headYaw WRITE setHeadYaw NOTIFY spatialMoved)
  Q_PROPERTY(double headPitch READ headPitch WRITE setHeadPitch NOTIFY spatialMoved)
  Q_PROPERTY(bool cinemaHall READ cinemaHall WRITE setCinemaHall NOTIFY spatialMoved)
  Q_PROPERTY(int spatialMoving READ spatialMoving NOTIFY spatialMoved)      // how many objects are travelling now
  bool headTrack() const { return headOn_; } void setHeadTrack(bool on);
  double headYaw() const { return headYaw_; } void setHeadYaw(double deg);
  double headPitch() const { return headPitch_; } void setHeadPitch(double deg);
  bool cinemaHall() const { return cinema_; } void setCinemaHall(bool on);
  int spatialMoving() const;
  Q_INVOKABLE void setSpatialOrbit(int strip, double degPerSecond);         // 0 stops the object where it is
  Q_INVOKABLE QVariantList spatialScene() const;                             // every 360 object: [{strip, az, el, dist, orbit, heardAz, heardEl}]
  Q_SIGNAL void spatialMoved();
  // OUTPUT STAGE + PROFILER (SOUND CARD page): DC removal before the limiter, TPDF dither for 16 / 24-bit devices, block-time figures in telemetry
  Q_PROPERTY(bool dcBlock READ dcBlock WRITE setDcBlock NOTIFY stateChanged)
  Q_PROPERTY(int outputBits READ outputBits WRITE setOutputBits NOTIFY stateChanged)   // -1 AUTO (the device's own word length; the default), 0 float / 32-bit, 16, 24
  Q_PROPERTY(int effectiveOutputBits READ effectiveOutputBits NOTIFY stateChanged)   // what AUTO resolved to on the open device
  bool dcBlock() const { return settings_.value("audio/dcBlock", true).toBool(); } void setDcBlock(bool v) { settings_.setValue("audio/dcBlock", v); if (rt_) rt_->setDcBlock(v); emit stateChanged(); }
  int outputBits() const { return settings_.value("audio/outputBitsMode", -1).toInt(); }
  void setOutputBits(int b) { b = b == 16 || b == 24 || b == -1 ? b : 0; settings_.setValue("audio/outputBitsMode", b); if (rt_) rt_->setOutputBits(effectiveOutputBits()); emit stateChanged(); }
  int effectiveOutputBits() const { const int b = outputBits(); return b >= 0 ? b : rt_ ? rt_->deviceOutputBits() : 0; }
  Q_INVOKABLE void resetProfiler() { if (rt_) rt_->resetProfiler(); }
  Q_INVOKABLE void setNullTestInput(bool on) { nullTone_ = on; }   // the null device delivers a -12 dBFS test signal on inputs 1-2 (sine 440 / 660 Hz) instead of silence: meters and faders show something (--shots, demos)
  bool nullTestInput() const { return nullTone_; }
  Q_INVOKABLE QVariantList spectrum(int channel);
  Q_INVOKABLE QVariantMap soundScore();   // SOUND SCORE of the master, 0 .. 100, with its six parts and a tip each (empty "valid" false while silent)
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
  // WINDOWS SOUND SYNC: with the master fader at -inf the console also mutes the Windows default output (the system's own endpoint volume, the
  // documented interface: no driver and no registry are touched), so nothing at all is heard; raising the fader puts the Windows mute back as it was.
  Q_PROPERTY(bool windowsMuteSync READ windowsMuteSync WRITE setWindowsMuteSync NOTIFY systemSoundChanged)
  Q_PROPERTY(bool windowsMuted READ windowsMuted NOTIFY systemSoundChanged)             // the Windows default output is muted now (by the fader or by Windows itself)
  Q_PROPERTY(bool windowsSoundAvailable READ windowsSoundAvailable NOTIFY systemSoundChanged)
  bool windowsMuteSync() const { return winSync_; } void setWindowsMuteSync(bool on);
  bool windowsMuted() const { return winMuted_; } bool windowsSoundAvailable() const { return winAvail_; }
  void setSystemMuteBackend(std::function<bool(bool&)> get, std::function<bool(bool)> set) { sysGet_ = std::move(get); sysSet_ = std::move(set); }   // tests
  void syncSystemSound();                 // applies the rule and reads the state (called on fader moves and twice a second)
  void restoreSystemSound();              // on exit: a mute the console set is taken back
  Q_SIGNAL void systemSoundChanged();
  Q_INVOKABLE void setColor(int strip, int color);           // 0..7 (Theme.stripColors)
  int selectedStrip() const { return selected_; }
  void setSelectedStrip(int i) { if (i < 0 || i >= kStrips || i == selected_) return; selected_ = i; emit selectedStripChanged(); }
  Q_INVOKABLE void setMasterLimiter(bool on, double ceilingDb, double releaseMs = -1);   // releaseMs < 0 keeps the current release
  Q_INVOKABLE QVariantMap masterLimiter() const;                    // {on, ceiling, release}
  Q_INVOKABLE double masterLimiterReductionDb() const { return rt_ ? rt_->limiterReductionDb() : 0.0; }
  Q_INVOKABLE double masteringDeessDb() const { return master_.deessReductionDb(); }
  QVariantMap monitor() const;
  QVariantList inputRoutes() const; QVariantList outputRoutes() const; QVariantList outputSources() const;
  int deviceInputs() const { return devIn_; } int deviceOutputs() const { return devOut_; }
  Q_INVOKABLE QString inputLabel(int input) const;   // "UM2 · IN 3": the short device name + the channel, for a strip's INPUT chip
  QString inputDeviceName() const { return inDevName_; } QString outputDeviceName() const { return outDevName_; }
  Q_INVOKABLE bool deviceEnabled(const QString& backend, const QString& id) const;
  Q_INVOKABLE void setDeviceEnabled(const QString& backend, const QString& id, bool on);   // OFF: never auto-picked, not opened
  Q_INVOKABLE void setInputRoute(int strip, int input);
  Q_INVOKABLE void setOutputRoute(int out, int source);
  Q_INVOKABLE void resetRouting();                      // input s -> strip s, monitor on 1-2, bus k on 3+2k / 4+2k
  Q_INVOKABLE void setMonitor(int source, double db, bool dim, bool mono, bool mute);
  Q_INVOKABLE void setMonitorSync(bool on);   // SYNC: master and monitor faders move together by the same dB
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
  Q_INVOKABLE void toggleMuteAll();
  Q_PROPERTY(bool allMuted READ allMuted NOTIFY stripsChanged)
  bool allMuted() const;
  Q_INVOKABLE void clearSolo();
  Q_INVOKABLE void resetFaders();                     // every strip and the master to 0 dB, pans centre
  Q_INVOKABLE void flattenAllDsp();
  // PLUGIN slots: 10 per strip. A slot holds a built-in stage (gate | eq | xover | comp | limiter | delay | polarity: inserting it switches that
  // stage on, removing it switches it off) or a VST3 reference ("vst3:<path>"), kept for the VST3 host that is not built yet (shown as pending).
  static constexpr int kPluginSlots = 64;   // the rack's capacity; what an edition may fill: pluginSlotLimit() (all editions 64 = unlimited in practice)
  Q_PROPERTY(int pluginSlotLimit READ pluginSlotLimit NOTIFY licenseChanged)
  int pluginSlotLimit() const { return kPluginSlots; }   // every edition, BASIC included, may fill the whole rack
  Q_INVOKABLE QVariantList plugins(int strip) const;  // [{slot, kind, name, bypass, builtIn, pending}]
  Q_INVOKABLE void setPlugin(int strip, int slot, const QString& kind);   // "" clears the slot
  Q_INVOKABLE int addToRack(int strip, const QString& kind);
  Q_INVOKABLE int playSampleOnStrip(int strip, int sampleId);
  Q_INVOKABLE QString loadSampleFile(int strip, int slot, const QString& pathOrUrl);   // that slot's SAMPLE PLAYER plays the user's file ("" = back to the built-in samples); "" or the error
  Q_INVOKABLE QString sampleFileOf(int strip, int slot) const;   // the SAMPLE PLAYER of that channel (added if needed) set to that sample and started; the slot, or -1   // the slot that holds that kind (already there, or put in the first free slot); -1 = the rack is full
  Q_INVOKABLE void setPluginBypass(int strip, int slot, bool bypass);
  Q_INVOKABLE QVariantList pluginCatalog() const;     // the built-in stages, for the chooser
  Q_INVOKABLE void movePlugin(int strip, int from, int to);   // reorder the rack (swap two slots)
  // VST3 (engine/cpp/app/vst3): plug-ins found in the VST3 folders (Settings adds more), loaded into a slot as "vst3:<module path>|<class id>"
  Q_INVOKABLE QVariantList vstCatalog() const;        // [{kind, name, vendor, module, cid, format}] from the last scan: VST3 and VST2
  Q_PROPERTY(int vstCount READ vstCount NOTIFY vstCatalogChanged)          // hosted VST3 / VST2 plug-ins found by the last scan
  Q_PROPERTY(bool vstScanning READ vstScanning NOTIFY vstCatalogChanged)
  int vstCount() const { return static_cast<int>(vstCatalog_.size()); } bool vstScanning() const { return vstScanning_; }
  Q_SIGNAL void vstCatalogChanged();
  Q_INVOKABLE QVariantList rackPresets(const QString& kind) const;       // factory presets of a built-in effect ("fx:reverb" ...): [{id, name, category}]
  Q_INVOKABLE bool applyRackPreset(int strip, int slot, int presetId);   // sets that slot's knobs (not its MIX)
  Q_INVOKABLE int rescanVst(const QStringList& extraDirs);   // scans the platform folders + extraDirs; returns the plug-in count
  Q_INVOKABLE QVariantList pluginParams(int strip, int slot) const;   // [{id, title, units, value (normalized), text, stepCount, bypass, hidden, readOnly}]
  Q_INVOKABLE void setPluginParam(int strip, int slot, int id, double normalized);
  Q_INVOKABLE QString pluginError(int strip, int slot) const;
  Q_INVOKABLE QString pluginParamText(int strip, int slot, int id, double normalized) const;   // what the plug-in calls that position ("250 ms", "+12 dB"): for a knob's printed scale
  Q_INVOKABLE int pluginCount(int strip) const;
  // A PLUG-IN'S OWN WINDOW: the editor its maker made, in a window of its own that can be moved and closed like any other. True when the
  // plug-in has an editor and it opened (Windows; a plug-in without one, or another system, gets the console's own knob view instead).
  Q_INVOKABLE bool openPluginWindow(int strip, int slot, bool evenHere = false);   // evenHere: also off Windows (the tests)
  Q_INVOKABLE void closePluginWindows();
  Q_INVOKABLE int pluginWindows() const { return static_cast<int>(pluginWins_.size()); }
  bool eventFilter(QObject* o, QEvent* e) override;
  Q_INVOKABLE int retryPlugins();   // tries again every plug-in slot that is waiting (its file was missing, busy or not scanned yet); returns how many came back
  Q_INVOKABLE void saveNow() { save(); settings_.sync(); }   // the console (faders, DSP, racks and what their plug-ins hold) to the settings, now
  Q_INVOKABLE void copyChannel(int strip);
  Q_INVOKABLE bool pasteChannel(int strip);          // the copied DSP + sends + pan onto this strip (not the name, colour, fader or groups)
  bool channelClipboard() const { return clipboard_.has_value(); }
  Q_INVOKABLE void clearClips();
  // channel DSP; type: 0 off, 1 peaking, 2 low shelf, 3 high shelf, 4 low pass, 5 high pass, 6 notch, 7 all-pass 1st, 8 all-pass 2nd
  Q_INVOKABLE void setEq(int strip, int band, int type, double freq, double q, double gainDb);
  // CHANNEL EQ: three bands of the strip's own (LOW shelf 100 Hz, MID bell with its frequency, HIGH shelf 10 kHz), apart from the ten MIC EQ bands
  Q_INVOKABLE QVariantMap channelEq(int strip) const;                       // {low, mid, midFreq, high, on}
  Q_INVOKABLE void setChannelEq(int strip, double lowDb, double midDb, double midFreq, double highDb, bool on = true);
  Q_INVOKABLE QVariantMap eqBand(int strip, int band) const;
  Q_INVOKABLE void setDelay(int strip, double ms);
  Q_INVOKABLE void setPolarity(int strip, bool invert);
  Q_INVOKABLE void setAutoPolarity(int strip, bool on);   // AUTO PHASE: the engine turns the strip round when it is out of phase with the mix
  Q_INVOKABLE void setCrossover(int strip, int type, int hpOrder, double hpFreq, int lpOrder, double lpFreq);
  Q_INVOKABLE void setChannelLimiter(int strip, bool on, double ceilingDb, double releaseMs);
  Q_INVOKABLE void setCompressor(int strip, bool on, double thresholdDb, double ratio, double kneeDb, double attackMs, double releaseMs, double makeupDb);
  // LIMIT CHANNELS: the channel limiter of every strip on (at each strip's own ceiling, -1 dB when it was never set) or off, in one press
  Q_INVOKABLE void limitAllChannels(bool on);
  Q_INVOKABLE bool allChannelsLimited() const;
  // AUTO -INF: every strip that has no real input on the running sound card (no input picked, or an input the card does not have) gets its
  // fader at -inf, so an unused channel cannot add noise; returns how many were pulled down
  Q_INVOKABLE int infUnused();
  // MASTER MUTE: the master to -inf and back to where it was
  Q_PROPERTY(bool masterMuted READ masterMuted WRITE setMasterMuted NOTIFY stripsChanged)
  bool masterMuted() const { return masterMuted_; } void setMasterMuted(bool on);
  Q_INVOKABLE double closeNoise(int strip);   // with nobody playing: reads the strip's input level now (hum, hiss, room) and sets the gate 6 dB above it; returns the threshold, or 0 when there is no signal to read
  Q_INVOKABLE void setGate(int strip, bool on, double thresholdDb, double rangeDb, double attackMs, double holdMs, double releaseMs);
  Q_INVOKABLE QVariantList compressorCurve(int strip, int points) const;
  // COMPRESSION TARGET: measures the strip's input (10 s) and suggests the threshold / ratio that would bring its spread to the target; it never applies itself
  Q_PROPERTY(double compTargetDb READ compTargetDb WRITE setCompTargetDb NOTIFY compTargetChanged)
  double compTargetDb() const { return compTargetDb_; }
  void setCompTargetDb(double db) { compTargetDb_ = std::clamp(db, 2.0, 30.0); emit compTargetChanged(); }
  Q_INVOKABLE QVariantMap compSuggestion(int strip) const;   // {ready, spreadDb, medianDb, targetDb, thresholdDb, ratio, verdict}
  Q_INVOKABLE bool applyCompSuggestion(int strip);           // the user's click: sets the channel compressor to the suggestion   // [{in, out}] dB, -60..0, the static transfer without make-up
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
  void compTargetChanged();
  void stateChanged();
  void devicesChanged();
  void hotplug(bool added, const QString& what);   // a device appeared (added) or went away
  void stripsChanged();
  void masterChainChanged();
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
  int meterTick_ = 0; int stripTick_ = 0; mutable QVariantList stripCache_, liteCache_; mutable bool stripDirty_ = true; mutable std::vector<int> stripVer_ = std::vector<int>(32, 0);
  const QVariantList& stripCache() const {
    if (!stripDirty_) return stripCache_;
    QVariantList fresh = buildStripState(); QVariantList lite;
    for (int i = 0; i < fresh.size(); ++i) { QVariantMap m = fresh[i].toMap(); m.remove("dsp"); lite << m;   // the mixer's view: no EQ / dynamics detail
      if (i >= stripCache_.size() || fresh[i] != stripCache_[i]) ++stripVer_[static_cast<std::size_t>(i)]; }   // only the strips that really changed get a new version
    stripCache_ = fresh; liteCache_ = lite; stripDirty_ = false; return stripCache_; }
  QVariantList buildStripState() const;
  static constexpr int kBuses = 16, kGroups = 8;   // buses 0-7 AUX, 8-15 FX
  struct SendUi { double db = -60; bool pre = false; };
  struct PluginUi { QString kind; bool bypass = false; std::shared_ptr<am::host::Plugin> vst; QString state, error; };   // vst: loaded instance (null = pending with error)
  std::vector<bool> muteKeep_; QString asioOpenId_, dacOpenId_, micOffNote_, asioNote_; bool asioReread_ = false;
  bool vstScanning_ = false; std::vector<QVariantMap> vstCatalog_; std::vector<std::shared_ptr<am::host::Plugin>> graveyard_; QTimer graveTimer_; QTimer saveTimer_; QTimer motionTimer_; struct PluginWin { QWindow* win; std::shared_ptr<am::host::Plugin> pl; int strip, slot; }; QList<PluginWin> pluginWins_; QTimer pluginIdle_; void closePluginWin(int index); bool mirror_ = false; bool headOn_ = false, cinema_ = false; double headYaw_ = 0, headPitch_ = 0; double orbit_[32] = {0}; qint64 motionAt_ = 0; void pushSpatial(int i); void moveObjects(); bool masterMuted_ = false; double masterKeepDb_ = 0;
  bool loadVst(int strip, int slot);                      // from kind + state at the runtime's rate; false -> error set
  void rebuildInserts(int strip);                         // the strip's VST chain into the runtime
  void reloadAllVst();                                    // after open (new rate / block)
  struct StripUi { bool spOn = false; double spAz = 0, spEl = 0, spDist = 1, spSpread = 0; QString name; double db = 0, pan = 0; bool autoPol = false; int overComp = 0; am::DynamicsTracker dyn; bool mute = false, solo = false; am::dsp::Channel dsp{48000}; SendUi sends[kBuses]; int dca = 0; unsigned muteGroups = 0; int color = 0; PluginUi plugins[kPluginSlots]; bool automix = false; double amWeight = 1; bool link = false; double trimDb = 0; int eqPreset = 0; };
  std::vector<QJsonObject> history_; int histPos_ = -1; bool restoring_ = false; QTimer histTimer_; QJsonObject abOther_; QString abSlot_ = "A";
  QString btNote_;
  bool scanning_ = false, opening_ = false, timedOut_ = false;
  bool winSync_ = true, winMuted_ = false, winAvail_ = false, weMuted_ = false, winBefore_ = false; std::function<bool(bool&)> sysGet_; std::function<bool(bool)> sysSet_; QTimer sysTimer_;
  am::MasterChain master_; am::MasterChain::Params mp_; QString masterPreset_; void pushMaster();
  int partner(int i) const { return (i ^ 1) < kStrips ? (i ^ 1) : -1; }
  void factoryLayout();
  double compTargetDb_ = 6; int dynTick_ = 0;   // CH 1-2 MAIN, CH 3-4 LIVE (linked pairs), CH 5-7 RESERVE
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
  double masterDb_ = 0; bool masterLim_ = true; double masterCeil_ = -0.5, masterRel_ = 100;
  static constexpr int kOutputs = 32;
  int inRoute_[kStrips] = {}; int outRoute_[kOutputs] = {}; int devIn_ = 0, devOut_ = 0; QString inDevName_, outDevName_; void rememberDeviceNames(const std::string& backend, const std::string& in, const std::string& out);
  int openBits_ = 0; bool openExclusive_ = false;
  bool spkOn_ = false; QVariantMap spkSaved_; void feedSpeakers(int strip);
  QVariantMap license_; int licensedChannels_ = 10;
  struct EqPresetUi { QString name; am::dsp::EqBand bands[10]; int hpOrder = 0; double hpFreq = 80; int lpOrder = 0; double lpFreq = 16000; bool used = false; };
  static const EqPresetUi* factoryEqPreset(int slot); EqPresetUi userEq_[10]; void loadUserEq(); void storeUserEq() const;
  bool nullTone_ = false; void nullInputFor(am::DeviceConfig& cfg) const;
  QString autoStatus_ = "off"; class QFileSystemWatcher* keyWatch_ = nullptr; QTimer keyScan_;
  QString lastRoute_; am::Runtime::ExtraSource* extra_ = nullptr; am::rec::Recorder recorder_; unsigned armed_ = 0; QString recMsg_; double clipDb_ = -0.1; int peakHoldMs_ = 1400;
  struct MonitorUi { int source = 0; double db = 0; bool dim = false, mono = false, mute = false, sync = true; } mon_;   // sync: the monitor fader moves with the master fader (and back)
  int vuRef_ = -18; double vu_[kStrips] = {}; double vuMaster_ = 0;                     // VU ballistics (300 ms to 99 %), run on the UI timer
  std::optional<QJsonObject> clipboard_;
  QTimer timer_; QTimer hotplug_; std::string fingerprint_; bool simulated_ = false;
  mutable QSettings settings_;
};
