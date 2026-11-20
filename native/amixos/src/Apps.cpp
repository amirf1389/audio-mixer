#include "Apps.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QDesktopServices>
#include <QProcess>
#include <QUrl>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QThread>
#include <cmath>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#endif
#include <algorithm>

#include "Engine.h"
#include "audiomixer/app_sound.hpp"

using am::Runtime; using am::app::Capture;

Apps::Apps(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  load();
  connect(engine_, &Engine::stateChanged, this, &Apps::attach);
  connect(engine_, &Engine::routingChanged, this, [this] { if (!placing_) prune(); emit changed(); });
  engine_->appInputName = [this](int input) { return inputName(input); };
  timer_.setInterval(1000); connect(&timer_, &QTimer::timeout, this, &Apps::poll); timer_.start();
}
Apps::~Apps() {
  engine_->appInputName = nullptr;
  if (am::Runtime* rt = engine_->runtime()) { rt->setAppInput(nullptr); QThread::msleep(40); }   // one more block passes before the slots go
  if (cap_) cap_->closeAll();
}
Capture& Apps::cap() { if (!cap_) { cap_ = std::make_unique<Capture>(); if (am::Runtime* rt = engine_->runtime()) rt->setAppInput(cap_.get()); } return *cap_; }

// the engine makes a new am::Runtime at every device open: hand it the slots again, at its sample rate
void Apps::attach() {
  am::Runtime* rt = engine_->runtime(); const double rate = rt ? rt->sampleRate() : 0;
  if (rt == rt_ && rate == rate_) return;
  rt_ = rt; rate_ = rate;
  if (rt && cap_) { cap_->setSampleRate(rate); rt->setAppInput(cap_.get()); }
  emit changed();
}

int Apps::count() const { int n = cap_ ? cap_->used() : 0; for (const QString& w : waiting_) if (!w.isEmpty()) ++n; return n; }
int Apps::slotOfInput(int input) const { if (!Runtime::isAppInput(input)) return -1; const int k = input - Runtime::kAppInBase; return k >= 2 * Capture::kSlots ? k - 2 * Capture::kSlots : k / 2; }
QString Apps::inputName(int input) const {
  const int slot = slotOfInput(input); if (slot < 0) return {};
  QString n = waiting_[slot]; if (cap_) { const Capture::Info i = cap_->info(slot); if (i.used) n = QString::fromStdString(i.label); }
  if (n.isEmpty()) n = QStringLiteral("APP %1").arg(slot + 1);
  n.remove(QRegularExpression(QStringLiteral("\\s*\\(.*\\)$"))); if (n.size() > 12) n = n.left(12);
  const int k = input - Runtime::kAppInBase; return k >= 2 * Capture::kSlots ? n : n + (k % 2 ? QStringLiteral(" R") : QStringLiteral(" L"));
}
QVariantList Apps::captures() const {
  QVariantList l; const QVariantList routes = engine_->inputRoutes();
  for (int k = 0; k < Capture::kSlots; ++k) {
    const Capture::Info i = cap_ ? cap_->info(k) : Capture::Info(); if (!i.used && waiting_[k].isEmpty()) continue;
    QVariantList strips; for (int s = 0; s < routes.size(); ++s) if (slotOfInput(routes[s].toInt()) == k) strips << s;
    QVariantMap m; m["slot"] = k; m["strips"] = strips;
    m["device"] = i.used ? i.isDevice : !waitingDev_[k].isEmpty();
    if (i.used) { m["name"] = QString::fromStdString(i.label); m["pid"] = static_cast<qulonglong>(i.pid); m["state"] = QString::fromStdString(i.state); m["test"] = i.test; m["gaps"] = static_cast<qulonglong>(i.gaps);
      m["peakDb"] = i.peak > 1e-6 ? 20.0 * std::log10(i.peak) : -120.0;
      m["message"] = i.state == "failed" ? QString::fromStdString(i.error) : i.state == "silent" ? QStringLiteral("captured, but the program plays nothing now (or its stream is protected)") : i.state == "opening" ? QStringLiteral("opening") : QString(); }
    else { m["name"] = waiting_[k]; m["pid"] = 0; m["state"] = "waiting"; m["test"] = false; m["gaps"] = 0; m["peakDb"] = -120.0; m["message"] = waitingDev_[k].isEmpty() ? QStringLiteral("waiting for the program to play again") : QStringLiteral("waiting for the device to be plugged in again"); }
    l << m; }
  return l;
}

QVariantList Apps::programs() {
  QVariantList src = testMode ? testPrograms : engine_->appSources(), out;
  for (const QVariant& v : src) { QVariantMap p = v.toMap(); std::uint32_t pid = 0; am::app::parseDeviceId(p["id"].toString().toStdString(), pid); p["pid"] = static_cast<qulonglong>(pid); int slot = -1;
    if (cap_) for (int k = 0; k < Capture::kSlots; ++k) { const Capture::Info i = cap_->info(k); if (i.used && !i.test && i.pid == pid) slot = k; }
    p["slot"] = slot; out << p; }
  return out;
}

// the strip (and, for stereo, the next one) listens to the slot; a slot's earlier strips stay as they are
void Apps::place(int slot, int strip, bool stereo) {
  placing_ = true;
  if (stereo && strip + 1 < engine_->strips()) { engine_->setInputRoute(strip, Runtime::kAppInBase + 2 * slot); engine_->setInputRoute(strip + 1, Runtime::kAppInBase + 2 * slot + 1); engine_->setPan(strip, -1); engine_->setPan(strip + 1, 1); }
  else engine_->setInputRoute(strip, Runtime::kAppInBase + 2 * Capture::kSlots + slot);
  placing_ = false; prune();
}
QVariantMap Apps::finish(int slot, const QString& name, int strip, bool stereo, const std::string& err) {
  if (slot < 0) { note_ = QString::fromStdString(err); emit changed(); return QVariantMap{{"ok", false}, {"slot", -1}, {"message", note_}}; }
  waiting_[slot].clear(); place(slot, strip, stereo); save();
  note_ = stereo && strip + 1 < engine_->strips() ? QStringLiteral("%1 is on CH %2 (left) and CH %3 (right).").arg(name).arg(strip + 1).arg(strip + 2) : QStringLiteral("%1 is on CH %2.").arg(name).arg(strip + 1);
  emit changed(); return QVariantMap{{"ok", true}, {"slot", slot}, {"message", note_}};
}
QVariantMap Apps::capture(const QString& id, const QString& name, int strip, bool stereo) {
  auto refuse = [&](const QString& why) { note_ = why; emit changed(); return QVariantMap{{"ok", false}, {"slot", -1}, {"message", why}}; };
  if (strip < 0 || strip >= engine_->strips()) return refuse(QStringLiteral("Pick a strip first."));
  if (strip >= engine_->licensedChannels() || (stereo && strip + 1 >= engine_->licensedChannels())) return refuse(QStringLiteral("That strip is not part of this edition."));
  if (!engine_->runtime()) return refuse(QStringLiteral("Start the sound card first (SOUND CARD), then pick the program."));
  std::uint32_t pid = 0; if (!am::app::parseDeviceId(id.toStdString(), pid)) return refuse(QStringLiteral("That is not a program."));
  // already captured: the strip joins its slot (one program on several strips costs one capture)
  for (int k = 0; cap_ && k < Capture::kSlots; ++k) { const Capture::Info i = cap_->info(k); if (i.used && !i.test && i.pid == pid) return finish(k, name, strip, stereo, {}); }
  int want = -1; for (int k = 0; k < Capture::kSlots; ++k) if (waiting_[k] == name) want = k;   // the slot that waited for it
  std::string err; const int slot = cap().open(pid, name.toStdString(), engine_->runtime()->sampleRate(), err, want);
  return finish(slot, name, strip, stereo, err);
}
// ---- WINDOWS RECORDING DEVICES
QVariantList Apps::recordingDevices() {
  QVariantList out; const QVariantList src = testMode ? testDevices : engine_->devices();
  for (const QVariant& v : src) { const QVariantMap d = v.toMap(); const QString id = d["id"].toString();
    if (d["backend"].toString() != "wasapi" || d["inChannels"].toInt() <= 0 || id.startsWith(QLatin1String("loopback:"))) continue;
    int slot = -1; if (cap_) for (int k = 0; k < Capture::kSlots; ++k) { const Capture::Info i = cap_->info(k); if (i.used && i.isDevice && QString::fromStdString(i.device) == id) slot = k; }
    out << QVariantMap{{"id", id}, {"name", d["name"]}, {"kind", d["kind"]}, {"isDefault", d["defaultIn"].toBool()}, {"channels", d["inChannels"]}, {"rate", d["defaultRate"]}, {"slot", slot}}; }
  return out;
}
QVariantMap Apps::captureDevice(const QString& id, const QString& name, int strip, bool stereo) {
  auto refuse = [&](const QString& why) { note_ = why; emit changed(); return QVariantMap{{"ok", false}, {"slot", -1}, {"message", why}}; };
  if (strip < 0 || strip >= engine_->strips()) return refuse(QStringLiteral("Pick a strip first."));
  if (strip >= engine_->licensedChannels() || (stereo && strip + 1 >= engine_->licensedChannels())) return refuse(QStringLiteral("That strip is not part of this edition."));
  if (!engine_->runtime()) return refuse(QStringLiteral("Start the sound card first (SOUND CARD), then pick the device."));
  if (id.isEmpty() || id.startsWith(QLatin1String("loopback:"))) return refuse(QStringLiteral("That is not a recording device."));
  if (QString::fromStdString(engine_->currentInputId()) == id) return refuse(QStringLiteral("%1 is the sound card's own input already: it is on the strips as IN 1, IN 2 ...").arg(name));
  for (int k = 0; cap_ && k < Capture::kSlots; ++k) { const Capture::Info i = cap_->info(k); if (i.used && i.isDevice && QString::fromStdString(i.device) == id) return finish(k, name, strip, stereo, {}); }
  int want = -1; for (int k = 0; k < Capture::kSlots; ++k) if (waitingDev_[k] == id) want = k;
  std::string err; const int slot = cap().openDevice(id.toStdString(), name.toStdString(), engine_->runtime()->sampleRate(), err, want);
  if (slot >= 0) waitingDev_[slot].clear();
  return finish(slot, name, strip, stereo, err);
}
void Apps::readProperties(const QString& id) {
  auto pack = [](const Capture::DeviceProps& p) { return QVariantMap{{"ok", p.ok}, {"channels", p.channels}, {"rate", p.rate}, {"bits", p.bits}, {"levelPct", p.level < 0 ? -1 : int(p.level * 100 + 0.5)}, {"muted", p.muted},
    {"enhancements", p.enhancements == 1 ? "on" : p.enhancements == 0 ? "off" : "unknown"}, {"error", QString::fromStdString(p.error)}}; };
  if (testMode) { Capture::DeviceProps p; p.ok = true; p.channels = 2; p.rate = 48000; p.bits = 24; p.level = 0.8; p.enhancements = 1; emit propertiesReady(id, pack(p)); return; }
  QPointer<Apps> self(this); const std::string dev = id.toStdString();   // Windows' audio service can take its time: never on the interface thread
  std::thread([self, dev, id, pack] { const Capture::DeviceProps p = Capture::deviceProps(dev); QMetaObject::invokeMethod(qApp, [self, id, p, pack] { if (self) emit self->propertiesReady(id, pack(p)); }, Qt::QueuedConnection); }).detach();
}
bool Apps::openWindowsSound(const QString& what) {
  if (what != "settings" && what != "recording" && what != "playback" && what != "mixer") return false;
  lastPanel = what; if (testMode) return true;
#ifdef Q_OS_WIN
  if (what == "settings") return QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:sound")));
  if (what == "mixer") return QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:apps-volume")));
  return QProcess::startDetached(QStringLiteral("control.exe"), {what == "recording" ? QStringLiteral("mmsys.cpl,,1") : QStringLiteral("mmsys.cpl,,0")});   // the classic Sound panel on its Recording / Playback tab
#else
  return false;
#endif
}
QVariantMap Apps::captureTest(const QString& name, double hz, int strip, bool stereo) {
  if (strip < 0 || strip >= engine_->strips() || !engine_->runtime()) { note_ = QStringLiteral("Start the sound card first (SOUND CARD), then pick the program."); emit changed(); return QVariantMap{{"ok", false}, {"slot", -1}, {"message", note_}}; }
  std::string err; const int slot = cap().openTest(name.toStdString(), hz, engine_->runtime()->sampleRate(), err);
  return finish(slot, name, strip, stereo, err);
}
void Apps::release(int slot) {
  if (slot < 0 || slot >= Capture::kSlots) return;
  placing_ = true; const QVariantList routes = engine_->inputRoutes();
  for (int s = 0; s < routes.size(); ++s) if (slotOfInput(routes[s].toInt()) == slot) engine_->setInputRoute(s, -1);
  placing_ = false; waiting_[slot].clear(); waitingDev_[slot].clear(); if (cap_) cap_->close(slot); save(); emit changed();
}
// a slot no strip listens to is given back
void Apps::prune() {
  bool heard[Capture::kSlots] = {}; const QVariantList routes = engine_->inputRoutes();
  for (const QVariant& r : routes) { const int k = slotOfInput(r.toInt()); if (k >= 0) heard[k] = true; }
  bool any = false;
  for (int k = 0; k < Capture::kSlots; ++k) { if (heard[k]) continue; if (cap_ && cap_->info(k).used) { cap_->close(k); any = true; } if (!waiting_[k].isEmpty()) { waiting_[k].clear(); waitingDev_[k].clear(); any = true; } }
  if (any) save();
}
// once a second: a program that closed leaves its slot waiting; a waiting slot looks for its program among those that play now
// ---- AUTO LIVE
QString Apps::streamAppOf(const QString& processName) {
  QString n = processName.trimmed().toLower(); if (n.endsWith(QLatin1String(".exe"))) n.chop(4);
  static const std::pair<const char*, const char*> known[] = {{"tiktok live studio", "TikTok LIVE Studio"}, {"tiktoklivestudio", "TikTok LIVE Studio"}, {"obs64", "OBS Studio"}, {"obs32", "OBS Studio"}, {"obs studio", "OBS Studio"},
    {"streamlabs obs", "Streamlabs"}, {"streamlabs desktop", "Streamlabs"}, {"streamlabs", "Streamlabs"}, {"xsplit.core", "XSplit"}, {"xsplit broadcaster", "XSplit"}, {"vmix64", "vMix"}, {"vmix", "vMix"},
    {"twitch studio", "Twitch Studio"}, {"twitchstudio", "Twitch Studio"}, {"prism live studio", "PRISM Live Studio"}, {"prismlivestudio", "PRISM Live Studio"}};
  if (n == QLatin1String("obs")) return QStringLiteral("OBS Studio");
  for (const auto& [key, name] : known) if (n == QLatin1String(key) || n.startsWith(QLatin1String(key))) return QString::fromLatin1(name);
  return QString();
}
void Apps::setAutoLive(bool on) { s_.setValue("live/auto", on); streamHandled_ = false; if (!on && !streamApp_.isEmpty()) streamApp_.clear(); emit changed(); }
void Apps::checkStreamApps(const QStringList& names) {
  QString found; for (const QString& n : names) { found = streamAppOf(n); if (!found.isEmpty()) break; }
  if (found.isEmpty()) { if (!streamApp_.isEmpty()) { streamApp_.clear(); streamHandled_ = false; emit changed(); } return; }   // it was closed: the next start counts again
  if (found != streamApp_) { streamApp_ = found; streamHandled_ = false; emit changed(); }
  if (!autoLive() || streamHandled_) return;
  streamHandled_ = true;   // once per run of the program
  if (!engine_->liveMode()) { note_ = found + QStringLiteral(" is running: ") + engine_->startLive(engine_->selectedStrip()); emit changed(); }
}
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
static QStringList runningProgramNames() {   // names only (a snapshot of the process list); nothing is opened or read from the programs
  QStringList l; HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (snap == INVALID_HANDLE_VALUE) return l;
  PROCESSENTRY32W e; e.dwSize = sizeof e; if (Process32FirstW(snap, &e)) do { l << QString::fromWCharArray(e.szExeFile); } while (Process32NextW(snap, &e));
  CloseHandle(snap); return l;
}
#else
static QStringList runningProgramNames() { return {}; }
#endif
void Apps::poll() {
  if (autoLive() && ++procTick_ >= 5 && !procLooking_) { procTick_ = 0;   // every 5 seconds, on a thread of its own
    if (processesNow) checkStreamApps(processesNow());
    else { procLooking_ = true; QPointer<Apps> self(this); std::thread([self] { const QStringList names = runningProgramNames(); QMetaObject::invokeMethod(qApp, [self, names] { if (self) { self->procLooking_ = false; self->checkStreamApps(names); } }, Qt::QueuedConnection); }).detach(); } }
  bool wait = false; QString sig;
  for (int k = 0; k < Capture::kSlots; ++k) {
    if (cap_) { const Capture::Info i = cap_->info(k);
      if (i.used && i.state == "failed") { waiting_[k] = QString::fromStdString(i.label); waitingDev_[k] = i.isDevice ? QString::fromStdString(i.device) : QString(); cap_->close(k); }
      if (i.used) sig += QString::fromStdString(i.state) + QLatin1Char('|'); }
    if (!waiting_[k].isEmpty()) { wait = true; sig += QStringLiteral("w|"); } }
  if (sig != lastSig_) { lastSig_ = sig; emit changed(); }
  if (!wait || !engine_->runtime()) return;
  // a recording device that waits: tried again every 5 seconds (it opens the moment Windows has it back)
  if (++devTick_ >= 5) { devTick_ = 0; bool got = false;
    for (int k = 0; k < Capture::kSlots; ++k) { if (waitingDev_[k].isEmpty() || !Capture::deviceAvailable()) continue; std::string err;
      if (cap().openDevice(waitingDev_[k].toStdString(), waiting_[k].toStdString(), engine_->runtime()->sampleRate(), err, k) == k) { waiting_[k].clear(); waitingDev_[k].clear(); got = true; } }
    if (got) emit changed(); }
  bool progWait = false; for (int k = 0; k < Capture::kSlots; ++k) if (!waiting_[k].isEmpty() && waitingDev_[k].isEmpty()) progWait = true;
  if (!progWait || looking_) return;
  auto take = [this](const std::vector<std::pair<quint32, QString>>& now) {
    looking_ = false; if (!engine_->runtime()) return; bool got = false;
    for (int k = 0; k < Capture::kSlots; ++k) { if (waiting_[k].isEmpty() || !waitingDev_[k].isEmpty()) continue;
      for (const auto& [pid, name] : now) if (name == waiting_[k]) { std::string err; if (cap().open(pid, name.toStdString(), engine_->runtime()->sampleRate(), err, k) == k) { waiting_[k].clear(); got = true; } break; } }
    if (got) emit changed(); };
  if (sessionsNow) { take(sessionsNow()); return; }
  if (!Capture::available()) return;
  looking_ = true; QPointer<Apps> self(this);   // Windows' audio service can take its time: asked on a thread of its own, never on the interface thread
  std::thread([self, take] { std::vector<std::pair<quint32, QString>> now; for (const am::app::Session& s : am::app::sessions()) now.emplace_back(s.pid, QString::fromStdString(s.label));
    QMetaObject::invokeMethod(qApp, [self, take, now] { if (self) take(now); }, Qt::QueuedConnection); }).detach();
}

// remembered by name: [{slot, name}] of the programs on slots (a test tone is not kept)
void Apps::save() {
  QJsonArray a; for (int k = 0; k < Capture::kSlots; ++k) { QString n = waiting_[k], dev = waitingDev_[k]; if (cap_) { const Capture::Info i = cap_->info(k); if (i.used && !i.test && (i.pid != 0 || i.isDevice)) { n = QString::fromStdString(i.label); dev = i.isDevice ? QString::fromStdString(i.device) : QString(); } else if (i.used) n.clear(); }
    if (!n.isEmpty()) { QJsonObject o{{"slot", k}, {"name", n}}; if (!dev.isEmpty()) o["dev"] = dev; a << o; } }
  s_.setValue("apps/captures", QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)));
}
void Apps::load() {
  const QJsonArray a = QJsonDocument::fromJson(s_.value("apps/captures").toString().toUtf8()).array();
  for (const QJsonValue& v : a) { const QJsonObject o = v.toObject(); const int k = o.value("slot").toInt(-1); QString n = o.value("name").toString().left(80); n.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f<>]")));
    QString dev = o.value("dev").toString().left(200); dev.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f<>]")));
    if (k >= 0 && k < Capture::kSlots && !n.isEmpty()) { waiting_[k] = n; waitingDev_[k] = dev; } }
  prune();   // a slot the saved console no longer listens to is not waited for
}

// ---- WINDOWS SOUND SETTINGS (documented Core Audio only: IMMDeviceEnumerator, the endpoint property store)
#ifdef _WIN32
namespace {
const CLSID kWsEnumClsid = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID kWsEnumIid = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const PROPERTYKEY kWsName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};        // PKEY_Device_FriendlyName
const PROPERTYKEY kWsContainer = {{0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}}, 2};   // PKEY_Device_ContainerId
const PROPERTYKEY kWsFormat = {{0xf19f064d, 0x082c, 0x4e27, {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}}, 0};      // PKEY_AudioEngine_DeviceFormat: the shared-mode format
QString wsId(IMMDevice* d) { LPWSTR w = nullptr; QString id; if (d && SUCCEEDED(d->GetId(&w)) && w) { id = QString::fromWCharArray(w); CoTaskMemFree(w); } return id; }
QVariantMap wsEndpoint(IMMDevice* d, const QString& flow) {
  QVariantMap m{{"id", wsId(d)}, {"flow", flow}, {"name", QString()}, {"container", QString()}, {"rate", 0}, {"bits", 0}, {"channels", 0}, {"isFloat", false}};
  IPropertyStore* ps = nullptr; if (!d || FAILED(d->OpenPropertyStore(STGM_READ, &ps)) || !ps) return m;
  PROPVARIANT v; PropVariantInit(&v);
  if (SUCCEEDED(ps->GetValue(kWsName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) m["name"] = QString::fromWCharArray(v.pwszVal);
  PropVariantClear(&v); PropVariantInit(&v);
  if (SUCCEEDED(ps->GetValue(kWsContainer, &v)) && v.vt == VT_CLSID && v.puuid) { const GUID& g = *v.puuid;
    m["container"] = QString::asprintf("{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", static_cast<unsigned long>(g.Data1), g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]); }
  PropVariantClear(&v); PropVariantInit(&v);
  if (SUCCEEDED(ps->GetValue(kWsFormat, &v)) && v.vt == VT_BLOB && v.blob.pBlobData && v.blob.cbSize >= sizeof(WAVEFORMATEX)) {
    const WAVEFORMATEX* f = reinterpret_cast<const WAVEFORMATEX*>(v.blob.pBlobData); int bits = f->wBitsPerSample; bool fl = f->wFormatTag == 3;   // WAVE_FORMAT_IEEE_FLOAT
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && v.blob.cbSize >= sizeof(WAVEFORMATEXTENSIBLE)) { const WAVEFORMATEXTENSIBLE* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
      if (x->Samples.wValidBitsPerSample) bits = x->Samples.wValidBitsPerSample; fl = x->SubFormat.Data1 == 3; }
    m["rate"] = static_cast<int>(f->nSamplesPerSec); m["bits"] = bits; m["channels"] = static_cast<int>(f->nChannels); m["isFloat"] = fl; }
  PropVariantClear(&v); ps->Release(); return m;
}
QVariantMap wsScan() {
  QVariantMap out{{"available", true}, {"note", QString()}}; const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // the interface thread is STA already: fine
  IMMDeviceEnumerator* en = nullptr;
  if (FAILED(CoCreateInstance(kWsEnumClsid, nullptr, CLSCTX_ALL, kWsEnumIid, reinterpret_cast<void**>(&en))) || !en) { out["available"] = false; out["note"] = QStringLiteral("Windows' audio service did not answer."); if (SUCCEEDED(co)) CoUninitialize(); return out; }
  auto def = [&](EDataFlow f, ERole r, const char* flow) { IMMDevice* d = nullptr; QVariantMap m; if (SUCCEEDED(en->GetDefaultAudioEndpoint(f, r, &d)) && d) { m = wsEndpoint(d, QLatin1String(flow)); d->Release(); } return m; };
  out["playback"] = def(eRender, eConsole, "out"); out["recording"] = def(eCapture, eConsole, "in");
  out["commPlayback"] = def(eRender, eCommunications, "out"); out["commRecording"] = def(eCapture, eCommunications, "in");
  QVariantList eps;
  for (const EDataFlow f : {eRender, eCapture}) { IMMDeviceCollection* col = nullptr; if (FAILED(en->EnumAudioEndpoints(f, DEVICE_STATE_ACTIVE, &col)) || !col) continue;
    UINT n = 0; col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) { IMMDevice* d = nullptr; if (SUCCEEDED(col->Item(i, &d)) && d) { eps << wsEndpoint(d, f == eRender ? QStringLiteral("out") : QStringLiteral("in")); d->Release(); } }
    col->Release(); }
  out["endpoints"] = eps; en->Release(); if (SUCCEEDED(co)) CoUninitialize(); return out;
}
}  // namespace
#endif
QVariantMap Apps::windowsDefaults() {
  if (!testWindows.isEmpty()) return testWindows;
#ifdef _WIN32
  return wsScan();
#else
  return QVariantMap{{"available", false}, {"note", QStringLiteral("Windows only: Windows' default devices and formats are read on Windows.")}, {"playback", QVariantMap()}, {"recording", QVariantMap()},
                     {"commPlayback", QVariantMap()}, {"commRecording", QVariantMap()}, {"endpoints", QVariantList()}};
#endif
}
QString Apps::alsaCard(const QString& id) {
  static const QRegularExpression named(QStringLiteral("CARD=([^,]+)")), num(QStringLiteral("^(?:plug)?hw:(\\d+)"));
  QRegularExpressionMatch m = named.match(id); if (m.hasMatch()) return m.captured(1);
  m = num.match(id); return m.hasMatch() ? m.captured(1) : QString();
}
QVariantList Apps::pairInterfaces(const QVariantList& devices, const QVariantMap& containers) {
  struct Group { QString key, name, backend; QVariantMap in, out; int inRank = 99, outRank = 99; };
  std::vector<Group> groups;
  auto rank = [](const QString& id) { return id.startsWith(QLatin1String("plughw:")) ? 0 : id.startsWith(QLatin1String("hw:")) ? 1 : id.startsWith(QLatin1String("sysdefault:")) ? 2 : 3; };
  for (const QVariant& v : devices) { const QVariantMap d = v.toMap(); const QString backend = d["backend"].toString(), id = d["id"].toString();
    if (id.isEmpty() || id == "default" || id == "-" || id.startsWith(QLatin1String("loopback:"))) continue;
    QString key, name = d["name"].toString();
    if (backend == "wasapi") { const QString c = containers.value(id).toString().toLower();
      if (c.isEmpty() || c == "{00000000-0000-0000-ffff-ffffffffffff}" || c == "{00000000-0000-0000-0000-000000000000}") continue;   // the computer's own (built-in) devices share the null container
      key = "win:" + c; const int a = name.indexOf('('), b = name.lastIndexOf(')'); if (a >= 0 && b > a + 1) name = name.mid(a + 1, b - a - 1).trimmed(); }   // "Speakers (USB Audio CODEC)" -> the box
    else if (backend == "alsa") { const QString card = alsaCard(id); if (card.isEmpty()) continue; key = "alsa:" + card; const int c = name.indexOf(','); if (c > 0) name = name.left(c).trimmed(); }
    else continue;
    auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.key == key; });
    if (it == groups.end()) { groups.push_back(Group{key, name, backend, {}, {}}); it = groups.end() - 1; }
    const int r = backend == "alsa" ? rank(id) : 0;
    if (d["inChannels"].toInt() > 0 && r < it->inRank) { it->in = d; it->inRank = r; }
    if (d["outChannels"].toInt() > 0 && r < it->outRank) { it->out = d; it->outRank = r; } }
  QVariantList usb, other;
  for (const Group& g : groups) { if (g.in.isEmpty() || g.out.isEmpty()) continue;
    const bool isUsb = g.name.contains(QLatin1String("usb"), Qt::CaseInsensitive) || g.in["name"].toString().contains(QLatin1String("usb"), Qt::CaseInsensitive) || g.out["name"].toString().contains(QLatin1String("usb"), Qt::CaseInsensitive);
    const QVariantMap p{{"key", g.key}, {"name", g.name}, {"backend", g.backend}, {"in", g.in["id"]}, {"out", g.out["id"]}, {"inChannels", g.in["inChannels"]}, {"outChannels", g.out["outChannels"]},
                        {"rate", g.out["defaultRate"].toDouble() > 0 ? g.out["defaultRate"].toDouble() : 48000.0}, {"usb", isUsb}};
    (isUsb ? usb : other) << p; }
  return usb + other;
}
QVariantList Apps::interfacePairs() {
  QVariantMap containers = testContainers;
#ifdef _WIN32
  if (containers.isEmpty() && !testMode) for (const QVariant& e : wsScan()["endpoints"].toList()) { const QVariantMap m = e.toMap(); containers[m["id"].toString()] = m["container"]; }
#endif
  return pairInterfaces(engine_->devices(), containers);
}
