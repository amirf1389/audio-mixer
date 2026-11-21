#include "fx/fx_plugin.hpp"
#include <QHash>
#include <QCryptographicHash>
#include <QSettings>
#include <QFile>
#ifdef _WIN32
#include <objbase.h>
#endif
#include <mutex>
#include <chrono>
#include <atomic>
#include <thread>
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include "Engine.h"
#include <QQuickWindow>
#include "measure/score.hpp"
#include "hires/hires.hpp"
#include "fx/speakers.hpp"
#include <QUrl>
#include "Extension.h"

#include "license/license.hpp"
#include "paths/paths.hpp"
#include "fx/presets.hpp"
#include <QFileSystemWatcher>
static bool looksBluetooth(const QString& n);
#include <QRegularExpression>
#include "audiomixer/driver_intel.hpp"
#include "audiomixer/sysopt.hpp"
#include "audiomixer/simd.hpp"
#include "audiomixer/latency.hpp"
static void warmSystemInfo();   // SYSTEM OPTIMIZATION: looks the hardware up on a background thread (defined with systemInfo)
static const am::sysopt::CpuInfo& cpuOnce();
#include <QGuiApplication>
#include <QTimer>
#include <QDir>
#include <QStandardPaths>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QFileInfo>
#include <cmath>

using am::dsp::FilterType;
using am::dsp::XoverType;

static double toDb(double lin) { return lin > 1e-9 ? 20.0 * std::log10(lin) : -120.0; }

static constexpr double kIsoCentres[10] = {31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
Engine::Engine(QObject* parent) : QObject(parent) {
  am::drv::setOpenRouting(QSettings().value("audio/openRouting", true).toBool());
  winSync_ = QSettings().value("mixer/windowsMuteSync", true).toBool(); sysGet_ = [](bool& m) { return am::systemMuteGet(m); }; sysSet_ = [](bool m) { return am::systemMuteSet(m); };
  winGain_ = QSettings().value("mixer/windowsGainMode", false).toBool(); volGet_ = [](float& f) { return am::systemVolumeGet(f); }; volSet_ = [](float f) { return am::systemVolumeSet(f); }; volSync_ = false;
  // A WINDOWS MUTE LEFT BEHIND: the fader at -inf mutes Windows too; if the console then crashed, was killed or the computer went off,
  // Windows stayed muted for every program (no sound anywhere) and the console no longer knew. The mute is written down when it is set
  // and given back here, at the next start.
  if (QSettings().value("mixer/weMutedWindows", false).toBool()) { const bool before = QSettings().value("mixer/winBefore", false).toBool(); const auto set = sysSet_; svcPost([set, before] { set(before); }); QSettings().remove("mixer/weMutedWindows"); }
  connect(this, &Engine::stripsChanged, this, [this] { stripDirty_ = true; ++stripTick_; });
  for (auto sig : {&Engine::groupsChanged, &Engine::busesChanged, &Engine::routingChanged, &Engine::recordChanged, &Engine::licenseChanged}) connect(this, sig, this, [this] { stripDirty_ = true; });   // DCA / mute groups, sends, arm and the plan feed the strip state too   // first receiver: the cache is stale before any binding reads it
  sysTimer_.setInterval(500); connect(&sysTimer_, &QTimer::timeout, this, &Engine::syncSystemSound); connect(&sysTimer_, &QTimer::timeout, this, &Engine::syncSystemVolume); sysTimer_.start();
#ifdef _WIN32
  { static std::once_flag once; std::call_once(once, [] { std::thread([] { CoInitializeEx(nullptr, COINIT_MULTITHREADED); for (;;) std::this_thread::sleep_for(std::chrono::hours(1)); }).detach(); }); }   // keeps the multithreaded COM apartment alive: the sound-card objects are made on short-lived worker threads
#endif
  strips_.resize(kStrips);
  factoryLayout();
  for (int k = 0; k < kBuses; ++k) buses_[k].name = k < 8 ? QStringLiteral("AUX %1").arg(k + 1) : QStringLiteral("FX %1").arg(k - 7);
  for (int g = 0; g < kGroups; ++g) { dcas_[g].name = QStringLiteral("DCA %1").arg(g + 1); muteGroups_[g].name = QStringLiteral("MUTE %1").arg(g + 1); }
  freshInstallReset(); load(); loadUserEq();
  // NEVER START SILENT: a master (or control room) saved at -inf is brought back to 0 dB at start; -inf is a live decision, not a start state
  if (masterDb_ <= -60.0) { masterDb_ = 0.0; masterMuted_ = false; startNote_ = QStringLiteral("The master was saved at -inf: it starts at 0 dB."); }
  if (mon_.db <= -60.0) mon_.db = 0.0;
  refreshDevices();
  ecoUi_ = settings_.value("ui/eco", false).toBool(); applyUiRate(); oneVoice_ = settings_.value("audio/oneVoice", true).toBool(); watchdog_ = settings_.value("audio/watchdog", true).toBool(); wdClock_.start();
  sysFast_ = settings_.value("sys/fastCores", true).toBool(); sysEco_ = settings_.value("sys/ecoBackground", true).toBool(); sysTrim_ = settings_.value("sys/trimMinimised", true).toBool();
  am::sysopt::setFastCoresForAudio(sysFast_); warmSystemInfo();
  vuRef_ = std::clamp(settings_.value("ui/vuRef", -18).toInt(), -24, -8);
  for (int i = 0; i < kStrips; ++i) inRoute_[i] = i;
  headroom_ = std::clamp(settings_.value("console/headroomDb", 0).toInt(), 0, 12); if (headroom_ % 3) headroom_ = 0;
  for (int o = 0; o < kOutputs; ++o) outRoute_[o] = am::Runtime::defaultOutputSource(o);
  refreshLicense();
  histTimer_.setSingleShot(true); histTimer_.setInterval(400); connect(&histTimer_, &QTimer::timeout, this, &Engine::recordHistory);
  for (auto sig : {&Engine::stripsChanged, &Engine::busesChanged, &Engine::groupsChanged, &Engine::monitorChanged, &Engine::routingChanged}) connect(this, sig, this, [this] { if (!restoring_) histTimer_.start(); });
  history_.push_back(consoleJson()); histPos_ = 0;
  // THE CONSOLE IS WRITTEN AS IT CHANGES, not only when the program closes: two seconds after the last change of a fader, a rack, a bus,
  // the master chain or the routing. A crash, a forced shutdown or an update that restarts the program then loses nothing.
  connect(&motionTimer_, &QTimer::timeout, this, &Engine::moveObjects);
  // plug-in windows: their editors are idled (VST2 editors redraw there), and a window whose plug-in left its slot (removed, replaced, reloaded) is closed
  connect(&pluginIdle_, &QTimer::timeout, this, [this] { for (int k = pluginWins_.size() - 1; k >= 0; --k) { const PluginWin& w = pluginWins_[k];
      if (strips_[static_cast<std::size_t>(w.strip)].plugins[w.slot].vst != w.pl) closePluginWin(k); else w.pl->editorIdle(); } });
  live_ = settings_.value("live/on", false).toBool(); liveStrip_ = std::clamp(settings_.value("live/strip", 0).toInt(), 0, kStrips - 1);   // closed in LIVE MODE: it starts in it again
  saveTimer_.setSingleShot(true); saveTimer_.setInterval(2000); connect(&saveTimer_, &QTimer::timeout, this, [this] { if (!restoring_) { save(); settings_.sync(); } });
  for (auto sig : {&Engine::stripsChanged, &Engine::busesChanged, &Engine::masterChainChanged, &Engine::routingChanged, &Engine::groupsChanged}) connect(this, sig, this, [this] { if (!saveTimer_.isActive()) saveTimer_.start(); });   // two seconds after the FIRST change: a stream of changes (a controller being moved for a minute) cannot keep pushing the save away
  privacyNow = [] {
#ifdef _WIN32
    // what Windows Settings → Privacy & security → Microphone holds: the device switch, the user's switch, and the one for desktop apps
    const QString k = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone"); bool any = false;
    for (const QString& root : {QStringLiteral("HKEY_LOCAL_MACHINE\\"), QStringLiteral("HKEY_CURRENT_USER\\")}) for (const QString& sub : {QString(), QStringLiteral("\\NonPackaged")}) {
      const QString v = QSettings(root + k + sub, QSettings::NativeFormat).value(QStringLiteral("Value")).toString();
      if (v.compare(QLatin1String("Deny"), Qt::CaseInsensitive) == 0) return 1; if (!v.isEmpty()) any = true; }
    return any ? 0 : -1;
#else
    return -1;
#endif
  };
  privacyOff_ = privacyNow() == 1; privacyTimer_.setInterval(3000); connect(&privacyTimer_, &QTimer::timeout, this, &Engine::checkMicPrivacy); privacyTimer_.start();
  graveTimer_.setSingleShot(true); graveTimer_.setInterval(1500); connect(&graveTimer_, &QTimer::timeout, this, [this] { graveyard_.clear(); fxGraveyard_.clear(); });
  taps_.b.store(&scope_); sources_.b.store(&gen_);   // removed plug-ins die on the UI thread, after the audio thread has let go
  QTimer::singleShot(0, this, [this] { startupScan(); });   // after the window is up: the first scan never delays the start
  connect(&timer_, &QTimer::timeout, this, &Engine::tick);
  tick();
  if (settings_.value("spatial/front10", false).toBool()) { spkOn_ = true; spkSaved_ = settings_.value("spatial/front10Saved").toMap(); }   // the speaker array was on when the program closed: the saved console already holds its feeds
}
Engine::~Engine() {
  closePluginWindows(); save(); stop();
  { std::lock_guard<std::mutex> l(svcM_); svcStop_ = true; svcQ_.clear(); } svcCv_.notify_all();
  if (svcThread_.joinable()) svcThread_.detach();   // a call stuck in the audio service must not hold the exit; the process ends anyway
}
void Engine::svcPost(std::function<void()> job) {
  { std::lock_guard<std::mutex> l(svcM_); if (svcStop_) return; svcQ_.push_back(std::move(job)); }
  if (!svcThread_.joinable()) svcThread_ = std::thread([this] {
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // this thread's own COM apartment, once
#endif
    for (;;) { std::function<void()> j; { std::unique_lock<std::mutex> l(svcM_); svcCv_.wait(l, [this] { return svcStop_ || !svcQ_.empty(); }); if (svcStop_) return; j = std::move(svcQ_.front()); svcQ_.pop_front(); } j(); } });
  svcCv_.notify_one();
}

QStringList Engine::backends() const { QStringList l; for (const auto& b : am::availableBackends()) l << QString::fromStdString(b); return l; }

void Engine::setHotplugWatch(bool on) { if (on) { if (!hotplug_.isActive()) { hotplug_.setInterval(3000); connect(&hotplug_, &QTimer::timeout, this, &Engine::checkHotplug, Qt::UniqueConnection); hotplug_.start(); } } else hotplug_.stop(); emit stateChanged(); }
void Engine::checkHotplug() {
  if (simulated_ || fpBusy_) return; fpBusy_ = true;   // on the audio service worker: enumerating endpoints can block (see svcPost)
  svcPost([this] { const std::string f = am::deviceFingerprint(); QMetaObject::invokeMethod(this, [this, f] { fpBusy_ = false; applyFingerprint(f); }, Qt::QueuedConnection); });
}
void Engine::applyFingerprint(const std::string& f) {
  if (simulated_ || f == fingerprint_) return;
  const bool first = fingerprint_.empty(); fingerprint_ = f; if (first) return;   // the first poll only learns the baseline
  const QStringList before = [&] { QStringList l; for (const QVariant& v : devices_) l << v.toMap()["name"].toString(); return l; }();
  refreshDevices();
  QStringList after; for (const QVariant& v : devices_) after << v.toMap()["name"].toString();
  QStringList added, gone; for (const QString& n : after) if (!before.contains(n)) added << n; for (const QString& n : before) if (!after.contains(n)) gone << n;
  if (rt_ && !gone.isEmpty() && (gone.contains(inDevName_) || gone.contains(outDevName_))) { const int frames = blockFrames_ > 0 ? blockFrames_ : 256; stop(); status_ = QStringLiteral("The sound card was unplugged: %1").arg(gone.join(", ")); emit stateChanged();
    if (watchdog_) QTimer::singleShot(700, this, [this, frames, gone] { if (rt_) return;   // FAILOVER: the best device left takes over
      const bool ok = autoStart(frames); ++failovers_; watchdogNote_ = ok ? QStringLiteral("WATCHDOG: %1 was unplugged; the sound moved to %2.").arg(gone.join(", "), outDevName_.isEmpty() ? QString::fromStdString(curBackend_) : outDevName_) : QStringLiteral("WATCHDOG: %1 was unplugged and no other sound card could be opened.").arg(gone.join(", "));
      emit watchdogChanged(); }); }
  if (!added.isEmpty()) emit hotplug(true, added.join(", ")); else if (!gone.isEmpty()) emit hotplug(false, gone.join(", "));
}
// UNIVERSAL ASIO DETECTION: every ASIO driver, of any brand, gets who made it and what it is (interface, desk, DJ, guitar, onboard,
// network, virtual, wrapper), a status from the live probe, and the channel counts it had the last time it was read. A driver held by
// another program (or idle and not answering) reads 0 / 0; its remembered counts (asio/seen/<name>) are shown instead, marked as such.
void Engine::annotateAsio(QVariantMap& m, bool probed) {
  const QString name = m["name"].toString(); const am::drv::AsioIdentity a = am::drv::identifyAsio(name.toStdString());
  m["maker"] = QString::fromStdString(a.maker); m["model"] = QString::fromStdString(a.model); m["asioType"] = QString::fromStdString(a.type);
  m["note"] = QString::fromStdString(a.note); m["known"] = a.known; m["own"] = a.own;
  const QString err = m.value("probeError").toString(); const int in = m["inChannels"].toInt(), out = m["outChannels"].toInt();
  const QString key = QStringLiteral("asio/seen/") + QString(name).replace('/', '_').replace('\\', '_');
  QString status;
  if (a.own || a.type == "virtual" || a.type == "network") status = QStringLiteral("virtual");
  else if (err.contains("bit")) status = QStringLiteral("other bitness");
  else if (!err.isEmpty()) status = QStringLiteral("busy");
  else if (in + out > 0) status = QStringLiteral("ready");
  else status = QStringLiteral("no channels");
  m["status"] = status; m["statusText"] = err;
  if (probed && in + out > 0 && err.isEmpty()) {   // read just now: remember it
    QVariantList rates = m.value("sampleRates").toList(); settings_.setValue(key, QVariantMap{{"in", in}, {"out", out}, {"rate", m["defaultRate"]}, {"rates", rates}, {"when", QDateTime::currentDateTime().toString(Qt::ISODate)}});
    m["lastSeen"] = false; return; }
  const QVariantMap seen = settings_.value(key).toMap();
  if (in + out == 0 && !seen.isEmpty()) { m["lastSeen"] = true; m["seenIn"] = seen["in"]; m["seenOut"] = seen["out"]; m["seenRate"] = seen["rate"]; m["seenWhen"] = seen["when"]; }
  else m["lastSeen"] = false;
}
void Engine::simulateDevices(const QVariantList& devices) { simulated_ = true; devices_.clear(); for (const QVariant& v : devices) { QVariantMap m = v.toMap(); if (!m.contains("kind")) m["kind"] = QString::fromStdString(am::classifyDevice(m["backend"].toString().toStdString(), m["name"].toString().toStdString(), -1, m["inChannels"].toInt() > 0 && m["outChannels"].toInt() == 0)); if (m["backend"].toString() == "asio") annotateAsio(m, true); devices_ << m; } emit devicesChanged(); }
bool Engine::guarded(std::function<bool()> work, int timeoutMs) {
  struct St { std::atomic<bool> done{false}; bool ok = false; }; auto st = std::make_shared<St>();
  std::thread([st, work = std::move(work)] { st->ok = work(); st->done.store(true); }).detach();
  QElapsedTimer t; t.start();
  while (!st->done.load() && t.elapsed() < timeoutMs) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); }   // the window keeps painting and answering: never "Not Responding"
  timedOut_ = !st->done.load(); return !timedOut_ && st->ok;
}
void Engine::refreshDevices() {
  if (simulated_) { emit devicesChanged(); return; }
  if (scanning_) return;   // a scan is already running (the interface stays live during it)
  scanning_ = true; emit busyChanged();
  auto found = std::make_shared<std::vector<am::DeviceInfo>>(); auto eps = std::make_shared<std::vector<am::EndpointInfo>>();
  guarded([found, eps] { *eps = am::listEndpoints(); *found = am::listDevices(); return true; }, 8000);
  absent_.clear();   // endpoints that exist but cannot be opened now: headphones on an empty jack, a device switched off in Windows
  for (const am::EndpointInfo& ep : *eps) if (ep.state != "active") absent_ << QVariantMap{{"name", QString::fromStdString(ep.name)}, {"flow", QString::fromStdString(ep.flow)}, {"state", QString::fromStdString(ep.state)}, {"kind", QString::fromStdString(ep.kind)}};
  if (timedOut_) { found = std::make_shared<std::vector<am::DeviceInfo>>(am::listDevices(false)); status_ = QStringLiteral("A driver did not answer the scan in 8 s: the list is shown without loading ASIO drivers (channel counts assumed 2 / 2). SOFT RESTART scans again."); qWarning("device scan timed out: the list was built without probing ASIO drivers"); }
  scanning_ = false; emit busyChanged();
  devices_.clear();
  for (const am::DeviceInfo& d : *found) {
    QVariantMap m; m["backend"] = QString::fromStdString(d.backend); m["id"] = QString::fromStdString(d.id); m["name"] = QString::fromStdString(d.name);
    m["inChannels"] = d.inChannels; m["outChannels"] = d.outChannels; m["defaultRate"] = d.defaultRate; m["defaultIn"] = d.defaultIn; m["defaultOut"] = d.defaultOut; m["kind"] = QString::fromStdString(d.kind);
    { static const QStringList wired{"digital", "line", "interface", "dac", "wrapper", "virtual", "app"};   // a cable or a converter: a brand in the name ("JBL", "Bose", "wireless") does not make it Bluetooth
      m["bluetooth"] = d.kind == "bluetooth" || (!wired.contains(m["kind"].toString()) && looksBluetooth(m["name"].toString())); } m["enabled"] = deviceEnabled(m["backend"].toString(), m["id"].toString());
    QVariantList rates; for (double r : d.sampleRates) rates << r; m["sampleRates"] = rates;
    if (d.backend == "asio") { m["probeError"] = QString::fromStdString(d.probeError); annotateAsio(m, !timedOut_); }
    if (!bluetoothAudio() && (m["bluetooth"].toBool() || d.kind == "bluetooth")) continue;   // BLUETOOTH AUDIO switched off
    devices_ << m;
  }
  emit devicesChanged();
}

// DEVICES ON / OFF: a device switched OFF on CONNECTIONS is never picked by AUTO DETECT / auto start (neither as the device nor as the
// paired microphone) and is not opened by hand either; kept as "backend|id" in audio/offDevices.
bool Engine::deviceEnabled(const QString& backend, const QString& id) const { return !settings_.value("audio/offDevices").toStringList().contains(backend + "|" + id); }
void Engine::setDeviceEnabled(const QString& backend, const QString& id, bool on) {
  QStringList off = settings_.value("audio/offDevices").toStringList(); const QString k = backend + "|" + id; off.removeAll(k); if (!on) off << k; settings_.setValue("audio/offDevices", off);
  for (QVariant& v : devices_) { QVariantMap m = v.toMap(); if (m["backend"].toString() == backend && m["id"].toString() == id) { m["enabled"] = on; v = m; } }
  emit devicesChanged();
}
bool Engine::open(const QString& backend, const QString& inDevice, const QString& outDevice, double sampleRate, int frames, int inChannels, int outChannels) {
  if (!opening_) { oneVoiceActed_ = false; ov_.reset(); if (!oneVoiceNote_.isEmpty()) { oneVoiceNote_.clear(); emit oneVoiceChanged(); } }   // ONE VOICE looks again on every device
  if (opening_) return false;   // a start is already under way (asked again from the interface while it waits): nothing is stopped or replaced under it
  for (const QString& dev : {inDevice, outDevice}) if (!dev.isEmpty() && dev != "-" && !deviceEnabled(backend, dev)) { status_ = QStringLiteral("That device is switched OFF on CONNECTIONS: switch it ON to use it."); emit stateChanged(); return false; }
  stop();
  // WASAPI: THE HEADSET'S MICROPHONE STAYS OFF, always. A capture device that is a headset or Bluetooth microphone is never opened on
  // WASAPI, and with headphones or a headset as the output the system's default microphone (which is the headset's own when one is
  // plugged in) is not opened either. A microphone picked by name that is not a headset's is opened as asked.
  QString inDev = inDevice; int inCh = inChannels; micOffNote_.clear();
  // NO INPUT ASKED, NO MICROPHONE: an empty input means "the system's default" only when input channels are asked for. With 0 input
  // channels and no device named it is "no input" ("-"). It used to become 2 channels of the default device here, so a start meant to be
  // outputs only (the remembered device, CONNECTIONS with no input picked, a muted input) opened the microphone by itself.
  if (inDev.isEmpty() && inCh <= 0 && backend != "asio") inDev = QStringLiteral("-");
  // 2 IN / 2 OUT at least, on every driver (ASIO, WASAPI, DirectSound, Bluetooth): a device that reported 0 channels (busy when the list was read, or a
  // driver that does not say) still opens its first pair; ASIO and DirectSound take only what the device has, so asking for two is safe
  if (inDev != "-" && inCh < 2) inCh = 2;
  if (outChannels < 2 && outDevice != "-") outChannels = 2;
  if (backend == "wasapi" && inCh > 0 && headsetMicOff() && !askedMic_) {   // not when the user pressed INPUT: MIC
    const auto kindOf = [this](const QString& id, bool capture) { for (const QVariant& v : devices_) { const QVariantMap m = v.toMap(); if (m["backend"].toString() != "wasapi") continue;
        if (id.isEmpty() ? (capture ? m["defaultIn"].toBool() && m["inChannels"].toInt() > 0 : m["defaultOut"].toBool() && m["outChannels"].toInt() > 0) : m["id"].toString() == id) return m["kind"].toString(); } return QString(); };
    const QString ik = kindOf(inDevice, true), ok = kindOf(outDevice, false);
    if (ik == "headset" || ik == "bluetooth" || (inDevice.isEmpty() && (ok == "headphones" || ok == "headset" || ok == "bluetooth"))) {
      inCh = 0; inDev.clear(); micOffNote_ = QStringLiteral("WASAPI: the headset's microphone is kept off. Outputs only."); }
  }
  if (privacyOff_ && inCh > 0 && backend != "asio" && isMicrophoneInput(inDev)) { inCh = 0; inDev.clear(); micOffNote_ = QStringLiteral("The microphone is switched off in Windows privacy, so the console opens none (outputs only). It comes back by itself when Windows allows it again."); }
  if (micAccessOff() && inCh > 0 && backend != "asio" && isMicrophoneInput(inDev)) { inCh = 0; inDev.clear(); micOffNote_ = QStringLiteral("MIC ACCESS OFF: no microphone is opened on Windows audio. Outputs only."); }
  rt_ = std::make_unique<am::Runtime>(kStrips);
  am::DeviceConfig cfg; cfg.backend = backend.isEmpty() ? "auto" : backend.toStdString(); cfg.bits = openBits_; if (openExclusive_) cfg.exclusive = true; cfg.inDevice = inDev.toStdString(); cfg.outDevice = outDevice.toStdString();
  cfg.sampleRate = sampleRate > 0 ? sampleRate : 48000; cfg.frames = static_cast<unsigned>(frames > 0 ? frames : 256); blockFrames_ = static_cast<int>(cfg.frames); cfg.inChannels = inCh; cfg.outChannels = outChannels;
  devIn_ = std::clamp(inCh, 0, kStrips); devOut_ = std::clamp(outChannels, 0, kOutputs);
  secureInputFor(cfg); inChannels = cfg.inChannels;
  std::string err; nullInputFor(cfg); rememberDeviceNames(cfg.backend, cfg.inDevice, cfg.outDevice);
  { opening_ = true; emit busyChanged(); auto pending = std::move(rt_); am::Runtime* raw = pending.get(); auto e = std::make_shared<std::string>();   // rt_ is empty while the driver starts: nothing in the interface can touch a half-open runtime
    const bool ok = guarded([raw, cfg, e] { return raw->open(cfg, *e) && raw->start(*e); }, 12000); opening_ = false; if (!timedOut_) rt_ = std::move(pending); emit busyChanged();
    if (timedOut_) { (void)pending.release();   // the driver is still inside open / start on its thread: the runtime is left to it, never destroyed under it
      status_ = QStringLiteral("The driver did not answer in 12 s (%1): the console carries on without it. Try another driver on CONNECTIONS or SOFT RESTART.").arg(QString::fromStdString(cfg.backend).toUpper()); qWarning("driver start timed out: %s", cfg.backend.c_str()); emit stateChanged(); return false; }
    err = *e;
    if (!ok) { status_ = QStringLiteral("Could not open the sound card: %1").arg(QString::fromStdString(err)); rt_.reset(); emit stateChanged(); return false; } }
  rt_->setExtraSource(&sources_);
  for (auto& s : strips_) s.dsp.setSampleRate(cfg.sampleRate);
  reloadAllVst(); pushAll();
  status_ = QStringLiteral("%1 • %2 Hz • %3 frames • %4 in / %5 out").arg(QString::fromStdString(rt_->backendName())).arg(cfg.sampleRate).arg(cfg.frames).arg(inChannels).arg(outChannels);
  timer_.start(); emit stateChanged(); return true;
}
// the first plug-in scan: never inside a driver start that is still waiting (it would run nested in that start's wait loop, with the runtime half open)
void Engine::startupScan() { if (opening_ || scanning_) { QTimer::singleShot(250, this, [this] { startupScan(); }); return; } rescanVst(settings_.value("vst3/dirs").toStringList() + settings_.value("vst2/dirs").toStringList()); }
void Engine::stop() { if (recorder_.recording()) stopRecording(); if (rt_) { rt_->setExtraSource(nullptr); rt_->setTap(nullptr); for (int k = 0; k < kBuses; ++k) rt_->setBusProcessor(k, nullptr); rt_->stop(); rt_.reset(); status_ = QStringLiteral("Stopped"); timer_.stop(); emit stateChanged(); } }

// ---- AMPLIFIER / SYSTEM PROCESSOR
static am::AmpParams ampFromMap(const QVariantMap& m) {
  am::AmpParams p; p.on = m.value("on").toBool(); p.gainDb = std::clamp(m.value("gainDb", 0).toDouble(), -60.0, 12.0); p.mute = m.value("mute").toBool(); p.invert = m.value("invert").toBool();
  p.delayMs = std::clamp(m.value("delayMs", 0).toDouble(), 0.0, am::AmpChannel::kMaxDelayMs); p.hpOrder = std::clamp(m.value("hpOrder", 0).toInt(), 0, 8); p.hpHz = std::clamp(m.value("hpHz", 80).toDouble(), 10.0, 20000.0);
  p.lpOrder = std::clamp(m.value("lpOrder", 0).toInt(), 0, 8); p.lpHz = std::clamp(m.value("lpHz", 18000).toDouble(), 20.0, 24000.0); p.type = static_cast<am::dsp::XoverType>(std::clamp(m.value("type", 1).toInt(), 0, 2));
  p.limitOn = m.value("limitOn").toBool(); p.limitDb = std::clamp(m.value("limitDb", -1).toDouble(), -30.0, 0.0); p.releaseMs = std::clamp(m.value("releaseMs", 200).toDouble(), 10.0, 2000.0);
  const QVariantList eq = m.value("eq").toList();   // [{type (0 off, 1 peaking, 2 low shelf, 3 high shelf, 6 notch), freq, q, gain}] x 8
  for (int b = 0; b < am::AmpParams::kBands && b < eq.size(); ++b) { const QVariantMap e = eq[b].toMap(); const int ty = e.value("type", 0).toInt();
    p.eq[b] = {static_cast<am::dsp::FilterType>(ty == 1 || ty == 2 || ty == 3 || ty == 6 ? ty : 0), e.value("freq", 1000).toDouble(), e.value("q", 1.4).toDouble(), e.value("gain", 0).toDouble()}; }
  return p;
}
void Engine::pushAll() {
  if (rt_) for (int o = 0; o < 64; ++o) if (!ampUi_[o].isEmpty()) rt_->setAmp(o, ampFromMap(ampUi_[o]));
  if (!rt_) return;
  for (int k = 0; k < kBuses; ++k) { rt_->setBusLevel(k, buses_[k].db); rt_->setBusMute(k, buses_[k].mute); rt_->setBusReturn(k, buses_[k].returnDb); }
  for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].automix) rt_->setAutomix(i, true, strips_[static_cast<std::size_t>(i)].amWeight);
  for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].autoPol) rt_->setAutoPolarity(i, true);
  pushAutoLevel(); pushDuck(); pushLoudAll(); rt_->setDcBlock(dcBlock()); rt_->setAnalogChannels(dspMode() == QLatin1String("analog")); rt_->setFast(fastMode()); rt_->setOutputBits(effectiveOutputBits());
  pushMaster(); rt_->setMasterProcessor(&master_);   // the master chain: program EQ (the RTA EQ rack) and mastering
  for (int i = 0; i < kStrips; ++i) {
    const StripUi& s = strips_[static_cast<std::size_t>(i)];
    pushStrip(i); rt_->setPan(i, s.pan); pushSpatial(i); rt_->setSolo(i, s.solo); rt_->setTrimDb(i, s.trimDb);
    for (int k = 0; k < kBuses; ++k) rt_->setSend(i, k, s.sends[k].db, s.sends[k].pre);
    const am::dsp::ChannelParams& p = s.dsp.params();
    for (int b = 0; b < am::dsp::kEqBands; ++b) { const auto& e = p.eq[static_cast<std::size_t>(b)]; rt_->setEqBand(i, b, e.type, e.freq, e.q, e.gainDb); }
    rt_->setDelayMs(i, p.delayMs); rt_->setPolarity(i, p.polarityInvert); rt_->setCrossover(i, p.xoverType, p.hpOrder, p.hpFreq, p.lpOrder, p.lpFreq); rt_->setChannelLimiter(i, p.limiterOn, p.limiterCeilDb, p.limiterReleaseMs);
    rt_->setCompressor(i, p.compOn, p.compThresholdDb, p.compRatio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, p.compMakeupDb); rt_->setGate(i, p.gateOn, p.gateThresholdDb, p.gateRangeDb, p.gateAttackMs, p.gateHoldMs, p.gateReleaseMs);
  }
  pushMasterLevel(); rt_->setLimiter(limiterOnNow(), ceilingNow(), masterRel_);
  rt_->setMonitor(mon_.source, mon_.db, mon_.dim, mon_.mono, mon_.mute); rt_->setClipDb(clipDb_); rt_->setPeakHoldMs(peakHoldMs_); rt_->setWidth(width_);
  scope_.setSampleRate(rt_->sampleRate()); rt_->setTap(&taps_);
  for (int k = 0; k < kBuses; ++k) makeBusFx(k);
  for (int i = 0; i < kStrips; ++i) rebuildInserts(i);
  for (int i = 0; i < kStrips; ++i) rt_->setInputRoute(i, inRoute_[i]);
  for (int o = 0; o < kOutputs; ++o) rt_->setOutputRoute(o, outRoute_[o]);
  // SPEAKERS ON A DIGITAL CABLE: when the driver names an S/PDIF / optical / coaxial / AES pair beyond outputs 1-2 and that pair is still on
  // its default routing, it plays what outputs 1-2 play (the monitor of the mix), so speakers or an amplifier on the digital cable are heard
  // at once. A pair the user routed on purpose is left alone.
  digitalOut_ = -1;
  if (const int d = am::digitalOutputPair(rt_->outputNames()); d >= 2 && d + 1 < kOutputs) {
    digitalOut_ = d;
    if (outRoute_[d] == am::Runtime::defaultOutputSource(d) && outRoute_[d + 1] == am::Runtime::defaultOutputSource(d + 1)) {
      outRoute_[d] = am::Runtime::kSrcMonL; outRoute_[d + 1] = am::Runtime::kSrcMonR; rt_->setOutputRoute(d, outRoute_[d]); rt_->setOutputRoute(d + 1, outRoute_[d + 1]); emit routingChanged(); } }
}

QVariantList Engine::stripState() const { return stripCache(); }
bool Engine::anyMute() const { for (const StripUi& s : strips_) if (s.mute) return true; return false; }
bool Engine::anySolo() const { for (const StripUi& s : strips_) if (s.solo) return true; return false; }
// one strip as the interface reads it (any strip the engine holds, also one the console does not show now)
QVariantMap Engine::stripMap(int i) const {
  const StripUi& s = strips_[static_cast<std::size_t>(std::clamp(i, 0, kStrips - 1))]; QVariantMap m; m["index"] = i; m["autoPol"] = s.autoPol; m["name"] = s.name; m["db"] = s.db; m["pan"] = s.pan; m["mute"] = s.mute; m["solo"] = s.solo; m["dsp"] = s.dsp.active(); m["dca"] = s.dca; m["muteGroups"] = static_cast<int>(s.muteGroups); m["effectiveDb"] = effectiveDb(i); m["effectiveMute"] = effectiveMute(i); m["color"] = s.color; m["plugins"] = pluginCount(i); m["link"] = s.link; m["trimDb"] = s.trimDb; m["armed"] = armed_.test(static_cast<std::size_t>(i)); m["automix"] = s.automix; m["amWeight"] = s.amWeight; m["loudRide"] = loudUi_.strips[std::clamp(i, 0, kStrips - 1)].ride; m["loudGate"] = loudUi_.strips[std::clamp(i, 0, kStrips - 1)].gate;
    const auto& p = s.dsp.params(); bool eqOn = false; for (const auto& b : p.eq) eqOn = eqOn || b.type != am::dsp::FilterType::Off;
    m["gateOn"] = p.gateOn; m["compOn"] = p.compOn; m["limOn"] = p.limiterOn; m["eqOn"] = eqOn; m["xoverOn"] = p.hpOrder > 0 || p.lpOrder > 0; m["delayOn"] = p.delayMs > 0; m["polarity"] = p.polarityInvert;
    QVariantList sb; for (int k = 0; k < kBuses; ++k) if (s.sends[k].db > -60) sb << (k + 1); m["sendsTo"] = sb; return m;
}
QVariantList Engine::buildStripState() const {
  QVariantList l;
  for (int i = 0, nShown = shownStrips(); i < nShown; ++i) l << stripMap(i);
  return l;
}
// ── aux sends ──
QVariantList Engine::busState() const { QVariantList l; for (int k = 0; k < kBuses; ++k) { QVariantMap m; m["index"] = k; m["name"] = buses_[k].name; m["db"] = buses_[k].db; m["returnDb"] = buses_[k].returnDb; m["mute"] = buses_[k].mute; m["outputs"] = QStringLiteral("OUT %1-%2").arg(3 + 2 * k).arg(4 + 2 * k); m["kind"] = k < 8 ? "aux" : "fx"; m["fx"] = buses_[k].fxKind; m["fxName"] = buses_[k].fx ? QString::fromLatin1(buses_[k].fx->name()) : QString(); l << m; } return l; }
void Engine::setSend(int i, int k, double db, bool pre) { if (i < 0 || i >= kStrips || k < 0 || k >= kBuses) return; SendUi& sd = strips_[static_cast<std::size_t>(i)].sends[k]; sd.db = std::clamp(db, -60.0, 10.0); sd.pre = pre; if (rt_) rt_->setSend(i, k, sd.db, sd.pre); emit stripsChanged(); }
QVariantMap Engine::send(int i, int k) const { QVariantMap m; if (i < 0 || i >= kStrips || k < 0 || k >= kBuses) return m; const SendUi& sd = strips_[static_cast<std::size_t>(i)].sends[k]; m["db"] = sd.db; m["pre"] = sd.pre; return m; }
void Engine::setBusLevel(int k, double db) { if (k < 0 || k >= kBuses) return; buses_[k].db = std::clamp(db, -60.0, 10.0); if (rt_) rt_->setBusLevel(k, buses_[k].db); emit busesChanged(); }
void Engine::setBusMute(int k, bool on) { if (k < 0 || k >= kBuses) return; buses_[k].mute = on; if (rt_) rt_->setBusMute(k, on); emit busesChanged(); }
void Engine::setBusReturn(int k, double db) { if (k < 0 || k >= kBuses) return; buses_[k].returnDb = std::clamp(db, -60.0, 10.0); if (rt_) rt_->setBusReturn(k, buses_[k].returnDb); emit busesChanged(); }
void Engine::setBusName(int k, const QString& n) { if (k < 0 || k >= kBuses) return; buses_[k].name = n.left(16); emit busesChanged(); }
// ── group faders ──
QVariantList Engine::dcaState() const {
  QVariantList l;
  for (int g = 0; g < kGroups; ++g) { QVariantMap m; m["index"] = g + 1; m["name"] = dcas_[g].name; m["db"] = dcas_[g].db; m["mute"] = dcas_[g].mute; QVariantList mem; for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].dca == g + 1) mem << i; m["members"] = mem; m["count"] = mem.size(); m["color"] = dcas_[g].color; m["duck"] = duckUi_.on && duckUi_.targetDca == g + 1 ? 1 : duckUi_.on && duckUi_.keyDca == g + 1 ? 2 : 0; l << m; }
  return l;
}
QVariantList Engine::muteGroupState() const {
  QVariantList l;
  for (int g = 0; g < kGroups; ++g) { QVariantMap m; m["index"] = g + 1; m["name"] = muteGroups_[g].name; m["active"] = muteGroups_[g].active; QVariantList mem; for (int i = 0; i < kStrips; ++i) if ((strips_[static_cast<std::size_t>(i)].muteGroups >> g) & 1u) mem << i; m["members"] = mem; l << m; }
  return l;
}
void Engine::setDca(int i, int g) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dca = std::clamp(g, 0, kGroups); pushStrip(i); pushLoud(i); if (rt_ && duckUi_.on) rt_->setDuckRole(i, duckRole(i)); emit stripsChanged(); emit groupsChanged(); emit loudChanged(); }
void Engine::setDcaLevel(int g, double db) { if (g < 1 || g > kGroups) return; dcas_[g - 1].db = std::clamp(db, -60.0, 10.0); for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].dca == g) pushStrip(i); emit stripsChanged(); emit groupsChanged(); }
void Engine::setDcaMute(int g, bool on) { if (g < 1 || g > kGroups) return; dcas_[g - 1].mute = on; for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].dca == g) pushStrip(i); emit stripsChanged(); emit groupsChanged(); }
void Engine::setDcaName(int g, const QString& n) { if (g < 1 || g > kGroups) return; dcas_[g - 1].name = n.left(16); for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].dca == g) pushLoud(i); emit groupsChanged(); emit loudChanged(); }
void Engine::setDcaColor(int g, int c) { if (g < 1 || g > kGroups) return; dcas_[g - 1].color = std::clamp(c, 0, 7); emit groupsChanged(); }
// ---- DUCKER · VOICE OVER MUSIC: the roles go to the engine strip by strip; the envelope runs there (audiomixer/ducker.hpp)
int Engine::duckRole(int i) const {
  if (i < 0 || i >= kStrips) return 0; const int d = strips_[static_cast<std::size_t>(i)].dca;
  if ((duckUi_.keyDca > 0 && d == duckUi_.keyDca) || duckUi_.keyStrips.contains(i)) return 1;
  if ((duckUi_.targetDca > 0 && d == duckUi_.targetDca) || duckUi_.targetStrips.contains(i)) return 2;
  return 0;
}
void Engine::pushDuck() {
  if (!rt_) return; am::DuckParams p; p.on = duckUi_.on; p.thresholdDb = duckUi_.thr; p.depthDb = duckUi_.depth; p.attackMs = duckUi_.att; p.holdMs = duckUi_.hold; p.releaseMs = duckUi_.rel; p.liftDb = duckUi_.lift;
  rt_->setDucker(p); for (int i = 0; i < kStrips; ++i) rt_->setDuckRole(i, duckUi_.on ? duckRole(i) : 0);
}
QVariantMap Engine::duckState() const {
  auto ints = [](const QList<int>& l) { QVariantList v; for (int x : l) v << x; return v; };
  QVariantList keys, targets; for (int i = 0; i < kStrips; ++i) { const int r = duckRole(i); if (r == 1) keys << i; else if (r == 2) targets << i; }
  return {{"on", duckUi_.on}, {"keyDca", duckUi_.keyDca}, {"targetDca", duckUi_.targetDca}, {"keyStrips", ints(duckUi_.keyStrips)}, {"targetStrips", ints(duckUi_.targetStrips)}, {"threshold", duckUi_.thr}, {"depth", duckUi_.depth},
          {"attack", duckUi_.att}, {"hold", duckUi_.hold}, {"release", duckUi_.rel}, {"lift", duckUi_.lift}, {"keys", keys}, {"targets", targets}};
}
void Engine::setDuck(const QVariantMap& c) {
  auto strips = [](const QVariant& v) { QList<int> l; for (const QVariant& x : v.toList()) { const int i = x.toInt(); if (i >= 0 && i < kStrips && !l.contains(i)) l << i; } return l; };
  if (c.contains("on")) duckUi_.on = c.value("on").toBool();
  if (c.contains("keyDca")) duckUi_.keyDca = std::clamp(c.value("keyDca").toInt(), 0, kGroups);
  if (c.contains("targetDca")) duckUi_.targetDca = std::clamp(c.value("targetDca").toInt(), 0, kGroups);
  if (c.contains("keyStrips")) duckUi_.keyStrips = strips(c.value("keyStrips"));
  if (c.contains("targetStrips")) duckUi_.targetStrips = strips(c.value("targetStrips"));
  if (c.contains("threshold")) duckUi_.thr = std::clamp(c.value("threshold").toDouble(), -80.0, 0.0);
  if (c.contains("depth")) duckUi_.depth = std::clamp(c.value("depth").toDouble(), -40.0, 0.0);
  if (c.contains("attack")) duckUi_.att = std::clamp(c.value("attack").toDouble(), 1.0, 500.0);
  if (c.contains("hold")) duckUi_.hold = std::clamp(c.value("hold").toDouble(), 0.0, 5000.0);
  if (c.contains("release")) duckUi_.rel = std::clamp(c.value("release").toDouble(), 10.0, 5000.0);
  if (c.contains("lift")) duckUi_.lift = std::clamp(c.value("lift").toDouble(), 0.0, 6.0);
  if (duckUi_.keyDca > 0 && duckUi_.keyDca == duckUi_.targetDca) duckUi_.targetDca = 0;   // a group is not its own key
  pushDuck(); emit duckChanged(); emit groupsChanged();
}
double Engine::duckGrDb() const { if (!rt_ || !duckUi_.on) return 0; return std::min(0.0, rt_->snapshot().duckGrDb); }
QJsonObject Engine::duckJson() const { QJsonObject o; const QVariantMap st = duckState(); for (auto it = st.cbegin(); it != st.cend(); ++it) if (it.key() != QLatin1String("keys") && it.key() != QLatin1String("targets")) o[it.key()] = QJsonValue::fromVariant(it.value()); return o; }
void Engine::duckFromJson(const QJsonObject& o) { duckUi_ = DuckUi{}; if (!o.isEmpty()) setDuck(o.toVariantMap()); else { pushDuck(); emit duckChanged(); } }
// ---- AUTO DCA (see Engine.h, audiomixer/classify.hpp)
void Engine::startAutoDca(double seconds) {
  if (autoBusy_) return; autoProp_.clear(); autoBusy_ = true; emit autoDcaChanged();
  const int ms = static_cast<int>(std::clamp(seconds, 0.5, 20.0) * 1000);
  if (!rt_) { autoBusy_ = false; autoProp_ = classifyStrips({}); emit autoDcaChanged(); return; }
  rt_->startAnalysis();
  QTimer::singleShot(ms, this, [this] { if (!rt_) { autoBusy_ = false; emit autoDcaChanged(); return; } rt_->stopAnalysis(); autoWait_ = 0;
    auto* t = new QTimer(this); t->setInterval(30);   // the audio thread takes the stop at its next block
    connect(t, &QTimer::timeout, this, [this, t] {
      if (rt_ && !rt_->analysisReady() && ++autoWait_ < 70) return;
      t->stop(); t->deleteLater(); std::vector<am::FeatureSummary> f; if (rt_ && rt_->analysisReady()) for (int i = 0; i < licensedChannels_ && i < kStrips; ++i) f.push_back(rt_->analysisFeatures(i));
      autoProp_ = classifyStrips(f); autoBusy_ = false; emit autoDcaChanged(); });
    t->start(); });
}
QVariantList Engine::classifyStrips(const std::vector<am::FeatureSummary>& f) {
  QVariantList out; const bool mic = isMicrophoneInput(QString::fromStdString(curIn_));
  const int n = std::min(kStrips, std::max(licensedChannels_, static_cast<int>(f.size())));
  for (int i = 0; i < n && i < licensedChannels_; ++i) {
    const StripUi& s = strips_[static_cast<std::size_t>(i)]; am::ChannelHints h; h.name = s.name.toStdString(); h.linked = s.link; h.microphone = mic && inRoute_[i] >= 0;
    const am::ChannelVerdict v = am::classifyChannel(i < static_cast<int>(f.size()) ? f[static_cast<std::size_t>(i)] : am::FeatureSummary{}, h);
    QVariantMap m; m["strip"] = i; m["name"] = s.name.isEmpty() ? QStringLiteral("CH %1").arg(i + 1) : s.name; m["cls"] = QString::fromLatin1(am::channelClassName(v.cls));
    m["why"] = QString::fromStdString(v.why); m["voice"] = v.voice; m["music"] = v.music; m["instrument"] = v.instrument; out << m; }
  return out;
}
void Engine::setAutoDcaClass(int strip, const QString& cls) {
  for (QVariant& r : autoProp_) { QVariantMap m = r.toMap(); if (m.value("strip").toInt() != strip) continue; m["cls"] = cls; m["why"] = QStringLiteral("set by hand"); r = m; }
  emit autoDcaChanged();
}
void Engine::cancelAutoDca() { autoProp_.clear(); emit autoDcaChanged(); }
void Engine::applyAutoDca(bool muteSilent) {
  if (autoProp_.isEmpty()) return;
  struct G { const char* cls; const char* name; int color; int group = 0; };
  G gs[3] = {{"VOICE", "VOICES", 2}, {"MUSIC", "MUSIC", 5}, {"INSTRUMENT", "INSTRUMENTS", 3}};
  for (G& g : gs) {   // a group of that name is used again; otherwise the first DCA with no members
    bool any = false; for (const QVariant& r : autoProp_) any = any || r.toMap().value("cls").toString() == QLatin1String(g.cls); if (!any) continue;
    for (int d = 1; d <= kGroups && !g.group; ++d) if (dcas_[d - 1].name == QLatin1String(g.name)) g.group = d;
    for (int d = 1; d <= kGroups && !g.group; ++d) { bool used = false; for (const G& o : gs) used = used || o.group == d; if (used) continue;
      bool empty = true; for (int i = 0; i < kStrips && empty; ++i) empty = strips_[static_cast<std::size_t>(i)].dca != d; if (empty) g.group = d; }
    if (g.group) { dcas_[g.group - 1].name = QString::fromLatin1(g.name); dcas_[g.group - 1].color = g.color; } }
  for (const QVariant& r : autoProp_) { const QVariantMap m = r.toMap(); const int i = m.value("strip").toInt(); const QString c = m.value("cls").toString(); if (i < 0 || i >= kStrips) continue;
    if (c == QLatin1String("SILENT")) { if (muteSilent) { strips_[static_cast<std::size_t>(i)].mute = true; pushStrip(i); } continue; }
    for (const G& g : gs) if (c == QLatin1String(g.cls) && g.group) { strips_[static_cast<std::size_t>(i)].dca = g.group; pushStrip(i); } }
  if (gs[0].group && gs[1].group && duckUi_.keyDca == 0 && duckUi_.targetDca == 0) { duckUi_.keyDca = gs[0].group; duckUi_.targetDca = gs[1].group; }   // the ducker is set up, not switched on
  autoProp_.clear(); pushDuck(); emit stripsChanged(); emit groupsChanged(); emit duckChanged(); emit autoDcaChanged();
}
void Engine::setMuteGroupMember(int i, int g, bool member) { if (i < 0 || i >= kStrips || g < 1 || g > kGroups) return; unsigned& m = strips_[static_cast<std::size_t>(i)].muteGroups; if (member) m |= 1u << (g - 1); else m &= ~(1u << (g - 1)); pushStrip(i); emit stripsChanged(); emit groupsChanged(); }
void Engine::setMuteGroupActive(int g, bool on) { if (g < 1 || g > kGroups) return; muteGroups_[g - 1].active = on; for (int i = 0; i < kStrips; ++i) pushStrip(i); emit stripsChanged(); emit groupsChanged(); }
// effective = the strip's own fader + its DCA's fader (in dB), muted when the strip, its DCA or an active mute group it belongs to is muted
double Engine::effectiveDb(int i) const { if (i < 0 || i >= kStrips) return -60; const StripUi& s = strips_[static_cast<std::size_t>(i)]; const double g = s.dca > 0 ? dcas_[s.dca - 1].db : 0; return std::clamp(s.db + g, -60.0, 10.0); }
bool Engine::effectiveMute(int i) const { if (i < 0 || i >= kStrips) return true; if (i >= licensedChannels_) return true; const StripUi& s = strips_[static_cast<std::size_t>(i)]; if (s.mute) return true; if (s.dca > 0 && dcas_[s.dca - 1].mute) return true; for (int g = 0; g < kGroups; ++g) if ((s.muteGroups >> g) & 1u && muteGroups_[g].active) return true; return false; }
void Engine::pushStrip(int i) { if (!rt_) return; rt_->setGainDb(i, effectiveDb(i)); rt_->setMute(i, effectiveMute(i)); }
void Engine::setGain(int i, double db) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].db = std::clamp(db, -60.0, 10.0); pushStrip(i); if (rt_ && loudUi_.strips[i].ride) rt_->rideTouch(i, 5); if (linked(i)) { strips_[static_cast<std::size_t>(partner(i))].db = strips_[static_cast<std::size_t>(i)].db; pushStrip(partner(i)); if (rt_ && loudUi_.strips[partner(i)].ride) rt_->rideTouch(partner(i), 5); } emit stripsChanged(); }   // a hand on the fader (UI, MIDI, remote): AUTO FADER pauses 5 s and rides from the new position
// 360 and IAMF are one place: a strip with an IAMF ELEMENT in its rack is placed by that plug-in (the 360 page moves its knobs and reads
// them back), and the strip's own 360 stage stays off so the sound is never placed twice. Without one, the 360 stage places it as before.
int Engine::iamfSlot(int i) const { if (i < 0 || i >= kStrips) return -1; const StripUi& s = strips_[static_cast<std::size_t>(i)];
  for (int k = 0; k < kPluginSlots; ++k) if (s.plugins[k].kind == QStringLiteral("fx:iamfelement") && s.plugins[k].vst) return k; return -1; }
void Engine::setSpatial(int i, bool on, double az, double el, double dist, double spread) {
  if (i < 0 || i >= kStrips) return; StripUi& s = strips_[static_cast<std::size_t>(i)];
  if (const int k = iamfSlot(i); k >= 0) { PluginUi& p = s.plugins[k]; const double a = std::fmod(az + 540.0, 360.0) - 180.0;
    s.spAz = a; s.spEl = std::clamp(el, -90.0, 90.0); s.spDist = std::clamp(dist, 0.5, 20.0); s.spSpread = std::clamp(spread, 0.0, 1.0); s.spOn = false; pushSpatial(i);
    p.vst->setParam(0, (s.spAz + 180) / 360); p.vst->setParam(1, (s.spEl + 90) / 180); p.vst->setParam(2, std::log(s.spDist / 0.5) / std::log(40.0)); p.vst->setParam(3, s.spSpread);
    p.bypass = !on; p.vst->setBypass(!on); if (spkOn_) feedSpeakers(i); emit stripsChanged(); emit spatialMoved(); return; }
  s.spOn = on; s.spAz = std::fmod(az, 360.0); s.spEl = std::clamp(el, -90.0, 90.0); s.spDist = std::clamp(dist, 0.3, 20.0); s.spSpread = std::clamp(spread, 0.0, 1.0);
  pushSpatial(i); emit stripsChanged(); emit spatialMoved();
}
// what the engine is given: the object's place in the room, seen from a head that may be turned
void Engine::pushSpatial(int i) {
  if (!rt_ || i < 0 || i >= kStrips) return; const StripUi& s = strips_[static_cast<std::size_t>(i)];
  double az = s.spAz, el = s.spEl;
  if (headOn_) { az = std::fmod(az - headYaw_ + 540.0, 360.0) - 180.0; el = std::clamp(el - headPitch_, -90.0, 90.0); }
  rt_->setSpatial(i, s.spOn && !spkOn_, az, el, s.spDist, s.spSpread);
  if (spkOn_) feedSpeakers(i);
}
QVariantList Engine::speakerLayout() const { QVariantList l; for (int k = 0; k < am::fx::kFront10; ++k) { QVariantMap m; m["name"] = QString::fromLatin1(am::fx::kFront10Layout[k].name); m["az"] = am::fx::kFront10Layout[k].az; m["el"] = am::fx::kFront10Layout[k].el; m["out"] = k + 1; l << m; } return l; }
void Engine::feedSpeakers(int i) {
  if (i < 0 || i >= kStrips) return; const QVariantMap sp = spatial(i); const bool placed = sp.value("on").toBool();
  double az = placed ? sp["az"].toDouble() : 0, el = placed ? sp["el"].toDouble() : 0;
  if (placed && headOn_) { az = std::fmod(az - headYaw_ + 540.0, 360.0) - 180.0; el = std::clamp(el - headPitch_, -90.0, 90.0); }
  double g[am::fx::kFront10]; am::fx::front10Gains(az, el, placed ? sp["spread"].toDouble() : 0.3, placed ? sp["dist"].toDouble() : 1, g);
  for (int k = 0; k < am::fx::kFront10; ++k) setSend(i, k, g[k] > 0.001 ? 20 * std::log10(g[k]) : -60.0, false);
}
void Engine::setSpeakerArray(bool on) {
  if (on == spkOn_) return; constexpr int N = am::fx::kFront10;
  if (on) {   // keep what the ten buses and outputs 1-10 were, then make them the speaker feeds
    QVariantList sends, buses, outs;
    for (int i = 0; i < kStrips; ++i) { QVariantList r; for (int k = 0; k < N; ++k) { const SendUi& sd = strips_[static_cast<std::size_t>(i)].sends[k]; r << sd.db << sd.pre; } sends << QVariant(r); }
    for (int k = 0; k < N; ++k) { const BusUi& b = buses_[k]; buses << QVariant(QVariantList{b.name, b.db, b.returnDb, b.mute}); outs << outRoute_[k]; }
    spkSaved_ = QVariantMap{{"sends", sends}, {"buses", buses}, {"outs", outs}}; settings_.setValue("spatial/front10Saved", spkSaved_);
    spkOn_ = true; settings_.setValue("spatial/front10", true);
    for (int k = 0; k < N; ++k) { buses_[k].name = QStringLiteral("SPK ") + QString::fromLatin1(am::fx::kFront10Layout[k].name); setBusLevel(k, 0); setBusMute(k, false); setBusReturn(k, -60); setOutputRoute(k, am::Runtime::kSrcBus0L + 2 * k); }
    for (int i = 0; i < kStrips; ++i) pushSpatial(i), feedSpeakers(i);
  } else {      // give everything back as it was
    spkOn_ = false; settings_.setValue("spatial/front10", false); const QVariantMap sv = spkSaved_.isEmpty() ? settings_.value("spatial/front10Saved").toMap() : spkSaved_;
    const QVariantList sends = sv["sends"].toList(), buses = sv["buses"].toList(), outs = sv["outs"].toList();
    for (int i = 0; i < kStrips && i < sends.size(); ++i) { const QVariantList r = sends[i].toList(); for (int k = 0; k < N && 2 * k + 1 < r.size(); ++k) setSend(i, k, r[2 * k].toDouble(), r[2 * k + 1].toBool()); }
    for (int k = 0; k < N && k < buses.size(); ++k) { const QVariantList b = buses[k].toList(); if (b.size() < 4) continue; buses_[k].name = b[0].toString(); setBusLevel(k, b[1].toDouble()); setBusReturn(k, b[2].toDouble()); setBusMute(k, b[3].toBool()); }
    for (int k = 0; k < N && k < outs.size(); ++k) setOutputRoute(k, outs[k].toInt());
    spkSaved_.clear(); for (int i = 0; i < kStrips; ++i) pushSpatial(i);
  }
  emit busesChanged(); emit spatialMoved();
}
void Engine::setHeadTrack(bool on) { if (on == headOn_) return; headOn_ = on; for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].spOn) pushSpatial(i); emit spatialMoved(); }
void Engine::setHeadYaw(double d) { d = std::fmod(d + 540.0, 360.0) - 180.0; if (d == headYaw_) return; headYaw_ = d; if (headOn_) for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].spOn) pushSpatial(i); emit spatialMoved(); }
void Engine::setHeadPitch(double d) { d = std::clamp(d, -60.0, 60.0); if (d == headPitch_) return; headPitch_ = d; if (headOn_) for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].spOn) pushSpatial(i); emit spatialMoved(); }
int Engine::spatialMoving() const { int n = 0; for (int i = 0; i < kStrips; ++i) if (orbit_[i] != 0 && strips_[static_cast<std::size_t>(i)].spOn) ++n; return n; }
void Engine::setSpatialOrbit(int i, double dps) {
  if (i < 0 || i >= kStrips) return; orbit_[i] = std::clamp(dps, -180.0, 180.0);
  if (spatialMoving() > 0) { if (!motionTimer_.isActive()) { motionAt_ = QDateTime::currentMSecsSinceEpoch(); motionTimer_.start(33); } } else motionTimer_.stop();
  emit spatialMoved();
}
// the moving objects, 30 times a second on the interface thread: a new direction goes through the engine's control ring like a knob turn, and
// the engine glides to it inside the audio thread. The console is not rewritten for this (no stripsChanged): only the 360 page is told.
void Engine::moveObjects() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch(); const double dt = std::clamp(static_cast<double>(now - motionAt_) / 1000.0, 0.0, 0.25); motionAt_ = now; bool any = false;
  for (int i = 0; i < kStrips; ++i) { StripUi& s = strips_[static_cast<std::size_t>(i)]; if (orbit_[i] == 0) continue;
    if (iamfSlot(i) >= 0) { const QVariantMap sp = spatial(i); if (!sp["on"].toBool()) continue; s.spAz = sp["az"].toDouble(); const double a = std::fmod(s.spAz + orbit_[i] * dt + 540.0, 360.0) - 180.0;
      const auto& v = s.plugins[iamfSlot(i)].vst; v->setParam(0, (a + 180) / 360); s.spAz = a; any = true; continue; }
    if (!s.spOn) continue;
    s.spAz = std::fmod(s.spAz + orbit_[i] * dt + 540.0, 360.0) - 180.0; pushSpatial(i); any = true; }
  if (any) emit spatialMoved(); else motionTimer_.stop();
}
void Engine::setCinemaHall(bool on) {
  if (on == cinema_) return; cinema_ = on;
  // ten fixed points: azimuth (right is positive), elevation, distance in metres
  static const double pts[10][3] = {{-30, 0, 9}, {0, 0, 9}, {30, 0, 9}, {-90, 0, 6}, {90, 0, 6}, {-140, 0, 8}, {140, 0, 8}, {-60, 50, 5}, {60, 50, 5}, {0, 70, 5}};
  // ten travelling objects: where each starts, its height, its distance and its speed in degrees a second (a negative one goes the other way round)
  static const double mov[10][4] = {{0, 5, 4, 24}, {36, 20, 6, -18}, {72, -10, 3, 40}, {108, 35, 7, -12}, {144, 0, 5, 30}, {180, 15, 8, -36}, {-144, 45, 4, 15}, {-108, -5, 6, -27}, {-72, 25, 3, 50}, {-36, 60, 7, -9}};
  for (int k = 0; k < 20 && k < kStrips; ++k) {
    if (on) { if (k < 10) { setSpatial(k, true, pts[k][0], pts[k][1], pts[k][2], 0.15); orbit_[k] = 0; } else { setSpatial(k, true, mov[k - 10][0], mov[k - 10][1], mov[k - 10][2], 0.05); orbit_[k] = mov[k - 10][3]; }
      setSend(k, 15, -18.0, false); }
    else { const StripUi& s = strips_[static_cast<std::size_t>(k)]; orbit_[k] = 0; setSpatial(k, false, s.spAz, s.spEl, s.spDist, s.spSpread); setSend(k, 15, -60.0, false); }
  }
  if (on) { setBusFx(15, QStringLiteral("hall")); setBusFxMix(15, 1.0); setBusReturn(15, -6.0); } else setBusReturn(15, -60.0);   // the room: the hall reverb on FX 8, returned under the objects
  if (on) { motionAt_ = QDateTime::currentMSecsSinceEpoch(); motionTimer_.start(33); } else motionTimer_.stop();
  emit spatialMoved();
}
QVariantList Engine::spatialScene() const {
  QVariantList l;
  for (int i = 0; i < kStrips; ++i) { const QVariantMap sp = spatial(i); if (!sp["on"].toBool()) continue; const double az = sp["az"].toDouble(), el = sp["el"].toDouble();
    QVariantMap m; m["strip"] = i; m["az"] = az; m["el"] = el; m["dist"] = sp["dist"]; m["orbit"] = orbit_[i]; m["iamf"] = sp["iamf"];
    m["heardAz"] = headOn_ ? std::fmod(az - headYaw_ + 540.0, 360.0) - 180.0 : az; m["heardEl"] = headOn_ ? std::clamp(el - headPitch_, -90.0, 90.0) : el; l << m; }
  return l;
}
QVariantMap Engine::spatial(int i) const { QVariantMap m; if (i < 0 || i >= kStrips) return m; const StripUi& s = strips_[static_cast<std::size_t>(i)];
  if (const int k = iamfSlot(i); k >= 0) { const auto& v = s.plugins[k].vst; m["iamf"] = true; m["iamfSlot"] = k; m["on"] = !s.plugins[k].bypass;
    m["az"] = v->param(0) * 360 - 180; m["el"] = v->param(1) * 180 - 90; m["dist"] = 0.5 * std::pow(40.0, v->param(2)); m["spread"] = v->param(3); m["render"] = v->param(4) < 0.5 ? "BINAURAL" : "SPEAKERS"; return m; }
  m["iamf"] = false; m["on"] = s.spOn; m["az"] = s.spAz; m["el"] = s.spEl; m["dist"] = s.spDist; m["spread"] = s.spSpread; return m; }
void Engine::pushMaster() { mp_.fs = rt_ && running() ? sampleRate() : (mp_.fs > 0 ? mp_.fs : 48000); master_.set(mp_); }
QString Engine::exportApo(int strip, const QString& path) {
  if (strip < -1 || strip >= kStrips) return {};
  const QString p = path.trimmed().isEmpty() ? QString::fromStdString(am::paths::file("equalizer-apo.txt")) : path.trimmed(); QFile f(p); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return {};
  QString out = QStringLiteral("# AmF X1 %1: filters for Equalizer APO. In its config.txt add the line:  Include: %2\n").arg(strip < 0 ? QStringLiteral("PROGRAM EQ") : QStringLiteral("CH %1 EQ").arg(strip + 1), p);
  double top = 0; for (int b = 0; b < 10; ++b) { const QVariantMap e = eqBand(strip, b); if (e["type"].toInt() > 0) top = std::max(top, e["gainDb"].toDouble()); }
  out += QStringLiteral("Preamp: %1 dB\n").arg(-top, 0, 'f', 1);   // headroom for the highest boost: the system mixer must not clip
  int n = 0; static const char* kinds[] = {"", "PK", "LSC", "HSC", "LPQ", "HPQ", "NO"};
  for (int b = 0; b < 10; ++b) { const QVariantMap e = eqBand(strip, b); const int t = e["type"].toInt(); if (t < 1 || t > 6) continue; ++n;
    out += QStringLiteral("Filter %1: ON %2 Fc %3 Hz").arg(n).arg(QLatin1String(kinds[t])).arg(e["freq"].toDouble(), 0, 'f', 1); if (t <= 3) out += QStringLiteral(" Gain %1 dB").arg(e["gainDb"].toDouble(), 0, 'f', 1); out += QStringLiteral(" Q %1\n").arg(e["q"].toDouble(), 0, 'f', 2); }
  f.write(out.toUtf8()); return p;
}
int Engine::importApo(int strip, const QString& path) {
  if (strip < -1 || strip >= kStrips) return 0;
  QFile f(path.trimmed().isEmpty() ? QString::fromStdString(am::paths::file("equalizer-apo.txt")) : path.trimmed()); if (!f.open(QIODevice::ReadOnly | QIODevice::Text) || f.size() > 1024 * 1024) return 0;
  static const QRegularExpression re(QStringLiteral("^\\s*Filter\\s*\\d*\\s*:\\s*ON\\s+(PK|PEQ|LSC|LS|HSC|HS|LPQ|LP|HPQ|HP|NO)\\b.*?Fc\\s+([0-9.]+)\\s*Hz(?:.*?Gain\\s+(-?[0-9.]+)\\s*dB)?(?:.*?Q\\s+([0-9.]+))?"), QRegularExpression::CaseInsensitiveOption);
  int n = 0; QList<QVariantList> got;
  while (!f.atEnd() && got.size() < 10) { const QString line = QString::fromUtf8(f.readLine()); const auto m = re.match(line); if (!m.hasMatch()) continue; const QString k = m.captured(1).toUpper();
    const int t = k.startsWith("PK") || k == "PEQ" ? 1 : k.startsWith("LS") ? 2 : k.startsWith("HS") ? 3 : k.startsWith("LP") ? 4 : k.startsWith("HP") ? 5 : 6;
    got.append({t, m.captured(2).toDouble(), m.captured(4).isEmpty() ? 0.707 : m.captured(4).toDouble(), m.captured(3).toDouble()}); }
  if (got.isEmpty()) return 0;
  for (int b = 0; b < 10; ++b) { if (b < got.size()) { setEq(strip, b, got[b][0].toInt(), got[b][1].toDouble(), got[b][2].toDouble(), got[b][3].toDouble()); ++n; } else setEq(strip, b, 0, kIsoCentres[b], 1.41, 0); }
  return n;
}
QVariantList Engine::rackPresets(const QString& kind) const {
  QVariantList l; if (!kind.startsWith("fx:")) return l; const std::string k = kind.mid(3).toStdString();
  for (const auto& p : am::fx::presets()) if (p.kind == k) { QVariantMap m; m["id"] = p.id; m["name"] = QString::fromStdString(p.name); m["category"] = QString::fromStdString(p.category); l << m; }
  return l;
}
bool Engine::applyRackPreset(int i, int slot, int presetId) {
  if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return false; PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; const am::fx::Preset* pr = am::fx::preset(presetId);
  if (!pr || !p.vst || p.kind != QStringLiteral("fx:") + QString::fromStdString(pr->kind)) return false;
  std::string st; char b[64]; for (const auto& [id, v] : pr->values) { std::snprintf(b, sizeof b, "%s=%.4f;", id.c_str(), v); st += b; }   // the knobs of the preset; MIX stays where the insert has it
  const bool ok = p.vst->setState(st); if (ok) emit stripsChanged(); return ok;
}
QVariantMap Engine::mastering() const {
  QVariantMap m; m["eqOn"] = mp_.eqOn; m["analog"] = mp_.analog; m["on"] = mp_.on; m["low"] = mp_.lowDb; m["air"] = mp_.airDb; m["mid"] = mp_.midDb; m["compMix"] = mp_.compMix; m["bassMonoOn"] = mp_.bassMonoOn; m["bassMonoHz"] = mp_.bassMonoHz; m["thr"] = mp_.thrDb; m["ratio"] = mp_.ratio; m["attack"] = mp_.attackMs; m["release"] = mp_.releaseMs; m["makeup"] = mp_.makeupDb; m["drive"] = mp_.drive; m["gain"] = mp_.gainDb; m["toneOn"] = mp_.toneOn; m["compOn"] = mp_.compOn; m["satOn"] = mp_.satOn; m["deessOn"] = mp_.deessOn; m["deessThr"] = mp_.deessThrDb; m["deessMax"] = mp_.deessMaxDb; m["exciterOn"] = mp_.exciterOn; m["exciter"] = mp_.exciterAmt; m["bassMonoOn"] = mp_.bassMonoOn; m["bassMonoHz"] = mp_.bassMonoHz; m["preset"] = masterPreset_; return m;
}
void Engine::setDspMode(const QString& m) {
  const bool fast = m == QLatin1String("fast"), wasFast = fastMode();   // FAST first: leaving it gives the limiter back before ENGINEER sets its own
  if (fast && !wasFast) {
    settings_.setValue("audio/fastPrevLim", masterLim_); settings_.setValue("audio/fastPrevCeil", masterCeil_); settings_.setValue("audio/fast", true);
    setMasterLimiter(true, -1.0); if (rt_) rt_->setFast(true); applyUiRate();
    am::sysopt::trimMemory(false);   // the heap's free blocks back to the system (never the working set while sound runs)
  }
  if (!fast && wasFast) {
    settings_.setValue("audio/fast", false); if (rt_) rt_->setFast(false);
    setMasterLimiter(settings_.value("audio/fastPrevLim", masterLim_).toBool(), settings_.value("audio/fastPrevCeil", masterCeil_).toDouble());
    settings_.remove("audio/fastPrevLim"); settings_.remove("audio/fastPrevCeil"); applyUiRate();
  }
  const bool eng = m == QLatin1String("engineer"), was = settings_.value("audio/engineer", false).toBool();
  if (eng && !was) { settings_.setValue("audio/engineerPrevBits", outputBits()); settings_.setValue("audio/engineer", true); setDcBlock(true); setOutputBits(24); setMasterLimiter(true, -1.0); }
  if (!eng && was) { settings_.setValue("audio/engineer", false); setOutputBits(settings_.value("audio/engineerPrevBits", -1).toInt()); settings_.remove("audio/engineerPrevBits"); }
  QVariantMap v; v["analog"] = (m == "analog"); setMastering(v); if (rt_) rt_->setAnalogChannels(m == "analog");   // ANALOG: the master and every channel
  emit masterChainChanged();
}

void Engine::setMastering(const QVariantMap& m) {
  auto d = [&m](const char* k, double cur, double lo, double hi) { return m.contains(k) ? std::clamp(m.value(k).toDouble(), lo, hi) : cur; };
  if (m.contains("eqOn")) mp_.eqOn = m.value("eqOn").toBool(); if (m.contains("on")) mp_.on = m.value("on").toBool();
  mp_.lowDb = d("low", mp_.lowDb, -12, 12); mp_.airDb = d("air", mp_.airDb, -12, 12); mp_.midDb = d("mid", mp_.midDb, -12, 12); mp_.compMix = d("compMix", mp_.compMix, 0, 1); mp_.thrDb = d("thr", mp_.thrDb, -40, 0); mp_.ratio = d("ratio", mp_.ratio, 1, 10); mp_.attackMs = d("attack", mp_.attackMs, 0.5, 200); mp_.releaseMs = d("release", mp_.releaseMs, 20, 2000);
  if (m.contains("analog")) mp_.analog = m["analog"].toBool();
  mp_.makeupDb = d("makeup", mp_.makeupDb, 0, 18); mp_.drive = d("drive", mp_.drive, 0, 1); mp_.gainDb = d("gain", mp_.gainDb, -12, 12); for (auto [k, b] : std::initializer_list<std::pair<const char*, bool*>>{{"toneOn", &mp_.toneOn}, {"compOn", &mp_.compOn}, {"satOn", &mp_.satOn}, {"deessOn", &mp_.deessOn}, {"exciterOn", &mp_.exciterOn}, {"bassMonoOn", &mp_.bassMonoOn}}) if (m.contains(k)) *b = m.value(k).toBool();
  mp_.deessThrDb = d("deessThr", mp_.deessThrDb, -50, 0); mp_.deessMaxDb = d("deessMax", mp_.deessMaxDb, 0, 18); mp_.exciterAmt = d("exciter", mp_.exciterAmt, 0, 1); mp_.bassMonoHz = d("bassMonoHz", mp_.bassMonoHz, 40, 400);
  masterPreset_ = m.value("preset").toString(); pushMaster(); emit masterChainChanged();
}
QStringList Engine::masteringPresets() const { return {"AMFB SIGNATURE", "TRANSPARENT", "STREAMING", "WARM", "PUNCH", "LOUD", "BROADCAST VOICE", "SHORT VIDEO / LIVE", "METAL", "HARD ROCK"}; }
void Engine::applyMasteringPreset(const QString& name) {
  struct P { const char* n; double low, air, thr, ratio, att, rel, mk, drive, gain; };
  static const P k[] = {{"TRANSPARENT", 0, 0, -8, 1.5, 30, 250, 0, 0, 0}, {"STREAMING", 1, 1.5, -14, 2, 30, 200, 2, 0.1, 1}, {"WARM", 2.5, -1, -16, 2, 40, 300, 2, 0.35, 0},
    {"PUNCH", 2, 2, -18, 3, 60, 120, 3, 0.2, 1}, {"LOUD", 1.5, 2, -20, 4, 10, 100, 5, 0.4, 3}, {"BROADCAST VOICE", -2, 2, -22, 3, 5, 150, 5, 0.1, 0},
    {"SHORT VIDEO / LIVE", 1, 2.5, -20, 3, 12, 140, 5, 0.15, 1}};   // for a phone's small speaker (short videos, live streams): firm, bright, steady, loud without pumping
  if (name == QLatin1String("AMFB SIGNATURE")) {   // the house sound: a firm low end kept mono, open air with a little harmonic sparkle, gentle glue; meant to be heard at once against a plain sound card
    mp_.on = true; mp_.toneOn = mp_.compOn = mp_.satOn = true; mp_.lowDb = 3; mp_.airDb = 2.5; mp_.thrDb = -16; mp_.ratio = 2; mp_.attackMs = 30; mp_.releaseMs = 220; mp_.makeupDb = 2; mp_.drive = 0.15; mp_.gainDb = 1;
    mp_.exciterOn = true; mp_.exciterAmt = 0.25; mp_.bassMonoOn = true; mp_.bassMonoHz = 90; mp_.deessOn = false; masterPreset_ = name; pushMaster(); emit masterChainChanged(); return; }
  struct G { const char* n; double low, mid, air, thr, ratio, att, rel, mk, drive, gain, exc, monoHz; };   // guitar music: a tight mono low end (kick and bass stay centred), mids shaped, fast glue, more drive
  static const G g[] = {{"METAL", 2.5, -2.5, 3, -20, 4, 8, 90, 4, 0.45, 1.5, 0.3, 110},       // scooped mids, hard bright top, fast attack for double kicks, dense and loud
                        {"HARD ROCK", 2, 1, 2, -18, 3, 20, 140, 3, 0.35, 1, 0.2, 90}};       // forward guitar mids, slower attack so the drums still hit
  for (const G& p : g) if (name == QLatin1String(p.n)) { mp_.on = true; mp_.toneOn = mp_.compOn = mp_.satOn = true; mp_.lowDb = p.low; mp_.midDb = p.mid; mp_.airDb = p.air; mp_.thrDb = p.thr; mp_.ratio = p.ratio; mp_.attackMs = p.att; mp_.releaseMs = p.rel; mp_.makeupDb = p.mk; mp_.drive = p.drive; mp_.gainDb = p.gain;
    mp_.exciterOn = true; mp_.exciterAmt = p.exc; mp_.bassMonoOn = true; mp_.bassMonoHz = p.monoHz; mp_.deessOn = false; masterPreset_ = name; pushMaster(); emit masterChainChanged(); return; }
  for (const P& p : k) if (name == QLatin1String(p.n)) { mp_.on = true; mp_.toneOn = mp_.compOn = mp_.satOn = true; mp_.lowDb = p.low; mp_.airDb = p.air; mp_.thrDb = p.thr; mp_.ratio = p.ratio; mp_.attackMs = p.att; mp_.releaseMs = p.rel; mp_.makeupDb = p.mk; mp_.drive = p.drive; mp_.gainDb = p.gain; masterPreset_ = name; pushMaster(); emit masterChainChanged(); return; }
}
void Engine::setPan(int i, double p) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].pan = std::clamp(p, -1.0, 1.0); if (rt_) rt_->setPan(i, strips_[static_cast<std::size_t>(i)].pan); if (linked(i)) { const int j = partner(i); strips_[static_cast<std::size_t>(j)].pan = -strips_[static_cast<std::size_t>(i)].pan; if (rt_) rt_->setPan(j, strips_[static_cast<std::size_t>(j)].pan); } emit stripsChanged(); }
void Engine::setMute(int i, bool on) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].mute = on; pushStrip(i); if (linked(i)) { strips_[static_cast<std::size_t>(partner(i))].mute = on; pushStrip(partner(i)); } emit stripsChanged(); }
void Engine::setSolo(int i, bool on) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].solo = on; if (rt_) rt_->setSolo(i, on); if (linked(i)) { const int j = partner(i); strips_[static_cast<std::size_t>(j)].solo = on; if (rt_) rt_->setSolo(j, on); } emit stripsChanged(); }
void Engine::setName(int i, const QString& n) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].name = n.left(24); emit stripsChanged(); }
void Engine::setColor(int i, int c) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].color = std::clamp(c, 0, 7); emit stripsChanged(); }
void Engine::setMaster(double db) {
  startNote_.clear();
  const double was = masterDb_; masterDb_ = std::clamp(db, -60.0, 10.0); pushMasterLevel();
  if (mon_.sync && masterDb_ != was) { mon_.db = std::clamp(mon_.db + (masterDb_ - was), -60.0, 10.0); if (rt_) rt_->setMonitor(mon_.source, mon_.db, mon_.dim, mon_.mono, mon_.mute); emit monitorChanged(); }
  emit stripsChanged(); syncSystemSound();
}
void Engine::setWindowsMuteSync(bool on) { if (winSync_ == on) return; winSync_ = on; QSettings().setValue("mixer/windowsMuteSync", on); syncSystemSound(); emit systemSoundChanged(); }
void Engine::syncSystemSound() {
  if (!sysGet_ || !sysSet_) return;
  if (!sysAsync_) { bool now = false; const bool ok = sysGet_(now); applySystemSound(ok, now, sysSet_); return; }   // the tests' backend
  if (sysBusy_) { sysAgain_ = true; return; } sysBusy_ = true;   // one question at a time; a change meanwhile asks again when it is answered
  const auto get = sysGet_; const auto set = sysSet_;
  svcPost([this, get, set] { bool now = false; const bool ok = get(now);
    QMetaObject::invokeMethod(this, [this, ok, now, set] { sysBusy_ = false;
      applySystemSound(ok, now, [this, set](bool m) { svcPost([set, m] { set(m); }); return true; });   // the change is sent to the worker too
      if (sysAgain_) { sysAgain_ = false; syncSystemSound(); } }, Qt::QueuedConnection); });
}
void Engine::applySystemSound(bool ok, bool now, const std::function<bool(bool)>& sysSet) {
  const bool want = winSync_ && masterDb_ <= -60.0; bool changed = ok != winAvail_; winAvail_ = ok;
  const bool had = weMuted_;
  if (ok && want && !weMuted_) { winBefore_ = now; if (sysSet(true)) { weMuted_ = true; now = true; } }           // fader at -inf: Windows goes silent too
  else if (ok && !want && weMuted_) { if (sysSet(winBefore_)) now = winBefore_; weMuted_ = false; }               // fader up (or sync off): the mute goes back to what it was
  else if (ok && weMuted_ && !now) weMuted_ = false;                                                                // somebody un-muted Windows by hand: leave it to them
  if (weMuted_ != had) { QSettings q; if (weMuted_) { q.setValue("mixer/weMutedWindows", true); q.setValue("mixer/winBefore", winBefore_); } else q.remove("mixer/weMutedWindows"); q.sync(); }
  if (ok && now != winMuted_) { winMuted_ = now; changed = true; }
  if (changed) emit systemSoundChanged();
}
// ---- WINDOWS VOLUME and GAIN MODE
double Engine::trimForLevel(double l) { return l <= 0.0005 ? -120.0 : std::clamp(30.0 * std::log10(l), -120.0, 0.0); }
bool Engine::windowsSetsLevel() const { return rt_ && rt_->backendName() == "wasapi" && !rt_->exclusive() && (rt_->outDevice().empty() || rt_->outDevice() == "-"); }
double Engine::windowsTrimDb() const { return (winGain_ && winVol_ >= 0 && !windowsSetsLevel()) ? trimForLevel(winVol_ / 100.0) : 0.0; }
QString Engine::windowsTrimNote() const {
  if (winVol_ < 0) return QStringLiteral("The Windows volume is not available on this computer.");
  if (!winGain_) return QStringLiteral("GAIN MODE off: the Windows volume does not change the console's level.");
  if (windowsSetsLevel()) return QStringLiteral("This output plays through Windows' own mixer, which already sets its level with this slider: no extra trim (no double cut).");
  return QStringLiteral("GAIN MODE: the Windows volume is a trim after the master (%1 dB now).").arg(windowsTrimDb() <= -119 ? QStringLiteral("off") : QString::number(windowsTrimDb(), 'f', 1));
}
// the level the engine gets: the fader plus the GAIN MODE trim. The mixer takes -60 dB and below as off, so a trimmed fader stays audible
// down to -59.9 dB; it is off only when the fader is off or the Windows slider is at 0
double Engine::outMasterDb() const { return masterDb_; }   // the fader itself: GAIN MODE's trim is a separate stage after the meters (Mixer::setOutputTrimDb)
void Engine::pushMasterLevel() { if (rt_) { rt_->setMasterDb(masterDb_); rt_->setOutputTrimDb(windowsTrimDb()); } }
void Engine::setWindowsGainMode(bool on) { if (winGain_ == on) return; winGain_ = on; QSettings().setValue("mixer/windowsGainMode", on); pushMasterLevel(); emit systemSoundChanged(); }
void Engine::setWindowsVolume(int v) {
  v = std::clamp(v, 0, 100); if (!volSet_ || v == winVol_) return; const double was = windowsTrimDb(); winVol_ = v;
  if (volSync_) volSet_(static_cast<float>(v / 100.0)); else { const auto set = volSet_; svcPost([set, v] { set(static_cast<float>(v / 100.0)); }); }
  if (windowsTrimDb() != was) pushMasterLevel(); emit systemSoundChanged();
}
void Engine::syncSystemVolume() {
  if (!volGet_) return;
  const auto take = [this](bool ok, float f) { const int v = ok ? static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 100.0f)) : -1; if (v == winVol_) return;
    const double was = windowsTrimDb(); winVol_ = v; if (windowsTrimDb() != was) pushMasterLevel(); emit systemSoundChanged(); };
  if (volSync_) { float f = 0; const bool ok = volGet_(f); take(ok, f); return; }
  if (volBusy_) return; volBusy_ = true; const auto get = volGet_;
  svcPost([this, get, take] { float f = 0; const bool ok = get(f); QMetaObject::invokeMethod(this, [this, ok, f, take] { volBusy_ = false; take(ok, f); }, Qt::QueuedConnection); });
}
void Engine::restoreSystemSound() { if (weMuted_ && sysSet_) { sysSet_(winBefore_); weMuted_ = false; QSettings().remove("mixer/weMutedWindows"); } }
bool Engine::inputMutedBySystem() const { return rt_ && running() && rt_->inputSilentSeconds() > 2.0; }
void Engine::setHeadroomDb(int db) {
  db = db <= 0 ? 0 : db <= 3 ? 3 : db <= 6 ? 6 : db <= 9 ? 9 : 12; if (db == headroom_) return;
  headroom_ = db; settings_.setValue("console/headroomDb", db); if (rt_) rt_->setLimiter(limiterOnNow(), ceilingNow(), masterRel_); emit stripsChanged();
}
void Engine::setMasterLimiter(bool on, double c, double rel) { masterLim_ = on; masterCeil_ = std::clamp(c, -24.0, 0.0); if (rel >= 0) masterRel_ = std::clamp(rel, 10.0, 2000.0); if (rt_) rt_->setLimiter(limiterOnNow(), ceilingNow(), masterRel_); emit stripsChanged(); emit masterChainChanged(); }
QVariantMap Engine::masterLimiter() const { QVariantMap m; m["on"] = masterLim_; m["ceiling"] = masterCeil_; m["release"] = masterRel_; return m; }
void Engine::clearClips() { if (rt_) rt_->clearClips(); }
// ---- licence (engine/cpp/app/license): the vendor public key is compiled in; keys and tokens are verified offline
static QVariantMap licenseMap(const am::lic::Status& st) {
  QVariantMap m; m["state"] = QString::fromStdString(st.state); m["plan"] = QString::fromStdString(st.planId); m["planName"] = QString::fromStdString(st.planName); m["channels"] = st.channels; m["source"] = QString::fromStdString(st.source);
  m["id"] = QString::fromStdString(st.id); m["name"] = QString::fromStdString(st.name); m["serial"] = QString::fromStdString(st.serial); m["editionName"] = QString::fromStdString(st.editionName); m["expires"] = static_cast<qlonglong>(st.expires); m["boundToThisPc"] = st.boundToThisPc; m["machine"] = QString::fromStdString(st.machine); m["message"] = QString::fromStdString(st.message);
  m["term"] = QString::fromStdString(st.term); m["needsActivation"] = st.needsActivation; m["graceDaysLeft"] = st.graceDaysLeft;
  m["baseChannels"] = st.baseChannels; m["extendPacks"] = st.extendPacks; m["extendChannels"] = st.extendChannels;   // CHANNEL EXTEND: +30 per pack on PRO / STUDIO / MASTER
  return m;
}
QVariantMap Engine::storeActivation(const QString& activation) {
  const am::lic::Verdict v = am::lic::storeActivation(activation.toStdString(), QDateTime::currentMSecsSinceEpoch()); refreshLicense();
  QVariantMap m; m["ok"] = v.ok; m["reason"] = QString::fromStdString(v.reason); m["message"] = QString::fromStdString(v.message); return m; }
bool Engine::storeRevocations(const QString& list) { const bool ok = am::lic::storeRevocations(list.toStdString()); if (ok) refreshLicense(); return ok; }
QString Engine::edition() const { return license_.value("planName", "BASIC").toString(); }
void Engine::refreshLicense() {
  const am::lic::Status st = am::lic::status(QDateTime::currentMSecsSinceEpoch());
  license_ = licenseMap(st); const int before = licensedChannels_; licensedChannels_ = std::clamp(st.channels, 1, kStrips);
#ifdef AMIXOS_TEST_LICENSE
  // TEST BUILDS ONLY (cmake -DAMIXOS_TEST_LICENSE=ON; never set for a package): AMIX_TEST_CHANNELS=300 shows the console as it is with
  // that many licensed channels, so the large console can be tested and photographed without a vendor key. Not in release builds.
  if (const int n = testChannels_ > 0 ? testChannels_ : qEnvironmentVariableIntValue("AMIX_TEST_CHANNELS"); n > 0) { licensedChannels_ = std::clamp(n, 1, kStrips); const bool u = licensedChannels_ >= 300;
    license_["state"] = "active"; license_["plan"] = u ? "ultimate" : "master"; license_["planName"] = u ? "ULTIMATE" : "MASTER"; license_["channels"] = licensedChannels_; license_["source"] = "key"; license_["id"] = "TEST-BUILD"; license_["name"] = "test build";
    license_["baseChannels"] = u ? 300 : 32; license_["extendChannels"] = u ? 0 : std::max(0, licensedChannels_ - 32); license_["extendPacks"] = u ? 0 : (std::max(0, licensedChannels_ - 32) + 29) / 30; }
#endif   // BASIC 1-10, PRO 1-20, STUDIO and MASTER 1-32, +30 per EXTEND pack, ULTIMATE all 300
  if (before != licensedChannels_ && rt_) for (int i = 0; i < kStrips; ++i) pushStrip(i);
  emit licenseChanged(); if (before != licensedChannels_) emit stripsChanged();
}
static bool isExtendKey(const QString& key) { return key.trimmed().startsWith(QLatin1String("AMXX1.")); }
QVariantMap Engine::checkKey(const QString& key) const {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (isExtendKey(key)) {   // a CHANNEL EXTEND pack: what it would add to the plan running now
    const am::lic::Verdict x = am::lic::verifyExtend(key.toStdString(), now, am::lic::machineId());
    QVariantMap m; m["valid"] = x.ok; m["reason"] = QString::fromStdString(x.reason); m["message"] = QString::fromStdString(x.message); m["genuine"] = x.reason != "bad-signature" && x.reason != "malformed"; m["extend"] = true;
    if (!x.ok) return m;
    const QString plan = license_.value("plan", "basic").toString(); const int base = license_.value("baseChannels", 10).toInt(), have = license_.value("channels", 10).toInt();
    const bool takes = plan == "pro" || plan == "studio" || plan == "master"; const int to = std::min(am::lic::kMaxLicensedChannels, have + am::lic::kExtendChannels);
    m["plan"] = plan; m["planName"] = license_.value("planName", "BASIC"); m["channels"] = takes ? to : have; m["change"] = takes && to > have ? "upgrade" : "same"; m["canActivate"] = takes && to > have;
    m["id"] = QString::fromStdString(x.payload["id"].asString()); m["boundToThisPc"] = true; m["expires"] = static_cast<qlonglong>(x.payload["expires"].asNumber(0));
    m["message"] = plan == "ultimate" ? QStringLiteral("Genuine extend key, but ULTIMATE already runs all 300 channels.")
                 : !takes ? QStringLiteral("Genuine extend key. It adds 30 channels to PRO, STUDIO or MASTER: activate one of those first (BASIC stays at %1 channels).").arg(base)
                 : to > have ? QStringLiteral("Genuine extend key: %1 goes from %2 to %3 channels.").arg(license_.value("planName").toString()).arg(have).arg(to)
                 : QStringLiteral("Genuine extend key, but this console already runs 300 channels.");
    return m;
  } const am::lic::Verdict v = am::lic::verifyKey(key.toStdString(), now, am::lic::machineId());
  QVariantMap m; m["valid"] = v.ok; m["reason"] = QString::fromStdString(v.reason); m["message"] = QString::fromStdString(v.message); m["genuine"] = v.reason != "bad-signature" && v.reason != "malformed";
  if (!v.ok) return m;
  const am::lic::Plan* cur = am::lic::plan(license_.value("plan", "basic").toString().toStdString()); const am::lic::Plan* to = am::lic::plan(v.planId); if (!cur) cur = &am::lic::basic();
  const int d = to->rank - cur->rank; m["plan"] = QString::fromLatin1(to->id); m["planName"] = QString::fromLatin1(to->name); m["channels"] = to->channels; m["change"] = d > 0 ? "upgrade" : d < 0 ? "downgrade" : "same"; m["canActivate"] = d >= 0;
  m["name"] = QString::fromStdString(v.payload["name"].asString()); m["id"] = QString::fromStdString(v.payload["id"].asString()); m["boundToThisPc"] = v.payload.has("mid"); m["expires"] = static_cast<qlonglong>(v.payload["expires"].asNumber(0));
  m["message"] = d > 0 ? QStringLiteral("Genuine key: upgrades %1 to %2 (%3 channels).").arg(QString::fromLatin1(cur->name), QString::fromLatin1(to->name)).arg(to->channels) : d == 0 ? QStringLiteral("Genuine key for the %1 plan you already have (renews it).").arg(QString::fromLatin1(to->name)) : QStringLiteral("Genuine key, but %1 is lower than your %2 plan: activating it would downgrade.").arg(QString::fromLatin1(to->name), QString::fromLatin1(cur->name));
  return m;
}
QVariantMap Engine::activateKey(const QString& key) {
  if (isExtendKey(key)) {   // a CHANNEL EXTEND pack is kept beside the licence; it counts while PRO / STUDIO / MASTER runs
    const am::lic::Verdict x = am::lic::addExtend(key.toStdString(), QDateTime::currentMSecsSinceEpoch());
    QVariantMap m; m["ok"] = x.ok; m["reason"] = QString::fromStdString(x.reason); m["message"] = QString::fromStdString(x.message); m["extend"] = true;
    if (x.ok) { refreshLicense(); m["message"] = license_.value("extendPacks").toInt() > 0 ? QStringLiteral("Extend pack added: %1 now runs %2 channels.").arg(edition()).arg(licensedChannels_)
                                                : QStringLiteral("Extend pack kept on this computer. It adds 30 channels as soon as PRO, STUDIO or MASTER is active."); }
    return m;
  }
  const am::lic::Verdict v = am::lic::activate(key.toStdString(), QDateTime::currentMSecsSinceEpoch());
  QVariantMap m; m["ok"] = v.ok; m["reason"] = QString::fromStdString(v.reason); m["message"] = QString::fromStdString(v.message);
  if (v.ok) { refreshLicense(); m["message"] = QStringLiteral("%1 activated on this computer.").arg(edition()); }
  return m;
}
QVariantList Engine::fxPresets() const {
  QVariantList l; for (const auto& p : am::fx::presets()) { QVariantMap m; m["id"] = p.id; m["name"] = QString::fromStdString(p.name); m["kind"] = QString::fromStdString(p.kind); m["category"] = QString::fromStdString(p.category); m["mix"] = p.mix; l << m; }
  return l;
}
bool Engine::applyFxPreset(int k, int id) {
  if (k < 0 || k >= kBuses) return false; const am::fx::Preset* p = am::fx::preset(id); if (!p) return false;
  BusUi& b = buses_[k]; const QString kind = QString::fromStdString(p->kind);
  if (b.fxKind != kind) { b.fxKind = kind; b.fxValues.clear(); b.fxBypass = false; makeBusFx(k); if (b.returnDb <= -60) { b.returnDb = 0; if (rt_) rt_->setBusReturn(k, 0); } }
  for (const auto& [pid, v] : p->values) { b.fxValues[QString::fromStdString(pid)] = v; if (b.fx) b.fx->set(pid, v); }
  b.fxMix = p->mix; if (b.fx) b.fx->setMix(p->mix); b.fxBypass = false; if (b.fx) b.fx->setBypass(false); b.fxPreset = id;
  emit busesChanged(); return true;
}
// ---- AUTO LICENCE
static QString amixHome() { return QString::fromStdString(am::paths::dataDir()); }
bool Engine::autoLicense() const { return settings_.value("license/auto", true).toBool(); }
QString Engine::autoLicenseRequestPath() const { return amixHome() + "/key-request.json"; }
void Engine::setAutoLicense(bool on) {
  settings_.setValue("license/auto", on);
  if (!on) { delete keyWatch_; keyWatch_ = nullptr; keyScan_.stop(); autoStatus_ = "off"; emit licenseChanged(); return; }
  if (!keyWatch_) {
    keyWatch_ = new QFileSystemWatcher(this);
    for (const QString& d : {amixHome(), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)}) if (!d.isEmpty() && QDir(d).exists()) keyWatch_->addPath(d);
    connect(keyWatch_, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) { keyScan_.start(); });
    keyScan_.setSingleShot(true); keyScan_.setInterval(600); connect(&keyScan_, &QTimer::timeout, this, [this] { scanForKeys(); });
  }
  autoStatus_ = license_.value("state").toString() == "active" && license_.value("source").toString() == "key" ? "active" : "watching " + QString::number(keyWatch_->directories().size()) + " folders for a key file";
  emit licenseChanged(); scanForKeys();
}
QString Engine::writeLicenseRequest(const QString& plan, const QString& name, const QString& email) {
  QFile f(autoLicenseRequestPath());
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { autoStatus_ = "cannot write " + autoLicenseRequestPath(); emit licenseChanged(); return QString(); }
  f.write(licenseRequest(plan, name, email).toUtf8()); f.close(); autoStatus_ = "request written; waiting for the vendor's key file"; emit licenseChanged(); return autoLicenseRequestPath();
}
int Engine::scanForKeys() {
  if (license_.value("state").toString() == "active" && license_.value("source").toString() == "key") { autoStatus_ = "active"; emit licenseChanged(); return 0; }
  static const QRegularExpression keyRe("AMIX1\\.[A-Za-z0-9_\\-\\.]+"); int tried = 0; QStringList dirs{amixHome(), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)};
  for (const QString& d : dirs) { if (d.isEmpty()) continue; QDir dir(d); if (!dir.exists()) continue;
    for (const QFileInfo& fi : dir.entryInfoList({"*.amixkey", "*.key", "key*.txt", "amix*.txt"}, QDir::Files, QDir::Time)) {
      if (fi.size() > 16384) continue; QFile f(fi.filePath()); if (!f.open(QIODevice::ReadOnly)) continue; const QString text = QString::fromUtf8(f.readAll()); ++tried;
      auto it = keyRe.globalMatch(text); while (it.hasNext()) { const QString key = it.next().captured(0); const QVariantMap r = activateKey(key);
        if (r["ok"].toBool()) { autoStatus_ = "activated from " + fi.fileName(); emit licenseChanged(); return tried; } autoStatus_ = fi.fileName() + ": " + r["message"].toString(); } } }
  if (tried == 0 && autoStatus_ != "off" && !autoStatus_.startsWith("request")) autoStatus_ = "watching for a key file (*.amixkey, *.key, key*.txt) in " + amixHome() + " or Downloads";
  emit licenseChanged(); return tried;
}
bool Engine::deactivateKey() { const bool r = am::lic::deactivate(); refreshLicense(); return r; }
bool Engine::removeExtendPacks() { const bool r = am::lic::removeExtends(); refreshLicense(); return r; }
QString Engine::licenseRequest(const QString& plan, const QString& name, const QString& email) const { return QString::fromStdString(am::lic::request(plan.toStdString(), name.toStdString(), email.toStdString(), QDateTime::currentMSecsSinceEpoch(), AMIX_VERSION).dump()); }
QString Engine::vendorKeyFingerprint() const { return QString::fromStdString(am::lic::fingerprint(am::lic::kVendorJwk)); }

// ---- driver intelligence
static QVariantMap attemptMap(const am::drv::Attempt& a) { QVariantMap m; m["mode"] = QString::fromStdString(a.mode); m["backend"] = QString::fromStdString(a.backend); m["in"] = QString::fromStdString(a.inDevice); m["out"] = QString::fromStdString(a.outDevice); m["exclusive"] = a.exclusive; m["why"] = QString::fromStdString(a.why); m["fromLock"] = a.fromLock; m["pinned"] = a.pinned; return m; }
static std::vector<am::DeviceInfo> devInfos(const QVariantList& l) { std::vector<am::DeviceInfo> v; for (const QVariant& x : l) { const QVariantMap m = x.toMap(); am::DeviceInfo d; d.backend = m["backend"].toString().toStdString(); d.id = m["id"].toString().toStdString(); d.name = m["name"].toString().toStdString(); d.inChannels = m["inChannels"].toInt(); d.outChannels = m["outChannels"].toInt(); d.defaultIn = m["defaultIn"].toBool(); d.defaultOut = m["defaultOut"].toBool(); d.defaultRate = m["defaultRate"].toDouble(); v.push_back(d); } return v; }
bool Engine::openBluetooth(const QString& backend, const QString& inId, const QString& outId, int frames) {
  auto find = [&](const QString& b, const QString& id) { for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["backend"].toString() == b && d["id"].toString() == id) return d; } return QVariantMap(); };
  const QVariantMap out = find(backend, outId); if (out.isEmpty()) { btNote_ = QStringLiteral("That Bluetooth output is not in the list any more: RESCAN (is it still connected?)"); status_ = btNote_; emit stateChanged(); return false; }
  const double own = out["defaultRate"].toDouble() > 8000 ? out["defaultRate"].toDouble() : 48000.0; const int fr = std::max(512, frames); const bool wantIn = inId != "-" && !inId.isEmpty();
  struct Try { QString backend, in, out; double rate; QString what; }; QList<Try> tries;
  tries.append({backend, wantIn ? inId : QStringLiteral("-"), outId, own, QStringLiteral("at the device's own rate")});
  if (own != 48000) tries.append({backend, wantIn ? inId : QStringLiteral("-"), outId, 48000, QStringLiteral("at 48 kHz")});
  if (wantIn) tries.append({backend, QStringLiteral("-"), outId, own, QStringLiteral("without the hands-free microphone (opening it and the music profile together was refused)")});
  const QString other = backend == "dsound" ? QStringLiteral("wasapi") : QStringLiteral("dsound"); QString stem = out["name"].toString(); const int par = stem.indexOf('('); if (par >= 0) stem = stem.mid(par + 1).remove(')').trimmed();
  for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["backend"].toString() != other || d["outChannels"].toInt() <= 0 || d["id"].toString().startsWith("loopback:")) continue; if (d["name"].toString() == out["name"].toString() || (!stem.isEmpty() && d["name"].toString().contains(stem, Qt::CaseInsensitive))) { tries.append({other, QStringLiteral("-"), d["id"].toString(), d["defaultRate"].toDouble() > 8000 ? d["defaultRate"].toDouble() : 48000.0, QStringLiteral("on ") + other.toUpper() + QStringLiteral(" instead (the first path was refused)")}); break; } }
  QStringList failed;
  for (const Try& t : tries) { const int inCh = t.in == "-" ? 0 : 2; if (open(t.backend, t.in, t.out, t.rate, fr, inCh, 2)) { btNote_ = QStringLiteral("Bluetooth started %1: %2, %3 Hz, %4 frames%5").arg(t.what, t.backend.toUpper()).arg(t.rate, 0, 'f', 0).arg(fr).arg(failed.isEmpty() ? QString() : QStringLiteral("  ·  before that: ") + failed.join("; ")); lastRoute_ = btNote_; emit stateChanged(); return true; }
    failed << t.what + ": " + status_; }
  btNote_ = QStringLiteral("Bluetooth did not start. Tried: %1. Check that the device is connected and set as an output in Windows, then RESCAN.").arg(failed.join("; ")); status_ = btNote_; emit stateChanged(); return false;
}
void Engine::rememberDeviceNames(const std::string& backend, const std::string& in, const std::string& out) {
  curBackend_ = backend; curIn_ = in; curOut_ = out;
  auto nameOf = [&](const std::string& id, bool wantIn) { if (id == "-") return QString(); for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["backend"].toString().toStdString() != backend) continue; if (wantIn ? d["inChannels"].toInt() <= 0 : d["outChannels"].toInt() <= 0) continue; if (id.empty() ? (wantIn ? d["defaultIn"].toBool() : d["defaultOut"].toBool()) : (d["id"].toString().toStdString() == id || d["name"].toString().toStdString() == id)) return d["name"].toString(); } return QString::fromStdString(id.empty() ? backend + " default" : id); };
  inDevName_ = nameOf(in, true); outDevName_ = nameOf(out, false);
}
QVariantMap Engine::ioInfo() const {
  QVariantMap m; if (!rt_ || !running()) return m; const QString b = backend();
  auto kindOf = [&](const QString& name, bool wantIn) { for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["backend"].toString() == b && d["name"].toString() == name && (wantIn ? d["inChannels"] : d["outChannels"]).toInt() > 0) return d["kind"].toString(); } return QString::fromStdString(am::classifyDevice(b.toStdString(), name.toStdString(), -1, wantIn)); };
  m["digitalOut"] = digitalOut_ >= 0 ? digitalOut_ + 1 : 0; m["backend"] = b; m["inName"] = inDevName_; m["outName"] = outDevName_; m["inKind"] = devIn_ > 0 ? kindOf(inDevName_, true) : QString(); m["outKind"] = kindOf(outDevName_, false); m["inputs"] = devIn_; m["outputs"] = devOut_;
  m["blockMs"] = sampleRate() > 0 ? 1000.0 * blockFrames_ / sampleRate() : 0.0; return m;
}
QString Engine::inputLabel(int input) const {
  if (am::Runtime::isAppInput(input)) return QStringLiteral("APP · ") + (appInputName ? appInputName(input) : QString::number(input - am::Runtime::kAppInBase + 1));
  if (input < 0 || !rt_) return QStringLiteral("—"); QString n = inDevName_; n.remove(QRegularExpression("\\s*\\(.*\\)$")); if (n.size() > 14) n = n.left(14);
  return (n.isEmpty() ? QString() : n + " · ") + "IN " + QString::number(input + 1);
}
void Engine::setInputAllowed(const QString& backend, const QString& id, bool on) {
  QStringList l = allowedInputs(); const QString k = backend + "|" + id; l.removeAll(k); if (on && !backend.isEmpty() && !id.isEmpty()) l << k; settings_.setValue("audio/allowedInputs", l); emit devicesChanged();
}
QVariantList Engine::inputCandidates() const {
  QVariantList l; for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); const QString b = d["backend"].toString(), id = d["id"].toString();
    if (b == "null" || d["inChannels"].toInt() <= 0) continue;   // COMPUTER SOUND and APP AUDIO are listed too: they can be allowed like a microphone
    QVariantMap m; m["backend"] = b; m["id"] = id; m["name"] = d["name"]; m["kind"] = d["kind"]; m["defaultIn"] = d["defaultIn"]; m["allowed"] = inputAllowed(b, id); l << m; }
  return l;
}
// SECURE INPUT, applied to every stream the console opens (open, SMART START and its fallbacks): the input device is resolved (the
// default capture device when none is named; ASIO's input is the driver itself) and opened only if it is on the allowed list; an
// allowed WASAPI input is opened exclusively. Anything else: no input at all (outputs keep working), never a silent fallback.
void Engine::secureInputFor(am::DeviceConfig& cfg) {
  if (!secureInput() || cfg.inChannels <= 0 || cfg.inDevice == "-" || cfg.backend == "null") return;
  const QString b = QString::fromStdString(cfg.backend); QString id = QString::fromStdString(cfg.inDevice), name;
  if (id.isEmpty() && b == "asio") id = QString::fromStdString(cfg.outDevice);
  for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["backend"].toString() != b || d["inChannels"].toInt() <= 0) continue;
    if (id.isEmpty() ? d["defaultIn"].toBool() : d["id"].toString() == id) { id = d["id"].toString(); name = d["name"].toString(); break; } }
  if (id.isEmpty() || !inputAllowed(b, id)) {
    cfg.inChannels = 0; cfg.inDevice = "-"; devIn_ = 0;
    micOffNote_ = QStringLiteral("SECURE INPUT: %1 is not on the allowed list, so no input is opened (outputs only). Allow it on SOUND CARD → AUDIO ACCESS.").arg(name.isEmpty() ? QStringLiteral("the default input") : name);
    return; }
  cfg.inDevice = id.toStdString();   // the default resolved to the exact allowed device: a new default cannot slip in between
  if (b == "wasapi" && !id.startsWith("loopback:")) cfg.exclusive = true;   // exclusive: no other program shares, taps or mixes into the allowed microphone (a loopback is always shared)
}
QString Engine::hiresSummary() const { QStringList l; for (const auto& i : am::hr::libraries()) l << QString::fromStdString(i.name + " " + i.version) + (i.available ? QString() : QStringLiteral(" (not installed)")); return l.join(QStringLiteral(" · ")); }
QString Engine::inputSource() const { if (!rt_ || devIn_ <= 0 || curIn_ == "-") return QStringLiteral("none"); if (curIn_.rfind("loopback:app:", 0) == 0) return QStringLiteral("app"); return curIn_.rfind("loopback:", 0) == 0 ? QStringLiteral("music") : QStringLiteral("mic"); }
// APP SOUND: the programs that have a sound stream on the default output now (Windows' own audio sessions), read afresh at each call
QVariantList Engine::appSources() {
  refreshDevices(); QVariantList l;
  for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); const QString id = d["id"].toString(); if (!id.startsWith(QLatin1String("loopback:app:"))) continue;
    QString name = d["name"].toString(); if (name.startsWith(QLatin1String("App sound: "))) name = name.mid(11);
    l << QVariantMap{{"id", id}, {"name", name}, {"current", rt_ && QString::fromStdString(curIn_) == id}}; }
  return l;
}
QString Engine::appSourceName() const {
  if (curIn_.rfind("loopback:app:", 0) != 0) return {};
  for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["id"].toString() == QString::fromStdString(curIn_)) { QString n = d["name"].toString(); return n.startsWith(QLatin1String("App sound: ")) ? n.mid(11) : n; } }
  return QStringLiteral("app");
}
bool Engine::computerSoundAvailable() const { for (const QVariant& v : devices_) if (v.toMap()["id"].toString() == "loopback:system") return true; return false; }
QVariantMap Engine::setInputSource(const QString& source) {
  // "mic", "music" (COMPUTER SOUND: every program but this one) or an APP SOUND id "loopback:app:<pid>" (one program only)
  const bool app = source.startsWith(QLatin1String("loopback:app:")), music = source == "music" || app;
  const QString loopIn = app ? source : QStringLiteral("loopback:system");
  const QString what = app ? QStringLiteral("APP SOUND") : QStringLiteral("COMPUTER SOUND");
  // phones linked: the service holds the sound card, so the input is changed there
  if (!rt_ && remoteInput && remoteInput(music ? loopIn : QString())) {
    status_ = music ? QStringLiteral("INPUT: %1, on the remote service (phones are linked).").arg(what) : QStringLiteral("INPUT: MIC, on the remote service (phones are linked).");
    emit stateChanged(); return QVariantMap{{"remote", true}}; }
  if (!rt_) { status_ = QStringLiteral("Start the sound card first (SOUND CARD), then pick the input source."); emit stateChanged(); return {}; }
  const QString backend = QString::fromStdString(curBackend_);
  if (music && backend != "wasapi") { status_ = QStringLiteral("%1 as the input is a Windows audio (WASAPI) stream: open the headphones or speakers on WASAPI first (SOUND CARD).").arg(what); emit stateChanged(); return {}; }
  if (music && !computerSoundAvailable()) { status_ = QStringLiteral("COMPUTER SOUND needs Windows 10 version 2004 or later; on older Windows pick APP AUDIO on STREAM & MEDIA with a different output device."); emit stateChanged(); return {}; }
  if (!music && privacyOff_ && backend != "asio") { status_ = QStringLiteral("The microphone is switched off in Windows privacy: the console follows and opens none. Allow it in Windows when you want it back."); emit stateChanged(); return {}; }
  if (!music && micAccessOff() && backend != "asio") { status_ = QStringLiteral("MIC ACCESS is OFF on SOUND CARD → AUDIO ACCESS: switch it on to use the microphone."); emit stateChanged(); return {}; }
  // what plays now, to come back to if the microphone cannot be opened (the sound is never left off by a press on INPUT)
  const std::string keepB = curBackend_, keepIn = curIn_, keepOut = curOut_; const int keepInCh = devIn_, keepOutCh = devOut_;
  const QString out = QString::fromStdString(curOut_);
  // the microphone: the system's default input on Windows audio / DirectSound; on ASIO the driver's own inputs (the same device)
  const QString in = music ? loopIn : (backend == "asio" ? out : QString());
  const double rate = sampleRate() > 0 ? sampleRate() : 48000; const int frames = blockFrames_ > 0 ? blockFrames_ : 256, outCh = devOut_ > 0 ? devOut_ : 2;
  askedMic_ = !music;   // asked for by name: the automatic headset rule (headsetMicOff) does not take it away
  const bool ok = open(backend, in, out, rate, frames, 2, outCh);
  askedMic_ = false;
  const QString why = micOffNote_;
  if (!ok || (!music && devIn_ <= 0)) {
    const QString reason = !ok ? status_ : (why.isEmpty() ? QStringLiteral("the sound card opened without an input") : why);
    if (!ok) open(QString::fromStdString(keepB), QString::fromStdString(keepIn), QString::fromStdString(keepOut), rate, frames, keepInCh, keepOutCh);   // the sound comes back as it was
    status_ = (music ? QStringLiteral("%1 could not be opened: ").arg(what) : QStringLiteral("The microphone could not be opened: ")) + reason
              + (music ? QString() : QStringLiteral(" · Check that a microphone is plugged in or built in, that Windows lets desktop apps use it (Settings → Privacy & security → Microphone), and that no other program holds it."));
    emit stateChanged(); return {}; }
  QVariantMap m; m["backend"] = backend; m["in"] = in; m["out"] = out; m["rate"] = rate; m["frames"] = frames; m["inChannels"] = 2; m["outChannels"] = outCh;
  status_ = (app ? QStringLiteral("INPUT: APP SOUND (%1 only) on CH 1-2. A service that protects its stream (DRM) may come through silent: Windows does not give its sound to other programs · ").arg(appSourceName())
             : music ? QStringLiteral("INPUT: COMPUTER SOUND on CH 1-2 (every app but AmF X1, so no loop on these headphones / speakers) · ") : QStringLiteral("INPUT: MIC on CH 1-2 · ")) + status_;
  emit stateChanged(); return m;
}
// ---- LOW LATENCY SOUND DETECTION
QVariantMap Engine::latencyReport() const {
  QVariantMap m; if (!rt_) { m["running"] = false; m["verdict"] = QStringLiteral("Start the sound card to measure its latency."); return m; }
  am::latency::Input in; in.backend = curBackend_; in.exclusive = rt_->exclusive(); in.rate = sampleRate() > 0 ? sampleRate() : 48000; in.blockFrames = blockFrames_ > 0 ? blockFrames_ : 256;
  rt_->latencyFrames(in.inFrames, in.outFrames); in.hasInput = devIn_ > 0;
  for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["id"].toString() == QString::fromStdString(curOut_) && d["kind"].toString() == "bluetooth") in.bluetoothOut = true; }
  in.hybrid = cpuOnce().topo.classes > 1; in.fastCores = sysFast_;
  const am::Snapshot sn = rt_->snapshot(); in.xruns = sn.xruns; in.maxBlockUs = sn.maxBlockUs;
  const am::latency::Report r = am::latency::assess(in);
  m["running"] = true; m["driver"] = QString::fromStdString(r.driver); m["rate"] = in.rate; m["frames"] = in.blockFrames; m["inMs"] = r.inMs; m["outMs"] = r.outMs; m["blockMs"] = r.blockMs; m["totalMs"] = r.totalMs; m["reported"] = r.reported;
  m["level"] = QString::fromStdString(r.levelName); m["verdict"] = QString::fromStdString(r.verdict);
  const int fb = am::latency::suggestFastBlock(in); m["fastBlock"] = fb; m["fastBlockMs"] = 1000.0 * fb / in.rate;   // TRUE BUFFER: shown, never applied by itself
  QStringList adv; for (const auto& a : r.advice) adv << QString::fromStdString(a); m["advice"] = adv; m["roundTrip"] = lastRoundTrip_;
  return m;
}
// the test: a click mixed into the master (after whatever source was there) and input 1 recorded raw for 0.8 s, block by block in step
struct RoundTripProbe : am::Runtime::ExtraSource, am::Runtime::Tap {
  std::vector<float> click, rec; std::size_t clickAt = 0; std::atomic<std::size_t> outPos{0}, inPos{0};
  am::Runtime::ExtraSource* prevExtra = nullptr; am::Runtime::Tap* prevTap = nullptr; int prevRaw = -1;
  void render(float* L, float* R, std::size_t frames, double rate) override {
    if (prevExtra) prevExtra->render(L, R, frames, rate);
    const std::size_t p = outPos.load(); for (std::size_t i = 0; i < frames; ++i) { const std::size_t k = p + i; if (k >= clickAt && k < clickAt + click.size()) { L[i] += click[k - clickAt]; R[i] += click[k - clickAt]; } }
    outPos.store(p + frames); }
  void onBlock(const float* const* strips, int n, const float* L, const float* R, std::size_t frames, double rate) override {
    if (prevTap) prevTap->onBlock(strips, n, L, R, frames, rate);
    const std::size_t p = inPos.load(); if (n > 0 && strips[0]) for (std::size_t i = 0; i < frames && p + i < rec.size(); ++i) rec[p + i] = strips[0][i];
    inPos.store(p + frames); }
};
QVariantMap Engine::startRoundTrip() {
  if (!rt_) return {{"ok", false}, {"note", QStringLiteral("Start the sound card first.")}};
  if (devIn_ <= 0) return {{"ok", false}, {"note", QStringLiteral("The round trip needs an input: switch AUTO INPUT on, or open the interface with its inputs.")}};
  if (rtProbe_) return {{"ok", false}, {"note", QStringLiteral("A round trip test is already running.")}};
  const double rate = sampleRate() > 0 ? sampleRate() : 48000;
  auto p = std::make_shared<RoundTripProbe>(); p->click = am::latency::testClick(rate); p->clickAt = static_cast<std::size_t>(0.1 * rate); p->rec.assign(static_cast<std::size_t>(0.8 * rate), 0.f);
  p->prevExtra = rt_->extraSource(); p->prevTap = rt_->tap(); p->prevRaw = rt_->tapRawStrip();
  rtProbe_ = p; rt_->setTapRawStrip(0); rt_->setTap(p.get()); rt_->setExtraSource(p.get());
  lastRoundTrip_ = QVariantMap{{"state", "running"}};
  QTimer::singleShot(900, this, [this] { finishRoundTrip(0); });
  return {{"ok", true}};
}
void Engine::finishRoundTrip(int tries) {
  auto p = rtProbe_; if (!p) return;
  if (p->inPos.load() < p->rec.size() && tries < 11 && rt_) { QTimer::singleShot(100, this, [this, tries] { finishRoundTrip(tries + 1); }); return; }   // not all recorded yet; 0.9 s + 11 x 0.1 s = a clean stop at 2 s
  if (rt_) { rt_->setExtraSource(p->prevExtra); rt_->setTap(p->prevTap); rt_->setTapRawStrip(p->prevRaw); }
  QTimer::singleShot(200, this, [p] {});   // keeps the probe alive a moment longer, past any block that still holds the old pointers
  rtProbe_.reset();
  const double rate = sampleRate() > 0 ? sampleRate() : 48000; double conf = 0;
  const double ms = am::latency::roundTripMs(p->rec.data(), std::min(p->rec.size(), p->inPos.load()), p->clickAt, rate, &conf);
  QVariantMap r;
  if (ms < 0) { r["state"] = "none"; r["note"] = QStringLiteral("No click heard within 2 s: the test needs a loop cable from output 1 to input 1 (or the microphone near the speaker, volume up). Connect one and try again."); r["confidence"] = conf; }
  else { r["state"] = "done"; r["ms"] = ms; r["confidence"] = conf;
    r["note"] = QStringLiteral("Round trip measured: %1 ms (%2), from the output through the converters and back in.").arg(ms, 0, 'f', 1).arg(QString::fromLatin1(am::latency::levelName(am::latency::levelOf(ms)))); }
  lastRoundTrip_ = r; emit stateChanged();
}
QVariantMap Engine::setAutoInput(bool on) {
  setMicAtStart(on);
  if (!rt_) {   // phones linked: the service holds the sound card
    if (remoteInput && remoteInput(on ? QString() : QStringLiteral("-"))) { status_ = on ? QStringLiteral("AUTO INPUT ON, on the remote service (phones are linked).") : QStringLiteral("AUTO INPUT OFF: the remote service captures nothing."); emit stateChanged(); return QVariantMap{{"remote", true}}; }
    status_ = on ? QStringLiteral("AUTO INPUT ON: the input opens when the sound card starts.") : QStringLiteral("AUTO INPUT OFF: no input will be opened."); emit stateChanged(); return {}; }
  if (on) {
    QVariantMap r = setInputSource(QStringLiteral("mic"));
    if (r.isEmpty() && curBackend_ == "wasapi" && computerSoundAvailable()) r = setInputSource(QStringLiteral("music"));   // no microphone here: the computer's sound
    return r; }
  if (devIn_ > 0) {   // OFF: the sound card is reopened with outputs only, so nothing is captured
    const double rate = sampleRate() > 0 ? sampleRate() : 48000; const int frames = blockFrames_ > 0 ? blockFrames_ : 256, outCh = devOut_ > 0 ? devOut_ : 2;
    if (!open(QString::fromStdString(curBackend_), QStringLiteral("-"), QString::fromStdString(curOut_), rate, frames, 0, outCh)) return {}; }
  status_ = QStringLiteral("AUTO INPUT OFF: no input is open, nothing is captured · ") + status_; emit stateChanged();
  return QVariantMap{{"backend", QString::fromStdString(curBackend_)}, {"in", QStringLiteral("-")}, {"out", QString::fromStdString(curOut_)}, {"rate", sampleRate()}, {"frames", blockFrames_}, {"inChannels", 0}, {"outChannels", devOut_}};
}
QVariantMap Engine::secureInputPreview(const QString& backend, const QString& in, const QString& out, int inChannels) {
  am::DeviceConfig cfg; cfg.backend = backend.toStdString(); cfg.inDevice = in.toStdString(); cfg.outDevice = out.toStdString(); cfg.inChannels = inChannels;
  const QString keepNote = micOffNote_; const int keepIn = devIn_; micOffNote_.clear(); secureInputFor(cfg);
  QVariantMap m; m["in"] = QString::fromStdString(cfg.inDevice); m["inChannels"] = cfg.inChannels; m["exclusive"] = cfg.exclusive; m["note"] = micOffNote_;
  micOffNote_ = keepNote; devIn_ = keepIn; return m;
}
void Engine::nullInputFor(am::DeviceConfig& cfg) const {
  if (cfg.backend != "null" || !nullTone_) return; const double fs = cfg.sampleRate > 0 ? cfg.sampleRate : 48000; const int ch = cfg.inChannels;
  if (ch < 1) return;   // opened with no inputs: there is no channel to put the tone on
  cfg.nullInput = [fs, ch](float* const* in, std::size_t frames, std::uint64_t frame0) { for (std::size_t i = 0; i < frames; ++i) { const double t = static_cast<double>(frame0 + i) / fs; in[0][i] = static_cast<float>(0.25 * std::sin(6.283185307 * 440 * t)); if (ch > 1) in[1][i] = static_cast<float>(0.25 * std::sin(6.283185307 * 660 * t)); } };   // the second tone only where a second channel exists
}
QVariantList Engine::driverPlan(const QString& backend, const QString& in, const QString& out) const { QVariantList l; for (const auto& a : am::drv::plan(backend.toStdString(), in.toStdString(), out.toStdString(), devInfos(devices_), QDateTime::currentMSecsSinceEpoch())) l << attemptMap(a); return l; }
QVariantList Engine::driverLocks() const { QVariantList l; for (const auto& k : am::drv::locks()) { QVariantMap m; m["name"] = QString::fromStdString(k.name); m["mode"] = QString::fromStdString(k.mode); m["device"] = QString::fromStdString(k.device); m["reason"] = QString::fromStdString(k.reason); m["at"] = static_cast<qlonglong>(k.at); m["retryAt"] = k.mode == "asio" ? 0 : static_cast<qlonglong>(k.at + am::drv::kRetryMs); l << m; } return l; }
QVariantList Engine::driverPins() const { QVariantList l; for (const auto& p : am::drv::pins(devInfos(devices_))) { QVariantMap m; m["name"] = QString::fromStdString(p.name); m["on"] = p.on; m["reason"] = QString::fromStdString(p.reason); l << m; } return l; }
void Engine::setDriverPin(const QString& name, bool on) { am::drv::setPin(name.toStdString(), on); emit devicesChanged(); }
bool Engine::openRouting() const { return am::drv::openRouting(); }
void Engine::setOpenRouting(bool on) { am::drv::setOpenRouting(on); settings_.setValue("audio/openRouting", on); emit devicesChanged(); }
QString Engine::driverFallback() const { return QString::fromStdString(am::drv::fallback()); }
void Engine::setDriverFallback(const QString& f) { am::drv::setFallback(f.toStdString()); emit devicesChanged(); }
static bool looksBluetooth(const QString& n) { static const QRegularExpression re("bluetooth|hands-free|hands free|\\bhfp\\b|a2dp|airpods|\\bbt\\b|wireless|headset|earbuds|buds|jbl|bose|sony wh|wf-|galaxy buds|soundcore|boat|beats", QRegularExpression::CaseInsensitiveOption); return re.match(n).hasMatch(); }
QVariantList Engine::bluetoothDevices() const {
  QVariantList l; for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["backend"] == "null" || d["backend"] == "asio") continue; if (!looksBluetooth(d["name"].toString())) continue; l << d; }
  std::stable_sort(l.begin(), l.end(), [](const QVariant& a, const QVariant& b) { return (a.toMap()["backend"] == "dsound") > (b.toMap()["backend"] == "dsound"); });
  return l;
}
int Engine::forgetDriverLocks(const QString& name) { const int n = am::drv::forget(name.toStdString()); emit devicesChanged(); return n; }
bool Engine::openSmart(const QString& backend, const QString& inAsked, const QString& out, double rate, int frames, int inCh, int outCh) {
  if (opening_) return false;   // a start is already under way (the interface stays live during it)
  // NO INPUT ASKED, NO MICROPHONE (see open()): no device named and 0 input channels is "no input", never the default microphone; the
  // RESCUE steps below then do not add a default microphone either
  const QString in = inAsked.isEmpty() && inCh <= 0 && backend != "asio" ? QStringLiteral("-") : inAsked;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  auto steps = am::drv::plan(backend.toStdString(), in.toStdString(), out.toStdString(), devInfos(devices_), now);
  // RESCUE: when the whole plan fails the console must not stay without input and output. After the planned steps come the Realtek ASIO
  // driver (scanned from the device list, opened even when pinned), Realtek Windows shared (RWS) and the system's default endpoints.
  const std::size_t firstRescue = steps.size();
  if (backend != "null") { const auto more = am::drv::rescue(devInfos(devices_), in != "-", out != "-", steps); steps.insert(steps.end(), more.begin(), more.end()); }
  micOffNote_.clear();   // SECURE INPUT may set it again for the step that opens
  QStringList tried; const QString asioName = backend == "asio" ? (out.isEmpty() || out == "-" ? in : out) : QString();
  for (std::size_t i = 0; i < steps.size(); ++i) {
    const auto& a = steps[i];
    stop(); rt_ = std::make_unique<am::Runtime>(kStrips);
    am::DeviceConfig cfg; cfg.backend = a.backend; cfg.inDevice = a.inDevice; cfg.outDevice = a.outDevice; cfg.sampleRate = rate > 0 ? rate : 48000; cfg.frames = static_cast<unsigned>(frames > 0 ? frames : 256); blockFrames_ = static_cast<int>(cfg.frames); cfg.inChannels = inCh; cfg.outChannels = outCh; cfg.exclusive = a.exclusive; cfg.preferInt32 = preferInt32();
    devIn_ = std::clamp(inCh, 0, kStrips); devOut_ = std::clamp(outCh, 0, kOutputs);
    secureInputFor(cfg);
    std::string err; nullInputFor(cfg); rememberDeviceNames(cfg.backend, cfg.inDevice, cfg.outDevice);
    bool started = false;
    { opening_ = true; emit busyChanged(); auto pending = std::move(rt_); am::Runtime* raw = pending.get(); auto e = std::make_shared<std::string>(); started = guarded([raw, cfg, e] { return raw->open(cfg, *e) && raw->start(*e); }, 12000); opening_ = false; if (!timedOut_) rt_ = std::move(pending); emit busyChanged();
      if (timedOut_) { (void)pending.release(); err = "the driver did not answer in 12 s"; qWarning("driver start timed out: %s", cfg.backend.c_str()); } else err = *e; }
    if (started) {
      rt_->setExtraSource(&sources_);
      for (auto& st : strips_) st.dsp.setSampleRate(cfg.sampleRate);
      reloadAllVst(); pushAll();
      status_ = QStringLiteral("%1 • %2 Hz • %3 frames • %4 in / %5 out").arg(QString::fromStdString(rt_->backendName())).arg(cfg.sampleRate).arg(cfg.frames).arg(cfg.inChannels).arg(outCh);
      const QString mode = QString::fromStdString(a.mode).toUpper();
      if (i >= firstRescue) lastRoute_ = QStringLiteral("RESCUE: %1 failed (%2) → %3 works (%4).").arg(tried.join("; "), QString::fromStdString(err), mode == "RWS" ? QStringLiteral("RWS · Realtek Windows shared") : mode, QString::fromStdString(a.outDevice != "-" ? a.outDevice : a.inDevice));
      else if (a.pinned) lastRoute_ = QStringLiteral("REALTEK PIN: %1 → %2 (%3). The ASIO driver was not opened.").arg(asioName, mode, QString::fromStdString(a.inDevice != "-" ? a.inDevice : a.outDevice));
      else if (a.fromLock) lastRoute_ = QStringLiteral("AI DRIVER LOCK: %1 went straight to %2 (learned earlier: %3).").arg(asioName, mode, QString::fromStdString(a.why));
      else if (i > 0) { lastRoute_ = QStringLiteral("FALLBACK: %1 failed (%2) → %3 works. Locked for next time.").arg(tried.join("; "), QString::fromStdString(err), mode); if (!asioName.isEmpty()) am::drv::learn(asioName.toStdString(), a.mode, a.inDevice != "-" ? a.inDevice : a.outDevice, tried.join("; ").toStdString(), now); }
      else { lastRoute_ = QStringLiteral("%1 opened first try.").arg(mode); if (!asioName.isEmpty() && a.mode == "asio") am::drv::learn(asioName.toStdString(), "asio", asioName.toStdString(), "clean run", now); }
      timer_.start(); emit stateChanged(); return true;
    }
    tried << QStringLiteral("%1: %2").arg(QString::fromStdString(a.mode).toUpper(), QString::fromStdString(err)); rt_.reset();
  }
  status_ = QStringLiteral("Could not open the sound card on any path: %1").arg(tried.join(" | ")); lastRoute_ = status_; emit stateChanged(); return false;
}

QVariantList Engine::inputRoutes() const { QVariantList l; for (int i = 0, n = shownStrips(); i < n; ++i) l << inRoute_[i]; return l; }
QVariantList Engine::outputRoutes() const { QVariantList l; for (int o = 0; o < kOutputs; ++o) l << outRoute_[o]; return l; }
QVariantList Engine::outputSources() const {
  QVariantList l; auto add = [&](int idx, const QString& name, const QString& group) { QVariantMap m; m["index"] = idx; m["name"] = name; m["group"] = group; l << m; };
  add(am::Runtime::kSrcOff, "OFF", "off"); add(am::Runtime::kSrcMasterL, "MASTER L", "master"); add(am::Runtime::kSrcMasterR, "MASTER R", "master"); add(am::Runtime::kSrcMasterMono, "MASTER MONO", "master"); add(am::Runtime::kSrcMonL, "MONITOR L", "monitor"); add(am::Runtime::kSrcMonR, "MONITOR R", "monitor");
  for (int k = 0; k < kBuses; ++k) { add(am::Runtime::kSrcBus0L + 2 * k, buses_[k].name + " L", "bus"); add(am::Runtime::kSrcBus0L + 2 * k + 1, buses_[k].name + " R", "bus"); }
  return l;
}
void Engine::setInputRoute(int strip, int input) { if (strip < 0 || strip >= kStrips) return; inRoute_[strip] = cleanRoute(input); if (rt_) rt_->setInputRoute(strip, inRoute_[strip]); emit routingChanged(); }
void Engine::setOutputRoute(int out, int source) { if (out < 0 || out >= kOutputs) return; outRoute_[out] = source == am::Runtime::kSrcMasterMono ? source : std::clamp(source, 0, am::Runtime::kSrcBus0L + 2 * kBuses - 1); if (rt_) rt_->setOutputRoute(out, outRoute_[out]); emit routingChanged(); }
void Engine::resetRouting() { for (int i = 0; i < kStrips; ++i) inRoute_[i] = i; for (int o = 0; o < kOutputs; ++o) outRoute_[o] = am::Runtime::defaultOutputSource(o); if (rt_) { for (int i = 0; i < kStrips; ++i) rt_->setInputRoute(i, inRoute_[i]); for (int o = 0; o < kOutputs; ++o) rt_->setOutputRoute(o, outRoute_[o]); } emit routingChanged(); }
// ---- bus effects
QVariantList Engine::fxCatalog() const { QVariantList l; for (const auto& [k, n] : am::fx::catalog()) { QVariantMap m; m["kind"] = QString::fromStdString(k); m["name"] = QString::fromStdString(n); l << m; } return l; }
void Engine::makeBusFx(int k) {
  BusUi& b = buses_[k]; if (rt_) rt_->setBusProcessor(k, nullptr); if (b.fx) { fxGraveyard_.push_back(std::move(b.fx)); graveTimer_.start(); }   // restart the timer: an effect retired now must outlive the block that still runs it (a timer started for an earlier one could fire a moment later)
  if (!b.fxKind.isEmpty()) { b.fx = am::fx::make(b.fxKind.toStdString(), rt_ ? rt_->sampleRate() : 48000); if (b.fx) { for (const auto& [id, v] : b.fxValues) b.fx->set(id.toStdString(), v); b.fx->setMix(b.fxMix); b.fx->setBypass(b.fxBypass); if (rt_) rt_->setBusProcessor(k, b.fx.get()); } }
  graveTimer_.start();
}
QVariantMap Engine::busFx(int k) const {
  QVariantMap m; if (k < 0 || k >= kBuses) return m; const BusUi& b = buses_[k]; m["preset"] = b.fxPreset; m["kind"] = b.fxKind; m["bypass"] = b.fxBypass; m["mix"] = b.fxMix; m["name"] = b.fx ? QString::fromLatin1(b.fx->name()) : QString();
  QVariantList ps; if (b.fx) for (const auto& p : b.fx->params()) { QVariantMap x; x["id"] = QString::fromStdString(p.id); x["title"] = QString::fromStdString(p.title); x["units"] = QString::fromStdString(p.units); x["value"] = p.value; x["default"] = p.def; x["text"] = QString::fromStdString(b.fx->text(p.id, p.value)); ps << x; } m["params"] = ps;
  return m;
}
void Engine::setBusFx(int k, const QString& kind) { if (k < 0 || k >= kBuses) return; BusUi& b = buses_[k]; if (b.fxKind == kind) return; b.fxKind = kind; b.fxPreset = 0; b.fxValues.clear(); b.fxBypass = false; makeBusFx(k); if (!kind.isEmpty() && b.returnDb <= -60) { b.returnDb = 0; if (rt_) rt_->setBusReturn(k, 0); } emit busesChanged(); }
void Engine::setBusFxParam(int k, const QString& id, double v) { if (k < 0 || k >= kBuses) return; BusUi& b = buses_[k]; b.fxPreset = 0; b.fxValues[id] = std::clamp(v, 0.0, 1.0); if (b.fx) b.fx->set(id.toStdString(), b.fxValues[id]); emit busesChanged(); }
void Engine::setBusFxBypass(int k, bool on) { if (k < 0 || k >= kBuses) return; buses_[k].fxBypass = on; if (buses_[k].fx) buses_[k].fx->setBypass(on); emit busesChanged(); }
void Engine::setBusFxMix(int k, double wet) { if (k < 0 || k >= kBuses) return; buses_[k].fxMix = std::clamp(wet, 0.0, 1.0); if (buses_[k].fx) buses_[k].fx->setMix(buses_[k].fxMix); emit busesChanged(); }
void Engine::setAutomix(int i, bool on, double w) { if (i < 0 || i >= kStrips) return; StripUi& s = strips_[static_cast<std::size_t>(i)]; s.automix = on; s.amWeight = std::clamp(w, 0.25, 4.0); if (rt_) rt_->setAutomix(i, on, s.amWeight); emit stripsChanged(); }
void Engine::setWidth(double w) { width_ = std::clamp(w, 0.0, 2.0); if (rt_) rt_->setWidth(width_); emit stripsChanged(); }
QVariantMap Engine::scopeFrame(int ch, double windowMs, int points, bool trigger) {
  QVariantMap m; std::vector<float> w; const std::size_t span = static_cast<std::size_t>(std::max(1.0, windowMs) * scope_.sampleRate() / 1000.0);
  scope_.window(ch, span, points, trigger, w); QVariantList l; l.reserve(static_cast<int>(w.size())); for (float v : w) l << v; m["samples"] = l;
  const ScopeTap::Figures f = scope_.figures(ch, span); m["vpp"] = f.vpp; m["vrms"] = f.vrms; m["freq"] = f.freq; m["crest"] = f.crest; m["running"] = rt_ != nullptr;
  return m;
}
QVariantMap Engine::soundScore() {
  am::measure::ScoreInput in; const QVariantMap& t = telemetry_;
  in.shortTerm = t.value("shortTerm", -120).toDouble(); in.truePeak = t.value("truePeak", -120).toDouble(); in.peakDb = t.value("masterPeakDb", -120).toDouble(); in.rmsDb = t.value("masterRmsDb", -120).toDouble();
  in.correlation = t.value("correlation", 1).toDouble(); in.xruns = t.value("xruns", 0).toInt(); in.masterClip = t.value("masterClip", false).toBool(); in.target = autoLevelTarget();
  for (const QVariant& m : meters_) { const QVariantMap x = m.toMap(); if (x.value("clip").toBool()) ++in.clipStrips; if (x.value("overComp").toBool()) ++in.overCompStrips; }
  std::vector<float> b; scope_.spectrum(0, b); for (float v : b) in.spectrum.push_back(v);
  const am::measure::Score sc = am::measure::soundScore(in); QVariantMap r; r["valid"] = sc.valid; r["total"] = sc.total; r["grade"] = QString::fromStdString(sc.grade); r["tilt"] = sc.tiltDbPerOct; r["roughness"] = sc.roughnessDb;
  QVariantList parts; for (const auto& q : sc.parts) { QVariantMap m; m["name"] = QString::fromStdString(q.name); m["points"] = q.points; m["max"] = q.max; m["tip"] = QString::fromStdString(q.tip); parts << m; } r["parts"] = parts; return r; }
QVariantList Engine::spectrum(int ch) { std::vector<float> b; scope_.spectrum(ch, b); QVariantList l; for (float v : b) l << v; return l; }
void Engine::setGraphicEq(int i, const QVariantList& gains, bool on) {
  if (i == -1) { for (int b = 0; b < 10; ++b) { const double g = b < gains.size() ? std::clamp(gains[b].toDouble(), -24.0, 24.0) : 0.0; mp_.eq[b] = am::dsp::EqBand{on && std::fabs(g) > 0.01 ? FilterType::Peaking : FilterType::Off, kIsoCentres[b], 1.41, g}; } pushMaster(); emit masterChainChanged(); return; }
  if (i < 0 || i >= kStrips) return;
  for (int b = 0; b < 10 && b < am::dsp::kEqBands; ++b) { const double g = b < gains.size() ? std::clamp(gains[b].toDouble(), -24.0, 24.0) : 0.0; setEq(i, b, on && std::fabs(g) > 0.01 ? static_cast<int>(FilterType::Peaking) : static_cast<int>(FilterType::Off), kIsoCentres[b], 1.41, g); }
}
QVariantList Engine::graphicEq(int i) const {
  QVariantList l; if (i < 0 || i >= kStrips) return l; const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params();
  for (int b = 0; b < 10 && b < am::dsp::kEqBands; ++b) { const auto& e = p.eq[static_cast<std::size_t>(b)]; l << (std::fabs(e.freq - kIsoCentres[b]) < 1.0 ? e.gainDb : 0.0); }
  return l;
}
void Engine::copyEqToAll(int i) {
  if (i < 0 || i >= kStrips) return; const auto src = strips_[static_cast<std::size_t>(i)].dsp.params();
  for (int j = 0; j < kStrips; ++j) { if (j == i) continue; for (int b = 0; b < am::dsp::kEqBands; ++b) { const auto& e = src.eq[static_cast<std::size_t>(b)]; setEq(j, b, static_cast<int>(e.type), e.freq, e.q, e.gainDb); } setCrossover(j, static_cast<int>(src.xoverType), src.hpOrder, src.hpFreq, src.lpOrder, src.lpFreq); }
}
QVariantList Engine::lissajous(int points) { std::vector<float> lr; scope_.pairs(points, lr); QVariantList l; l.reserve(static_cast<int>(lr.size())); for (float v : lr) l << v; return l; }
QVariantMap Engine::monitor() const { QVariantMap m; m["source"] = mon_.source; m["db"] = mon_.db; m["dim"] = mon_.dim; m["mono"] = mon_.mono; m["mute"] = mon_.mute; m["sync"] = mon_.sync; return m; }
void Engine::setMonitor(int source, double db, bool dim, bool mono, bool mute) {
  const double was = mon_.db;
  mon_.source = std::clamp(source, 0, kBuses); mon_.db = std::clamp(db, -60.0, 10.0); mon_.dim = dim; mon_.mono = mono; mon_.mute = mute;
  if (rt_) rt_->setMonitor(mon_.source, mon_.db, mon_.dim, mon_.mono, mon_.mute);
  if (mon_.sync && mon_.db != was) { masterDb_ = std::clamp(masterDb_ + (mon_.db - was), -60.0, 10.0); pushMasterLevel(); emit stripsChanged(); syncSystemSound(); }
  emit monitorChanged();
}
void Engine::setMonitorSync(bool on) { mon_.sync = on; emit monitorChanged(); }
void Engine::resetStripDsp(int i) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dsp.set(am::dsp::ChannelParams{}); if (rt_) pushAll(); emit stripsChanged(); }
// The factory channel layout: CH 1-2 MAIN (the device's inputs 1-2, a linked stereo pair), CH 3-4 LIVE (a linked stereo pair for the
// live source), CH 5-7 RESERVE (free for whatever the user wants; open in every edition), the rest numbered.
void Engine::factoryLayout() {
  std::vector<StripUi>& st = strips_;
  for (int i = 0; i < kStrips; ++i) st[static_cast<std::size_t>(i)].name = QStringLiteral("CH %1").arg(i + 1, 2, 10, QLatin1Char('0'));
  // CH 1 DIGITAL AUDIO and CH 2 MIC: the device's inputs 1 and 2, two separate sources, centred; CH 3-4 LIVE L/R a linked pair
  st[0].name = QStringLiteral("DIGITAL AUDIO"); st[1].name = QStringLiteral("MIC"); st[2].name = QStringLiteral("LIVE L"); st[3].name = QStringLiteral("LIVE R");
  st[2].link = st[3].link = true; st[2].pan = -1; st[3].pan = 1;
  for (int i = 4; i < 7; ++i) st[static_cast<std::size_t>(i)].name = QStringLiteral("RESERVE %1").arg(i + 1);
  // 360 AUDIO positions ready for the four (OFF: plain stereo until 360 is switched on): MAIN at the front (±30°), LIVE wider (±60°), one metre away
  const double az[4] = {0, 0, -60, 60};   // DIGITAL AUDIO and MIC in front, LIVE wide
  for (int i = 0; i < 4; ++i) { StripUi& u = st[static_cast<std::size_t>(i)]; u.spOn = false; u.spAz = az[i]; u.spEl = 0; u.spDist = 1; u.spSpread = 0; }
  // the sends, after the fader: MAIN and LIVE to AUX 1, every other channel to AUX 2, at 0 dB
  for (int i = 0; i < kStrips; ++i) { SendUi& snd = st[static_cast<std::size_t>(i)].sends[i < 4 ? 0 : 1]; snd.db = 0; snd.pre = false; }
}
void Engine::resetStrip(int i) {
  if (i < 0 || i >= kStrips) return; StripUi& s = strips_[static_cast<std::size_t>(i)];
  s.db = 0; s.pan = 0; s.mute = false; s.solo = false; s.dca = 0; s.muteGroups = 0; for (SendUi& sd : s.sends) sd = SendUi{}; s.dsp.set(am::dsp::ChannelParams{});
  if (rt_) pushAll(); emit stripsChanged(); emit groupsChanged();
}
void Engine::setPeakHold(int ms) { peakHoldMs_ = std::clamp(ms, 0, 999999); if (rt_) rt_->setPeakHoldMs(peakHoldMs_); }
void Engine::setClipThreshold(double db) { clipDb_ = std::clamp(db, -12.0, 0.0); if (rt_) rt_->setClipDb(clipDb_); }
QJsonArray Engine::scenesExport() const { QJsonArray sc; for (const SceneUi& x : scenes_) { QJsonObject o; o["name"] = x.name; o["at"] = x.at; o["data"] = x.data; sc << o; } return sc; }
QJsonArray Engine::presetsExport() const { QJsonArray pr; for (const PresetUi& p : userPresets_) { QJsonObject o; o["name"] = p.name; o["category"] = p.category; o["dsp"] = p.dsp; pr << o; } return pr; }
void Engine::consoleImport(const QJsonObject& console, const QJsonArray& scenes, const QJsonArray& presets) {
  applyConsoleJson(console, false);
  for (int i = 0; i < kScenes && i < scenes.size(); ++i) { const QJsonObject o = scenes[i].toObject(); SceneUi& x = scenes_[static_cast<std::size_t>(i)]; x.name = o.value("name").toString().left(24); x.at = o.value("at").toString(); x.data = o.value("data").toObject(); }
  userPresets_.clear(); for (const QJsonValue& v : presets) { const QJsonObject o = v.toObject(); if (o.value("name").toString().isEmpty()) continue; userPresets_.push_back({o.value("name").toString().left(24), o.value("category").toString().left(16), o.value("dsp").toObject()}); }
  save(); emit scenesChanged(); emit presetsChanged();
}
// ───────── LOUDNESS MANAGER ─────────
double Engine::loudTargetOf(int i) const {
  if (loudUi_.globalOn) return loudUi_.globalTarget;
  int c = i >= 0 && i < kStrips ? loudUi_.strips[i].cls : -1;
  if (c < 0 && i >= 0 && i < kStrips && strips_[static_cast<std::size_t>(i)].dca > 0) {   // auto: from the DCA group's name (AUTO DCA names them VOICES / MUSIC / INSTRUMENTS)
    const QString n = dcas_[strips_[static_cast<std::size_t>(i)].dca - 1].name.toUpper();
    c = n.contains(QLatin1String("MUSIC")) ? 1 : n.contains(QLatin1String("INSTR")) ? 2 : 0; }
  return c == 1 || c == 2 ? -20.0 : -18.0;   // voice -18, music -20, instrument -20
}
void Engine::pushLoud(int i) {
  if (!rt_ || i < 0 || i >= kStrips) return; const LoudStripUi& u = loudUi_.strips[i];
  am::RideParams p; p.on = u.ride; p.gate = u.gate; p.targetLufs = loudTargetOf(i); p.rangeDb = loudUi_.range; p.upDbS = loudUi_.up; p.downDbS = loudUi_.down; p.gateLufs = loudUi_.gateThr; p.holdS = loudUi_.hold;
  rt_->setRide(i, p);
}
void Engine::pushLoudAll() { for (int i = 0; i < kStrips; ++i) pushLoud(i); pushAutoLevel(); }
QVariantMap Engine::loudState() const {
  QVariantList st; int rides = 0, gates = 0; const int n = shownStrips();
  for (int i = 0; i < n && i < kStrips; ++i) { const LoudStripUi& u = loudUi_.strips[i]; rides += u.ride; gates += u.gate; st << QVariantMap{{"ride", u.ride}, {"gate", u.gate}, {"cls", u.cls}, {"target", loudTargetOf(i)}}; }
  return {{"masterOn", loudUi_.masterOn}, {"preset", loudUi_.preset}, {"masterTarget", loudUi_.masterTarget}, {"range", loudUi_.range}, {"speed", loudUi_.speed}, {"up", loudUi_.up}, {"down", loudUi_.down},
          {"gateThr", loudUi_.gateThr}, {"hold", loudUi_.hold}, {"globalOn", loudUi_.globalOn}, {"globalTarget", loudUi_.globalTarget}, {"strips", st}, {"rides", rides}, {"gates", gates}};
}
void Engine::setLoud(const QVariantMap& c) {
  if (c.contains("masterOn")) loudUi_.masterOn = c.value("masterOn").toBool();
  if (c.contains("preset")) { const QString p = c.value("preset").toString().toUpper();
    if (p == QLatin1String("STREAMING")) loudUi_.masterTarget = -14; else if (p == QLatin1String("PODCAST")) loudUi_.masterTarget = -16; else if (p == QLatin1String("BROADCAST")) loudUi_.masterTarget = -23;
    loudUi_.preset = p == QLatin1String("STREAMING") || p == QLatin1String("PODCAST") || p == QLatin1String("BROADCAST") ? p : QStringLiteral("CUSTOM"); }
  if (c.contains("masterTarget")) { loudUi_.masterTarget = std::clamp(c.value("masterTarget").toDouble(), -30.0, -8.0); if (!c.contains("preset")) loudUi_.preset = QStringLiteral("CUSTOM"); }
  if (c.contains("range")) loudUi_.range = std::clamp(c.value("range").toDouble(), 0.0, 20.0);
  if (c.contains("speed")) { const QString sp = c.value("speed").toString().toLower();   // slow 1.5 / 3, medium 3 / 6, fast 6 / 12 dB/s (up / down)
    const double k = sp == QLatin1String("slow") ? 0.5 : sp == QLatin1String("fast") ? 2.0 : 1.0; loudUi_.speed = k == 0.5 ? QStringLiteral("slow") : k == 2.0 ? QStringLiteral("fast") : QStringLiteral("medium"); loudUi_.up = 3 * k; loudUi_.down = 6 * k; }
  if (c.contains("up")) loudUi_.up = std::clamp(c.value("up").toDouble(), 0.1, 40.0);
  if (c.contains("down")) loudUi_.down = std::clamp(c.value("down").toDouble(), 0.1, 40.0);
  if (c.contains("gateThr")) loudUi_.gateThr = std::clamp(c.value("gateThr").toDouble(), -80.0, -20.0);
  if (c.contains("hold")) loudUi_.hold = std::clamp(c.value("hold").toDouble(), 0.1, 30.0);
  if (c.contains("globalOn")) loudUi_.globalOn = c.value("globalOn").toBool();
  if (c.contains("globalTarget")) loudUi_.globalTarget = std::clamp(c.value("globalTarget").toDouble(), -40.0, -6.0);
  if (c.contains("strips")) { const QVariantList l = c.value("strips").toList(); for (int i = 0; i < l.size() && i < kStrips; ++i) { const QVariantMap m = l[i].toMap(); LoudStripUi& u = loudUi_.strips[i]; u.ride = m.value("ride").toBool(); u.gate = m.value("gate").toBool(); u.cls = std::clamp(m.value("cls", -1).toInt(), -1, 2); } }
  pushLoudAll(); emit loudChanged(); emit stripsChanged();
}
void Engine::setLoudStrip(int i, const QVariantMap& c) {
  if (i < 0 || i >= kStrips) return; LoudStripUi& u = loudUi_.strips[i];
  if (c.contains("ride")) u.ride = c.value("ride").toBool();
  if (c.contains("gate")) u.gate = c.value("gate").toBool();
  if (c.contains("cls")) u.cls = std::clamp(c.value("cls").toInt(), -1, 2);
  pushLoud(i); if (linked(i)) { loudUi_.strips[partner(i)] = u; pushLoud(partner(i)); } emit loudChanged(); emit stripsChanged();
}
void Engine::setLoudAll(const QString& what, bool on) {
  for (int i = 0; i < shownStrips() && i < kStrips; ++i) { if (what == QLatin1String("gate")) loudUi_.strips[i].gate = on; else loudUi_.strips[i].ride = on; pushLoud(i); }
  emit loudChanged(); emit stripsChanged();
}
QJsonObject Engine::loudJson() const { QJsonObject o = QJsonObject::fromVariantMap(loudState()); o.remove("rides"); o.remove("gates"); QJsonArray st; for (int i = 0; i < kStrips; ++i) { const LoudStripUi& u = loudUi_.strips[i]; if (!u.ride && !u.gate && u.cls < 0) { st.append(QJsonObject{}); continue; } st.append(QJsonObject{{"ride", u.ride}, {"gate", u.gate}, {"cls", u.cls}}); } while (!st.isEmpty() && st.last().toObject().isEmpty()) st.removeLast(); o["strips"] = st; return o; }
void Engine::loudFromJson(const QJsonObject& o) { loudUi_ = LoudUi{}; if (!o.isEmpty()) { const QVariantMap m = o.toVariantMap(); QVariantMap c = m; if (m.value("preset").toString() == QLatin1String("CUSTOM")) c.remove("preset"); else c.remove("masterTarget"); c.remove("speed"); setLoud(c); if (m.value("preset").toString() == QLatin1String("CUSTOM")) loudUi_.preset = QStringLiteral("CUSTOM"); loudUi_.speed = m.value("speed", QStringLiteral("medium")).toString(); emit loudChanged(); } else { pushLoudAll(); emit loudChanged(); } }
void Engine::factoryReset() {
  keepStrips_ = 32;
  for (int i = 0; i < kStrips; ++i) { StripUi& s = strips_[static_cast<std::size_t>(i)]; for (PluginUi& p : s.plugins) if (p.vst) graveyard_.push_back(p.vst); s = StripUi{}; s.dsp.setSampleRate(48000); }
  factoryLayout();
  for (int k = 0; k < kBuses; ++k) { if (rt_) rt_->setBusProcessor(k, nullptr); if (buses_[k].fx) { fxGraveyard_.push_back(std::move(buses_[k].fx)); graveTimer_.start(); } buses_[k] = BusUi{}; buses_[k].name = k < 8 ? QStringLiteral("AUX %1").arg(k + 1) : QStringLiteral("FX %1").arg(k - 7); } width_ = 1.0;
  for (DcaUi& d : dcas_) d = DcaUi{}; for (int g = 0; g < kGroups; ++g) dcas_[g].name = QStringLiteral("DCA %1").arg(g + 1); duckUi_ = DuckUi{}; pushDuck(); loudUi_ = LoudUi{}; pushLoudAll(); emit loudChanged(); autoProp_.clear();
  for (MuteGroupUi& m : muteGroups_) m = MuteGroupUi{}; for (int g = 0; g < kGroups; ++g) muteGroups_[g].name = QStringLiteral("MUTE %1").arg(g + 1);
  masterDb_ = 0; masterLim_ = true; masterCeil_ = -0.5; masterRel_ = 100; mp_ = am::MasterChain::Params(); masterPreset_.clear(); mon_ = MonitorUi{}; for (int o = 0; o < 64; ++o) { ampUi_[o].clear(); if (rt_) rt_->setAmp(o, am::AmpParams{}); } for (int i = 0; i < kStrips; ++i) inRoute_[i] = i; for (int o = 0; o < kOutputs; ++o) outRoute_[o] = am::Runtime::defaultOutputSource(o);
  for (SceneUi& x : scenes_) x = SceneUi{}; currentScene_ = -1; recallSafe_.reset(); userPresets_.clear(); selected_ = 0; clipboard_.reset(); history_.clear(); histPos_ = -1; abOther_ = QJsonObject(); abSlot_ = "A";
  graveTimer_.start(); if (rt_) pushAll(); save(); history_.push_back(consoleJson()); histPos_ = 0;
  emit stripsChanged(); emit busesChanged(); emit groupsChanged(); emit scenesChanged(); emit presetsChanged(); emit selectedStripChanged(); emit monitorChanged(); emit routingChanged(); emit clipboardChanged(); emit historyChanged();
}
QString Engine::recordFolder() const { const QString d = settings_.value("record/folder").toString(); return d.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/A mix OS Recordings" : d; }
bool Engine::startRecording() {
  if (!rt_) { recMsg_ = QStringLiteral("Start the engine on CONNECTIONS first."); emit recordChanged(); return false; }
  if (recorder_.recording()) return true;
  am::rec::Options o; o.folder = recordFolder().toStdString(); o.master = recordMaster(); o.float32 = recordFloat(); o.format = recordFormat(); for (int i = 0; i < kStrips; ++i) if (armed_.test(static_cast<std::size_t>(i))) o.strips.push_back(i);
  std::string err; if (!recorder_.start(o, rt_->sampleRate(), err)) { recMsg_ = QString::fromStdString(err); emit recordChanged(); return false; }
  taps_.a.store(&recorder_); recMsg_ = QStringLiteral("Recording %1 file(s) into %2 · %3 thread(s) writing at low priority").arg(recorder_.files().size()).arg(recordFolder()).arg(recorder_.workers()); emit recordChanged(); return true;
}
void Engine::stopRecording() { if (!recorder_.recording()) return; taps_.a.store(nullptr); recorder_.stop(); recMsg_ = QStringLiteral("Recorded %1 s, %2 file(s)%3 in %4").arg(recorder_.seconds(), 0, 'f', 1).arg(recorder_.files().size()).arg(recorder_.dropped() ? QStringLiteral(" (%1 blocks DROPPED: disk too slow)").arg(recorder_.dropped()) : QString()).arg(recordFolder()); emit recordChanged(); }
void Engine::setArmed(int i, bool on) { if (i < 0 || i >= kStrips) return;
  if (on && !armed_.test(static_cast<std::size_t>(i)) && static_cast<int>(armed_.count()) >= am::rec::kMaxTracks) { recMsg_ = QStringLiteral("MULTITRACK: at most 100 channels can be armed at once."); emit recordChanged(); return; }
  armed_.set(static_cast<std::size_t>(i), on); emit stripsChanged(); emit recordChanged(); }
// DirectSound lists a Bluetooth or USB headset twice: the music side and the HANDS-FREE side (telephone quality, and opening its microphone
// switches the whole headset to that mode). Auto detect leaves the hands-free side off; it can still be picked by hand on CONNECTIONS.
static bool handsFree(const QVariantMap& d) { static const QRegularExpression re("hands[- ]?free|\\bhfp\\b|\\bag audio\\b|headset", QRegularExpression::CaseInsensitiveOption); return d["kind"].toString() == "headset" || re.match(d["name"].toString()).hasMatch(); }
void Engine::setAutoPrefer(const QString& p) { static const QStringList ok = {"best", "asio", "wasapi", "dsound", "bluetooth", "manual"}; settings_.setValue("audio/autoPrefer", ok.contains(p) ? p : QStringLiteral("best")); emit devicesChanged(); }
// DAC DETECTION: the playback-only converters among the devices (kind "dac"), listed for a manual OPEN as outputs only
QVariantList Engine::dacsFound() const {
  QVariantList l; for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["kind"].toString() != "dac" || d["outChannels"].toInt() < 1) continue;
    QVariantMap m; m["backend"] = d["backend"]; m["id"] = d["id"]; m["name"] = d["name"]; m["outChannels"] = d["outChannels"]; m["rate"] = d["defaultRate"];
    QVariantList rates = d["sampleRates"].toList(); double top = d["defaultRate"].toDouble(); for (const QVariant& r : rates) top = std::max(top, r.toDouble()); m["maxRate"] = top;
    m["current"] = running() && dacOpenId_ == d["backend"].toString() + "|" + d["id"].toString(); l << m; }
  return l;
}
bool Engine::openDac(const QString& backendName, const QString& id, int frames, double askRate, int bits) {
  for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["kind"].toString() != "dac" || d["backend"].toString() != backendName || d["id"].toString() != id) continue;
    const double rate = askRate >= 8000 && askRate <= 1100000 ? askRate : d["defaultRate"].toDouble() > 0 ? d["defaultRate"].toDouble() : 48000.0;
    openBits_ = bits; openExclusive_ = backendName == "wasapi" && (askRate > 0 || bits != 0);
    const bool ok = open(backendName, QString(), id, rate, frames > 0 ? frames : 256, 0, std::min(32, std::max(2, d["outChannels"].toInt()))); openBits_ = 0; openExclusive_ = false; dacOpenId_ = ok ? backendName + "|" + id : QString(); emit devicesChanged(); return ok; }
  status_ = QStringLiteral("That DAC is not in the list any more: press RESCAN."); emit stateChanged(); return false;
}
// MANUAL: the drivers are only listed (discovery); nothing is picked or opened until the operator chooses one
QVariantList Engine::asioDrivers() const {
  QVariantList l; for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["backend"].toString() != "asio") continue;
    QVariantMap m; m["id"] = d["id"]; m["name"] = d["name"]; m["inChannels"] = d["inChannels"]; m["outChannels"] = d["outChannels"]; m["rate"] = d["defaultRate"]; m["kind"] = d["kind"];
    for (const char* k : {"maker", "model", "asioType", "note", "known", "own", "status", "statusText", "lastSeen"}) m[k] = d.value(k);
    if (d.value("lastSeen").toBool()) { m["inChannels"] = d["seenIn"]; m["outChannels"] = d["seenOut"]; if (d["seenRate"].toDouble() > 0) m["rate"] = d["seenRate"]; m["seenWhen"] = d["seenWhen"]; }
    m["current"] = running() && backend() == "asio" && asioOpenId_ == d["id"].toString(); l << m; }
  return l;
}
// THE CHANNELS ARE READ BY THEMSELVES: as soon as an ASIO driver runs, every input the device really has is put on the strip of the same
// number (desk channel n on strip n) and the strips beyond get no input. A desk the console knows by name also names its strips.
int Engine::readAsioChannels(const QString& id) {
  QString prefix = QStringLiteral("CH"), name; bool desk = false;
  for (const QVariant& v : mixersFound()) { const QVariantMap m = v.toMap(); if (m["id"].toString() == id) { desk = true; prefix = m["prefix"].toString(); name = m["name"].toString(); } }
  if (name.isEmpty()) for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["backend"].toString() == "asio" && d["id"].toString() == id) name = d["name"].toString(); }
  const int n = assignMixerChannels(prefix, desk);
  asioNote_ = n > 0 ? QStringLiteral("%1 input channel(s) read from %2: channel n is on strip n.").arg(n).arg(name.isEmpty() ? QStringLiteral("the device") : name) + (n > licensedChannels_ ? QStringLiteral(" This edition runs strips 1 to %1.").arg(licensedChannels_) : QString())
                   : QStringLiteral("%1 has no inputs: outputs only.").arg(name.isEmpty() ? QStringLiteral("The device") : name);
  emit stateChanged(); return n;
}
bool Engine::openAsioDriver(const QString& id, int frames) {
  for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["backend"].toString() != "asio" || d["id"].toString() != id) continue;
    if (d["inChannels"].toInt() + d["outChannels"].toInt() == 0 && !asioReread_) {   // its device was held by the running stream when the list was read: let go of it and read the real device
      stop(); asioReread_ = true; refreshDevices(); const bool ok = openAsioDriver(id, frames); asioReread_ = false; return ok; }
    const bool unknown = d["inChannels"].toInt() + d["outChannels"].toInt() == 0;   // still unknown: ask for everything, the driver gives what the device has
    const double rate = d["defaultRate"].toDouble() > 0 ? d["defaultRate"].toDouble() : 48000.0;
    if (unknown) { const bool ok = open(QStringLiteral("asio"), id, id, rate, frames > 0 ? frames : asioFrames(rate), inputCap(), 32); asioOpenId_ = ok ? id : QString(); if (ok) readAsioChannels(id); else asioNote_.clear(); emit devicesChanged(); return ok; }
    const bool ok = open(QStringLiteral("asio"), id, id, rate, frames > 0 ? frames : asioFrames(rate), std::min(inputCap(), std::max(2, d["inChannels"].toInt())), std::min(32, std::max(2, d["outChannels"].toInt()))); asioOpenId_ = ok ? id : QString(); if (ok) readAsioChannels(id); else asioNote_.clear(); emit devicesChanged(); return ok; }   // this driver or nothing: no other driver is tried in its place
  status_ = QStringLiteral("That ASIO driver is not in the list any more: press RESCAN."); emit stateChanged(); return false;
}
// ASIO runs on a 10 ms buffer: the number of frames that is 10 ms at this rate (480 at 48 kHz). The driver then takes the nearest size it
// offers (512 on most), so the real figure is shown once it runs.
int Engine::asioFrames(double rate) const { return std::max(16, static_cast<int>((rate > 0 ? rate : 48000.0) * 0.010 + 0.5)); }
// AUTO DETECT for ASIO: the interface's own driver before a wrapper (ASIO4ALL, FlexASIO), the one with more channels among equals.
// It only picks; pressing the button is still the operator's decision.
// AMFB's OWN VIRTUAL ASIO DRIVER ("Audio Mixer", drivers/asio): installed with the console so OTHER programs can reach it. It is not a sound
// card and carries no signal of its own; while idle it still reports 2 in / 2 out, so ranking by channels picked it over the real interface
// (whose driver, not running, reports none). It is never chosen automatically; it can still be opened by hand.
static bool ownVirtualAsio(const QString& name) { return name.trimmed().compare(QStringLiteral("Audio Mixer"), Qt::CaseInsensitive) == 0; }
QString Engine::bestAsioDriver() const {
  QString best; int bestRank = -1;
  for (const QVariant& v : asioDrivers()) { const QVariantMap m = v.toMap(); const QString k = m["kind"].toString(); if (ownVirtualAsio(m["name"].toString()) || ownVirtualAsio(m["id"].toString())) continue;
    const QString t = m["asioType"].toString(), st = m["status"].toString();
    if (m["own"].toBool() || t == "virtual" || t == "network" || st == "other bitness") continue;   // not a sound card here, or cannot be loaded by this build
    const int typeRank = (t == "interface" || t == "mixer" || t == "dj" || t == "guitar") ? 300 : t == "onboard" ? 150 : t == "wrapper" ? 0 : (k == "interface" ? 200 : k == "wrapper" ? 0 : 100);
    const int rank = typeRank + (st == "ready" ? 40 : m["lastSeen"].toBool() ? 30 : 0) + (m["known"].toBool() ? 5 : 0) + std::min(64, m["inChannels"].toInt() + m["outChannels"].toInt());
    if (rank > bestRank) { bestRank = rank; best = m["id"].toString(); } }
  return best;
}
bool Engine::autoOpenAsio() {
  const QString id = bestAsioDriver();
  if (id.isEmpty()) { status_ = QStringLiteral("AUTO DETECT: no ASIO driver was found. Install the interface's own driver, then SCAN."); emit stateChanged(); return false; }
  return openAsioDriver(id, 0);
}
QVariantMap Engine::autoDetect() const {
  const QString prefer = autoPrefer(); if (prefer == "manual") return {};
  QVariantMap d = autoDetectFor(prefer);
  if (d.isEmpty() && prefer != "best") { d = autoDetectFor(QStringLiteral("best")); if (!d.isEmpty()) d["why"] = QStringLiteral("no %1 device here, so the best of the others: ").arg(prefer.toUpper()) + d["why"].toString(); }
  return d;
}
QVariantMap Engine::autoDetectFor(const QString& prefer) const {
  QVariantMap best; int bestRank = -1;
  for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); const QString b = d["backend"].toString(), name = d["name"].toString(); if (b == "null" || d["id"].toString().startsWith("loopback:")) continue;
    const bool seen = b == "asio" && d.value("lastSeen").toBool();
    const bool in = d["inChannels"].toInt() > 0 || (seen && d["seenIn"].toInt() > 0), out = d["outChannels"].toInt() > 0 || (seen && d["seenOut"].toInt() > 0); if (!in && !out) continue;
    if (b == "asio") { const QString t = d.value("asioType").toString(); if (t == "virtual" || t == "network" || d.value("status").toString() == "other bitness") continue; }   // virtual and network drivers: never automatic
    if (!d.value("enabled", true).toBool()) continue;   // switched OFF on CONNECTIONS
    if (b == "asio" && ownVirtualAsio(name)) continue;   // our own virtual driver: never automatic
    if (prefer == "bluetooth") { if (d["kind"].toString() != "bluetooth" && !looksBluetooth(name)) continue; } else if (prefer != "best" && b != prefer) continue;
    int rank = 0; QString why;
    if (b == "asio") { const bool wrapper = am::drv::isWrapper(name.toStdString()); const bool pinned = am::drv::pinOf(name.toStdString()).on; rank = wrapper ? 60 : pinned ? 50 : 100 + (d.value("asioType").toString() == "onboard" ? -20 : 0) + (d.value("status").toString() == "ready" ? 5 : 0); why = wrapper ? "an ASIO wrapper (ASIO4ALL-style): used when nothing has its own driver" : pinned ? "Realtek onboard: pinned to its Windows endpoint, SMART START routes it" : "the device's own ASIO driver"; }
    else if (b == "wasapi" || b == "coreaudio") { rank = (d["defaultOut"].toBool() ? 40 : 30) + (in && out ? 2 : 0); why = "the system's " + QString(d["defaultOut"].toBool() ? "default " : "") + "endpoint"; }
    else if (b == "alsa") { rank = (name.contains("default") ? 25 : 20); why = "an ALSA device"; }
    else if (b == "dsound") { rank = (d["defaultOut"].toBool() ? 12 : 10) + (in && out ? 2 : 0); why = "DirectSound (no lock; the Bluetooth path)";
      if (handsFree(d)) { rank = 1; why = "DirectSound, a headset's hands-free side: used only when nothing else is there"; } }
    if (rank > bestRank) { bestRank = rank; best = d; best["why"] = why; }
  }
  if (best.isEmpty()) return best;
  if (best["backend"].toString() == "asio" && best.value("lastSeen").toBool()) { best["inChannels"] = best["seenIn"]; best["outChannels"] = best["seenOut"]; best["why"] = best["why"].toString() + " (busy or idle when read: its last known channels are used)"; }
  const QString id = best["id"].toString(); best["in"] = best["inChannels"].toInt() > 0 ? id : "-"; best["out"] = best["outChannels"].toInt() > 0 ? id : "-";
  best["outName"] = best["outChannels"].toInt() > 0 ? best["name"] : QVariant(QString()); best["inName"] = best["inChannels"].toInt() > 0 ? best["name"] : QVariant(QString()); best["outKind"] = best["outChannels"].toInt() > 0 ? best["kind"] : QVariant(QString()); best["inKind"] = best["inChannels"].toInt() > 0 ? best["kind"] : QVariant(QString());
  // endpoint systems list each direction on its own (WASAPI, DirectSound): pair the pick with the other direction's endpoint of the same backend,
  // the same device by name first (a headset's microphone with its earphones), then the system default, then any. Without this a start had 0 inputs.
  for (const bool wantIn : {true, false}) {
    if (best[wantIn ? "in" : "out"].toString() != "-") continue;
    QVariantMap pick; int pr = -1; QString stem = best["name"].toString(); const int par = stem.indexOf('('); if (par >= 0) stem = stem.mid(par + 1).remove(')').trimmed();   // "Speakers (USB Audio CODEC)" -> "USB Audio CODEC"
    for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); if (d["backend"] != best["backend"] || d["id"].toString().startsWith("loopback:")) continue; if ((wantIn ? d["inChannels"] : d["outChannels"]).toInt() <= 0) continue; if (!d.value("enabled", true).toBool()) continue;
      if (best["backend"] == "dsound" && handsFree(d)) continue;   // DirectSound: the headset's hands-free microphone stays off (opening it drops the headset to telephone quality)
      const int r = ((wantIn ? d["defaultIn"] : d["defaultOut"]).toBool() ? 4 : 0) + (!stem.isEmpty() && d["name"].toString().contains(stem, Qt::CaseInsensitive) ? 2 : 0) + 1;   /* the system's DEFAULT microphone first (what Windows uses), then the same device by name */ if (r > pr) { pr = r; pick = d; } }
    if (pick.isEmpty()) continue;
    best[wantIn ? "in" : "out"] = pick["id"]; best[wantIn ? "inChannels" : "outChannels"] = pick[wantIn ? "inChannels" : "outChannels"]; best[wantIn ? "inName" : "outName"] = pick["name"]; best[wantIn ? "inKind" : "outKind"] = pick["kind"];
  } best["rate"] = best["defaultRate"].toDouble() > 0 ? best["defaultRate"].toDouble() : 48000.0;
  return best;
}
QVariantMap Engine::ioCheck() const {
  QVariantMap m; m["running"] = rt_ != nullptr; QVariantList ins, outs; if (!rt_) { m["verdict"] = QStringLiteral("The sound card is not running."); m["inputs"] = ins; m["outputs"] = outs; m["realtime"] = false; return m; }
  const am::Snapshot sn = rt_->snapshot(); auto db = [](double v) { return v > 1e-6 ? 20.0 * std::log10(v) : -120.0; };
  int live = 0, same = 0;
  for (int ch = 0; ch < devIn_; ++ch) { int strip = -1; for (int i = 0; i < kStrips && strip < 0; ++i) if (inRoute_[i] == ch) strip = i;
    QVariantMap e; e["ch"] = ch + 1; e["strip"] = strip + 1; e["name"] = strip >= 0 ? strips_[static_cast<std::size_t>(strip)].name : QString();
    const double level = strip >= 0 ? db(std::max(sn.strips[strip].inPk, sn.strips[strip].rms)) : -120.0; e["db"] = level; e["live"] = level > -70.0; if (level > -70.0) ++live; if (strip == ch) ++same; ins << e; }
  for (int o = 0; o < devOut_; ++o) { const int src = outRoute_[o]; QVariantMap e; e["ch"] = o + 1; double level = -120.0; QString what = QStringLiteral("nothing");
    if (src == am::Runtime::kSrcMasterL || src == am::Runtime::kSrcMonL) { what = src == am::Runtime::kSrcMasterL ? QStringLiteral("MASTER L") : QStringLiteral("MONITOR L"); level = db(src == am::Runtime::kSrcMasterL ? sn.master[0].pk : sn.monitorPk[0]); }
    else if (src == am::Runtime::kSrcMasterR || src == am::Runtime::kSrcMonR) { what = src == am::Runtime::kSrcMasterR ? QStringLiteral("MASTER R") : QStringLiteral("MONITOR R"); level = db(src == am::Runtime::kSrcMasterR ? sn.master[1].pk : sn.monitorPk[1]); }
    else if (src >= am::Runtime::kSrcBus0L) { const int k = (src - am::Runtime::kSrcBus0L) / 2; if (k < kBuses) { what = buses_[k].name + ((src - am::Runtime::kSrcBus0L) % 2 ? QStringLiteral(" R") : QStringLiteral(" L")); level = db(sn.buses[k].pk); } }
    e["source"] = what; e["db"] = level; e["live"] = level > -70.0; outs << e; }
  const double blockMs = blockFrames_ > 0 && rt_->sampleRate() > 0 ? 1000.0 * blockFrames_ / rt_->sampleRate() : 0.0; const bool rtOk = sn.loadPct < 80.0 && sn.blocks > 0;
  m["backend"] = backend(); m["device"] = !outDevName_.isEmpty() ? outDevName_ : inDevName_; m["inputs"] = ins; m["outputs"] = outs; m["liveInputs"] = live; m["sameChannel"] = same;
  m["xruns"] = static_cast<qulonglong>(sn.xruns); m["loadPct"] = sn.loadPct; m["blockMs"] = blockMs; m["latencyMs"] = 2.0 * blockMs; m["realtime"] = rtOk;
  m["verdict"] = !rtOk ? QStringLiteral("The engine is not keeping up (load %1 %): use a larger block or close other programs.").arg(sn.loadPct, 0, 'f', 0)
               : devIn_ == 0 ? QStringLiteral("Real time is fine, but this device has no inputs: open it with its input side on CONNECTIONS.")
               : live == 0 ? QStringLiteral("Real time is fine. No signal is arriving on any input right now: play or speak into the device and look again.")
               : QStringLiteral("Real time is fine: %1 of %2 input(s) carry signal, %3 on the strip of the same number; about %4 ms through the console.").arg(live).arg(devIn_).arg(same).arg(2.0 * blockMs, 0, 'f', 1);
  return m;
}
QVariantList Engine::mixersFound() const {
  QVariantList l; const auto infos = devInfos(devices_);
  for (const am::DeviceInfo* d : am::drv::mixers(infos)) { const am::drv::MixerProfile p = am::drv::mixerProfile(d->name); QVariantMap m;
    m["id"] = QString::fromStdString(d->id); m["name"] = QString::fromStdString(d->name); m["maker"] = QString::fromStdString(p.maker); m["model"] = QString::fromStdString(p.model); m["prefix"] = QString::fromStdString(p.prefix); m["note"] = QString::fromStdString(p.note);
    m["inChannels"] = d->inChannels; m["outChannels"] = d->outChannels; m["running"] = rt_ != nullptr && backend() == QStringLiteral("asio") && (outDevName_ == QString::fromStdString(d->name) || inDevName_ == QString::fromStdString(d->name)); l << m; }
  return l;
}
int Engine::assignMixerChannels(const QString& prefix, bool nameStrips) {
  const int n = std::clamp(devIn_, 0, kStrips);
  for (int i = 0; i < kStrips; ++i) { setInputRoute(i, i < n ? i : -1); if (nameStrips && i < n) setName(i, QStringLiteral("%1 %2").arg(prefix.left(6)).arg(i + 1, 2, 10, QLatin1Char('0'))); }
  return n;
}
bool Engine::connectMixer(const QString& id, int frames) {
  for (const QVariant& v : mixersFound()) { const QVariantMap m = v.toMap(); if (m["id"].toString() != id) continue;
    const int in = std::clamp(m["inChannels"].toInt(), 0, inputCap()), out = std::clamp(m["outChannels"].toInt(), 2, 32);
    if (!openSmart("asio", in > 0 ? id : QStringLiteral("-"), id, 48000, frames > 0 ? frames : asioFrames(48000), in, out)) return false;
    const int n = assignMixerChannels(m["prefix"].toString(), true);
    lastRoute_ = QStringLiteral("MIXER: %1 %2 · %3 channel(s) on strips 1-%3, master on its returns 1-2  ·  ").arg(m["maker"].toString(), m["model"].toString()).arg(n) + lastRoute_; emit stateChanged(); return true; }
  status_ = QStringLiteral("That mixer is not in the device list any more: RESCAN."); emit stateChanged(); return false;
}
bool Engine::keepInputAtStart(const QString& backend, const QString& in) const {
  if (micAtStart() || in == "-") return true;
  if (backend == "asio" || backend == "null" || in.startsWith("loopback:")) return true;
  return backend != "wasapi" && backend != "dsound" && backend != "auto";   // Core Audio / ALSA on other systems: as before
}
bool Engine::autoStart(int frames) {
  if (autoPrefer() == "manual") { status_ = QStringLiteral("MANUAL selection is on: nothing is opened by itself. Pick the driver in the ASIO DRIVERS list."); emit stateChanged(); return false; }
  const QVariantMap d = autoDetect(); if (d.isEmpty()) { status_ = QStringLiteral("AUTO DETECT: no real sound card found (only the null test device)."); emit stateChanged(); return false; }
  const bool keepIn = keepInputAtStart(d["backend"].toString(), d["in"].toString());   // MIC AT ENGINE ON off: the speakers start without the microphone
  const QString autoIn = keepIn ? d["in"].toString() : autoInput(d["backend"].toString());   // the microphone blocked: COMPUTER SOUND instead where Windows has it
  const bool ok = openSmart(d["backend"].toString(), autoIn, d["out"].toString(), d["rate"].toDouble(), frames > 0 ? frames : 256, keepIn ? std::min(inputCap(), std::max(0, d["inChannels"].toInt())) : (autoIn == "-" ? 0 : 2), std::min(32, std::max(2, d["outChannels"].toInt())));
  if (ok && !keepIn) noteAutoInput(autoIn);
  if (ok) lastRoute_ = QStringLiteral("AUTO DETECT picked %1 (%2): %3").arg(d["name"].toString(), d["backend"].toString().toUpper(), d["why"].toString()) + "  ·  " + lastRoute_;
  emit stateChanged(); return ok;
}
void Engine::recordHistory() {
  const QJsonObject now = consoleJson(); if (histPos_ >= 0 && histPos_ < static_cast<int>(history_.size()) && history_[static_cast<std::size_t>(histPos_)] == now) return;
  history_.resize(static_cast<std::size_t>(histPos_ + 1)); history_.push_back(now); while (history_.size() > static_cast<std::size_t>(shownStrips() > 64 ? 20 : 100)) history_.erase(history_.begin()); histPos_ = static_cast<int>(history_.size()) - 1; emit historyChanged();   // (a 300-strip snapshot is ten times the size: fewer undo steps)
}
void Engine::undo() { histTimer_.stop(); if (histPos_ <= 0) return; restoring_ = true; applyConsoleJson(history_[static_cast<std::size_t>(--histPos_)], false); restoring_ = false; emit historyChanged(); }
void Engine::redo() { histTimer_.stop(); if (histPos_ + 1 >= static_cast<int>(history_.size())) return; restoring_ = true; applyConsoleJson(history_[static_cast<std::size_t>(++histPos_)], false); restoring_ = false; emit historyChanged(); }
void Engine::toggleAB() { const QJsonObject cur = consoleJson(); if (abOther_.isEmpty()) abOther_ = cur; const QJsonObject other = abOther_; abOther_ = cur; abSlot_ = abSlot_ == "A" ? "B" : "A"; applyConsoleJson(other, false); emit historyChanged(); }
void Engine::setLink(int i, bool on) { if (i < 0 || i >= kStrips || partner(i) < 0) return; strips_[static_cast<std::size_t>(i)].link = on; strips_[static_cast<std::size_t>(partner(i))].link = on; if (on) { const StripUi& a = strips_[static_cast<std::size_t>(i & ~1)]; StripUi& b = strips_[static_cast<std::size_t>(i | 1)]; b.db = a.db; b.mute = a.mute; b.solo = a.solo; b.pan = -a.pan; pushStrip(i | 1); if (rt_) { rt_->setPan(i | 1, b.pan); rt_->setSolo(i | 1, b.solo); } } emit stripsChanged(); }
bool Engine::linked(int i) const { return i >= 0 && i < kStrips && strips_[static_cast<std::size_t>(i)].link; }
void Engine::setTrim(int i, double db) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].trimDb = std::clamp(db, -24.0, 24.0); if (rt_) rt_->setTrimDb(i, strips_[static_cast<std::size_t>(i)].trimDb); emit stripsChanged(); }
double Engine::trim(int i) const { return i >= 0 && i < kStrips ? strips_[static_cast<std::size_t>(i)].trimDb : 0; }
static bool isVstKind(const QString& k) { return k.startsWith("vst3:") || k.startsWith("vst2:") || k.startsWith("fx:"); }   // everything that runs as a rack insert: hosted plug-ins and the ten built-in effects (fx:<kind>)
void Engine::muteAll(bool on) { muteKeep_.clear(); for (int i = 0; i < kStrips; ++i) { strips_[static_cast<std::size_t>(i)].mute = on; pushStrip(i); } emit stripsChanged(); }
bool Engine::allMuted() const { for (int i = 0; i < kStrips; ++i) if (!strips_[static_cast<std::size_t>(i)].mute) return false; return true; }
// MUTE ALL remembers which channels were muted before, so pressing it again gives the console back as it was
void Engine::toggleMuteAll() {
  if (!allMuted()) { std::vector<bool> keep(kStrips); for (int i = 0; i < kStrips; ++i) keep[static_cast<std::size_t>(i)] = strips_[static_cast<std::size_t>(i)].mute; muteAll(true); muteKeep_ = keep; return; }
  const std::vector<bool> keep = muteKeep_; muteKeep_.clear();
  for (int i = 0; i < kStrips; ++i) { strips_[static_cast<std::size_t>(i)].mute = keep.size() == static_cast<std::size_t>(kStrips) && keep[static_cast<std::size_t>(i)]; pushStrip(i); }
  emit stripsChanged();
}
void Engine::clearSolo() { for (int i = 0; i < kStrips; ++i) { strips_[static_cast<std::size_t>(i)].solo = false; if (rt_) rt_->setSolo(i, false); } emit stripsChanged(); }
void Engine::resetFaders() { for (int i = 0; i < kStrips; ++i) { StripUi& s = strips_[static_cast<std::size_t>(i)]; s.db = 0; s.pan = 0; pushStrip(i); if (rt_) rt_->setPan(i, 0); } masterDb_ = 0; pushMasterLevel(); emit stripsChanged(); }
void Engine::flattenAllDsp() { for (int i = 0; i < kStrips; ++i) { strips_[static_cast<std::size_t>(i)].dsp.set(am::dsp::ChannelParams{}); for (PluginUi& p : strips_[static_cast<std::size_t>(i)].plugins) if (!isVstKind(p.kind)) p = PluginUi{}; } if (rt_) pushAll(); emit stripsChanged(); }

static const struct { const char* kind; const char* name; } kBuiltIns[] = {{"gate", "NOISE GATE"}, {"eq", "CHANNEL EQ (3-BAND)"}, {"miceq", "MIC EQ (10-BAND)"}, {"xover", "X-OVER"}, {"comp", "COMPRESSOR"}, {"limiter", "LIMITER"}, {"delay", "DELAY"}, {"polarity", "POLARITY Ø"}};
QVariantList Engine::pluginCatalog() const { QVariantList l; const QStringList off = settings_.value("store/off").toStringList(); for (const auto& b : kBuiltIns) { QVariantMap m; m["kind"] = b.kind; m["name"] = b.name; m["builtIn"] = true; l << m; } for (const auto& [k, n] : am::fx::catalog()) { if (off.contains(QStringLiteral("fx:") + QString::fromStdString(k))) continue; QVariantMap m; m["kind"] = QStringLiteral("fx:") + QString::fromStdString(k); m["name"] = QString::fromStdString(n); m["builtIn"] = false; m["fx"] = true; m["vendor"] = QStringLiteral("AMFB Audio"); l << m; }
  for (const QVariantMap& v : vstCatalog_) l << v; return l; }
QVariantList Engine::vstCatalog() const { QVariantList l; for (const QVariantMap& v : vstCatalog_) l << v; return l; }
int Engine::rescanVst(const QStringList& extraDirs) {
  std::vector<std::string> dirs3 = am::vst3::defaultDirs(), dirs2 = am::vst2::defaultDirs(); for (const QString& d : extraDirs) if (!d.trimmed().isEmpty()) { dirs3.push_back(d.trimmed().toStdString()); dirs2.push_back(d.trimmed().toStdString()); }
  // every plug-in file is loaded once to read its name: on a worker with a time limit, so a slow or broken plug-in cannot hold the console up
  if (vstScanning_) return static_cast<int>(vstCatalog_.size());
  vstScanning_ = true; emit vstCatalogChanged(); auto found = std::make_shared<std::vector<QVariantMap>>(); auto done = std::make_shared<std::atomic<bool>>(false);
  guarded([found, done, dirs3, dirs2] {
  for (const std::string& mod : am::vst3::scan(dirs3)) { std::vector<am::vst3::ClassInfo> cls; std::string err; if (!am::vst3::inspect(mod, cls, err)) continue;
    for (const auto& c : cls) { QVariantMap m; m["kind"] = QStringLiteral("vst3:%1|%2").arg(QString::fromStdString(mod), QString::fromStdString(c.cid)); m["name"] = QString::fromStdString(c.name); m["vendor"] = QString::fromStdString(c.vendor); m["module"] = QString::fromStdString(mod); m["cid"] = QString::fromStdString(c.cid); m["subCategories"] = QString::fromStdString(c.subCategories); m["format"] = "VST3"; m["builtIn"] = false; found->push_back(m); } }
    for (const std::string& mod : am::vst2::scan(dirs2)) { am::host::Info info; std::string err; if (!am::vst2::inspect(mod, info, err)) continue;   // a .dll / .so that is not a VST2 plug-in is skipped quietly
    QVariantMap m; m["kind"] = QStringLiteral("vst2:%1").arg(QString::fromStdString(mod)); m["name"] = QString::fromStdString(info.name); m["vendor"] = QString::fromStdString(info.vendor); m["module"] = QString::fromStdString(mod); m["cid"] = QString::fromStdString(info.uid); m["subCategories"] = QString::fromStdString(info.category); m["format"] = "VST2"; m["builtIn"] = false; found->push_back(m); }
    done->store(true); return true; }, 180000);
  vstScanning_ = false;
  if (done->load()) vstCatalog_ = *found; else { status_ = QStringLiteral("The plug-in scan did not finish in 3 minutes (a plug-in is hanging): the list was left as it was. Move that plug-in out of the folder and RESCAN."); qWarning("VST scan timed out"); }
  if (done->load()) retryPlugins();   // a plug-in that was not there when the console was loaded may be there now
  emit vstCatalogChanged();
  emit stripsChanged(); return static_cast<int>(vstCatalog_.size());
}
static void splitVstKind(const QString& kind, QString& module, QString& cid) { const QString k = kind.mid(5); const int bar = k.lastIndexOf('|'); module = bar >= 0 ? k.left(bar) : k; cid = bar >= 0 ? k.mid(bar + 1) : QString(); }
bool Engine::loadVst(int i, int slot) {
  PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; p.vst.reset(); p.error.clear();
  if (!isVstKind(p.kind)) return false;
  if (p.kind.startsWith("fx:")) { auto fxp = am::fx::FxPlugin::make(p.kind.mid(3).toStdString(), rt_ ? rt_->sampleRate() : 48000); if (!fxp) { p.error = QStringLiteral("unknown effect"); return false; } if (!p.state.isEmpty()) fxp->setState(p.state.toStdString()); fxp->setBypass(p.bypass); p.vst = std::move(fxp); return true; }
  QString module, cid; splitVstKind(p.kind, module, cid); std::string err;
  const double rate = rt_ ? rt_->sampleRate() : 48000; const int block = 1024;   // larger runtime blocks are split by the insert adapter
  std::unique_ptr<am::host::Plugin> pl; if (p.kind.startsWith("vst3:")) pl = am::vst3::Plugin::load(module.toStdString(), cid.toStdString(), rate, block, err); else pl = am::vst2::Plugin::load(module.toStdString(), rate, block, err);
  if (!pl) { p.error = QString::fromStdString(err); return false; }
  if (!p.state.isEmpty()) pl->setState(p.state.toStdString());
  pl->setBypass(p.bypass); p.vst = std::move(pl); return true;
}
bool Engine::openPluginWindow(int i, int slot, bool evenHere) {
  if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return false;
#ifndef _WIN32
  if (!evenHere) return false;   // the editors of plug-ins need a native window of the kind their system gives; this build does that on Windows
#else
  (void)evenHere;
#endif
  const PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; if (!p.vst || !p.vst->hasEditor()) return false;
  for (const PluginWin& w : pluginWins_) if (w.pl == p.vst) { w.win->raise(); w.win->requestActivate(); return true; }   // already open: bring it to the front
  auto* win = new QWindow; win->setFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint | Qt::MSWindowsFixedSizeDialogHint);
  win->setTitle(QStringLiteral("%1  ·  CH %2").arg(QString::fromStdString(p.vst->info().name)).arg(i + 1)); win->resize(640, 400); win->create();
  int w = 640, h = 400;
  if (!win->handle()) { delete win; status_ = QStringLiteral("The plug-in's own window could not be created on this display."); emit stateChanged(); return false; }   // no native window (no display, a remote session that refuses one): asking for its id would fault
  if (!p.vst->openEditor(reinterpret_cast<void*>(win->winId()), w, h)) { delete win; return false; }
  // the editor reports device pixels: the window is sized in its own units (on a 150 % display a 900 px editor is a 600-unit window)
  { const qreal dpr = std::max<qreal>(1.0, win->devicePixelRatio()); w = static_cast<int>(std::lround(w / dpr)); h = static_cast<int>(std::lround(h / dpr)); }
  w = std::clamp(w, 120, 4000); h = std::clamp(h, 60, 3000); win->setMinimumSize(QSize(w, h)); win->setMaximumSize(QSize(w, h)); win->resize(w, h);
  win->installEventFilter(this); win->show(); pluginWins_.push_back({win, p.vst, i, slot});
  if (!pluginIdle_.isActive()) pluginIdle_.start(33);
  return true;
}
void Engine::closePluginWin(int k) {
  if (k < 0 || k >= pluginWins_.size()) return; PluginWin w = pluginWins_.takeAt(k);
  w.pl->closeEditor(); w.win->removeEventFilter(this); w.win->hide(); w.win->deleteLater();   // the editor leaves the window before the window goes
  if (pluginWins_.isEmpty()) pluginIdle_.stop();
}
void Engine::closePluginWindows() { undockPluginEditor(); while (!pluginWins_.isEmpty()) closePluginWin(0); }
QVariantMap Engine::dockPluginEditor(int i, int slot, QQuickItem* area, bool evenHere) {
  QVariantMap r{{"ok", false}, {"w", 0}, {"h", 0}, {"note", QString()}};
  if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots || !area || !area->window()) return r;
#ifndef _WIN32
  if (!evenHere) { r["note"] = QStringLiteral("The plug-in's own window opens inside the app on Windows."); return r; }
#else
  (void)evenHere;
#endif
  const PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; if (!p.vst || !p.vst->hasEditor()) { r["note"] = QStringLiteral("This plug-in has no window of its own: its knobs are on this panel."); return r; }
  undockPluginEditor();
  for (int k = pluginWins_.size() - 1; k >= 0; --k) if (pluginWins_[k].pl == p.vst) closePluginWin(k);   // an editor is shown once: the floating one closes
  auto* win = new QWindow(area->window()); win->setFlags(Qt::Widget | Qt::FramelessWindowHint); win->resize(640, 400); win->create();
  if (!win->handle()) { delete win; r["note"] = QStringLiteral("The plug-in's own window could not be created on this display."); return r; }
  int w = 640, h = 400;
  if (!p.vst->openEditor(reinterpret_cast<void*>(win->winId()), w, h)) { delete win; r["note"] = QStringLiteral("The plug-in did not open its window."); return r; }
  // the editor reports its size in device pixels: the dock is laid out in the window's units
  const qreal dpr = std::max<qreal>(1.0, win->devicePixelRatio()); w = std::clamp(static_cast<int>(std::lround(w / dpr)), 120, 4000); h = std::clamp(static_cast<int>(std::lround(h / dpr)), 60, 3000);
  win->resize(w, h); dock_ = {win, p.vst, i, slot, area}; placeDock(); win->show();
  if (!dockTimer_.isActive()) { dockTimer_.setInterval(50); connect(&dockTimer_, &QTimer::timeout, this, &Engine::placeDock, Qt::UniqueConnection); dockTimer_.start(); }
  if (!pluginIdle_.isActive()) pluginIdle_.start(33);
  r["ok"] = true; r["w"] = w; r["h"] = h; return r;
}
void Engine::placeDock() {
  if (!dock_.win) { dockTimer_.stop(); return; }
  // the plug-in was taken out of its slot (or replaced): its window goes with it
  if (dock_.strip < 0 || strips_[static_cast<std::size_t>(dock_.strip)].plugins[dock_.slot].vst != dock_.pl || !dock_.area) { undockPluginEditor(); return; }
  dock_.pl->editorIdle();
  QQuickItem* a = dock_.area; const bool show = a->isVisible() && a->window() && a->window()->isVisible() && a->opacity() > 0.01;
  const QPointF p = a->mapToScene(QPointF(0, 0)); const QRect g(QPoint(static_cast<int>(std::lround(p.x())), static_cast<int>(std::lround(p.y()))), dock_.win->size());
  if (dock_.win->geometry().topLeft() != g.topLeft()) dock_.win->setPosition(g.topLeft());
  if (show != dock_.win->isVisible()) dock_.win->setVisible(show);
}
void Engine::undockPluginEditor() {
  if (!dock_.win) return; Dock d = dock_; dock_ = Dock{}; dockTimer_.stop();
  if (d.pl) d.pl->closeEditor(); d.win->hide(); d.win->deleteLater();   // the editor leaves the window before the window goes
  if (pluginWins_.isEmpty()) pluginIdle_.stop(); emit pluginDockChanged();
}
bool Engine::eventFilter(QObject* o, QEvent* e) {
  if (e->type() == QEvent::Close) for (int k = 0; k < pluginWins_.size(); ++k) if (pluginWins_[k].win == o) { closePluginWin(k); return true; }
  return QObject::eventFilter(o, e);
}
int Engine::retryPlugins() {
  int back = 0;
  for (int i = 0; i < kStrips; ++i) { bool any = false;
    for (int k = 0; k < kPluginSlots; ++k) { PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[k]; if (isVstKind(p.kind) && !p.vst) { if (loadVst(i, k)) { ++back; any = true; } } }
    if (any) rebuildInserts(i); }
  if (back) emit stripsChanged();
  return back;
}
void Engine::rebuildInserts(int i) {
  if (!rt_) return; auto chain = std::make_shared<am::Runtime::InsertChain>();
  for (const PluginUi& p : strips_[static_cast<std::size_t>(i)].plugins) if (p.vst) chain->push_back(std::make_shared<am::host::MonoInsert>(p.vst, 1024));
  rt_->setInserts(i, chain->empty() ? nullptr : chain);
}
// After the sound card (re)opens. Plug-ins are reloaded only when the sample rate changed (they were made for the old one): a reopen or soft
// restart at the same rate keeps them. A reload runs on the interface thread (VST plug-ins must be created there), one plug-in at a time
// with the window answering Windows in between (user input held back), so a rack of heavy plug-ins no longer reads as "Not Responding".
void Engine::reloadAllVst() {
  const double rate = rt_ ? rt_->sampleRate() : 48000;
  bool allLoaded = true; for (int i = 0; i < kStrips && allLoaded; ++i) for (int k = 0; k < kPluginSlots; ++k) { const PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[k]; if (isVstKind(p.kind) && !p.vst) { allLoaded = false; break; } }
  if (allLoaded && vstRate_ == rate) { for (int i = 0; i < kStrips; ++i) for (int k = 0; k < kPluginSlots; ++k) if (strips_[static_cast<std::size_t>(i)].plugins[k].vst) { rebuildInserts(i); break; } return; }
  vstRate_ = rate;
  for (int i = 0; i < kStrips; ++i) { bool any = false;
    for (int k = 0; k < kPluginSlots; ++k) { PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[k]; if (!isVstKind(p.kind)) continue;
      if (p.vst) { p.state = QString::fromStdString(p.vst->state()); graveyard_.push_back(p.vst); }
      loadVst(i, k); any = true;
      if (!p.kind.startsWith("fx:")) QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20); }   // a hosted plug-in can take seconds: let the window answer
    if (any) rebuildInserts(i); }
  graveTimer_.start();
}
QString Engine::pluginParamText(int i, int slot, int id, double v) const { if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return {}; const PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; return p.vst ? QString::fromStdString(p.vst->paramText(static_cast<std::uint32_t>(id), std::clamp(v, 0.0, 1.0))) : QString(); }
QVariantList Engine::pluginParams(int i, int slot) const {
  QVariantList l; if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return l; const PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; if (!p.vst) return l;
  for (const auto& q : p.vst->params()) { if (q.hidden) continue; QVariantMap m; m["id"] = static_cast<int>(q.id); m["title"] = QString::fromStdString(q.title); m["units"] = QString::fromStdString(q.units); m["value"] = q.normalized; m["default"] = q.defaultNormalized; m["text"] = QString::fromStdString(p.vst->paramText(q.id, q.normalized)); m["stepCount"] = q.stepCount; m["bypass"] = q.bypass; m["readOnly"] = q.readOnly; l << m; }
  return l;
}
void Engine::setPluginParam(int i, int slot, int id, double v) { if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return; PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; if (!p.vst) return; p.vst->setParam(static_cast<std::uint32_t>(id), v); if (p.vst->hasBypass()) p.bypass = p.vst->bypass();
  if (p.kind == QStringLiteral("fx:iamfelement")) { StripUi& st = strips_[static_cast<std::size_t>(i)];   // the 360 dial follows the plug-in's knobs, and the strip keeps the place for when it is taken out
    st.spAz = p.vst->param(0) * 360 - 180; st.spEl = p.vst->param(1) * 180 - 90; st.spDist = 0.5 * std::pow(40.0, p.vst->param(2)); st.spSpread = p.vst->param(3); if (spkOn_) feedSpeakers(i); emit spatialMoved(); }
  if (linked(i) && !mirror_ && partner(i) >= 0 && strips_[static_cast<std::size_t>(partner(i))].plugins[slot].kind == p.kind) { mirror_ = true; setPluginParam(partner(i), slot, id, v); mirror_ = false; }
  emit stripsChanged(); }
QString Engine::pluginError(int i, int slot) const { if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return QString(); return strips_[static_cast<std::size_t>(i)].plugins[slot].error; }
QVariantList Engine::plugins(int i) const {
  QVariantList l; if (i < 0 || i >= kStrips) return l; const StripUi& s = strips_[static_cast<std::size_t>(i)];
  for (int k = 0; k < kPluginSlots; ++k) { const PluginUi& p = s.plugins[k]; QVariantMap m; m["slot"] = k; m["kind"] = p.kind; m["bypass"] = p.bypass; const bool vst = isVstKind(p.kind); m["builtIn"] = !p.kind.isEmpty() && !vst; m["pending"] = vst;
    QString name; for (const auto& b : kBuiltIns) if (p.kind == b.kind) name = b.name;
    if (vst) { if (p.vst) name = QString::fromStdString(p.vst->info().name); else { QString mod, cid; splitVstKind(p.kind, mod, cid); name = QFileInfo(mod).completeBaseName(); } }
    m["pending"] = vst && !p.vst; m["vst"] = vst; m["format"] = p.vst ? QString::fromStdString(p.vst->info().format) : (p.kind.startsWith("vst2:") ? "VST2" : vst ? "VST3" : ""); m["error"] = p.error; m["latency"] = p.vst ? static_cast<int>(p.vst->latencySamples()) : 0; m["name"] = name; l << m; }
  return l;
}
void Engine::applyPluginStage(int i, const QString& kind, bool on) {
  const am::dsp::ChannelParams& p = strips_[static_cast<std::size_t>(i)].dsp.params();
  if (kind == "gate") setGate(i, on, p.gateThresholdDb, p.gateRangeDb, p.gateAttackMs, p.gateHoldMs, p.gateReleaseMs);
  else if (kind == "comp") setCompressor(i, on, p.compThresholdDb, p.compRatio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, p.compMakeupDb);
  else if (kind == "limiter") setChannelLimiter(i, on, p.limiterCeilDb, p.limiterReleaseMs);
  else if (kind == "delay") setDelay(i, on ? (p.delayMs > 0 ? p.delayMs : 1.0) : 0);
  else if (kind == "polarity") setPolarity(i, on);
  else if (kind == "xover") setCrossover(i, static_cast<int>(p.xoverType), on ? (p.hpOrder > 0 ? p.hpOrder : 2) : 0, p.hpFreq, on ? p.lpOrder : 0, p.lpFreq);
  else if (kind == "eq" || kind == "miceq") {   // "eq": the CHANNEL EQ's three bands; "miceq": the ten MIC EQ bands. Off keeps every setting and only switches the bands off.
    const int from = kind == "eq" ? am::dsp::kChannelEqFirst : 0, to = kind == "eq" ? am::dsp::kEqBands : am::dsp::kChannelEqFirst;
    if (kind == "eq") { const QVariantMap c = channelEq(i); setChannelEq(i, c["low"].toDouble(), c["mid"].toDouble(), c["midFreq"].toDouble(), c["high"].toDouble(), on); }
    else if (on) setGraphicEq(i, graphicEq(i), true);
    else for (int b = from; b < to; ++b) { const auto e = p.eq[static_cast<std::size_t>(b)]; setEq(i, b, static_cast<int>(FilterType::Off), e.freq, e.q, e.gainDb); }
  }
}
bool Engine::matchStereoPair(int i) {
  const int j = partner(i); if (i < 0 || i >= kStrips || j < 0) return false;
  StripUi& a = strips_[static_cast<std::size_t>(i)]; StripUi& b = strips_[static_cast<std::size_t>(j)];
  b.dsp.set(a.dsp.params()); b.pan = -a.pan; b.trimDb = a.trimDb;
  for (int k = 0; k < kPluginSlots; ++k) { PluginUi& q = b.plugins[k]; const PluginUi& p = a.plugins[k];
    if (q.vst) graveyard_.push_back(q.vst);
    q = PluginUi{}; q.kind = p.kind; q.bypass = p.bypass; q.state = p.vst ? QString::fromStdString(p.vst->state()) : p.state;
    if (isVstKind(q.kind)) loadVst(j, k); }                             // its own instance: a plug-in keeps the memory of one channel only
  graveTimer_.start(); if (rt_) pushAll(); emit stripsChanged(); return true;
}
void Engine::setPlugin(int i, int slot, const QString& kind) {
  if (linked(i) && !mirror_ && partner(i) >= 0) { mirror_ = true; setPlugin(partner(i), slot, kind); mirror_ = false; }   // a linked pair keeps the same rack
  if (slot >= pluginSlotLimit() && !kind.isEmpty()) return;   // beyond the edition's slots: the rack shows them locked
  if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return; StripUi& s = strips_[static_cast<std::size_t>(i)]; PluginUi& p = s.plugins[slot];
  bool known = kind.isEmpty() || isVstKind(kind); for (const auto& b : kBuiltIns) if (kind == b.kind) known = true; if (!known) return;
  if (!kind.isEmpty() && !isVstKind(kind)) for (int k = 0; k < kPluginSlots; ++k) if (k != slot && s.plugins[k].kind == kind) s.plugins[k] = PluginUi{};   // one slot per built-in stage
  if (!p.kind.isEmpty() && !isVstKind(p.kind) && p.kind != kind) applyPluginStage(i, p.kind, false);
  if (p.vst) { graveyard_.push_back(p.vst); p.vst.reset(); graveTimer_.start(); }
  const bool wasIamf = p.kind == QStringLiteral("fx:iamfelement"), had360 = s.spOn;
  p.kind = kind.left(1024); p.bypass = false; p.state.clear(); p.error.clear();
  if (!kind.isEmpty() && !isVstKind(kind)) applyPluginStage(i, kind, true);
  if (isVstKind(kind)) loadVst(i, slot);
  rebuildInserts(i);
  if (kind == QStringLiteral("fx:iamfelement") && p.vst) { if (had360) setSpatial(i, true, s.spAz, s.spEl, s.spDist, s.spSpread); else { s.spOn = false; pushSpatial(i); } }   // the plug-in takes over the strip's place
  else if (wasIamf && iamfSlot(i) < 0 && kind != QStringLiteral("fx:iamfelement")) { s.spOn = true; pushSpatial(i); }   // taken out: the 360 stage holds the same place
  emit stripsChanged(); emit spatialMoved();
}
QVariantMap Engine::masterChainSeed() const {
  QVariantList eq; for (int b = 0; b < am::MasterChain::kBands; ++b) { const auto& e = mp_.eq[b]; eq << QVariantMap{{"type", static_cast<int>(e.type)}, {"freq", e.freq}, {"q", e.q}, {"gainDb", e.gainDb}}; }
  return {{"eqOn", mp_.eqOn}, {"eq", eq}, {"on", mp_.on}, {"analog", mp_.analog}, {"toneOn", mp_.toneOn}, {"compOn", mp_.compOn}, {"satOn", mp_.satOn}, {"deessOn", mp_.deessOn}, {"exciterOn", mp_.exciterOn},
          {"bassMonoOn", mp_.bassMonoOn}, {"lowDb", mp_.lowDb}, {"midDb", mp_.midDb}, {"airDb", mp_.airDb}, {"thrDb", mp_.thrDb}, {"ratio", mp_.ratio}, {"attackMs", mp_.attackMs}, {"releaseMs", mp_.releaseMs},
          {"makeupDb", mp_.makeupDb}, {"drive", mp_.drive}, {"gainDb", mp_.gainDb}, {"compMix", mp_.compMix}, {"deessThrDb", mp_.deessThrDb}, {"deessMaxDb", mp_.deessMaxDb}, {"exciterAmt", mp_.exciterAmt}, {"bassMonoHz", mp_.bassMonoHz}};
}
QVariantList Engine::programEqGains() const {
  QVariantList g; for (int b = 0; b < 10; ++b) g << (mp_.eq[b].type == FilterType::Peaking && std::fabs(mp_.eq[b].freq - kIsoCentres[b]) < 1 ? mp_.eq[b].gainDb : 0.0); return g;
}
QVariantList Engine::remoteRack(int i) const {
  QVariantList l; if (i < 0 || i >= kStrips) return l; const StripUi& s = strips_[static_cast<std::size_t>(i)];
  for (int k = 0; k < kPluginSlots; ++k) { const PluginUi& p = s.plugins[k]; if (!isVstKind(p.kind)) continue;
    QString name; if (p.vst) name = QString::fromStdString(p.vst->info().name); else { QString mod, cid; splitVstKind(p.kind, mod, cid); name = QFileInfo(mod).completeBaseName(); }
    l << QVariantMap{{"slot", k}, {"kind", p.kind}, {"name", name}, {"bypass", p.bypass}, {"state", p.vst ? QString::fromStdString(p.vst->state()) : p.state}}; }
  return l;
}
void Engine::takeRemotePlugin(int i, int slot, bool bypass, const QString& state) {
  if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return; PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; if (!isVstKind(p.kind)) return;
  p.bypass = bypass; if (!state.isEmpty()) p.state = state;
  if (p.vst) { if (!state.isEmpty()) p.vst->setState(state.toStdString()); p.vst->setBypass(bypass); }
  emit stripsChanged();
}
void Engine::setPluginBypass(int i, int slot, bool bypass) {
  if (linked(i) && !mirror_ && partner(i) >= 0) { mirror_ = true; setPluginBypass(partner(i), slot, bypass); mirror_ = false; }
  if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return; PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; if (p.kind.isEmpty()) return;
  p.bypass = bypass; if (!isVstKind(p.kind)) applyPluginStage(i, p.kind, !bypass); else if (p.vst) p.vst->setBypass(bypass); emit stripsChanged();
}
void Engine::movePlugin(int i, int from, int to) { if (i < 0 || i >= kStrips || from < 0 || from >= kPluginSlots || to < 0 || to >= kPluginSlots || from == to) return; std::swap(strips_[static_cast<std::size_t>(i)].plugins[from], strips_[static_cast<std::size_t>(i)].plugins[to]); rebuildInserts(i); emit stripsChanged(); }
int Engine::pluginCount(int i) const { if (i < 0 || i >= kStrips) return 0; int n = 0; for (const PluginUi& p : strips_[static_cast<std::size_t>(i)].plugins) if (!p.kind.isEmpty()) ++n; return n; }
void Engine::copyChannel(int i) { if (i < 0 || i >= kStrips) return; clipboard_ = stripJson(strips_[static_cast<std::size_t>(i)]); emit clipboardChanged(); }
bool Engine::pasteChannel(int i) {
  if (i < 0 || i >= kStrips || !clipboard_) return false; StripUi& s = strips_[static_cast<std::size_t>(i)]; const QJsonObject& o = *clipboard_;
  s.dsp.set(dspFromJson(o.value("dsp").toObject())); s.pan = std::clamp(o.value("pan").toDouble(0), -1.0, 1.0);
  const QJsonArray sends = o.value("sends").toArray(); for (int k = 0; k < kBuses && k < sends.size(); ++k) { const QJsonObject e = sends[k].toObject(); s.sends[k].db = std::clamp(e.value("db").toDouble(-60), -60.0, 10.0); s.sends[k].pre = e.value("pre").toBool(); }
  if (rt_) pushAll(); emit stripsChanged(); return true;
}

void Engine::setEq(int i, int band, int type, double f, double q, double g) {
  if (i == -1 && band >= 0 && band < am::MasterChain::kBands) { mp_.eq[band] = am::dsp::EqBand{static_cast<FilterType>(std::clamp(type, 0, 6)), std::clamp(f, 10.0, 24000.0), std::clamp(q, 0.1, 36.0), std::clamp(g, -24.0, 24.0)}; pushMaster(); emit masterChainChanged(); return; }   // strip -1 = the PROGRAM EQ on the master
  if (i < 0 || i >= kStrips || band < 0 || band >= am::dsp::kEqBands) return;
  const am::dsp::EqBand b{static_cast<FilterType>(std::clamp(type, 0, 8)), std::clamp(f, 10.0, 24000.0), std::clamp(q, 0.1, 36.0), std::clamp(g, -24.0, 24.0)};
  strips_[static_cast<std::size_t>(i)].dsp.setEqBand(band, b); strips_[static_cast<std::size_t>(i)].eqPreset = 0;
  if (rt_) rt_->setEqBand(i, band, b.type, b.freq, b.q, b.gainDb);
  emit stripsChanged();
}
void Engine::limitAllChannels(bool on) { for (int i = 0; i < kStrips; ++i) { const am::dsp::ChannelParams& p = strips_[static_cast<std::size_t>(i)].dsp.params(); setChannelLimiter(i, on, p.limiterCeilDb, p.limiterReleaseMs); } }
bool Engine::allChannelsLimited() const { for (int i = 0; i < kStrips; ++i) if (!strips_[static_cast<std::size_t>(i)].dsp.params().limiterOn) return false; return true; }
int Engine::infUnused() {
  std::vector<int> pull;   // decided first: a linked partner moves with its strip, and still counts as one pulled down
  for (int i = 0, nShown = shownStrips(); i < nShown; ++i) { const int r = inRoute_[i]; const bool net = (r >= am::Runtime::kNetInBase && r < am::Runtime::kNetInBase + am::Runtime::kNetInputs) || am::Runtime::isAppInput(r);   // a network or a program input: not a device channel
    if (!net && (r < 0 || r >= devIn_) && strips_[static_cast<std::size_t>(i)].db > -59.9) pull.push_back(i); }
  for (int i : pull) if (strips_[static_cast<std::size_t>(i)].db > -59.9) setGain(i, -60.0);   // -60 is the bottom of the fader: the engine treats it as silence
  return static_cast<int>(pull.size());
}
void Engine::setMasterMuted(bool on) {
  if (on == masterMuted_) return;
  if (on) { masterKeepDb_ = masterDb(); masterMuted_ = true; setMaster(-60.0); } else { masterMuted_ = false; setMaster(masterKeepDb_); }
  emit stripsChanged();
}
double Engine::closeNoise(int i) {
  if (i < 0 || i >= kStrips || !rt_) return 0;
  const am::Snapshot sn = rt_->snapshot(); const double pk = std::max(sn.strips[i].inPk, sn.strips[i].rms);
  if (pk < 1e-6) return 0;                                             // digital silence: nothing to close
  const double thr = std::clamp(20.0 * std::log10(pk) + 6.0, -80.0, -20.0);
  const am::dsp::ChannelParams& p = strips_[static_cast<std::size_t>(i)].dsp.params();
  setGate(i, true, thr, std::min(p.gateRangeDb, -40.0), 2.0, 150.0, 250.0); return thr;
}
QVariantMap Engine::channelEq(int i) const {
  QVariantMap m; if (i < 0 || i >= kStrips) return m; const auto& e = strips_[static_cast<std::size_t>(i)].dsp.params().eq; const int f = am::dsp::kChannelEqFirst;
  const auto& lo = e[static_cast<std::size_t>(f)]; const auto& mid = e[static_cast<std::size_t>(f + 1)]; const auto& hi = e[static_cast<std::size_t>(f + 2)];
  const bool set = mid.freq >= 100 && mid.freq <= 8000 && lo.type != FilterType::Peaking;   // untouched bands hold the engine's defaults, not a 3-band setting
  m["low"] = set ? lo.gainDb : 0.0; m["mid"] = set ? mid.gainDb : 0.0; m["midFreq"] = set ? mid.freq : 1000.0; m["high"] = set ? hi.gainDb : 0.0;
  m["on"] = strips_[static_cast<std::size_t>(i)].toneOn; return m;   // the EQ key's own state, not guessed from the bands (flat bands are switched off to save CPU)
}
void Engine::setChannelEq(int i, double lowDb, double midDb, double midFreq, double highDb, bool on) {
  if (i < 0 || i >= kStrips) return; const int f = am::dsp::kChannelEqFirst; const bool keep = restoring_; (void)keep; strips_[static_cast<std::size_t>(i)].toneOn = on;
  auto put = [&](int b, FilterType t, double freq, double q, double g) { g = std::clamp(g, -15.0, 15.0); setEq(i, b, static_cast<int>(on && std::fabs(g) > 0.01 ? t : FilterType::Off), freq, q, g); };
  put(f, FilterType::LowShelf, 100, 0.71, lowDb); put(f + 1, FilterType::Peaking, std::clamp(midFreq, 100.0, 8000.0), 0.9, midDb); put(f + 2, FilterType::HighShelf, 10000, 0.71, highDb);
}
QVariantMap Engine::eqBand(int i, int band) const {
  if (i == -1 && band >= 0 && band < am::MasterChain::kBands) { const auto& b = mp_.eq[band]; QVariantMap m; m["type"] = static_cast<int>(b.type); m["freq"] = b.freq; m["q"] = b.q; m["gainDb"] = b.gainDb; return m; }
  QVariantMap m; if (i < 0 || i >= kStrips || band < 0 || band >= am::dsp::kEqBands) return m;
  const auto& b = strips_[static_cast<std::size_t>(i)].dsp.params().eq[static_cast<std::size_t>(band)];
  m["type"] = static_cast<int>(b.type); m["freq"] = b.freq; m["q"] = b.q; m["gainDb"] = b.gainDb; return m;
}
void Engine::setDelay(int i, double ms) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dsp.setDelayMs(ms); if (rt_) rt_->setDelayMs(i, strips_[static_cast<std::size_t>(i)].dsp.params().delayMs); emit stripsChanged(); }
QVariantList Engine::storeItems() const {
  QVariantList l; const QStringList off = settings_.value("store/off").toStringList();
  for (const auto& [id, name, note] : {std::tuple<const char*, const char*, const char*>{"ext:AmF X1 Network", "AmF X1 Network", "phone remote and network control, run inside the console"},
                                       {"ext:AmF X1 Hybrid", "AmF X1 Hybrid", "sixteen-strip hybrid mixer with captured programs"}}) {
    QVariantMap m; m["id"] = id; m["name"] = name; m["kind"] = "extension"; m["note"] = note; m["installed"] = !Extension::path(QString::fromLatin1(name)).isEmpty();
    // installed is not enough: it must load (a wrong C++ runtime or a missing DLL beside it used to fail silently)
    static QHash<QString, QString> probed; if (!probed.contains(QString::fromLatin1(name))) { QString why; Extension::probe(QString::fromLatin1(name), why); probed[QString::fromLatin1(name)] = why; }
    m["loads"] = m["installed"].toBool() && probed[QString::fromLatin1(name)].isEmpty(); m["error"] = probed[QString::fromLatin1(name)];
    m["on"] = !off.contains(QString::fromLatin1(id)); l << m; }
  for (const auto& [k, n] : am::fx::catalog()) { const QString id = QStringLiteral("fx:") + QString::fromStdString(k);
    QVariantMap m; m["id"] = id; m["name"] = QString::fromStdString(n); m["kind"] = "effect"; m["note"] = "built-in rack effect"; m["installed"] = true; m["on"] = !off.contains(id); l << m; }
  return l;
}
void Engine::setStoreEnabled(const QString& id, bool on) {
  if (!id.startsWith("fx:") && !id.startsWith("ext:")) return;
  QStringList off = settings_.value("store/off").toStringList(); off.removeAll(id); if (!on) off << id; settings_.setValue("store/off", off); emit devicesChanged();
}
QVariantList Engine::ratesFor(const QString& backend, const QString& outDevice) const {
  for (const QVariant& v : devices_) { const QVariantMap m = v.toMap(); if (m["backend"].toString() == backend && (outDevice.isEmpty() || m["id"].toString() == outDevice) && !m["sampleRates"].toList().isEmpty()) return m["sampleRates"].toList(); }
  return {};
}
QString Engine::deviceFormat() const { return rt_ && running() ? QString::fromStdString(rt_->sampleFormat()) : QString(); }
QVariantMap Engine::compSuggestion(int i) const {
  QVariantMap m; if (i < 0 || i >= kStrips) return m; const am::CompSuggestion c = strips_[static_cast<std::size_t>(i)].dyn.suggest(compTargetDb_);
  m["ready"] = c.ready; m["spreadDb"] = c.spreadDb; m["medianDb"] = c.medianDb; m["targetDb"] = c.targetDb; m["thresholdDb"] = c.thresholdDb; m["ratio"] = c.ratio; m["verdict"] = QString::fromLatin1(c.verdict);
  return m;
}
bool Engine::applyCompSuggestion(int i) {
  if (i < 0 || i >= kStrips) return false; const am::CompSuggestion c = strips_[static_cast<std::size_t>(i)].dyn.suggest(compTargetDb_); if (!c.ready) return false;
  const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params();
  const double makeup = std::clamp(-c.thresholdDb * (1.0 - 1.0 / c.ratio) * 0.5, 0.0, 12.0);   // gives back about half of what the louder parts lose
  setCompressor(i, c.ratio > 1.0, c.thresholdDb, c.ratio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, c.ratio > 1.0 ? makeup : p.compMakeupDb);
  return true;
}
QString Engine::loadSampleFile(int i, int slot, const QString& pathOrUrl) {
  if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return QStringLiteral("no such slot");
  auto* fp = dynamic_cast<am::fx::FxPlugin*>(strips_[static_cast<std::size_t>(i)].plugins[slot].vst.get()); auto* sp = fp ? dynamic_cast<am::fx::SamplePlayer*>(fp->effect()) : nullptr;
  if (!sp) return QStringLiteral("that slot holds no SAMPLE PLAYER");
  if (pathOrUrl.isEmpty()) { sp->clearFile(); return {}; }
  const QString path = pathOrUrl.startsWith("file:") ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
  std::string err; if (!sp->loadFile(path.toStdString(), err)) return QString::fromStdString(err);
  return {};
}
QString Engine::sampleFileOf(int i, int slot) const {
  if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return {};
  auto* fp = dynamic_cast<am::fx::FxPlugin*>(strips_[static_cast<std::size_t>(i)].plugins[slot].vst.get()); auto* sp = fp ? dynamic_cast<am::fx::SamplePlayer*>(fp->effect()) : nullptr;
  return sp ? QString::fromStdString(sp->fileName()) : QString();
}
int Engine::playSampleOnStrip(int i, int id) {
  if (id < 1 || id > 100) return -1; const int k = addToRack(i, QStringLiteral("fx:sampler")); if (k < 0) return -1;
  setPluginParam(i, k, 0, (id - 1) / 99.0);   // SAMPLE
  setPluginParam(i, k, 1, 0.0); setPluginParam(i, k, 1, 1.0);   // PLAY off, then on: from the top
  return k;
}
// ---- LIVE MODE
void Engine::checkMicPrivacy() {
  const int v = privacyNow ? privacyNow() : -1; if (v < 0) return;
  const bool off = v == 1; if (off == privacyOff_) return;
  privacyOff_ = off; emit micPrivacyChanged(); emit recordChanged();   // the window restarts the sound card: without the microphone, or with it again
}
QString Engine::startLive(int strip) {
  if (live_) return QStringLiteral("LIVE MODE is already on (channel %1).").arg(liveStrip_ + 1);
  if (strip < 0 || strip >= shownStrips()) strip = 0;
  QStringList added;   // the slots this start filled: STOP empties these and nothing else
  for (const char* k : {"fx:clean", "fx:automiceq", "fx:rider", "fx:peakremover"}) { const QString kind = QString::fromLatin1(k); bool had = false;
    for (int sl = 0; sl < kPluginSlots; ++sl) if (strips_[static_cast<std::size_t>(strip)].plugins[sl].kind == kind) had = true;
    if (!had) { const int sl = addToRack(strip, kind); if (sl >= 0) added << QString::number(sl); } }
  settings_.setValue("live/strip", strip); settings_.setValue("live/slots", added.join(QLatin1Char(','))); settings_.setValue("live/headroomWas", headroom_); settings_.setValue("live/watchdogWas", watchdog_);
  if (headroom_ < 3) setHeadroomDb(3); setWatchdog(true);
  live_ = true; liveStrip_ = strip; settings_.setValue("live/on", true); settings_.sync(); emit liveChanged();
  return QStringLiteral("LIVE MODE is on: channel %1 has the voice chain (%2 added), the master is held under -%3 dBFS, the watchdog is on.").arg(strip + 1).arg(added.size()).arg(headroom_);
}
QString Engine::stopLive() {
  if (!live_) return QStringLiteral("LIVE MODE is off.");
  const int strip = liveStrip_; int n = 0;
  for (const QString& t : settings_.value("live/slots").toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) { const int sl = t.toInt(); if (sl >= 0 && sl < kPluginSlots && strip >= 0 && strip < kStrips) {
    const QString k = strips_[static_cast<std::size_t>(strip)].plugins[sl].kind; if (k == "fx:clean" || k == "fx:automiceq" || k == "fx:rider" || k == "fx:peakremover") { setPlugin(strip, sl, QString()); ++n; } } }   // only if the slot still holds what LIVE MODE put there
  setHeadroomDb(settings_.value("live/headroomWas", 0).toInt()); setWatchdog(settings_.value("live/watchdogWas", watchdog_).toBool());
  live_ = false; settings_.setValue("live/on", false); settings_.remove("live/slots"); settings_.sync(); emit liveChanged();
  return QStringLiteral("LIVE MODE is off: %1 effects taken out of channel %2, the master and the watchdog are as before.").arg(n).arg(strip + 1);
}
int Engine::addToRack(int i, const QString& kind) {
  if (i < 0 || i >= kStrips || kind.isEmpty()) return -1; const StripUi& s = strips_[static_cast<std::size_t>(i)];
  for (int k = 0; k < kPluginSlots; ++k) if (s.plugins[k].kind == kind) return k;
  for (int k = 0; k < kPluginSlots; ++k) if (s.plugins[k].kind.isEmpty()) { setPlugin(i, k, kind); return k; }
  return -1;
}
void Engine::setAutoPolarity(int i, bool on) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].autoPol = on; if (rt_) rt_->setAutoPolarity(i, on); emit stripsChanged(); }
void Engine::setPolarity(int i, bool inv) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dsp.setPolarity(inv); if (rt_) rt_->setPolarity(i, inv); emit stripsChanged(); }
void Engine::setCrossover(int i, int type, int hpO, double hpF, int lpO, double lpF) {
  if (i < 0 || i >= kStrips) return; const XoverType t = static_cast<XoverType>(std::clamp(type, 0, 2));
  strips_[static_cast<std::size_t>(i)].dsp.setCrossover(t, std::clamp(hpO, 0, 8), std::clamp(hpF, 10.0, 24000.0), std::clamp(lpO, 0, 8), std::clamp(lpF, 10.0, 24000.0));
  if (rt_) { const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params(); rt_->setCrossover(i, p.xoverType, p.hpOrder, p.hpFreq, p.lpOrder, p.lpFreq); }
  emit stripsChanged();
}
void Engine::setChannelLimiter(int i, bool on, double c, double r) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dsp.setLimiter(on, std::clamp(c, -30.0, 0.0), std::clamp(r, 1.0, 2000.0)); if (rt_) rt_->setChannelLimiter(i, on, c, r); emit stripsChanged(); }
void Engine::setCompressor(int i, bool on, double thr, double ratio, double knee, double att, double rel, double makeup) {
  if (i < 0 || i >= kStrips) return;
  strips_[static_cast<std::size_t>(i)].dsp.setCompressor(on, std::clamp(thr, -60.0, 0.0), std::clamp(ratio, 1.0, 20.0), std::clamp(knee, 0.0, 24.0), std::clamp(att, 0.1, 200.0), std::clamp(rel, 5.0, 2000.0), std::clamp(makeup, 0.0, 24.0));
  if (rt_) { const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params(); rt_->setCompressor(i, p.compOn, p.compThresholdDb, p.compRatio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, p.compMakeupDb); }
  emit stripsChanged();
}
void Engine::setGate(int i, bool on, double thr, double range, double att, double hold, double rel) {
  if (i < 0 || i >= kStrips) return;
  strips_[static_cast<std::size_t>(i)].dsp.setGate(on, std::clamp(thr, -90.0, 0.0), std::clamp(range, -90.0, 0.0), std::clamp(att, 0.1, 100.0), std::clamp(hold, 0.0, 2000.0), std::clamp(rel, 5.0, 3000.0));
  if (rt_) { const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params(); rt_->setGate(i, p.gateOn, p.gateThresholdDb, p.gateRangeDb, p.gateAttackMs, p.gateHoldMs, p.gateReleaseMs); }
  emit stripsChanged();
}
QVariantList Engine::compressorCurve(int i, int points) const {
  QVariantList l; if (i < 0 || i >= kStrips) return l; points = std::clamp(points, 8, 256); const auto& c = strips_[static_cast<std::size_t>(i)].dsp.compressor();
  for (int k = 0; k < points; ++k) { const double in = -60.0 + 60.0 * k / (points - 1); QVariantMap m; m["in"] = in; m["out"] = c.computeDb(in); l << m; }
  return l;
}
QVariantList Engine::gateCurve(int i, int points) const {
  QVariantList l; if (i < 0 || i >= kStrips) return l; points = std::clamp(points, 8, 256); const auto& g = strips_[static_cast<std::size_t>(i)].dsp.gate();
  for (int k = 0; k < points; ++k) { const double in = -90.0 + 90.0 * k / (points - 1); QVariantMap m; m["in"] = in; m["out"] = g.computeDb(in); l << m; }
  return l;
}
QVariantMap Engine::channelParams(int i) const {
  QVariantMap m; if (i < 0 || i >= kStrips) return m; const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params();
  m["compOn"] = p.compOn; m["compThresholdDb"] = p.compThresholdDb; m["compRatio"] = p.compRatio; m["compKneeDb"] = p.compKneeDb; m["compAttackMs"] = p.compAttackMs; m["compReleaseMs"] = p.compReleaseMs; m["compMakeupDb"] = p.compMakeupDb;
  m["gateOn"] = p.gateOn; m["gateThresholdDb"] = p.gateThresholdDb; m["gateRangeDb"] = p.gateRangeDb; m["gateAttackMs"] = p.gateAttackMs; m["gateHoldMs"] = p.gateHoldMs; m["gateReleaseMs"] = p.gateReleaseMs;
  m["polarityInvert"] = p.polarityInvert; m["delayMs"] = p.delayMs; m["xoverType"] = static_cast<int>(p.xoverType); m["hpOrder"] = p.hpOrder; m["hpFreq"] = p.hpFreq; m["lpOrder"] = p.lpOrder; m["lpFreq"] = p.lpFreq;
  m["limiterOn"] = p.limiterOn; m["limiterCeilDb"] = p.limiterCeilDb; m["limiterReleaseMs"] = p.limiterReleaseMs; return m;
}

// ---- EQ PRESETS
const Engine::EqPresetUi* Engine::factoryEqPreset(int slot) {
  using FT = am::dsp::FilterType; using B = am::dsp::EqBand;
  static const EqPresetUi k[10] = {
    // AmiX W Shape: lifted lows and highs, two gentle mid dips (a "W" on the curve): the house curve for playback and DJ mixes
    {"AmiX W Shape", {B{FT::LowShelf, 90, 0.8, 4}, B{FT::Peaking, 250, 1.2, -2.5}, B{FT::Peaking, 1000, 1.0, 1.5}, B{FT::Peaking, 3200, 1.2, -2.5}, B{FT::HighShelf, 9000, 0.8, 4}, B{}, B{}, B{}, B{}, B{}}, 2, 30, 0, 16000, true},
    // AmiX Golden EQ: the vocal / broadcast curve: clean lows, presence, a touch of air
    {"AmiX Golden EQ", {B{FT::HighPass, 85, 0.71, 0}, B{FT::Peaking, 180, 1.0, 1.5}, B{FT::Peaking, 450, 1.4, -2.0}, B{FT::Peaking, 2800, 1.1, 2.5}, B{FT::Peaking, 5500, 1.6, 1.5}, B{FT::HighShelf, 12000, 0.7, 2.0}, B{}, B{}, B{}, B{}}, 0, 80, 0, 16000, true},
    {"Vocal Presence", {B{FT::HighPass, 100, 0.71, 0}, B{FT::Peaking, 300, 1.2, -2}, B{FT::Peaking, 3000, 1.0, 3}, B{FT::HighShelf, 10000, 0.7, 1.5}, B{}, B{}, B{}, B{}, B{}, B{}}, 0, 80, 0, 16000, true},
    {"Warm Bass", {B{FT::LowShelf, 100, 0.7, 3}, B{FT::Peaking, 700, 1.0, -1.5}, B{FT::Peaking, 2500, 1.2, 1.5}, B{FT::LowPass, 8000, 0.71, 0}, B{}, B{}, B{}, B{}, B{}, B{}}, 0, 80, 0, 16000, true},
    {"Air Lift", {B{FT::HighShelf, 8000, 0.6, 3.5}, B{FT::Peaking, 12000, 1.0, 1.5}, B{}, B{}, B{}, B{}, B{}, B{}, B{}, B{}}, 0, 80, 0, 16000, true},
    {"De-Mud", {B{FT::Peaking, 200, 1.4, -3}, B{FT::Peaking, 400, 1.6, -2}, B{FT::Peaking, 4000, 1.0, 1}, B{}, B{}, B{}, B{}, B{}, B{}, B{}}, 2, 60, 0, 16000, true},
    {"Telephone", {B{FT::HighPass, 300, 0.71, 0}, B{FT::Peaking, 1800, 1.2, 4}, B{FT::LowPass, 3400, 0.71, 0}, B{}, B{}, B{}, B{}, B{}, B{}, B{}}, 4, 300, 4, 3400, true},
    {"Broadcast Voice", {B{FT::HighPass, 70, 0.71, 0}, B{FT::LowShelf, 120, 0.8, 2}, B{FT::Peaking, 250, 1.3, -1.5}, B{FT::Peaking, 2200, 1.0, 2}, B{FT::Peaking, 6500, 2.0, -1.5}, B{FT::HighShelf, 10000, 0.7, 1}, B{}, B{}, B{}, B{}}, 0, 80, 0, 16000, true},
    {"Kick Punch", {B{FT::Peaking, 60, 1.2, 3}, B{FT::Peaking, 300, 1.4, -3}, B{FT::Peaking, 4000, 1.0, 3}, B{FT::LowPass, 12000, 0.71, 0}, B{}, B{}, B{}, B{}, B{}, B{}}, 0, 80, 0, 16000, true},
    {"Acoustic Guitar", {B{FT::HighPass, 80, 0.71, 0}, B{FT::Peaking, 180, 1.2, -2}, B{FT::Peaking, 1200, 1.0, 1}, B{FT::Peaking, 5000, 1.2, 2}, B{FT::HighShelf, 11000, 0.7, 1.5}, B{}, B{}, B{}, B{}, B{}}, 0, 80, 0, 16000, true},
  };
  return slot >= 1 && slot <= 10 ? &k[slot - 1] : nullptr;
}
void Engine::loadUserEq() {
  const QJsonArray arr = QJsonDocument::fromJson(settings_.value("eq/userPresets").toByteArray()).array();
  for (int u = 0; u < 10 && u < arr.size(); ++u) { const QJsonObject o = arr[u].toObject(); EqPresetUi& e = userEq_[u]; e.used = !o.value("name").toString().isEmpty(); if (!e.used) continue; e.name = o.value("name").toString().left(24);
    const QJsonArray b = o.value("bands").toArray(); for (int i = 0; i < 10 && i < b.size(); ++i) { const QJsonObject x = b[i].toObject(); e.bands[i] = {static_cast<FilterType>(std::clamp(x.value("type").toInt(), 0, 8)), std::clamp(x.value("freq").toDouble(1000), 10.0, 24000.0), std::clamp(x.value("q").toDouble(1), 0.1, 36.0), std::clamp(x.value("gainDb").toDouble(0), -24.0, 24.0)}; }
    e.hpOrder = std::clamp(o.value("hpOrder").toInt(), 0, 8); e.hpFreq = std::clamp(o.value("hpFreq").toDouble(80), 10.0, 24000.0); e.lpOrder = std::clamp(o.value("lpOrder").toInt(), 0, 8); e.lpFreq = std::clamp(o.value("lpFreq").toDouble(16000), 10.0, 24000.0); }
}
void Engine::storeUserEq() const {
  QJsonArray arr; for (int u = 0; u < 10; ++u) { const EqPresetUi& e = userEq_[u]; QJsonObject o; if (e.used) { o["name"] = e.name; QJsonArray b; for (int i = 0; i < 10; ++i) { QJsonObject x; x["type"] = static_cast<int>(e.bands[i].type); x["freq"] = e.bands[i].freq; x["q"] = e.bands[i].q; x["gainDb"] = e.bands[i].gainDb; b << x; } o["bands"] = b; o["hpOrder"] = e.hpOrder; o["hpFreq"] = e.hpFreq; o["lpOrder"] = e.lpOrder; o["lpFreq"] = e.lpFreq; } arr << o; }
  settings_.setValue("eq/userPresets", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
QVariantList Engine::eqPresets() const {
  QVariantList l;
  for (int slot = 1; slot <= 20; ++slot) { const EqPresetUi* e = slot <= 10 ? factoryEqPreset(slot) : &userEq_[slot - 11]; QVariantMap m; m["slot"] = slot; m["locked"] = slot <= 10; m["empty"] = !e->used; m["name"] = e->used ? e->name : QStringLiteral("CUSTOM %1").arg(slot - 10);
    int n = 0; if (e->used) for (const auto& b : e->bands) if (b.type != FilterType::Off) ++n; m["bands"] = n; l << m; }
  return l;
}
bool Engine::applyEqPreset(int slot, int i) {
  if (i < 0 || i >= kStrips) return false; const EqPresetUi* e = slot >= 1 && slot <= 10 ? factoryEqPreset(slot) : slot >= 11 && slot <= 20 ? &userEq_[slot - 11] : nullptr; if (!e || !e->used) return false;
  for (int b = 0; b < 10; ++b) setEq(i, b, static_cast<int>(e->bands[b].type), e->bands[b].freq, e->bands[b].q, e->bands[b].gainDb);
  const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params(); setCrossover(i, static_cast<int>(p.xoverType), e->hpOrder, e->hpFreq, e->lpOrder, e->lpFreq);
  strips_[static_cast<std::size_t>(i)].eqPreset = slot; emit stripsChanged(); return true;
}
bool Engine::saveEqPreset(int slot, const QString& name, int i) {
  if (slot < 11 || slot > 20 || i < 0 || i >= kStrips) return false; EqPresetUi& e = userEq_[slot - 11]; const auto& p = strips_[static_cast<std::size_t>(i)].dsp.params();
  e.name = name.trimmed().isEmpty() ? QStringLiteral("CUSTOM %1").arg(slot - 10) : name.trimmed().left(24); for (int b = 0; b < 10; ++b) e.bands[b] = p.eq[static_cast<std::size_t>(b)]; e.hpOrder = p.hpOrder; e.hpFreq = p.hpFreq; e.lpOrder = p.lpOrder; e.lpFreq = p.lpFreq; e.used = true;
  storeUserEq(); strips_[static_cast<std::size_t>(i)].eqPreset = slot; emit stripsChanged(); return true;
}
bool Engine::clearEqPreset(int slot) { if (slot < 11 || slot > 20) return false; userEq_[slot - 11] = EqPresetUi(); storeUserEq(); for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].eqPreset == slot) strips_[static_cast<std::size_t>(i)].eqPreset = 0; emit stripsChanged(); return true; }
QVariantList Engine::eqPresetCurve(int slot, int points) const {
  QVariantList l; const EqPresetUi* e = slot >= 1 && slot <= 10 ? factoryEqPreset(slot) : slot >= 11 && slot <= 20 ? &userEq_[slot - 11] : nullptr; if (!e || !e->used) return l; points = std::clamp(points, 8, 512);
  am::dsp::Channel ch{48000}; for (int b = 0; b < 10; ++b) ch.setEqBand(b, e->bands[b]); ch.setCrossover(am::dsp::XoverType::Butterworth, e->hpOrder, e->hpFreq, e->lpOrder, e->lpFreq);
  for (int k = 0; k < points; ++k) { const double f = 20.0 * std::pow(1000.0, static_cast<double>(k) / (points - 1)); QVariantMap m; m["f"] = f; m["db"] = ch.gainDbAt(f); l << m; }
  return l;
}
QVariantList Engine::geqSlots() const {
  const QJsonArray arr = QJsonDocument::fromJson(settings_.value("eq/geqSlots").toByteArray()).array(); QVariantList l;
  for (int u = 0; u < 10; ++u) { const QJsonObject o = u < arr.size() ? arr[u].toObject() : QJsonObject(); QVariantMap m; m["slot"] = u + 1; const QString n = o.value("name").toString(); m["empty"] = n.isEmpty(); m["name"] = n.isEmpty() ? QStringLiteral("Custom %1").arg(u + 1) : n;
    QVariantList g; const QJsonArray ga = o.value("gains").toArray(); for (int b = 0; b < 10; ++b) g << (b < ga.size() ? std::clamp(ga[b].toDouble(), -12.0, 12.0) : 0.0); m["gains"] = g; l << m; }
  return l;
}
bool Engine::saveGeqSlot(int slot, const QString& name, const QVariantList& gains) {
  if (slot < 1 || slot > 10) return false; QJsonArray arr = QJsonDocument::fromJson(settings_.value("eq/geqSlots").toByteArray()).array(); while (arr.size() < 10) arr << QJsonObject();
  QJsonObject o; o["name"] = name.trimmed().isEmpty() ? QStringLiteral("Custom %1").arg(slot) : name.trimmed().left(24); QJsonArray g; for (int b = 0; b < 10; ++b) g << (b < gains.size() ? std::clamp(gains[b].toDouble(), -12.0, 12.0) : 0.0); o["gains"] = g; arr[slot - 1] = o;
  settings_.setValue("eq/geqSlots", QJsonDocument(arr).toJson(QJsonDocument::Compact)); emit presetsChanged(); return true;
}
bool Engine::clearGeqSlot(int slot) {
  if (slot < 1 || slot > 10) return false; QJsonArray arr = QJsonDocument::fromJson(settings_.value("eq/geqSlots").toByteArray()).array(); while (arr.size() < 10) arr << QJsonObject(); arr[slot - 1] = QJsonObject();
  settings_.setValue("eq/geqSlots", QJsonDocument(arr).toJson(QJsonDocument::Compact)); emit presetsChanged(); return true;
}
QVariantList Engine::responseCurve(int i, int points) const {
  QVariantList l; if (i < 0 || i >= kStrips) return l; points = std::clamp(points, 8, 512);
  for (int k = 0; k < points; ++k) { const double f = 20.0 * std::pow(1000.0, static_cast<double>(k) / (points - 1)); QVariantMap m; m["f"] = f; m["db"] = strips_[static_cast<std::size_t>(i)].dsp.gainDbAt(f); l << m; }
  return l;
}

QVariantList Engine::pages() const {
  // the pages of the console, grouped as the AmF X1 interface was (same words, same order); `ready` = carried over in this build
  struct P { const char* group; const char* id; const char* text; const char* icon; bool ready; };
  static const P k[] = {
    {"START", "home", "HOME", "⌂", true},
    {"MIX", "mixer", "MIXER", "☰", true}, {"MIX", "arrange", "ARRANGE", "≡", true}, {"MIX", "dsp", "TONE (EQ)", "∿", true}, {"MIX", "rtaeq", "RTA EQ", "◭", true}, {"MIX", "master", "MASTERING", "◈", true}, {"MIX", "dyn", "COMPRESSOR", "⩏", true}, {"MIX", "gate", "NOISE GATE", "▣", true},
    {"MIX", "fx", "EFFECTS", "✨", true}, {"MIX", "delay", "DELAY", "⧗", true}, {"MIX", "surround", "360 AUDIO", "◍", true}, {"MIX", "bus", "SEND TO", "⇉", true}, {"MIX", "dca", "GROUP FADERS", "☷", true},
    {"ROUTE", "io", "CONNECTIONS", "⚡", true}, {"ROUTE", "routing", "ROUTING", "⎇", true}, {"ROUTE", "frame", "I/O FRAME", "▦", true}, {"ROUTE", "amp", "AMP / SYSTEM", "⏚", true},
    {"METER", "meter", "LEVELS", "▓", true}, {"METER", "score", "SOUND SCORE", "★", true}, {"METER", "lufs", "LOUDNESS", "◎", true}, {"METER", "osc", "WAVEFORM", "∽", true}, {"METER", "phase", "PHASE", "☯", true}, {"METER", "acoustics", "ACOUSTICS", "⌇", true},
    {"SOURCE", "media", "STREAM & MEDIA", "▶", true}, {"SOURCE", "streaming", "STREAMING LINKS", "🔗", true}, {"SOURCE", "live", "LIVE SOURCES", "☉", true}, {"SOURCE", "ai", "AI MIX", "◇", true}, {"SOURCE", "assistant", "AI ASSISTANT", "✦", true}, {"SOURCE", "store", "STORE", "⬡", true}, {"SOURCE", "engctl", "ENGINE CONTROL", "⚡", true}, {"SOURCE", "tools", "TOOLS", "⚒", true}, {"SOURCE", "plugins", "PLUGINS", "⬡", true}, {"SOURCE", "samples", "SAMPLES", "▦", true},
    {"DEVICE", "asio", "SOUND CARD", "◉", true}, {"DEVICE", "bluetooth", "BLUETOOTH", "⮁", true}, {"DEVICE", "network", "NETWORK", "⮞", true}, {"DEVICE", "cloud", "CLOUD", "☁", true}, {"DEVICE", "cascade", "CASCADE", "⚭", true},
    {"SYSTEM", "update", "SOFTWARE UPDATE", "⟳", true}, {"SYSTEM", "presets", "PRESETS", "⬇", true}, {"SYSTEM", "sys", "SETTINGS", "⚙", true}, {"SYSTEM", "acct", "ACCOUNT", "☺", true}, {"SYSTEM", "access", "ACCESS", "⚿", true}, {"SYSTEM", "renew", "LICENSE", "★", true}, {"SYSTEM", "admin", "ADMIN", "⚑", true}, {"SYSTEM", "calib", "CALIBRATE", "⌖", true},
    {"HELP", "help", "HELP", "?", true}, {"HELP", "scenes", "SCENES", "⚐", true}, {"SYSTEM", "boot", "DIAGNOSTICS", "⏻", true},
  };
  QVariantList l; for (const P& p : k) { QVariantMap m; m["group"] = p.group; m["id"] = p.id; m["text"] = p.text; m["icon"] = QString::fromUtf8(p.icon); m["ready"] = p.ready; l << m; }
  return l;
}

void Engine::applyUiRate() { timer_.setTimerType(meterMs_ < 25 ? Qt::PreciseTimer : Qt::CoarseTimer); timer_.setInterval(uiLevel_ >= 2 ? (ecoUi_ ? 66 : fastMode() ? std::max(50, meterMs_) : meterMs_) : uiLevel_ == 1 ? 250 : 1000); }
void Engine::setUiActivity(int level) {
  level = std::clamp(level, 0, 2); if (level == uiLevel_) return; uiLevel_ = level; applyUiRate(); emit ecoUiChanged();
  if (sysEco_) am::sysopt::setThreadEco(level < 2);   // ECO IN THE BACKGROUND: the interface thread only; the sound has its own (HighQoS) thread
  if (level == 0 && sysTrim_) QTimer::singleShot(3000, this, [this] { if (uiLevel_ == 0) minimisedTrim(); });
  if (level == 2) tick();   // back in front: fresh meters at once
}
// MEMORY WHEN MINIMISED: the windows drop their cached textures, the heap gives its free blocks back, and when no sound runs in this program
// the working set is trimmed (the pages go to Windows' compressed store). With sound running only the first two: a trimmed working set would
// make the audio thread fault its pages back in, which can cost a block.
void Engine::minimisedTrim() {
  const std::uint64_t before = am::sysopt::memory().processMb;
  for (QWindow* w : QGuiApplication::allWindows()) if (auto* q = qobject_cast<QQuickWindow*>(w)) q->releaseResources();
  const bool soundHere = rt_ != nullptr || (remoteInProcess && remoteInProcess());
  const std::uint64_t after = am::sysopt::trimMemory(!soundHere);
  lastTrim_ = QString("%1 -> %2 MB%3").arg(before).arg(after).arg(soundHere ? " (sound running: heap only)" : "");
}
QVariantMap Engine::trimMemoryNow() { minimisedTrim(); return {{"note", lastTrim_}, {"processMb", static_cast<qulonglong>(am::sysopt::memory().processMb)}}; }
void Engine::setSysOpt(const QString& key, bool on) {
  if (key == "fastCores") { sysFast_ = on; settings_.setValue("sys/fastCores", on); am::sysopt::setFastCoresForAudio(on); }   // takes effect when the sound next starts
  else if (key == "eco") { sysEco_ = on; settings_.setValue("sys/ecoBackground", on); if (!on) am::sysopt::setThreadEco(false); }
  else if (key == "trim") { sysTrim_ = on; settings_.setValue("sys/trimMinimised", on); }
}
// the hardware does not change while the console runs: looked up once. The NPU search walks Windows' device list, so it is started on a
// background thread when the console starts (warmSystemInfo) and SETTINGS never waits for it.
static const am::sysopt::CpuInfo& cpuOnce() { static const am::sysopt::CpuInfo c = am::sysopt::cpu(); return c; }
static const am::sysopt::NpuInfo& npuOnce() { static const am::sysopt::NpuInfo n = am::sysopt::npu(); return n; }
static void warmSystemInfo() { std::thread([] { (void)cpuOnce(); (void)npuOnce(); }).detach(); }
QVariantMap Engine::firmwareInfo() const {
  static const am::sysopt::Firmware f = am::sysopt::firmware(); auto q = [](const std::string& v) { return QString::fromStdString(v); };
  return {{"maker", q(f.maker)}, {"model", q(f.model)}, {"board", q(f.board)}, {"biosVendor", q(f.biosVendor)}, {"biosVersion", q(f.biosVersion)}, {"biosDate", q(f.biosDate)}, {"os", q(f.os)}};
}
QVariantList Engine::soundDrivers() const {
  static const std::vector<am::sysopt::DriverInfo> d = am::sysopt::audioDrivers(); QVariantList l;
  for (const auto& x : d) l << QVariantMap{{"name", QString::fromStdString(x.name)}, {"provider", QString::fromStdString(x.provider)}, {"version", QString::fromStdString(x.version)}, {"date", QString::fromStdString(x.date)}};
  return l;
}
QVariantMap Engine::systemInfo() const {
  const am::sysopt::CpuInfo& c = cpuOnce();
  const am::sysopt::NpuInfo& n = npuOnce();
  const am::sysopt::MemInfo m = am::sysopt::memory();
  auto q = [](const std::string& s) { return QString::fromStdString(s); };
  QVariantMap r;
  r["cpuName"] = q(c.name.empty() ? std::string("Unknown processor") : c.name); r["cpuVendor"] = q(c.vendor); r["arch"] = q(c.arch); r["buildArch"] = q(c.buildArch); r["emulated"] = c.emulated;
  r["avx2"] = c.avx2; r["fma"] = c.fma; r["avx512"] = c.avx512; r["neon"] = c.neon; r["simd"] = q(am::simd::describe());   // the mixing kernels picked for this processor at start
  r["logical"] = c.topo.logical; r["classes"] = c.topo.classes; r["hybrid"] = c.topo.classes > 1; r["perfCores"] = c.topo.perfCores; r["effCores"] = c.topo.effCores; r["lowPowerCores"] = c.topo.lowPowerCores;
  r["npuFound"] = n.found; r["npuName"] = q(n.name); r["npuVendor"] = q(n.vendor);
  r["ramTotalMb"] = static_cast<qulonglong>(m.totalMb); r["ramFreeMb"] = static_cast<qulonglong>(m.availMb); r["ramLoadPct"] = m.loadPct;
  r["processMb"] = static_cast<qulonglong>(m.processMb); r["privateMb"] = static_cast<qulonglong>(m.privateMb);
  { const am::sysopt::Sensors z = am::sysopt::sensors();   // PROCESSOR PANEL: what the computer exposes now (nothing invented where it does not)
    r["loadOk"] = z.loadOk; r["cpuLoadPct"] = z.loadPct; r["clockOk"] = z.clockOk; r["mhz"] = z.mhz; r["perfPct"] = z.perfPct;
    QVariantList tl; for (double t : z.tempsC) tl << t; r["temps"] = tl; r["maxTempC"] = z.maxTempC;
    r["powerOk"] = z.powerOk; r["watts"] = z.watts; r["powerFrom"] = q(z.powerFrom); r["ac"] = z.ac; r["batteryPct"] = z.batteryPct; r["saver"] = z.saver; }
  r["audio"] = q(am::sysopt::audioThreadNote()); { const am::sysopt::AudioTune at = am::sysopt::audioThreadState(); r["audioState"] = at.state; r["audioHighQoS"] = at.highQoS; } r["fastCores"] = sysFast_; r["eco"] = sysEco_; r["trim"] = sysTrim_; r["lastTrim"] = lastTrim_;
  QStringList tips;   // English keys: the page translates them
  if (c.emulated) tips << "This x64 build of the mixing console runs on an ARM64 processor through Windows emulation (Prism). It works; emulation costs some extra CPU. A native ARM64 build would remove that cost.";
  if (c.topo.classes > 1) tips << (sysFast_ ? "Hybrid processor: the sound asks for the performance cores and is never slowed down to save power." : "Hybrid processor: the fast cores option for audio is off, so Windows may run the sound on a slower, energy-saving core. Switch it on if you hear dropouts.");
  if (n.found) tips << "AI accelerator (NPU) found. Nothing in the mixing console runs on it yet: the mixing is plain signal processing on the processor, which is what keeps the latency low.";
  if (m.loadPct >= 85) tips << "Memory is nearly full: close programs and plug-ins you do not use, or the sound may drop out.";
  r["tips"] = tips;
  return r;
}
void Engine::setOneVoice(bool on) { if (on == oneVoice_) return; oneVoice_ = on; settings_.setValue("audio/oneVoice", on); ov_.reset(); emit oneVoiceChanged(); }
void Engine::undoOneVoice() { if (!oneVoiceActed_) return; setInputRoute(0, 0); oneVoiceNote_.clear(); ov_.reset(); emit oneVoiceChanged(); }   // oneVoiceActed_ stays: not again on this device
void Engine::setEcoUi(bool on) { if (on == ecoUi_) return; ecoUi_ = on; settings_.setValue("ui/eco", on); applyUiRate(); emit ecoUiChanged(); }
void Engine::setWatchdog(bool on) { if (on == watchdog_) return; watchdog_ = on; settings_.setValue("audio/watchdog", on); wd_.reset(); emit watchdogChanged(); }
bool Engine::failover(const QString& why) {
  if (opening_) return false;
  const QString backend = QString::fromStdString(curBackend_), in = QString::fromStdString(curIn_), out = QString::fromStdString(curOut_);
  const double rate = rt_ ? rt_->sampleRate() : 48000; const int frames = blockFrames_ > 0 ? blockFrames_ : 256, inCh = devIn_, outCh = devOut_ > 0 ? devOut_ : 2;
  stop(); bool ok = !backend.isEmpty() && open(backend, in, out, rate, frames, inCh, outCh); bool moved = false;
  if (!ok) { ok = autoStart(frames); moved = ok; }
  ++failovers_; wd_.reset();
  watchdogNote_ = !ok ? QStringLiteral("WATCHDOG: %1; the sound card could not be started again.").arg(why)
                : moved ? QStringLiteral("WATCHDOG: %1; the sound moved to %2.").arg(why, outDevName_.isEmpty() ? QString::fromStdString(curBackend_) : outDevName_)
                        : QStringLiteral("WATCHDOG: %1; the sound card was started again.").arg(why);
  emit watchdogChanged(); return ok;
}
void Engine::tick() {
  QVariantList m; QVariantMap t;
  if (rt_) {
    const am::Snapshot s = rt_->snapshot();
    // WATCHDOG: no new block for three seconds while the engine is running: the device stalled; start again (three times a minute at most)
    if (watchdog_ && !opening_) { const am::Watchdog::Action act = wd_.feed(true, s.blocks, wdClock_.elapsed());
      if (act == am::Watchdog::Action::Restart) { QMetaObject::invokeMethod(this, [this] { failover(QStringLiteral("the sound stopped arriving")); }, Qt::QueuedConnection); }
      else if (act == am::Watchdog::Action::GiveUp) { watchdogNote_ = QStringLiteral("WATCHDOG: three restarts in a minute did not bring the sound back. Check the sound card, then press START."); emit watchdogChanged(); } }
    // ONE VOICE: the same mono microphone on inputs 1 and 2 (not on the null test device): CH 1 off, the voice heard once on CH 2
    // A MICROPHONE ONLY: computer sound or one program's sound that is the same on both sides (a mono podcast, a call) is music, not one
    // voice on two inputs; it keeps both channels.
    if (oneVoice_ && !oneVoiceActed_ && rt_->backendName() != "null" && isMicrophoneInput(QString::fromStdString(curIn_)) && inRoute_[0] == 0 && inRoute_[1] == 1 && ov_.update(s)) {
      oneVoiceActed_ = true; setInputRoute(0, -1);
      oneVoiceNote_ = QStringLiteral("Inputs 1 and 2 carry the same microphone (one voice on both sides): CH 1 is set to no input so the voice is heard once, on CH 2. UNDO puts it back.");
      emit oneVoiceChanged(); }
    const double vuA = 1.0 - std::exp(-timer_.interval() / 65.0);   // a VU reaches 99 % of a step in 300 ms: a one-pole with tau = 65 ms, run at the timer rate
    if (++dynTick_ >= 3) { dynTick_ = 0; for (int i = 0; i < kStrips; ++i) strips_[static_cast<std::size_t>(i)].dyn.push(s.strips[i].inPk > 1e-6 ? 20.0 * std::log10(s.strips[i].inPk) : -120.0); }   // ten a second: the input, before EQ and dynamics
    bool polFlipped = false;   // AUTO PHASE turned a strip round: keep the saved setting the same as what is heard
    for (int i = 0; i < kStrips; ++i) { StripUi& su = strips_[static_cast<std::size_t>(i)]; if (su.autoPol && s.strips[i].polInv != su.dsp.params().polarityInvert) { su.dsp.setPolarity(s.strips[i].polInv); polFlipped = true; } }
    if (polFlipped) emit stripsChanged();
    for (int i = 0, nShown = shownStrips(); i < nShown; ++i) { vu_[i] += (s.strips[i].rms - vu_[i]) * vuA; QVariantMap x; x["polCorr"] = s.strips[i].polCorr; x["polInv"] = s.strips[i].polInv; x["peakDb"] = toDb(s.strips[i].pk); x["rmsDb"] = toDb(s.strips[i].rms); x["holdDb"] = toDb(s.strips[i].hold); x["clip"] = s.strips[i].clipN > 0; x["compGrDb"] = s.strips[i].compGrDb; { int& oc = strips_[static_cast<std::size_t>(i)].overComp; oc = s.strips[i].compGrDb < -12.0 ? std::min(oc + 1, 90) : std::max(oc - 1, 0); x["overComp"] = oc > 15; }   /* OVER-COMPRESSED: more than 12 dB of reduction for half a second */ x["gateDb"] = s.strips[i].gateDb; x["inDb"] = toDb(s.strips[i].inPk); x["vuDb"] = toDb(vu_[i]); x["amGainDb"] = s.strips[i].amGainDb; x["rideDb"] = s.strips[i].rideDb; x["rideGateDb"] = s.strips[i].rideGateDb; x["rideLufs"] = s.strips[i].rideLufs; x["rideSusp"] = s.strips[i].rideSusp; m << x; }
    vuMaster_ += (std::max(s.master[0].rms, s.master[1].rms) - vuMaster_) * vuA; t["masterVuDb"] = toDb(vuMaster_);
    t["monitorLDb"] = toDb(s.monitorPk[0]); t["monitorRDb"] = toDb(s.monitorPk[1]);
    t["masterPeakDb"] = toDb(std::max(s.master[0].pk, s.master[1].pk)); t["masterLDb"] = toDb(s.master[0].pk); t["masterRDb"] = toDb(s.master[1].pk); t["masterRmsDb"] = toDb(std::max(s.master[0].rms, s.master[1].rms)); t["masterLRmsDb"] = toDb(s.master[0].rms); t["masterRRmsDb"] = toDb(s.master[1].rms);
    t["masterClip"] = s.master[0].clipN + s.master[1].clipN > 0; t["momentary"] = s.momentary; t["shortTerm"] = s.shortTerm; t["integrated"] = s.integrated; t["lra"] = s.lra; t["truePeak"] = s.truePeak; t["correlation"] = s.correlation; t["autoLevelDb"] = s.autoLevelDb;
    t["blocks"] = static_cast<qulonglong>(s.blocks); t["frames"] = static_cast<qulonglong>(s.frames); t["xruns"] = static_cast<qulonglong>(s.xruns); t["loadPct"] = s.loadPct; t["blockUs"] = s.blockUs; t["maxBlockUs"] = s.maxBlockUs; t["jitterUs"] = s.jitterUs;
    QVariantList bm; for (int k = 0; k < kBuses; ++k) { QVariantMap x; x["peakDb"] = toDb(s.buses[k].pk); x["rmsDb"] = toDb(s.buses[k].rms); x["holdDb"] = toDb(s.buses[k].hold); x["clip"] = s.buses[k].clipN > 0; bm << x; } busMeters_ = bm;
  } else {
    QVariantList bm; for (int k = 0; k < kBuses; ++k) { QVariantMap x; x["peakDb"] = -120.0; x["rmsDb"] = -120.0; x["holdDb"] = -120.0; x["clip"] = false; bm << x; } busMeters_ = bm;
    for (int i = 0, nShown = shownStrips(); i < nShown; ++i) { vu_[i] = 0; QVariantMap x; x["peakDb"] = -120.0; x["rmsDb"] = -120.0; x["holdDb"] = -120.0; x["clip"] = false; x["compGrDb"] = 0.0; x["gateDb"] = 0.0; x["inDb"] = -120.0; x["vuDb"] = -120.0; m << x; }
    vuMaster_ = 0; t["masterVuDb"] = -120.0; t["monitorLDb"] = -120.0; t["monitorRDb"] = -120.0;
    t["masterPeakDb"] = -120.0; t["masterLDb"] = -120.0; t["masterRDb"] = -120.0; t["masterRmsDb"] = -120.0; t["masterClip"] = false; t["momentary"] = -120.0; t["shortTerm"] = -120.0; t["integrated"] = -120.0; t["lra"] = 0.0; t["truePeak"] = -120.0; t["correlation"] = 0.0; t["autoLevelDb"] = 0.0; t["blocks"] = 0; t["frames"] = 0; t["xruns"] = 0; t["loadPct"] = 0.0; t["blockUs"] = 0.0; t["maxBlockUs"] = 0.0; t["jitterUs"] = 0.0;
  }
  meters_ = m; telemetry_ = t; ++meterTick_; emit metersChanged();
}

// ── persistence: the console as one JSON document (strips, sends, groups, buses, master). The same document is a SCENE; a strip's DSP part
// is a PRESET. QSettings keeps the current console, the 16 scenes and the user presets.
QJsonObject Engine::stripJson(const StripUi& s) const {
  const auto& p = s.dsp.params(); QJsonObject o; o["name"] = s.name; o["db"] = s.db; o["pan"] = s.pan; if (s.spOn || s.spAz != 0 || s.spEl != 0 || s.spDist != 1 || s.spSpread != 0) { QJsonObject sp; sp["on"] = s.spOn; sp["az"] = s.spAz; sp["el"] = s.spEl; sp["dist"] = s.spDist; sp["spread"] = s.spSpread; o["spatial"] = sp; } o["mute"] = s.mute; o["solo"] = s.solo;
  o["dsp"] = dspJson(p); if (!s.toneOn) o["toneOn"] = false;   // the 3-band EQ switched out (its bands are kept)
  QJsonArray sends; for (const SendUi& sd : s.sends) { QJsonObject e; e["db"] = sd.db; e["pre"] = sd.pre; sends << e; } o["sends"] = sends; o["dca"] = s.dca; o["muteGroups"] = static_cast<int>(s.muteGroups); o["color"] = s.color;
  QJsonArray pl; for (const PluginUi& p : s.plugins) { QJsonObject e; e["kind"] = p.kind; e["bypass"] = p.bypass; if (p.vst) e["state"] = QString::fromStdString(p.vst->state()); else if (!p.state.isEmpty()) e["state"] = p.state; pl << e; } o["plugins"] = pl; o["link"] = s.link; o["trimDb"] = s.trimDb; o["automix"] = s.automix; o["autoPol"] = s.autoPol; o["amWeight"] = s.amWeight;
  return o;
}
QJsonObject Engine::dspJson(const am::dsp::ChannelParams& p) {
  QJsonObject o;
  o["polarity"] = p.polarityInvert; o["delayMs"] = p.delayMs; o["xoverType"] = static_cast<int>(p.xoverType); o["hpOrder"] = p.hpOrder; o["hpFreq"] = p.hpFreq; o["lpOrder"] = p.lpOrder; o["lpFreq"] = p.lpFreq;
  o["limOn"] = p.limiterOn; o["limCeil"] = p.limiterCeilDb; o["limRel"] = p.limiterReleaseMs;
  o["compOn"] = p.compOn; o["compThr"] = p.compThresholdDb; o["compRatio"] = p.compRatio; o["compKnee"] = p.compKneeDb; o["compAtt"] = p.compAttackMs; o["compRel"] = p.compReleaseMs; o["compMakeup"] = p.compMakeupDb;
  o["gateOn"] = p.gateOn; o["gateThr"] = p.gateThresholdDb; o["gateRange"] = p.gateRangeDb; o["gateAtt"] = p.gateAttackMs; o["gateHold"] = p.gateHoldMs; o["gateRel"] = p.gateReleaseMs;
  QJsonArray eq; for (const auto& b : p.eq) { QJsonObject e; e["t"] = static_cast<int>(b.type); e["f"] = b.freq; e["q"] = b.q; e["g"] = b.gainDb; eq << e; } o["eq"] = eq;
  return o;
}
am::dsp::ChannelParams Engine::dspFromJson(const QJsonObject& o) {
  am::dsp::ChannelParams p; p.polarityInvert = o.value("polarity").toBool(); p.delayMs = std::clamp(o.value("delayMs").toDouble(0), 0.0, 50.0); p.xoverType = static_cast<XoverType>(std::clamp(o.value("xoverType").toInt(1), 0, 2));
  p.hpOrder = std::clamp(o.value("hpOrder").toInt(0), 0, 8); p.hpFreq = std::clamp(o.value("hpFreq").toDouble(80), 10.0, 24000.0); p.lpOrder = std::clamp(o.value("lpOrder").toInt(0), 0, 8); p.lpFreq = std::clamp(o.value("lpFreq").toDouble(4000), 10.0, 24000.0);
  p.limiterOn = o.value("limOn").toBool(); p.limiterCeilDb = std::clamp(o.value("limCeil").toDouble(-1), -30.0, 0.0); p.limiterReleaseMs = std::clamp(o.value("limRel").toDouble(80), 1.0, 2000.0);
  p.compOn = o.value("compOn").toBool(); p.compThresholdDb = std::clamp(o.value("compThr").toDouble(-20), -60.0, 0.0); p.compRatio = std::clamp(o.value("compRatio").toDouble(4), 1.0, 20.0); p.compKneeDb = std::clamp(o.value("compKnee").toDouble(6), 0.0, 24.0);
  p.compAttackMs = std::clamp(o.value("compAtt").toDouble(10), 0.1, 200.0); p.compReleaseMs = std::clamp(o.value("compRel").toDouble(100), 5.0, 2000.0); p.compMakeupDb = std::clamp(o.value("compMakeup").toDouble(0), 0.0, 24.0);
  p.gateOn = o.value("gateOn").toBool(); p.gateThresholdDb = std::clamp(o.value("gateThr").toDouble(-50), -90.0, 0.0); p.gateRangeDb = std::clamp(o.value("gateRange").toDouble(-80), -90.0, 0.0);
  p.gateAttackMs = std::clamp(o.value("gateAtt").toDouble(1), 0.1, 100.0); p.gateHoldMs = std::clamp(o.value("gateHold").toDouble(50), 0.0, 2000.0); p.gateReleaseMs = std::clamp(o.value("gateRel").toDouble(100), 5.0, 3000.0);
  const QJsonArray eq = o.value("eq").toArray();
  for (int b = 0; b < am::dsp::kEqBands && b < eq.size(); ++b) { const QJsonObject e = eq[b].toObject(); p.eq[static_cast<std::size_t>(b)] = {static_cast<FilterType>(std::clamp(e.value("t").toInt(0), 0, 8)), std::clamp(e.value("f").toDouble(1000), 10.0, 24000.0), std::clamp(e.value("q").toDouble(1), 0.1, 36.0), std::clamp(e.value("g").toDouble(0), -24.0, 24.0)}; }
  return p;
}
int Engine::stripFromJson(StripUi& s, const QJsonObject& o) {
  if (o.contains("name")) s.name = o.value("name").toString().left(24); s.db = std::clamp(o.value("db").toDouble(0), -60.0, 10.0); s.pan = std::clamp(o.value("pan").toDouble(0), -1.0, 1.0); s.mute = o.value("mute").toBool(); { const QJsonObject sp = o.value("spatial").toObject(); s.spOn = sp.value("on").toBool(); s.spAz = sp.value("az").toDouble(0); s.spEl = std::clamp(sp.value("el").toDouble(0), -90.0, 90.0); s.spDist = std::clamp(sp.value("dist").toDouble(1), 0.3, 20.0); s.spSpread = std::clamp(sp.value("spread").toDouble(0), 0.0, 1.0); } s.solo = o.value("solo").toBool();
  s.dsp.set(dspFromJson(o.value("dsp").toObject()));
  const QJsonArray sends = o.value("sends").toArray(); for (int k = 0; k < kBuses; ++k) { if (k < sends.size()) { const QJsonObject e = sends[k].toObject(); s.sends[k].db = std::clamp(e.value("db").toDouble(-60), -60.0, 10.0); s.sends[k].pre = e.value("pre").toBool(); } else s.sends[k] = SendUi{}; }
  s.dca = std::clamp(o.value("dca").toInt(0), 0, kGroups); s.muteGroups = static_cast<unsigned>(std::clamp(o.value("muteGroups").toInt(0), 0, 255)); s.color = std::clamp(o.value("color").toInt(0), 0, 7);
  s.link = o.value("link").toBool(); s.trimDb = std::clamp(o.value("trimDb").toDouble(0), -24.0, 24.0); s.toneOn = o.value("toneOn").toBool(true); s.automix = o.value("automix").toBool(); s.autoPol = o.value("autoPol").toBool(); s.amWeight = std::clamp(o.value("amWeight").toDouble(1), 0.25, 4.0);
  const QJsonArray pl = o.value("plugins").toArray(); for (int k = 0; k < kPluginSlots; ++k) { if (k < pl.size()) { const QJsonObject e = pl[k].toObject(); const QString kind = e.value("kind").toString().left(1024); if (s.plugins[k].kind != kind) s.plugins[k].vst.reset(); s.plugins[k].kind = kind; s.plugins[k].bypass = e.value("bypass").toBool(); s.plugins[k].state = e.value("state").toString(); } else s.plugins[k] = PluginUi{}; }
  // TEN PER CHANNEL: a desk saved with plug-ins past slot 10 has them moved up into free slots, in order; what still does not fit is named in the note
  QStringList dropped;
  for (int k = kPluginsPerChannel; k < kPluginSlots; ++k) { if (s.plugins[k].kind.isEmpty()) continue;
    int free = -1; for (int j = 0; j < kPluginsPerChannel; ++j) if (s.plugins[j].kind.isEmpty()) { free = j; break; }
    if (free >= 0) { s.plugins[free].kind = s.plugins[k].kind; s.plugins[free].bypass = s.plugins[k].bypass; s.plugins[free].state = s.plugins[k].state; } else dropped << s.plugins[k].kind.section('|', 0, 0);
    s.plugins[k] = PluginUi{}; }
  return static_cast<int>(dropped.size());
}
QJsonObject Engine::consoleJson() const {
  const int nSave = std::min(kStrips, std::max(shownStrips(), keepStrips_)); QJsonArray arr; for (int i = 0; i < nSave; ++i) arr << stripJson(strips_[static_cast<std::size_t>(i)]);
  QJsonArray buses; for (const BusUi& b : buses_) { QJsonObject o; o["name"] = b.name; o["db"] = b.db; o["returnDb"] = b.returnDb; o["mute"] = b.mute; o["fx"] = b.fxKind; o["fxMix"] = b.fxMix; o["fxBypass"] = b.fxBypass; QJsonObject fv; for (const auto& [id, v] : b.fxValues) fv[id] = v; o["fxValues"] = fv; buses << o; }
  QJsonArray dcas; for (const DcaUi& d : dcas_) { QJsonObject o; o["name"] = d.name; o["db"] = d.db; o["mute"] = d.mute; o["color"] = d.color; dcas << o; }
  QJsonArray mgs; for (const MuteGroupUi& g : muteGroups_) { QJsonObject o; o["name"] = g.name; o["active"] = g.active; mgs << o; }
  QJsonObject root; root["strips"] = arr; root["buses"] = buses; root["dcas"] = dcas; root["muteGroups"] = mgs; root["masterDb"] = masterDb_; root["masterRel"] = masterRel_; { QJsonObject pg; QJsonArray eq; for (const auto& b : mp_.eq) { QJsonObject e; e["type"] = static_cast<int>(b.type); e["freq"] = b.freq; e["q"] = b.q; e["gain"] = b.gainDb; eq.append(e); } pg["eq"] = eq; pg["eqOn"] = mp_.eqOn; pg["mastering"] = QJsonObject::fromVariantMap(mastering()); root["program"] = pg; } root["masterLim"] = masterLim_; root["masterCeil"] = masterCeil_; root["autoLevelOn"] = autoOn_; root["autoLevelTarget"] = autoTarget_; root["autoLevelMax"] = autoMax_; root["width"] = width_; root["ducker"] = duckJson(); root["loudness"] = loudJson(); root["v"] = 2;
  QJsonObject mon; mon["source"] = mon_.source; mon["db"] = mon_.db; mon["dim"] = mon_.dim; mon["mono"] = mon_.mono; mon["mute"] = mon_.mute; mon["sync"] = mon_.sync; root["monitor"] = mon; { QJsonArray amps; for (int o = 0; o < 64; ++o) if (!ampUi_[o].isEmpty()) { QJsonObject a = QJsonObject::fromVariantMap(ampUi_[o]); a["out"] = o; amps << a; } root["amps"] = amps; }
  QJsonArray ir; for (int i = 0; i < nSave; ++i) ir << inRoute_[i]; QJsonArray orr; for (int o = 0; o < kOutputs; ++o) orr << outRoute_[o]; root["inputRoutes"] = ir; root["outputRoutes"] = orr;
  return root;
}
// respectSafe: strips flagged RECALL SAFE keep what they have (a scene recall); false = the whole document (start-up)
void Engine::applyConsoleJson(const QJsonObject& root, bool respectSafe) {
  if (root.isEmpty()) return;
  masterDb_ = std::clamp(root.value("masterDb").toDouble(0), -60.0, 10.0); masterRel_ = std::clamp(root.value("masterRel").toDouble(100), 10.0, 2000.0); { const QJsonObject pg = root.value("program").toObject(); const QJsonArray eq = pg.value("eq").toArray(); for (int b = 0; b < am::MasterChain::kBands; ++b) { const QJsonObject e = b < eq.size() ? eq[b].toObject() : QJsonObject(); mp_.eq[b] = am::dsp::EqBand{static_cast<FilterType>(std::clamp(e.value("type").toInt(0), 0, 6)), std::clamp(e.value("freq").toDouble(kIsoCentres[b]), 10.0, 24000.0), std::clamp(e.value("q").toDouble(1.41), 0.1, 36.0), std::clamp(e.value("gain").toDouble(0), -24.0, 24.0)}; } mp_.on = false; if (pg.contains("mastering")) { QVariantMap mm = pg.value("mastering").toObject().toVariantMap(); setMastering(mm); } mp_.eqOn = pg.value("eqOn").toBool(true); pushMaster(); emit masterChainChanged(); } masterLim_ = root.value("masterLim").toBool(true); masterCeil_ = std::clamp(root.value("masterCeil").toDouble(-0.5), -12.0, 0.0);
  width_ = std::clamp(root.value("width").toDouble(1.0), 0.0, 2.0); autoOn_ = root.value("autoLevelOn").toBool(); autoTarget_ = std::clamp(root.value("autoLevelTarget").toDouble(-16), -30.0, -6.0); autoMax_ = std::clamp(root.value("autoLevelMax").toDouble(12), 0.0, 24.0);
  if (root.contains("inputRoutes")) { const QJsonArray ir = root.value("inputRoutes").toArray(); for (int i = 0; i < kStrips && i < ir.size(); ++i) inRoute_[i] = cleanRoute(ir[i].toInt(i)); emit routingChanged(); }
  if (root.contains("outputRoutes")) { const QJsonArray orr = root.value("outputRoutes").toArray(); for (int o = 0; o < kOutputs && o < orr.size(); ++o) outRoute_[o] = orr[o].toInt(0) == am::Runtime::kSrcMasterMono ? am::Runtime::kSrcMasterMono : std::clamp(orr[o].toInt(0), 0, am::Runtime::kSrcBus0L + 2 * kBuses - 1); emit routingChanged(); }
  if (root.contains("amps")) { for (auto& a : ampUi_) a.clear(); for (const QJsonValue& v : root.value("amps").toArray()) { const QJsonObject a = v.toObject(); const int o = a.value("out").toInt(-1); if (o >= 0 && o < 64) { QVariantMap m = a.toVariantMap(); m.remove("out"); ampUi_[o] = m; if (rt_) rt_->setAmp(o, ampFromMap(m)); } } emit ampChanged(); }
  if (root.contains("monitor")) { const QJsonObject mo = root.value("monitor").toObject(); mon_.source = std::clamp(mo.value("source").toInt(0), 0, kBuses); mon_.db = std::clamp(mo.value("db").toDouble(0), -60.0, 10.0); mon_.sync = mo.value("sync").toBool(true); mon_.dim = mo.value("dim").toBool(); mon_.mono = mo.value("mono").toBool(); mon_.mute = false; emit monitorChanged(); }   // never recall a muted control room
  const QJsonArray arr = root.value("strips").toArray(); keepStrips_ = std::max(keepStrips_, std::min(kStrips, static_cast<int>(arr.size())));
  for (int i = 0; i < kStrips && i < arr.size(); ++i) { if (respectSafe && recallSafe_.test(static_cast<std::size_t>(i))) continue; { const int left = stripFromJson(strips_[static_cast<std::size_t>(i)], arr[i].toObject()); if (left > 0) startNote_ = QStringLiteral("%1 had more than 10 plug-ins: the first 10 are kept (%2 left out).").arg(strips_[static_cast<std::size_t>(i)].name).arg(left); } }
  const QJsonArray buses = root.value("buses").toArray(); for (int k = 0; k < kBuses && k < buses.size(); ++k) { const QJsonObject o = buses[k].toObject(); if (o.contains("name")) buses_[k].name = o.value("name").toString().left(16); buses_[k].db = std::clamp(o.value("db").toDouble(0), -60.0, 10.0); buses_[k].returnDb = std::clamp(o.value("returnDb").toDouble(-60), -60.0, 10.0); buses_[k].mute = o.value("mute").toBool();
    buses_[k].fxKind = o.value("fx").toString(); buses_[k].fxMix = std::clamp(o.value("fxMix").toDouble(1), 0.0, 1.0); buses_[k].fxBypass = o.value("fxBypass").toBool(); buses_[k].fxValues.clear(); const QJsonObject fv = o.value("fxValues").toObject(); for (auto it = fv.begin(); it != fv.end(); ++it) buses_[k].fxValues[it.key()] = std::clamp(it.value().toDouble(), 0.0, 1.0); makeBusFx(k); }
  const QJsonArray dcas = root.value("dcas").toArray(); for (int g = 0; g < kGroups && g < dcas.size(); ++g) { const QJsonObject o = dcas[g].toObject(); if (o.contains("name")) dcas_[g].name = o.value("name").toString().left(16); dcas_[g].db = std::clamp(o.value("db").toDouble(0), -60.0, 10.0); dcas_[g].mute = o.value("mute").toBool(); dcas_[g].color = std::clamp(o.value("color").toInt(0), 0, 7); }
  duckFromJson(root.value("ducker").toObject()); loudFromJson(root.value("loudness").toObject());
  const QJsonArray mgs = root.value("muteGroups").toArray(); for (int g = 0; g < kGroups && g < mgs.size(); ++g) { const QJsonObject o = mgs[g].toObject(); if (o.contains("name")) muteGroups_[g].name = o.value("name").toString().left(16); muteGroups_[g].active = o.value("active").toBool(); }
  for (int i = 0; i < kStrips; ++i) for (int k = 0; k < kPluginSlots; ++k) { PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[k]; if (isVstKind(p.kind) && !p.vst) loadVst(i, k); else if (p.vst) { if (!p.state.isEmpty()) p.vst->setState(p.state.toStdString()); p.vst->setBypass(p.bypass); } }
  if (rt_) { for (auto& s : strips_) s.dsp.setSampleRate(s.dsp.sampleRate()); pushAll(); }
  emit stripsChanged(); emit busesChanged(); emit groupsChanged();
}
void Engine::save() {
  // only what changed is written: the scenes and the user presets are large and rarely change, and on Windows every value written goes
  // to the registry. Writing all of it again for one moved fader made the window stop for a moment on every save.
  static QHash<QString, QByteArray> written;
  auto put = [this](const char* key, const QString& v) { const QString k = QString::fromLatin1(key); const QByteArray h = QCryptographicHash::hash(v.toUtf8(), QCryptographicHash::Sha1);
    if (written.value(k) == h && settings_.contains(k)) return; settings_.setValue(k, v); written[k] = h; };
  put("console/state", QString::fromUtf8(QJsonDocument(consoleJson()).toJson(QJsonDocument::Compact)));
  QJsonArray sc; for (const SceneUi& x : scenes_) { QJsonObject o; o["name"] = x.name; o["at"] = x.at; o["data"] = x.data; sc << o; }
  put("console/scenes", QString::fromUtf8(QJsonDocument(sc).toJson(QJsonDocument::Compact)));
  { qulonglong low = 0; QStringList high; for (int i = 0; i < kStrips; ++i) if (recallSafe_.test(static_cast<std::size_t>(i))) { if (i < 32) low |= 1ull << i; else high << QString::number(i); }
    if (settings_.value("console/recallSafe").toULongLong() != low || !settings_.contains("console/recallSafe")) settings_.setValue("console/recallSafe", low);
    put("console/recallSafeHigh", high.join(QLatin1Char(','))); }   // strips 1-32 as the number older versions read, the rest as a list
  QJsonArray pr; for (const PresetUi& x : userPresets_) { QJsonObject o; o["name"] = x.name; o["category"] = x.category; o["dsp"] = x.dsp; pr << o; }
  put("console/presets", QString::fromUtf8(QJsonDocument(pr).toJson(QJsonDocument::Compact)));
}
// A NEW INSTALL STARTS FROM THE FACTORY CONSOLE: the setup writes install.id next to the program once per fresh install (an update keeps it).
// When it differs from the id last seen, the console state of an earlier install (left by an uninstall run as another account, or written
// back by a console still open during the uninstall) is cleared. Scenes, presets and Windows' sound restore stay; licence is not touched.
void Engine::freshInstallReset() {
  QFile f(QCoreApplication::applicationDirPath() + QStringLiteral("/install.id")); if (!f.open(QIODevice::ReadOnly)) return;
  const QString id = QString::fromUtf8(f.read(64)).trimmed(); if (id.isEmpty()) return;
  const QString seen = settings_.value("install/id").toString();
  if (seen == id) return;
  // FIRST START AFTER A NEW INSTALL: a full factory reset (main.cpp calls Settings::factoryReset: settings, console, scenes, presets,
  // arrangement, driver locks and network tokens; the licence and the edition stay), then markInstallSeen() so it happens once
  installId_ = id; freshInstall_ = !seen.isEmpty() || settings_.contains("console/state") || settings_.contains("general/setupDone");
  if (freshInstall_) { settings_.remove("console/state"); startNote_ = QStringLiteral("New install: everything starts from the factory state (the licence is kept)."); }
  settings_.setValue("install/id", id); settings_.sync();
}
void Engine::markInstallSeen() { if (installId_.isEmpty()) return; settings_.setValue("install/id", installId_); settings_.sync(); freshInstall_ = false; }
void Engine::load() {
  const QJsonObject root = QJsonDocument::fromJson(settings_.value("console/state").toString().toUtf8()).object();
  if (!root.isEmpty()) {
    if (root.value("v").toInt(1) < 2) {   // 0.1.0 wrote the DSP fields flat on the strip: lift them into "dsp"
      QJsonObject r2 = root; QJsonArray arr;
      for (const QJsonValue& v : root.value("strips").toArray()) { QJsonObject o = v.toObject(); if (!o.contains("dsp")) o["dsp"] = o; arr << o; }
      r2["strips"] = arr; applyConsoleJson(r2, false);
    } else applyConsoleJson(root, false);
  }
  scenes_.resize(kScenes);
  const QJsonArray sc = QJsonDocument::fromJson(settings_.value("console/scenes").toString().toUtf8()).array();
  for (int i = 0; i < kScenes && i < sc.size(); ++i) { const QJsonObject o = sc[i].toObject(); scenes_[static_cast<std::size_t>(i)].name = o.value("name").toString().left(24); scenes_[static_cast<std::size_t>(i)].at = o.value("at").toString(); scenes_[static_cast<std::size_t>(i)].data = o.value("data").toObject(); }
  { recallSafe_.reset(); const qulonglong low = settings_.value("console/recallSafe", 0).toULongLong(); for (int i = 0; i < 32; ++i) if ((low >> i) & 1ull) recallSafe_.set(static_cast<std::size_t>(i));
    for (const QString& t : settings_.value("console/recallSafeHigh").toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) { bool ok = false; const int i = t.toInt(&ok); if (ok && i >= 32 && i < kStrips) recallSafe_.set(static_cast<std::size_t>(i)); } }
  const QJsonArray pr = QJsonDocument::fromJson(settings_.value("console/presets").toString().toUtf8()).array();
  for (const QJsonValue& v : pr) { const QJsonObject o = v.toObject(); if (o.value("name").toString().isEmpty()) continue; userPresets_.push_back({o.value("name").toString().left(24), o.value("category").toString("USER").left(16), o.value("dsp").toObject()}); }
}

// ── SCENES: 16 snapshots of the whole console ──
QVariantList Engine::scenes() const {
  QVariantList l;
  for (int i = 0; i < kScenes; ++i) { const SceneUi& x = scenes_[static_cast<std::size_t>(i)]; QVariantMap m; m["index"] = i; m["name"] = x.name.isEmpty() ? QStringLiteral("SCENE %1").arg(i + 1, 2, 10, QLatin1Char('0')) : x.name; m["at"] = x.at; m["empty"] = x.data.isEmpty(); m["current"] = i == currentScene_; l << m; }
  return l;
}
void Engine::storeScene(int i, const QString& name) {
  if (i < 0 || i >= kScenes) return; SceneUi& x = scenes_[static_cast<std::size_t>(i)];
  x.data = consoleJson(); x.at = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"); if (!name.trimmed().isEmpty()) x.name = name.trimmed().left(24); else if (x.name.isEmpty()) x.name = QStringLiteral("SCENE %1").arg(i + 1, 2, 10, QLatin1Char('0'));
  currentScene_ = i; save(); emit scenesChanged();
}
bool Engine::recallScene(int i) {
  if (i < 0 || i >= kScenes || scenes_[static_cast<std::size_t>(i)].data.isEmpty()) return false;
  applyConsoleJson(scenes_[static_cast<std::size_t>(i)].data, true); currentScene_ = i; emit scenesChanged(); return true;
}
void Engine::clearScene(int i) { if (i < 0 || i >= kScenes) return; scenes_[static_cast<std::size_t>(i)] = SceneUi{}; if (currentScene_ == i) currentScene_ = -1; save(); emit scenesChanged(); }
void Engine::renameScene(int i, const QString& name) { if (i < 0 || i >= kScenes) return; scenes_[static_cast<std::size_t>(i)].name = name.trimmed().left(24); save(); emit scenesChanged(); }
void Engine::setRecallSafe(int strip, bool on) { if (strip < 0 || strip >= kStrips) return; recallSafe_.set(static_cast<std::size_t>(strip), on); save(); emit scenesChanged(); }
bool Engine::recallSafe(int strip) const { return strip >= 0 && strip < kStrips && recallSafe_.test(static_cast<std::size_t>(strip)); }

// ── PRESETS: a channel's DSP chain by name (factory ones built in, user ones saved) ──
namespace {
struct Factory { const char* name; const char* category; const char* json; };
// the chains as compact JSON (the same shape dspJson writes); only the fields that differ from the defaults are given
const Factory kFactory[] = {
  {"FLAT", "UTILITY", R"({})"},
  {"VOCAL", "VOICE", R"({"hpOrder":2,"hpFreq":90,"xoverType":0,"eq":[{"t":1,"f":250,"q":1.2,"g":-2.5},{"t":1,"f":3200,"q":1.0,"g":2.5},{"t":3,"f":10000,"q":0.7,"g":2}],"compOn":true,"compThr":-18,"compRatio":3,"compKnee":6,"compAtt":5,"compRel":80,"compMakeup":3,"gateOn":true,"gateThr":-48,"gateRange":-40,"gateAtt":1,"gateHold":80,"gateRel":150})"},
  {"PODCAST VOICE", "VOICE", R"({"hpOrder":2,"hpFreq":80,"xoverType":0,"eq":[{"t":1,"f":180,"q":1.0,"g":2},{"t":1,"f":400,"q":1.4,"g":-3},{"t":1,"f":4500,"q":1.2,"g":2},{"t":4,"f":14000,"q":0.7,"g":0}],"compOn":true,"compThr":-20,"compRatio":4,"compKnee":8,"compAtt":8,"compRel":120,"compMakeup":4,"limOn":true,"limCeil":-1,"limRel":80})"},
  {"KICK", "DRUMS", R"({"eq":[{"t":1,"f":60,"q":1.0,"g":4},{"t":1,"f":350,"q":1.4,"g":-5},{"t":1,"f":4000,"q":1.2,"g":3}],"compOn":true,"compThr":-12,"compRatio":4,"compKnee":2,"compAtt":15,"compRel":120,"compMakeup":2,"gateOn":true,"gateThr":-30,"gateRange":-80,"gateAtt":0.3,"gateHold":30,"gateRel":60})"},
  {"SNARE", "DRUMS", R"({"hpOrder":2,"hpFreq":80,"xoverType":0,"eq":[{"t":1,"f":200,"q":1.2,"g":2},{"t":1,"f":600,"q":1.5,"g":-3},{"t":1,"f":5000,"q":1.0,"g":3}],"compOn":true,"compThr":-14,"compRatio":4,"compKnee":4,"compAtt":10,"compRel":100,"compMakeup":2,"gateOn":true,"gateThr":-28,"gateRange":-60,"gateAtt":0.3,"gateHold":40,"gateRel":80})"},
  {"BASS", "INSTRUMENTS", R"({"hpOrder":1,"hpFreq":35,"xoverType":0,"eq":[{"t":1,"f":80,"q":1.0,"g":2},{"t":1,"f":250,"q":1.2,"g":-2},{"t":1,"f":900,"q":1.0,"g":1.5}],"compOn":true,"compThr":-20,"compRatio":5,"compKnee":4,"compAtt":10,"compRel":150,"compMakeup":4})"},
  {"ACOUSTIC GUITAR", "INSTRUMENTS", R"({"hpOrder":2,"hpFreq":100,"xoverType":0,"eq":[{"t":1,"f":220,"q":1.4,"g":-3},{"t":1,"f":2500,"q":1.0,"g":1.5},{"t":3,"f":9000,"q":0.7,"g":2}],"compOn":true,"compThr":-22,"compRatio":2.5,"compKnee":10,"compAtt":15,"compRel":200,"compMakeup":2})"},
  {"ELECTRIC GUITAR", "INSTRUMENTS", R"({"hpOrder":2,"hpFreq":110,"xoverType":0,"lpOrder":2,"lpFreq":9000,"eq":[{"t":1,"f":400,"q":1.2,"g":-2},{"t":1,"f":2200,"q":1.0,"g":2}],"gateOn":true,"gateThr":-50,"gateRange":-40,"gateAtt":2,"gateHold":120,"gateRel":250})"},
  {"KEYS / STEREO LINE", "INSTRUMENTS", R"({"hpOrder":1,"hpFreq":40,"xoverType":0,"compOn":true,"compThr":-24,"compRatio":2,"compKnee":12,"compAtt":20,"compRel":300,"compMakeup":1})"},
  {"CAR FRONT (TWEETER + MID)", "CAR AUDIO", R"({"hpOrder":4,"hpFreq":80,"xoverType":1,"eq":[{"t":1,"f":2500,"q":1.5,"g":-2},{"t":3,"f":8000,"q":0.7,"g":1.5}],"limOn":true,"limCeil":-1,"limRel":80})"},
  {"CAR REAR", "CAR AUDIO", R"({"hpOrder":4,"hpFreq":100,"xoverType":1,"lpOrder":2,"lpFreq":12000,"delayMs":1.2,"eq":[{"t":1,"f":3000,"q":1.2,"g":-3}],"limOn":true,"limCeil":-1,"limRel":80})"},
  {"CAR SUBWOOFER", "CAR AUDIO", R"({"lpOrder":4,"lpFreq":80,"xoverType":1,"hpOrder":2,"hpFreq":25,"eq":[{"t":1,"f":45,"q":1.0,"g":3}],"limOn":true,"limCeil":-0.5,"limRel":120})"},
  {"PA MAIN (FULL RANGE)", "LIVE", R"({"hpOrder":4,"hpFreq":40,"xoverType":1,"eq":[{"t":1,"f":250,"q":1.0,"g":-2},{"t":1,"f":2500,"q":1.4,"g":-1.5}],"limOn":true,"limCeil":-1,"limRel":100})"},
  {"STAGE MONITOR", "LIVE", R"({"hpOrder":4,"hpFreq":100,"xoverType":1,"eq":[{"t":6,"f":315,"q":8,"g":0},{"t":6,"f":2500,"q":8,"g":0},{"t":1,"f":4000,"q":1.0,"g":-2}],"limOn":true,"limCeil":-2,"limRel":60})"},
  {"TELEPHONE", "EFFECT", R"({"hpOrder":4,"hpFreq":300,"xoverType":0,"lpOrder":4,"lpFreq":3400,"eq":[{"t":1,"f":1500,"q":1.0,"g":4}],"compOn":true,"compThr":-20,"compRatio":8,"compKnee":2,"compAtt":1,"compRel":60,"compMakeup":4})"},
};
}  // namespace
QVariantList Engine::presets() const {
  QVariantList l;
  for (const Factory& f : kFactory) { QVariantMap m; m["name"] = f.name; m["category"] = f.category; m["factory"] = true; l << m; }
  for (const PresetUi& u : userPresets_) { QVariantMap m; m["name"] = u.name; m["category"] = u.category; m["factory"] = false; l << m; }
  return l;
}
QJsonObject Engine::presetPack() {
  QJsonObject pack; QJsonArray mast, fx, tone;
  // MASTERING: a preset's own settings are the ones that come out the same from two different starting points (what it leaves alone differs)
  const QVariantMap saved = mastering();
  auto base = [this](bool b, double k) { QVariantMap m; for (const char* x : {"eqOn", "analog", "on", "bassMonoOn", "toneOn", "compOn", "satOn", "deessOn", "exciterOn"}) m[x] = b;
    m["low"] = -11.5 * k; m["air"] = 11.5 * k; m["mid"] = -11.5 * k; m["compMix"] = k > 0 ? 0.123 : 0.877; m["bassMonoHz"] = k > 0 ? 333 : 41; m["thr"] = k > 0 ? -39.5 : -0.5; m["ratio"] = k > 0 ? 9.7 : 1.1;
    m["attack"] = k > 0 ? 197 : 0.6; m["release"] = k > 0 ? 1999 : 21; m["makeup"] = k > 0 ? 17.3 : 0.1; m["drive"] = k > 0 ? 0.987 : 0.013; m["gain"] = -11.3 * k; m["deessThr"] = k > 0 ? -49.1 : -0.9; m["deessMax"] = k > 0 ? 17.1 : 0.2; m["exciter"] = k > 0 ? 0.991 : 0.009; m["preset"] = QString(); setMastering(m); };
  for (const QString& n : masteringPresets()) {
    base(true, 1); applyMasteringPreset(n); const QVariantMap a = mastering(); base(false, -1); applyMasteringPreset(n); const QVariantMap b = mastering();
    QJsonObject params; for (auto it = a.cbegin(); it != a.cend(); ++it) if (it.key() != QLatin1String("preset") && b.value(it.key()) == it.value()) params[it.key()] = QJsonValue::fromVariant(it.value());
    mast << QJsonObject{{"name", n}, {"params", params}}; }
  setMastering(saved);
  for (const auto& p : am::fx::presets()) { QJsonObject v; for (const auto& [k, x] : p.values) v[QString::fromStdString(k)] = x; v["mix"] = p.mix;
    fx << QJsonObject{{"kind", QString::fromStdString(p.kind)}, {"id", p.id}, {"name", QString::fromStdString(p.name)}, {"category", QString::fromStdString(p.category)}, {"values", v}}; }
  for (const Factory& f : kFactory) tone << QJsonObject{{"kind", "chain"}, {"name", f.name}, {"category", f.category}, {"dsp", QJsonDocument::fromJson(QByteArray(f.json)).object()}};
  for (int slot = 1; slot <= 10; ++slot) { const EqPresetUi* e = factoryEqPreset(slot); QJsonArray bands;
    for (const auto& b : e->bands) if (b.type != FilterType::Off) bands << QJsonObject{{"type", static_cast<int>(b.type)}, {"freq", b.freq}, {"q", b.q}, {"gainDb", b.gainDb}};
    tone << QJsonObject{{"kind", "eq"}, {"slot", slot}, {"name", e->name}, {"category", "EQ"}, {"bands", bands}, {"hpOrder", e->hpOrder}, {"hpFreq", e->hpFreq}, {"lpOrder", e->lpOrder}, {"lpFreq", e->lpFreq}}; }
  pack["mastering"] = mast; pack["effects"] = fx; pack["tone"] = tone; return pack;
}
bool Engine::applyPreset(const QString& name, int strip) {
  if (strip < 0 || strip >= kStrips) return false;
  QJsonObject o; bool found = false;
  for (const Factory& f : kFactory) if (name == QLatin1String(f.name)) { o = QJsonDocument::fromJson(QByteArray(f.json)).object(); found = true; break; }
  if (!found) for (const PresetUi& u : userPresets_) if (u.name == name) { o = u.dsp; found = true; break; }
  if (!found) return false;
  StripUi& s = strips_[static_cast<std::size_t>(strip)]; s.dsp.set(dspFromJson(o));
  if (rt_) { const auto& p = s.dsp.params(); for (int b = 0; b < am::dsp::kEqBands; ++b) { const auto& e = p.eq[static_cast<std::size_t>(b)]; rt_->setEqBand(strip, b, e.type, e.freq, e.q, e.gainDb); } rt_->setDelayMs(strip, p.delayMs); rt_->setPolarity(strip, p.polarityInvert); rt_->setCrossover(strip, p.xoverType, p.hpOrder, p.hpFreq, p.lpOrder, p.lpFreq); rt_->setChannelLimiter(strip, p.limiterOn, p.limiterCeilDb, p.limiterReleaseMs); rt_->setCompressor(strip, p.compOn, p.compThresholdDb, p.compRatio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, p.compMakeupDb); rt_->setGate(strip, p.gateOn, p.gateThresholdDb, p.gateRangeDb, p.gateAttackMs, p.gateHoldMs, p.gateReleaseMs); }
  emit stripsChanged(); return true;
}
bool Engine::savePreset(const QString& name, int strip) {
  const QString n = name.trimmed().left(24); if (n.isEmpty() || strip < 0 || strip >= kStrips) return false;
  for (const Factory& f : kFactory) if (n == QLatin1String(f.name)) return false;   // factory names stay factory
  const QJsonObject d = dspJson(strips_[static_cast<std::size_t>(strip)].dsp.params());
  for (PresetUi& u : userPresets_) if (u.name == n) { u.dsp = d; save(); emit presetsChanged(); return true; }
  userPresets_.push_back({n, QStringLiteral("USER"), d}); save(); emit presetsChanged(); return true;
}
bool Engine::deletePreset(const QString& name) {
  for (std::size_t i = 0; i < userPresets_.size(); ++i) if (userPresets_[i].name == name) { userPresets_.erase(userPresets_.begin() + static_cast<long>(i)); save(); emit presetsChanged(); return true; }
  return false;
}

QVariantMap Engine::ampState(int out) const {
  if (out < 0 || out >= 64) return {};
  QVariantMap m{{"on", false}, {"gainDb", 0.0}, {"mute", false}, {"invert", false}, {"delayMs", 0.0}, {"hpOrder", 0}, {"hpHz", 80.0}, {"lpOrder", 0}, {"lpHz", 18000.0}, {"type", 1}, {"limitOn", false}, {"limitDb", -1.0}, {"releaseMs", 200.0}, {"name", QStringLiteral("OUT %1").arg(out + 1)}};
  { QVariantList eq; static const double f[] = {63, 125, 250, 500, 1000, 2000, 4000, 8000}; for (double hz : f) eq << QVariantMap{{"type", 0}, {"freq", hz}, {"q", 1.4}, {"gain", 0.0}}; m["eq"] = eq; }
  for (auto it = ampUi_[out].cbegin(); it != ampUi_[out].cend(); ++it) m[it.key()] = it.value();
  return m;
}
void Engine::setAmp(int out, const QVariantMap& p) {
  if (out < 0 || out >= 64) return; QVariantMap m = ampState(out); for (auto it = p.cbegin(); it != p.cend(); ++it) m[it.key()] = it.value();
  m["name"] = m["name"].toString().left(24); ampUi_[out] = m;
  if (rt_) rt_->setAmp(out, ampFromMap(m));
  emit ampChanged(); if (!restoring_ && !saveTimer_.isActive()) saveTimer_.start();   // saved two seconds after the first change: a slider being dragged does not write the whole console on every step (the window stopped answering)
}
int Engine::ampOutputsUsed() const { int n = 0; for (int o = 0; o < kOutputs && o < 64; ++o) if (!ampUi_[o].isEmpty() && ampUi_[o].value("on").toBool()) n = o + 1; return n; }
// RESET on AMP / SYSTEM: every output stage off and the outputs back on the master left / right, as on a new console
QString Engine::clearSystem() {
  for (int o = 0; o < 64 && o < kOutputs; ++o) { if (!ampUi_[o].isEmpty()) { ampUi_[o].clear(); if (rt_) rt_->setAmp(o, am::AmpParams{}); } }
  resetRouting(); emit ampChanged(); save();
  return QStringLiteral("Reset: every output stage is off and the outputs are back on the master left and right.");
}
QVariantList Engine::ampResponse(int out) const {
  QVariantList l; am::AmpChannel a; a.prepare(48000); a.set(ampFromMap(ampState(out)));
  for (int k = 0; k <= 60; ++k) { const double hz = 20.0 * std::pow(1000.0, k / 60.0); l << QVariantMap{{"hz", hz}, {"db", a.responseDbAt(hz)}}; }
  return l;
}
QVariantList Engine::ampMeters() const {
  QVariantList l; const int n = ampOutputs(); if (!rt_ || !running()) { for (int o = 0; o < n; ++o) l << QVariantMap{{"pk", -120.0}, {"gr", 0.0}}; return l; }
  const am::Snapshot s = rt_->snapshot();
  for (int o = 0; o < n; ++o) l << QVariantMap{{"pk", s.ampPk[o] > 1e-6 ? 20.0 * std::log10(s.ampPk[o]) : -120.0}, {"gr", s.ampGrDb[o]}};
  return l;
}
int Engine::ampOutputs() const { const int n = running() ? deviceOutputs() : 0; return std::clamp(std::max(n, 8), 1, 64); }   // at least 8: a system can be set up before the interface with all its outputs is connected

// SYSTEM SETUP: one click routes the master to the outputs and sets each output's crossover, the way a speaker processor's wizard does.
// layout: "full" (full-range L/R), "2way" (low + high per side), "3way" (low + mid + high per side); sub: "none", "stereo", "mono".
// Outputs are taken in order from output 1; the control room keeps nothing here (the PA gets the master). Returns what was set.
QString Engine::setupSystem(const QString& layout, const QString& sub, double xoverHz, bool force) {
  struct Way { QString name; int src; int hp; double hpHz; int lp; double lpHz; double limit; };
  QList<Way> ways; const int L = am::Runtime::kSrcMasterL, R = am::Runtime::kSrcMasterR, M = am::Runtime::kSrcMasterMono;
  const double x = std::clamp(xoverHz > 0 ? xoverHz : 100.0, 40.0, 200.0); const bool hasSub = sub == "stereo" || sub == "mono";
  const double fullHp = hasSub ? x : 40.0; const int fullHpOrder = hasSub ? 4 : 2;
  for (const auto& [side, src] : {std::pair<QString, int>{"L", L}, std::pair<QString, int>{"R", R}}) {
    if (layout == "3way") { ways << Way{"LOW " + side, src, fullHpOrder, fullHp, 4, 500, -2} << Way{"MID " + side, src, 4, 500, 4, 3000, -3} << Way{"HIGH " + side, src, 4, 3000, 0, 20000, -6}; }
    else if (layout == "2way") { ways << Way{"LOW " + side, src, fullHpOrder, fullHp, 4, 1800, -2} << Way{"HIGH " + side, src, 4, 1800, 0, 20000, -6}; }
    else ways << Way{"MAIN " + side, src, fullHpOrder, fullHp, 0, 20000, -1};
  }
  if (sub == "stereo") ways << Way{"SUB L", L, 2, 30, 4, x, -3} << Way{"SUB R", R, 2, 30, 4, x, -3};
  else if (sub == "mono") ways << Way{"SUB", M, 2, 30, 4, x, -3};
  const int n = static_cast<int>(ways.size());
  // a sound card with fewer outputs than the system needs: nothing is changed. (Before, a 2-way system on a stereo card put the lows of the
  // left side on output 1 and its highs on output 2, so headphones played bass on one ear and treble on the other.)
  if (const int haveNow = running() ? deviceOutputs() : 0; !force && haveNow > 0 && haveNow < n)
    return QStringLiteral("Not set up: this system needs %1 outputs and this sound card has %2. Nothing was changed. Connect a sound card with %1 or more outputs, or choose FULL RANGE with NO SUB (2 outputs).").arg(n).arg(haveNow);
  for (int o = 0; o < 64; ++o) {
    if (o < n) { const Way& w = ways[o]; setOutputRoute(o, w.src);
      QVariantMap a{{"on", true}, {"name", w.name}, {"gainDb", 0.0}, {"mute", false}, {"invert", false}, {"delayMs", 0.0}, {"hpOrder", w.hp}, {"hpHz", w.hpHz}, {"lpOrder", w.lp}, {"lpHz", w.lpHz}, {"type", 1}, {"limitOn", true}, {"limitDb", w.limit}};
      setAmp(o, a); }
    else if (!ampUi_[o].isEmpty() && o < kOutputs) { setOutputRoute(o, am::Runtime::kSrcOff); ampUi_[o].clear(); if (rt_) rt_->setAmp(o, am::AmpParams{}); }
  }
  emit ampChanged(); save();
  const int have = running() ? deviceOutputs() : 0;
  return QStringLiteral("%1 outputs set up (%2%3, crossover %4 Hz)%5").arg(n).arg(layout == "3way" ? "3-way" : layout == "2way" ? "2-way" : "full range").arg(hasSub ? QStringLiteral(" + %1 sub").arg(sub) : QString()).arg(x, 0, 'f', 0)
         .arg(have > 0 && have < n ? QStringLiteral(": this sound card has only %1 outputs, connect one with %2 or more").arg(have).arg(n) : QString());
}
