#include "Engine.h"

#include "license/license.hpp"
#include "audiomixer/driver_intel.hpp"
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

Engine::Engine(QObject* parent) : QObject(parent) {
  strips_.resize(kStrips);
  for (int i = 0; i < kStrips; ++i) strips_[static_cast<std::size_t>(i)].name = QStringLiteral("CH %1").arg(i + 1, 2, 10, QLatin1Char('0'));
  for (int k = 0; k < kBuses; ++k) buses_[k].name = QStringLiteral("BUS %1").arg(k + 1);
  for (int g = 0; g < kGroups; ++g) { dcas_[g].name = QStringLiteral("DCA %1").arg(g + 1); muteGroups_[g].name = QStringLiteral("MUTE %1").arg(g + 1); }
  load();
  refreshDevices();
  timer_.setInterval(33);
  vuRef_ = std::clamp(settings_.value("ui/vuRef", -18).toInt(), -24, -8);
  for (int i = 0; i < kStrips; ++i) inRoute_[i] = i;
  for (int o = 0; o < kOutputs; ++o) outRoute_[o] = am::Runtime::defaultOutputSource(o);
  refreshLicense();
  histTimer_.setSingleShot(true); histTimer_.setInterval(400); connect(&histTimer_, &QTimer::timeout, this, &Engine::recordHistory);
  for (auto sig : {&Engine::stripsChanged, &Engine::busesChanged, &Engine::groupsChanged, &Engine::monitorChanged, &Engine::routingChanged}) connect(this, sig, this, [this] { if (!restoring_) histTimer_.start(); });
  history_.push_back(consoleJson()); histPos_ = 0;
  graveTimer_.setSingleShot(true); graveTimer_.setInterval(1500); connect(&graveTimer_, &QTimer::timeout, this, [this] { graveyard_.clear(); fxGraveyard_.clear(); });
  taps_.b.store(&scope_); sources_.b.store(&gen_);   // removed plug-ins die on the UI thread, after the audio thread has let go
  rescanVst(settings_.value("vst3/dirs").toStringList() + settings_.value("vst2/dirs").toStringList());
  connect(&timer_, &QTimer::timeout, this, &Engine::tick);
  tick();
}
Engine::~Engine() { save(); stop(); }

QStringList Engine::backends() const { QStringList l; for (const auto& b : am::availableBackends()) l << QString::fromStdString(b); return l; }

void Engine::refreshDevices() {
  devices_.clear();
  for (const am::DeviceInfo& d : am::listDevices()) {
    QVariantMap m; m["backend"] = QString::fromStdString(d.backend); m["id"] = QString::fromStdString(d.id); m["name"] = QString::fromStdString(d.name);
    m["inChannels"] = d.inChannels; m["outChannels"] = d.outChannels; m["defaultRate"] = d.defaultRate; m["defaultIn"] = d.defaultIn; m["defaultOut"] = d.defaultOut;
    QVariantList rates; for (double r : d.sampleRates) rates << r; m["sampleRates"] = rates;
    devices_ << m;
  }
  emit devicesChanged();
}

bool Engine::open(const QString& backend, const QString& inDevice, const QString& outDevice, double sampleRate, int frames, int inChannels, int outChannels) {
  stop();
  rt_ = std::make_unique<am::Runtime>(kStrips);
  am::DeviceConfig cfg; cfg.backend = backend.isEmpty() ? "auto" : backend.toStdString(); cfg.inDevice = inDevice.toStdString(); cfg.outDevice = outDevice.toStdString();
  cfg.sampleRate = sampleRate > 0 ? sampleRate : 48000; cfg.frames = static_cast<unsigned>(frames > 0 ? frames : 256); blockFrames_ = static_cast<int>(cfg.frames); cfg.inChannels = inChannels; cfg.outChannels = outChannels;
  devIn_ = std::clamp(inChannels, 0, kStrips); devOut_ = std::clamp(outChannels, 0, kOutputs);
  std::string err;
  if (!rt_->open(cfg, err) || !rt_->start(err)) { status_ = QStringLiteral("Could not open the sound card: %1").arg(QString::fromStdString(err)); rt_.reset(); emit stateChanged(); return false; }
  rt_->setExtraSource(&sources_);
  for (auto& s : strips_) s.dsp.setSampleRate(cfg.sampleRate);
  reloadAllVst(); pushAll();
  status_ = QStringLiteral("%1 • %2 Hz • %3 frames • %4 in / %5 out").arg(QString::fromStdString(rt_->backendName())).arg(cfg.sampleRate).arg(cfg.frames).arg(inChannels).arg(outChannels);
  timer_.start(); emit stateChanged(); return true;
}
void Engine::stop() { if (recorder_.recording()) stopRecording(); if (rt_) { rt_->setExtraSource(nullptr); rt_->setTap(nullptr); for (int k = 0; k < kBuses; ++k) rt_->setBusProcessor(k, nullptr); rt_->stop(); rt_.reset(); status_ = QStringLiteral("Stopped"); timer_.stop(); emit stateChanged(); } }

void Engine::pushAll() {
  if (!rt_) return;
  for (int k = 0; k < kBuses; ++k) { rt_->setBusLevel(k, buses_[k].db); rt_->setBusMute(k, buses_[k].mute); rt_->setBusReturn(k, buses_[k].returnDb); }
  for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].automix) rt_->setAutomix(i, true, strips_[static_cast<std::size_t>(i)].amWeight);
  pushAutoLevel();
  for (int i = 0; i < kStrips; ++i) {
    const StripUi& s = strips_[static_cast<std::size_t>(i)];
    pushStrip(i); rt_->setPan(i, s.pan); rt_->setSolo(i, s.solo); rt_->setTrimDb(i, s.trimDb);
    for (int k = 0; k < kBuses; ++k) rt_->setSend(i, k, s.sends[k].db, s.sends[k].pre);
    const am::dsp::ChannelParams& p = s.dsp.params();
    for (int b = 0; b < am::dsp::kEqBands; ++b) { const auto& e = p.eq[static_cast<std::size_t>(b)]; rt_->setEqBand(i, b, e.type, e.freq, e.q, e.gainDb); }
    rt_->setDelayMs(i, p.delayMs); rt_->setPolarity(i, p.polarityInvert); rt_->setCrossover(i, p.xoverType, p.hpOrder, p.hpFreq, p.lpOrder, p.lpFreq); rt_->setChannelLimiter(i, p.limiterOn, p.limiterCeilDb, p.limiterReleaseMs);
    rt_->setCompressor(i, p.compOn, p.compThresholdDb, p.compRatio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, p.compMakeupDb); rt_->setGate(i, p.gateOn, p.gateThresholdDb, p.gateRangeDb, p.gateAttackMs, p.gateHoldMs, p.gateReleaseMs);
  }
  rt_->setMasterDb(masterDb_); rt_->setLimiter(masterLim_, masterCeil_);
  rt_->setMonitor(mon_.source, mon_.db, mon_.dim, mon_.mono, mon_.mute); rt_->setClipDb(clipDb_); rt_->setPeakHoldMs(peakHoldMs_); rt_->setWidth(width_);
  scope_.setSampleRate(rt_->sampleRate()); rt_->setTap(&taps_);
  for (int k = 0; k < kBuses; ++k) makeBusFx(k);
  for (int i = 0; i < kStrips; ++i) rebuildInserts(i);
  for (int i = 0; i < kStrips; ++i) rt_->setInputRoute(i, inRoute_[i]);
  for (int o = 0; o < kOutputs; ++o) rt_->setOutputRoute(o, outRoute_[o]);
}

QVariantList Engine::stripState() const {
  QVariantList l;
  for (int i = 0; i < kStrips; ++i) { const StripUi& s = strips_[static_cast<std::size_t>(i)]; QVariantMap m; m["index"] = i; m["name"] = s.name; m["db"] = s.db; m["pan"] = s.pan; m["mute"] = s.mute; m["solo"] = s.solo; m["dsp"] = s.dsp.active(); m["dca"] = s.dca; m["muteGroups"] = static_cast<int>(s.muteGroups); m["effectiveDb"] = effectiveDb(i); m["effectiveMute"] = effectiveMute(i); m["color"] = s.color; m["plugins"] = pluginCount(i); m["link"] = s.link; m["trimDb"] = s.trimDb; m["armed"] = ((armed_ >> i) & 1u) != 0; m["automix"] = s.automix; m["amWeight"] = s.amWeight;
    const auto& p = s.dsp.params(); bool eqOn = false; for (const auto& b : p.eq) eqOn = eqOn || b.type != am::dsp::FilterType::Off;
    m["gateOn"] = p.gateOn; m["compOn"] = p.compOn; m["limOn"] = p.limiterOn; m["eqOn"] = eqOn; m["xoverOn"] = p.hpOrder > 0 || p.lpOrder > 0; m["delayOn"] = p.delayMs > 0; m["polarity"] = p.polarityInvert;
    QVariantList sb; for (int k = 0; k < kBuses; ++k) if (s.sends[k].db > -60) sb << (k + 1); m["sendsTo"] = sb; l << m; }
  return l;
}
// ── aux sends ──
QVariantList Engine::busState() const { QVariantList l; for (int k = 0; k < kBuses; ++k) { QVariantMap m; m["index"] = k; m["name"] = buses_[k].name; m["db"] = buses_[k].db; m["returnDb"] = buses_[k].returnDb; m["mute"] = buses_[k].mute; m["outputs"] = QStringLiteral("OUT %1-%2").arg(3 + 2 * k).arg(4 + 2 * k); m["fx"] = buses_[k].fxKind; m["fxName"] = buses_[k].fx ? QString::fromLatin1(buses_[k].fx->name()) : QString(); l << m; } return l; }
void Engine::setSend(int i, int k, double db, bool pre) { if (i < 0 || i >= kStrips || k < 0 || k >= kBuses) return; SendUi& sd = strips_[static_cast<std::size_t>(i)].sends[k]; sd.db = std::clamp(db, -60.0, 10.0); sd.pre = pre; if (rt_) rt_->setSend(i, k, sd.db, sd.pre); emit stripsChanged(); }
QVariantMap Engine::send(int i, int k) const { QVariantMap m; if (i < 0 || i >= kStrips || k < 0 || k >= kBuses) return m; const SendUi& sd = strips_[static_cast<std::size_t>(i)].sends[k]; m["db"] = sd.db; m["pre"] = sd.pre; return m; }
void Engine::setBusLevel(int k, double db) { if (k < 0 || k >= kBuses) return; buses_[k].db = std::clamp(db, -60.0, 10.0); if (rt_) rt_->setBusLevel(k, buses_[k].db); emit busesChanged(); }
void Engine::setBusMute(int k, bool on) { if (k < 0 || k >= kBuses) return; buses_[k].mute = on; if (rt_) rt_->setBusMute(k, on); emit busesChanged(); }
void Engine::setBusReturn(int k, double db) { if (k < 0 || k >= kBuses) return; buses_[k].returnDb = std::clamp(db, -60.0, 10.0); if (rt_) rt_->setBusReturn(k, buses_[k].returnDb); emit busesChanged(); }
void Engine::setBusName(int k, const QString& n) { if (k < 0 || k >= kBuses) return; buses_[k].name = n.left(16); emit busesChanged(); }
// ── group faders ──
QVariantList Engine::dcaState() const {
  QVariantList l;
  for (int g = 0; g < kGroups; ++g) { QVariantMap m; m["index"] = g + 1; m["name"] = dcas_[g].name; m["db"] = dcas_[g].db; m["mute"] = dcas_[g].mute; QVariantList mem; for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].dca == g + 1) mem << i; m["members"] = mem; l << m; }
  return l;
}
QVariantList Engine::muteGroupState() const {
  QVariantList l;
  for (int g = 0; g < kGroups; ++g) { QVariantMap m; m["index"] = g + 1; m["name"] = muteGroups_[g].name; m["active"] = muteGroups_[g].active; QVariantList mem; for (int i = 0; i < kStrips; ++i) if ((strips_[static_cast<std::size_t>(i)].muteGroups >> g) & 1u) mem << i; m["members"] = mem; l << m; }
  return l;
}
void Engine::setDca(int i, int g) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dca = std::clamp(g, 0, kGroups); pushStrip(i); emit stripsChanged(); emit groupsChanged(); }
void Engine::setDcaLevel(int g, double db) { if (g < 1 || g > kGroups) return; dcas_[g - 1].db = std::clamp(db, -60.0, 10.0); for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].dca == g) pushStrip(i); emit stripsChanged(); emit groupsChanged(); }
void Engine::setDcaMute(int g, bool on) { if (g < 1 || g > kGroups) return; dcas_[g - 1].mute = on; for (int i = 0; i < kStrips; ++i) if (strips_[static_cast<std::size_t>(i)].dca == g) pushStrip(i); emit stripsChanged(); emit groupsChanged(); }
void Engine::setDcaName(int g, const QString& n) { if (g < 1 || g > kGroups) return; dcas_[g - 1].name = n.left(16); emit groupsChanged(); }
void Engine::setMuteGroupMember(int i, int g, bool member) { if (i < 0 || i >= kStrips || g < 1 || g > kGroups) return; unsigned& m = strips_[static_cast<std::size_t>(i)].muteGroups; if (member) m |= 1u << (g - 1); else m &= ~(1u << (g - 1)); pushStrip(i); emit stripsChanged(); emit groupsChanged(); }
void Engine::setMuteGroupActive(int g, bool on) { if (g < 1 || g > kGroups) return; muteGroups_[g - 1].active = on; for (int i = 0; i < kStrips; ++i) pushStrip(i); emit stripsChanged(); emit groupsChanged(); }
// effective = the strip's own fader + its DCA's fader (in dB), muted when the strip, its DCA or an active mute group it belongs to is muted
double Engine::effectiveDb(int i) const { if (i < 0 || i >= kStrips) return -60; const StripUi& s = strips_[static_cast<std::size_t>(i)]; const double g = s.dca > 0 ? dcas_[s.dca - 1].db : 0; return std::clamp(s.db + g, -60.0, 10.0); }
bool Engine::effectiveMute(int i) const { if (i < 0 || i >= kStrips) return true; if (i >= licensedChannels_) return true; const StripUi& s = strips_[static_cast<std::size_t>(i)]; if (s.mute) return true; if (s.dca > 0 && dcas_[s.dca - 1].mute) return true; for (int g = 0; g < kGroups; ++g) if ((s.muteGroups >> g) & 1u && muteGroups_[g].active) return true; return false; }
void Engine::pushStrip(int i) { if (!rt_) return; rt_->setGainDb(i, effectiveDb(i)); rt_->setMute(i, effectiveMute(i)); }
void Engine::setGain(int i, double db) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].db = std::clamp(db, -60.0, 10.0); pushStrip(i); if (linked(i)) { strips_[static_cast<std::size_t>(partner(i))].db = strips_[static_cast<std::size_t>(i)].db; pushStrip(partner(i)); } emit stripsChanged(); }
void Engine::setPan(int i, double p) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].pan = std::clamp(p, -1.0, 1.0); if (rt_) rt_->setPan(i, strips_[static_cast<std::size_t>(i)].pan); if (linked(i)) { const int j = partner(i); strips_[static_cast<std::size_t>(j)].pan = -strips_[static_cast<std::size_t>(i)].pan; if (rt_) rt_->setPan(j, strips_[static_cast<std::size_t>(j)].pan); } emit stripsChanged(); }
void Engine::setMute(int i, bool on) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].mute = on; pushStrip(i); if (linked(i)) { strips_[static_cast<std::size_t>(partner(i))].mute = on; pushStrip(partner(i)); } emit stripsChanged(); }
void Engine::setSolo(int i, bool on) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].solo = on; if (rt_) rt_->setSolo(i, on); if (linked(i)) { const int j = partner(i); strips_[static_cast<std::size_t>(j)].solo = on; if (rt_) rt_->setSolo(j, on); } emit stripsChanged(); }
void Engine::setName(int i, const QString& n) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].name = n.left(24); emit stripsChanged(); }
void Engine::setColor(int i, int c) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].color = std::clamp(c, 0, 7); emit stripsChanged(); }
void Engine::setMaster(double db) { masterDb_ = std::clamp(db, -60.0, 10.0); if (rt_) rt_->setMasterDb(masterDb_); emit stripsChanged(); }
void Engine::setMasterLimiter(bool on, double c) { masterLim_ = on; masterCeil_ = c; if (rt_) rt_->setLimiter(on, c); emit stripsChanged(); }
void Engine::clearClips() { if (rt_) rt_->clearClips(); }
// ---- licence (engine/cpp/app/license): the vendor public key is compiled in; keys and tokens are verified offline
static QVariantMap licenseMap(const am::lic::Status& st) {
  QVariantMap m; m["state"] = QString::fromStdString(st.state); m["plan"] = QString::fromStdString(st.planId); m["planName"] = QString::fromStdString(st.planName); m["channels"] = st.channels; m["source"] = QString::fromStdString(st.source);
  m["id"] = QString::fromStdString(st.id); m["name"] = QString::fromStdString(st.name); m["serial"] = QString::fromStdString(st.serial); m["editionName"] = QString::fromStdString(st.editionName); m["expires"] = static_cast<qlonglong>(st.expires); m["boundToThisPc"] = st.boundToThisPc; m["machine"] = QString::fromStdString(st.machine); m["message"] = QString::fromStdString(st.message);
  return m;
}
QString Engine::edition() const { return license_.value("planName", "BASIC").toString(); }
void Engine::refreshLicense() {
  const am::lic::Status st = am::lic::status(QDateTime::currentMSecsSinceEpoch());
  license_ = licenseMap(st); const int before = licensedChannels_; licensedChannels_ = std::clamp(st.channels, 1, kStrips);
  if (before != licensedChannels_ && rt_) for (int i = 0; i < kStrips; ++i) pushStrip(i);
  emit licenseChanged(); if (before != licensedChannels_) emit stripsChanged();
}
QVariantMap Engine::checkKey(const QString& key) const {
  const qint64 now = QDateTime::currentMSecsSinceEpoch(); const am::lic::Verdict v = am::lic::verifyKey(key.toStdString(), now, am::lic::machineId());
  QVariantMap m; m["valid"] = v.ok; m["reason"] = QString::fromStdString(v.reason); m["message"] = QString::fromStdString(v.message); m["genuine"] = v.reason != "bad-signature" && v.reason != "malformed";
  if (!v.ok) return m;
  const am::lic::Plan* cur = am::lic::plan(license_.value("plan", "basic").toString().toStdString()); const am::lic::Plan* to = am::lic::plan(v.planId); if (!cur) cur = &am::lic::basic();
  const int d = to->rank - cur->rank; m["plan"] = QString::fromLatin1(to->id); m["planName"] = QString::fromLatin1(to->name); m["channels"] = to->channels; m["change"] = d > 0 ? "upgrade" : d < 0 ? "downgrade" : "same"; m["canActivate"] = d >= 0;
  m["name"] = QString::fromStdString(v.payload["name"].asString()); m["id"] = QString::fromStdString(v.payload["id"].asString()); m["boundToThisPc"] = v.payload.has("mid"); m["expires"] = static_cast<qlonglong>(v.payload["expires"].asNumber(0));
  m["message"] = d > 0 ? QStringLiteral("Genuine key: upgrades %1 to %2 (%3 channels).").arg(QString::fromLatin1(cur->name), QString::fromLatin1(to->name)).arg(to->channels) : d == 0 ? QStringLiteral("Genuine key for the %1 plan you already have (renews it).").arg(QString::fromLatin1(to->name)) : QStringLiteral("Genuine key, but %1 is lower than your %2 plan: activating it would downgrade.").arg(QString::fromLatin1(to->name), QString::fromLatin1(cur->name));
  return m;
}
QVariantMap Engine::activateKey(const QString& key) {
  const am::lic::Verdict v = am::lic::activate(key.toStdString(), QDateTime::currentMSecsSinceEpoch());
  QVariantMap m; m["ok"] = v.ok; m["reason"] = QString::fromStdString(v.reason); m["message"] = QString::fromStdString(v.message);
  if (v.ok) { refreshLicense(); m["message"] = QStringLiteral("%1 activated on this computer.").arg(edition()); }
  return m;
}
bool Engine::deactivateKey() { const bool r = am::lic::deactivate(); refreshLicense(); return r; }
QString Engine::licenseRequest(const QString& plan, const QString& name, const QString& email) const { return QString::fromStdString(am::lic::request(plan.toStdString(), name.toStdString(), email.toStdString(), QDateTime::currentMSecsSinceEpoch(), AMIX_VERSION).dump()); }
QString Engine::vendorKeyFingerprint() const { return QString::fromStdString(am::lic::fingerprint(am::lic::kVendorJwk)); }

// ---- driver intelligence
static QVariantMap attemptMap(const am::drv::Attempt& a) { QVariantMap m; m["mode"] = QString::fromStdString(a.mode); m["backend"] = QString::fromStdString(a.backend); m["in"] = QString::fromStdString(a.inDevice); m["out"] = QString::fromStdString(a.outDevice); m["exclusive"] = a.exclusive; m["why"] = QString::fromStdString(a.why); m["fromLock"] = a.fromLock; m["pinned"] = a.pinned; return m; }
static std::vector<am::DeviceInfo> devInfos(const QVariantList& l) { std::vector<am::DeviceInfo> v; for (const QVariant& x : l) { const QVariantMap m = x.toMap(); am::DeviceInfo d; d.backend = m["backend"].toString().toStdString(); d.id = m["id"].toString().toStdString(); d.name = m["name"].toString().toStdString(); d.inChannels = m["inChannels"].toInt(); d.outChannels = m["outChannels"].toInt(); d.defaultIn = m["defaultIn"].toBool(); d.defaultOut = m["defaultOut"].toBool(); d.defaultRate = m["defaultRate"].toDouble(); v.push_back(d); } return v; }
QVariantList Engine::driverPlan(const QString& backend, const QString& in, const QString& out) const { QVariantList l; for (const auto& a : am::drv::plan(backend.toStdString(), in.toStdString(), out.toStdString(), devInfos(devices_), QDateTime::currentMSecsSinceEpoch())) l << attemptMap(a); return l; }
QVariantList Engine::driverLocks() const { QVariantList l; for (const auto& k : am::drv::locks()) { QVariantMap m; m["name"] = QString::fromStdString(k.name); m["mode"] = QString::fromStdString(k.mode); m["device"] = QString::fromStdString(k.device); m["reason"] = QString::fromStdString(k.reason); m["at"] = static_cast<qlonglong>(k.at); m["retryAt"] = k.mode == "asio" ? 0 : static_cast<qlonglong>(k.at + am::drv::kRetryMs); l << m; } return l; }
QVariantList Engine::driverPins() const { QVariantList l; for (const auto& p : am::drv::pins(devInfos(devices_))) { QVariantMap m; m["name"] = QString::fromStdString(p.name); m["on"] = p.on; m["reason"] = QString::fromStdString(p.reason); l << m; } return l; }
void Engine::setDriverPin(const QString& name, bool on) { am::drv::setPin(name.toStdString(), on); emit devicesChanged(); }
int Engine::forgetDriverLocks(const QString& name) { const int n = am::drv::forget(name.toStdString()); emit devicesChanged(); return n; }
bool Engine::openSmart(const QString& backend, const QString& in, const QString& out, double rate, int frames, int inCh, int outCh) {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const auto steps = am::drv::plan(backend.toStdString(), in.toStdString(), out.toStdString(), devInfos(devices_), now);
  QStringList tried; const QString asioName = backend == "asio" ? (out.isEmpty() || out == "-" ? in : out) : QString();
  for (std::size_t i = 0; i < steps.size(); ++i) {
    const auto& a = steps[i];
    stop(); rt_ = std::make_unique<am::Runtime>(kStrips);
    am::DeviceConfig cfg; cfg.backend = a.backend; cfg.inDevice = a.inDevice; cfg.outDevice = a.outDevice; cfg.sampleRate = rate > 0 ? rate : 48000; cfg.frames = static_cast<unsigned>(frames > 0 ? frames : 256); blockFrames_ = static_cast<int>(cfg.frames); cfg.inChannels = inCh; cfg.outChannels = outCh; cfg.exclusive = a.exclusive;
    devIn_ = std::clamp(inCh, 0, kStrips); devOut_ = std::clamp(outCh, 0, kOutputs);
    std::string err;
    if (rt_->open(cfg, err) && rt_->start(err)) {
      rt_->setExtraSource(&sources_);
      for (auto& st : strips_) st.dsp.setSampleRate(cfg.sampleRate);
      reloadAllVst(); pushAll();
      status_ = QStringLiteral("%1 • %2 Hz • %3 frames • %4 in / %5 out").arg(QString::fromStdString(rt_->backendName())).arg(cfg.sampleRate).arg(cfg.frames).arg(inCh).arg(outCh);
      const QString mode = QString::fromStdString(a.mode).toUpper();
      if (a.pinned) lastRoute_ = QStringLiteral("REALTEK PIN: %1 → %2 (%3). The ASIO driver was not opened.").arg(asioName, mode, QString::fromStdString(a.inDevice != "-" ? a.inDevice : a.outDevice));
      else if (a.fromLock) lastRoute_ = QStringLiteral("AI DRIVER LOCK: %1 went straight to %2 (learned earlier: %3).").arg(asioName, mode, QString::fromStdString(a.why));
      else if (i > 0) { lastRoute_ = QStringLiteral("FALLBACK: %1 failed (%2) → %3 works. Locked for next time.").arg(tried.join("; "), QString::fromStdString(err), mode); if (!asioName.isEmpty()) am::drv::learn(asioName.toStdString(), a.mode, a.inDevice != "-" ? a.inDevice : a.outDevice, tried.join("; ").toStdString(), now); }
      else { lastRoute_ = QStringLiteral("%1 opened first try.").arg(mode); if (!asioName.isEmpty() && a.mode == "asio") am::drv::learn(asioName.toStdString(), "asio", asioName.toStdString(), "clean run", now); }
      timer_.start(); emit stateChanged(); return true;
    }
    tried << QStringLiteral("%1: %2").arg(QString::fromStdString(a.mode).toUpper(), QString::fromStdString(err)); rt_.reset();
  }
  status_ = QStringLiteral("Could not open the sound card on any path: %1").arg(tried.join(" | ")); lastRoute_ = status_; emit stateChanged(); return false;
}

QVariantList Engine::inputRoutes() const { QVariantList l; for (int i = 0; i < kStrips; ++i) l << inRoute_[i]; return l; }
QVariantList Engine::outputRoutes() const { QVariantList l; for (int o = 0; o < kOutputs; ++o) l << outRoute_[o]; return l; }
QVariantList Engine::outputSources() const {
  QVariantList l; auto add = [&](int idx, const QString& name, const QString& group) { QVariantMap m; m["index"] = idx; m["name"] = name; m["group"] = group; l << m; };
  add(am::Runtime::kSrcOff, "OFF", "off"); add(am::Runtime::kSrcMasterL, "MASTER L", "master"); add(am::Runtime::kSrcMasterR, "MASTER R", "master"); add(am::Runtime::kSrcMonL, "MONITOR L", "monitor"); add(am::Runtime::kSrcMonR, "MONITOR R", "monitor");
  for (int k = 0; k < kBuses; ++k) { add(am::Runtime::kSrcBus0L + 2 * k, buses_[k].name + " L", "bus"); add(am::Runtime::kSrcBus0L + 2 * k + 1, buses_[k].name + " R", "bus"); }
  return l;
}
void Engine::setInputRoute(int strip, int input) { if (strip < 0 || strip >= kStrips) return; inRoute_[strip] = std::clamp(input, -1, kStrips - 1); if (rt_) rt_->setInputRoute(strip, inRoute_[strip]); emit routingChanged(); }
void Engine::setOutputRoute(int out, int source) { if (out < 0 || out >= kOutputs) return; outRoute_[out] = std::clamp(source, 0, am::Runtime::kSrcBus0L + 2 * kBuses - 1); if (rt_) rt_->setOutputRoute(out, outRoute_[out]); emit routingChanged(); }
void Engine::resetRouting() { for (int i = 0; i < kStrips; ++i) inRoute_[i] = i; for (int o = 0; o < kOutputs; ++o) outRoute_[o] = am::Runtime::defaultOutputSource(o); if (rt_) { for (int i = 0; i < kStrips; ++i) rt_->setInputRoute(i, inRoute_[i]); for (int o = 0; o < kOutputs; ++o) rt_->setOutputRoute(o, outRoute_[o]); } emit routingChanged(); }
// ---- bus effects
QVariantList Engine::fxCatalog() const { QVariantList l; for (const auto& [k, n] : am::fx::catalog()) { QVariantMap m; m["kind"] = QString::fromStdString(k); m["name"] = QString::fromStdString(n); l << m; } return l; }
void Engine::makeBusFx(int k) {
  BusUi& b = buses_[k]; if (rt_) rt_->setBusProcessor(k, nullptr); if (b.fx) fxGraveyard_.push_back(std::move(b.fx));
  if (!b.fxKind.isEmpty()) { b.fx = am::fx::make(b.fxKind.toStdString(), rt_ ? rt_->sampleRate() : 48000); if (b.fx) { for (const auto& [id, v] : b.fxValues) b.fx->set(id.toStdString(), v); b.fx->setMix(b.fxMix); b.fx->setBypass(b.fxBypass); if (rt_) rt_->setBusProcessor(k, b.fx.get()); } }
  graveTimer_.start();
}
QVariantMap Engine::busFx(int k) const {
  QVariantMap m; if (k < 0 || k >= kBuses) return m; const BusUi& b = buses_[k]; m["kind"] = b.fxKind; m["bypass"] = b.fxBypass; m["mix"] = b.fxMix; m["name"] = b.fx ? QString::fromLatin1(b.fx->name()) : QString();
  QVariantList ps; if (b.fx) for (const auto& p : b.fx->params()) { QVariantMap x; x["id"] = QString::fromStdString(p.id); x["title"] = QString::fromStdString(p.title); x["units"] = QString::fromStdString(p.units); x["value"] = p.value; x["default"] = p.def; x["text"] = QString::fromStdString(b.fx->text(p.id, p.value)); ps << x; } m["params"] = ps;
  return m;
}
void Engine::setBusFx(int k, const QString& kind) { if (k < 0 || k >= kBuses) return; BusUi& b = buses_[k]; if (b.fxKind == kind) return; b.fxKind = kind; b.fxValues.clear(); b.fxBypass = false; makeBusFx(k); if (!kind.isEmpty() && b.returnDb <= -60) { b.returnDb = 0; if (rt_) rt_->setBusReturn(k, 0); } emit busesChanged(); }
void Engine::setBusFxParam(int k, const QString& id, double v) { if (k < 0 || k >= kBuses) return; BusUi& b = buses_[k]; b.fxValues[id] = std::clamp(v, 0.0, 1.0); if (b.fx) b.fx->set(id.toStdString(), b.fxValues[id]); emit busesChanged(); }
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
QVariantList Engine::spectrum(int ch) { std::vector<float> b; scope_.spectrum(ch, b); QVariantList l; for (float v : b) l << v; return l; }
static constexpr double kIsoCentres[10] = {31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
void Engine::setGraphicEq(int i, const QVariantList& gains, bool on) {
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
QVariantMap Engine::monitor() const { QVariantMap m; m["source"] = mon_.source; m["db"] = mon_.db; m["dim"] = mon_.dim; m["mono"] = mon_.mono; m["mute"] = mon_.mute; return m; }
void Engine::setMonitor(int source, double db, bool dim, bool mono, bool mute) {
  mon_.source = std::clamp(source, 0, kBuses); mon_.db = std::clamp(db, -60.0, 10.0); mon_.dim = dim; mon_.mono = mono; mon_.mute = mute;
  if (rt_) rt_->setMonitor(mon_.source, mon_.db, mon_.dim, mon_.mono, mon_.mute);
  emit monitorChanged();
}
void Engine::resetStripDsp(int i) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dsp.set(am::dsp::ChannelParams{}); if (rt_) pushAll(); emit stripsChanged(); }
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
void Engine::factoryReset() {
  for (int i = 0; i < kStrips; ++i) { StripUi& s = strips_[static_cast<std::size_t>(i)]; for (PluginUi& p : s.plugins) if (p.vst) graveyard_.push_back(p.vst); s = StripUi{}; s.name = QStringLiteral("CH %1").arg(i + 1, 2, 10, QLatin1Char('0')); s.dsp.setSampleRate(48000); }
  for (int k = 0; k < kBuses; ++k) { if (rt_) rt_->setBusProcessor(k, nullptr); if (buses_[k].fx) fxGraveyard_.push_back(std::move(buses_[k].fx)); buses_[k] = BusUi{}; buses_[k].name = QStringLiteral("BUS %1").arg(k + 1); } width_ = 1.0;
  for (DcaUi& d : dcas_) d = DcaUi{}; for (int g = 0; g < kGroups; ++g) dcas_[g].name = QStringLiteral("DCA %1").arg(g + 1);
  for (MuteGroupUi& m : muteGroups_) m = MuteGroupUi{}; for (int g = 0; g < kGroups; ++g) muteGroups_[g].name = QStringLiteral("MUTE %1").arg(g + 1);
  masterDb_ = 0; masterLim_ = true; masterCeil_ = -0.5; mon_ = MonitorUi{}; for (int i = 0; i < kStrips; ++i) inRoute_[i] = i; for (int o = 0; o < kOutputs; ++o) outRoute_[o] = am::Runtime::defaultOutputSource(o);
  for (SceneUi& x : scenes_) x = SceneUi{}; currentScene_ = -1; recallSafe_ = 0; userPresets_.clear(); selected_ = 0; clipboard_.reset(); history_.clear(); histPos_ = -1; abOther_ = QJsonObject(); abSlot_ = "A";
  graveTimer_.start(); if (rt_) pushAll(); save(); history_.push_back(consoleJson()); histPos_ = 0;
  emit stripsChanged(); emit busesChanged(); emit groupsChanged(); emit scenesChanged(); emit presetsChanged(); emit selectedStripChanged(); emit monitorChanged(); emit routingChanged(); emit clipboardChanged(); emit historyChanged();
}
QString Engine::recordFolder() const { const QString d = settings_.value("record/folder").toString(); return d.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/A mix OS Recordings" : d; }
bool Engine::startRecording() {
  if (!rt_) { recMsg_ = QStringLiteral("Start the engine on CONNECTIONS first."); emit recordChanged(); return false; }
  if (recorder_.recording()) return true;
  am::rec::Options o; o.folder = recordFolder().toStdString(); o.master = recordMaster(); o.float32 = recordFloat(); for (int i = 0; i < kStrips; ++i) if ((armed_ >> i) & 1u) o.strips.push_back(i);
  std::string err; if (!recorder_.start(o, rt_->sampleRate(), err)) { recMsg_ = QString::fromStdString(err); emit recordChanged(); return false; }
  taps_.a.store(&recorder_); recMsg_ = QStringLiteral("Recording %1 file(s) into %2").arg(recorder_.files().size()).arg(recordFolder()); emit recordChanged(); return true;
}
void Engine::stopRecording() { if (!recorder_.recording()) return; taps_.a.store(nullptr); recorder_.stop(); recMsg_ = QStringLiteral("Recorded %1 s, %2 file(s)%3 in %4").arg(recorder_.seconds(), 0, 'f', 1).arg(recorder_.files().size()).arg(recorder_.dropped() ? QStringLiteral(" (%1 blocks DROPPED: disk too slow)").arg(recorder_.dropped()) : QString()).arg(recordFolder()); emit recordChanged(); }
void Engine::setArmed(int i, bool on) { if (i < 0 || i >= kStrips) return; if (on) armed_ |= 1u << i; else armed_ &= ~(1u << i); emit stripsChanged(); emit recordChanged(); }
QVariantMap Engine::autoDetect() const {
  QVariantMap best; int bestRank = -1;
  for (const QVariant& v : devices_) { const QVariantMap d = v.toMap(); const QString b = d["backend"].toString(), name = d["name"].toString(); if (b == "null") continue;
    const bool in = d["inChannels"].toInt() > 0, out = d["outChannels"].toInt() > 0; if (!in && !out) continue;
    int rank = 0; QString why;
    if (b == "asio") { const bool wrapper = am::drv::isWrapper(name.toStdString()); const bool pinned = am::drv::pinOf(name.toStdString()).on; rank = wrapper ? 60 : pinned ? 50 : 100; why = wrapper ? "an ASIO wrapper (ASIO4ALL-style): used when nothing has its own driver" : pinned ? "Realtek onboard: pinned to its Windows endpoint, SMART START routes it" : "the device's own ASIO driver"; }
    else if (b == "wasapi" || b == "coreaudio") { rank = (d["defaultOut"].toBool() ? 40 : 30) + (in && out ? 2 : 0); why = "the system's " + QString(d["defaultOut"].toBool() ? "default " : "") + "endpoint"; }
    else if (b == "alsa") { rank = (name.contains("default") ? 25 : 20); why = "an ALSA device"; }
    if (rank > bestRank) { bestRank = rank; best = d; best["why"] = why; }
  }
  if (best.isEmpty()) return best;
  const QString id = best["id"].toString(); best["in"] = best["inChannels"].toInt() > 0 ? id : "-"; best["out"] = best["outChannels"].toInt() > 0 ? id : "-"; best["rate"] = best["defaultRate"].toDouble() > 0 ? best["defaultRate"].toDouble() : 48000.0;
  return best;
}
bool Engine::autoStart(int frames) {
  const QVariantMap d = autoDetect(); if (d.isEmpty()) { status_ = QStringLiteral("AUTO DETECT: no real sound card found (only the null test device)."); emit stateChanged(); return false; }
  const bool ok = openSmart(d["backend"].toString(), d["in"].toString(), d["out"].toString(), d["rate"].toDouble(), frames > 0 ? frames : 256, std::min(32, std::max(0, d["inChannels"].toInt())), std::min(32, std::max(2, d["outChannels"].toInt())));
  if (ok) lastRoute_ = QStringLiteral("AUTO DETECT picked %1 (%2): %3").arg(d["name"].toString(), d["backend"].toString().toUpper(), d["why"].toString()) + "  ·  " + lastRoute_;
  emit stateChanged(); return ok;
}
void Engine::recordHistory() {
  const QJsonObject now = consoleJson(); if (histPos_ >= 0 && histPos_ < static_cast<int>(history_.size()) && history_[static_cast<std::size_t>(histPos_)] == now) return;
  history_.resize(static_cast<std::size_t>(histPos_ + 1)); history_.push_back(now); if (history_.size() > 100) history_.erase(history_.begin()); histPos_ = static_cast<int>(history_.size()) - 1; emit historyChanged();
}
void Engine::undo() { histTimer_.stop(); if (histPos_ <= 0) return; restoring_ = true; applyConsoleJson(history_[static_cast<std::size_t>(--histPos_)], false); restoring_ = false; emit historyChanged(); }
void Engine::redo() { histTimer_.stop(); if (histPos_ + 1 >= static_cast<int>(history_.size())) return; restoring_ = true; applyConsoleJson(history_[static_cast<std::size_t>(++histPos_)], false); restoring_ = false; emit historyChanged(); }
void Engine::toggleAB() { const QJsonObject cur = consoleJson(); if (abOther_.isEmpty()) abOther_ = cur; const QJsonObject other = abOther_; abOther_ = cur; abSlot_ = abSlot_ == "A" ? "B" : "A"; applyConsoleJson(other, false); emit historyChanged(); }
void Engine::setLink(int i, bool on) { if (i < 0 || i >= kStrips || partner(i) < 0) return; strips_[static_cast<std::size_t>(i)].link = on; strips_[static_cast<std::size_t>(partner(i))].link = on; if (on) { const StripUi& a = strips_[static_cast<std::size_t>(i & ~1)]; StripUi& b = strips_[static_cast<std::size_t>(i | 1)]; b.db = a.db; b.mute = a.mute; b.solo = a.solo; b.pan = -a.pan; pushStrip(i | 1); if (rt_) { rt_->setPan(i | 1, b.pan); rt_->setSolo(i | 1, b.solo); } } emit stripsChanged(); }
bool Engine::linked(int i) const { return i >= 0 && i < kStrips && strips_[static_cast<std::size_t>(i)].link; }
void Engine::setTrim(int i, double db) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].trimDb = std::clamp(db, -24.0, 24.0); if (rt_) rt_->setTrimDb(i, strips_[static_cast<std::size_t>(i)].trimDb); emit stripsChanged(); }
double Engine::trim(int i) const { return i >= 0 && i < kStrips ? strips_[static_cast<std::size_t>(i)].trimDb : 0; }
static bool isVstKind(const QString& k) { return k.startsWith("vst3:") || k.startsWith("vst2:"); }
void Engine::muteAll(bool on) { for (int i = 0; i < kStrips; ++i) { strips_[static_cast<std::size_t>(i)].mute = on; pushStrip(i); } emit stripsChanged(); }
void Engine::clearSolo() { for (int i = 0; i < kStrips; ++i) { strips_[static_cast<std::size_t>(i)].solo = false; if (rt_) rt_->setSolo(i, false); } emit stripsChanged(); }
void Engine::resetFaders() { for (int i = 0; i < kStrips; ++i) { StripUi& s = strips_[static_cast<std::size_t>(i)]; s.db = 0; s.pan = 0; pushStrip(i); if (rt_) rt_->setPan(i, 0); } masterDb_ = 0; if (rt_) rt_->setMasterDb(0); emit stripsChanged(); }
void Engine::flattenAllDsp() { for (int i = 0; i < kStrips; ++i) { strips_[static_cast<std::size_t>(i)].dsp.set(am::dsp::ChannelParams{}); for (PluginUi& p : strips_[static_cast<std::size_t>(i)].plugins) if (!isVstKind(p.kind)) p = PluginUi{}; } if (rt_) pushAll(); emit stripsChanged(); }

static const struct { const char* kind; const char* name; } kBuiltIns[] = {{"gate", "NOISE GATE"}, {"eq", "EQ"}, {"xover", "X-OVER"}, {"comp", "COMPRESSOR"}, {"limiter", "LIMITER"}, {"delay", "DELAY"}, {"polarity", "POLARITY Ø"}};
QVariantList Engine::pluginCatalog() const { QVariantList l; for (const auto& b : kBuiltIns) { QVariantMap m; m["kind"] = b.kind; m["name"] = b.name; m["builtIn"] = true; l << m; } for (const QVariantMap& v : vstCatalog_) l << v; return l; }
QVariantList Engine::vstCatalog() const { QVariantList l; for (const QVariantMap& v : vstCatalog_) l << v; return l; }
int Engine::rescanVst(const QStringList& extraDirs) {
  std::vector<std::string> dirs3 = am::vst3::defaultDirs(), dirs2 = am::vst2::defaultDirs(); for (const QString& d : extraDirs) if (!d.trimmed().isEmpty()) { dirs3.push_back(d.trimmed().toStdString()); dirs2.push_back(d.trimmed().toStdString()); }
  vstCatalog_.clear();
  for (const std::string& mod : am::vst3::scan(dirs3)) { std::vector<am::vst3::ClassInfo> cls; std::string err; if (!am::vst3::inspect(mod, cls, err)) continue;
    for (const auto& c : cls) { QVariantMap m; m["kind"] = QStringLiteral("vst3:%1|%2").arg(QString::fromStdString(mod), QString::fromStdString(c.cid)); m["name"] = QString::fromStdString(c.name); m["vendor"] = QString::fromStdString(c.vendor); m["module"] = QString::fromStdString(mod); m["cid"] = QString::fromStdString(c.cid); m["subCategories"] = QString::fromStdString(c.subCategories); m["format"] = "VST3"; m["builtIn"] = false; vstCatalog_.push_back(m); } }
  for (const std::string& mod : am::vst2::scan(dirs2)) { am::host::Info info; std::string err; if (!am::vst2::inspect(mod, info, err)) continue;   // a .dll / .so that is not a VST2 plug-in is skipped quietly
    QVariantMap m; m["kind"] = QStringLiteral("vst2:%1").arg(QString::fromStdString(mod)); m["name"] = QString::fromStdString(info.name); m["vendor"] = QString::fromStdString(info.vendor); m["module"] = QString::fromStdString(mod); m["cid"] = QString::fromStdString(info.uid); m["subCategories"] = QString::fromStdString(info.category); m["format"] = "VST2"; m["builtIn"] = false; vstCatalog_.push_back(m); }
  emit stripsChanged(); return static_cast<int>(vstCatalog_.size());
}
static void splitVstKind(const QString& kind, QString& module, QString& cid) { const QString k = kind.mid(5); const int bar = k.lastIndexOf('|'); module = bar >= 0 ? k.left(bar) : k; cid = bar >= 0 ? k.mid(bar + 1) : QString(); }
bool Engine::loadVst(int i, int slot) {
  PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; p.vst.reset(); p.error.clear();
  if (!isVstKind(p.kind)) return false;
  QString module, cid; splitVstKind(p.kind, module, cid); std::string err;
  const double rate = rt_ ? rt_->sampleRate() : 48000; const int block = 1024;   // larger runtime blocks are split by the insert adapter
  std::unique_ptr<am::host::Plugin> pl; if (p.kind.startsWith("vst3:")) pl = am::vst3::Plugin::load(module.toStdString(), cid.toStdString(), rate, block, err); else pl = am::vst2::Plugin::load(module.toStdString(), rate, block, err);
  if (!pl) { p.error = QString::fromStdString(err); return false; }
  if (!p.state.isEmpty()) pl->setState(p.state.toStdString());
  pl->setBypass(p.bypass); p.vst = std::move(pl); return true;
}
void Engine::rebuildInserts(int i) {
  if (!rt_) return; auto chain = std::make_shared<am::Runtime::InsertChain>();
  for (const PluginUi& p : strips_[static_cast<std::size_t>(i)].plugins) if (p.vst) chain->push_back(std::make_shared<am::host::MonoInsert>(p.vst, 1024));
  rt_->setInserts(i, chain->empty() ? nullptr : chain);
}
void Engine::reloadAllVst() { for (int i = 0; i < kStrips; ++i) { bool any = false; for (int k = 0; k < kPluginSlots; ++k) { PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[k]; if (isVstKind(p.kind)) { if (p.vst) p.state = QString::fromStdString(p.vst->state()); if (p.vst) graveyard_.push_back(p.vst); loadVst(i, k); any = true; } } if (any) rebuildInserts(i); } graveTimer_.start(); }
QVariantList Engine::pluginParams(int i, int slot) const {
  QVariantList l; if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return l; const PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; if (!p.vst) return l;
  for (const auto& q : p.vst->params()) { if (q.hidden) continue; QVariantMap m; m["id"] = static_cast<int>(q.id); m["title"] = QString::fromStdString(q.title); m["units"] = QString::fromStdString(q.units); m["value"] = q.normalized; m["default"] = q.defaultNormalized; m["text"] = QString::fromStdString(p.vst->paramText(q.id, q.normalized)); m["stepCount"] = q.stepCount; m["bypass"] = q.bypass; m["readOnly"] = q.readOnly; l << m; }
  return l;
}
void Engine::setPluginParam(int i, int slot, int id, double v) { if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return; PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[slot]; if (!p.vst) return; p.vst->setParam(static_cast<std::uint32_t>(id), v); if (p.vst->hasBypass()) p.bypass = p.vst->bypass(); emit stripsChanged(); }
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
  else if (kind == "eq") { if (on) { bool any = false; for (const auto& b : p.eq) any = any || b.type != am::dsp::FilterType::Off; if (!any) setEq(i, 0, static_cast<int>(am::dsp::FilterType::Peaking), 1000, 1.0, 0); } else for (int b = 0; b < am::dsp::kEqBands; ++b) setEq(i, b, static_cast<int>(am::dsp::FilterType::Off), p.eq[static_cast<std::size_t>(b)].freq, p.eq[static_cast<std::size_t>(b)].q, 0); }
}
void Engine::setPlugin(int i, int slot, const QString& kind) {
  if (i < 0 || i >= kStrips || slot < 0 || slot >= kPluginSlots) return; StripUi& s = strips_[static_cast<std::size_t>(i)]; PluginUi& p = s.plugins[slot];
  bool known = kind.isEmpty() || isVstKind(kind); for (const auto& b : kBuiltIns) if (kind == b.kind) known = true; if (!known) return;
  if (!kind.isEmpty() && !isVstKind(kind)) for (int k = 0; k < kPluginSlots; ++k) if (k != slot && s.plugins[k].kind == kind) s.plugins[k] = PluginUi{};   // one slot per built-in stage
  if (!p.kind.isEmpty() && !isVstKind(p.kind) && p.kind != kind) applyPluginStage(i, p.kind, false);
  if (p.vst) { graveyard_.push_back(p.vst); p.vst.reset(); graveTimer_.start(); }
  p.kind = kind.left(1024); p.bypass = false; p.state.clear(); p.error.clear();
  if (!kind.isEmpty() && !isVstKind(kind)) applyPluginStage(i, kind, true);
  if (isVstKind(kind)) loadVst(i, slot);
  rebuildInserts(i);
  emit stripsChanged();
}
void Engine::setPluginBypass(int i, int slot, bool bypass) {
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
  if (i < 0 || i >= kStrips || band < 0 || band >= am::dsp::kEqBands) return;
  const am::dsp::EqBand b{static_cast<FilterType>(std::clamp(type, 0, 8)), std::clamp(f, 10.0, 24000.0), std::clamp(q, 0.1, 36.0), std::clamp(g, -24.0, 24.0)};
  strips_[static_cast<std::size_t>(i)].dsp.setEqBand(band, b);
  if (rt_) rt_->setEqBand(i, band, b.type, b.freq, b.q, b.gainDb);
  emit stripsChanged();
}
QVariantMap Engine::eqBand(int i, int band) const {
  QVariantMap m; if (i < 0 || i >= kStrips || band < 0 || band >= am::dsp::kEqBands) return m;
  const auto& b = strips_[static_cast<std::size_t>(i)].dsp.params().eq[static_cast<std::size_t>(band)];
  m["type"] = static_cast<int>(b.type); m["freq"] = b.freq; m["q"] = b.q; m["gainDb"] = b.gainDb; return m;
}
void Engine::setDelay(int i, double ms) { if (i < 0 || i >= kStrips) return; strips_[static_cast<std::size_t>(i)].dsp.setDelayMs(ms); if (rt_) rt_->setDelayMs(i, strips_[static_cast<std::size_t>(i)].dsp.params().delayMs); emit stripsChanged(); }
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
    {"MIX", "mixer", "MIXER", "☰", true}, {"MIX", "arrange", "ARRANGE", "≡", true}, {"MIX", "dsp", "TONE (EQ)", "∿", true}, {"MIX", "dyn", "COMPRESSOR", "⩏", true}, {"MIX", "gate", "NOISE GATE", "▣", true},
    {"MIX", "fx", "EFFECTS", "✨", true}, {"MIX", "bus", "SEND TO", "⇉", true}, {"MIX", "dca", "GROUP FADERS", "☷", true},
    {"ROUTE", "io", "CONNECTIONS", "⚡", true}, {"ROUTE", "routing", "ROUTING", "⎇", true},
    {"METER", "meter", "LEVELS", "▓", true}, {"METER", "lufs", "LOUDNESS", "◎", true}, {"METER", "osc", "WAVEFORM", "∽", true}, {"METER", "phase", "PHASE", "☯", true},
    {"SOURCE", "media", "STREAM & MEDIA", "▶", false}, {"SOURCE", "live", "LIVE SOURCES", "☉", true}, {"SOURCE", "miceq", "MIC EQ", "♪", true}, {"SOURCE", "ai", "AI MIX", "⚙", true}, {"SOURCE", "tools", "TOOLS", "⚒", true}, {"SOURCE", "plugins", "PLUGINS", "⬡", true},
    {"DEVICE", "asio", "SOUND CARD", "◉", false}, {"DEVICE", "bluetooth", "BLUETOOTH", "⮁", false}, {"DEVICE", "network", "NETWORK", "⮞", true}, {"DEVICE", "cloud", "CLOUD", "☁", false}, {"DEVICE", "cascade", "CASCADE", "⚭", false},
    {"SYSTEM", "presets", "PRESETS", "⬇", true}, {"SYSTEM", "sys", "SETTINGS", "⚙", true}, {"SYSTEM", "acct", "ACCOUNT", "☺", true}, {"SYSTEM", "access", "ACCESS", "⚿", true}, {"SYSTEM", "renew", "LICENSE", "★", true}, {"SYSTEM", "admin", "ADMIN", "⚑", true}, {"SYSTEM", "calib", "CALIBRATE", "⌖", true},
    {"HELP", "help", "HELP", "?", true}, {"HELP", "scenes", "SCENES", "⚐", true}, {"SYSTEM", "boot", "DIAGNOSTICS", "⏻", true},
  };
  QVariantList l; for (const P& p : k) { QVariantMap m; m["group"] = p.group; m["id"] = p.id; m["text"] = p.text; m["icon"] = QString::fromUtf8(p.icon); m["ready"] = p.ready; l << m; }
  return l;
}

void Engine::tick() {
  QVariantList m; QVariantMap t;
  if (rt_) {
    const am::Snapshot s = rt_->snapshot();
    const double vuA = 1.0 - std::exp(-timer_.interval() / 65.0);   // a VU reaches 99 % of a step in 300 ms: a one-pole with tau = 65 ms, run at the timer rate
    for (int i = 0; i < kStrips; ++i) { vu_[i] += (s.strips[i].rms - vu_[i]) * vuA; QVariantMap x; x["peakDb"] = toDb(s.strips[i].pk); x["rmsDb"] = toDb(s.strips[i].rms); x["holdDb"] = toDb(s.strips[i].hold); x["clip"] = s.strips[i].clipN > 0; x["compGrDb"] = s.strips[i].compGrDb; x["gateDb"] = s.strips[i].gateDb; x["inDb"] = toDb(s.strips[i].inPk); x["vuDb"] = toDb(vu_[i]); x["amGainDb"] = s.strips[i].amGainDb; m << x; }
    vuMaster_ += (std::max(s.master[0].rms, s.master[1].rms) - vuMaster_) * vuA; t["masterVuDb"] = toDb(vuMaster_);
    t["monitorLDb"] = toDb(s.monitorPk[0]); t["monitorRDb"] = toDb(s.monitorPk[1]);
    t["masterPeakDb"] = toDb(std::max(s.master[0].pk, s.master[1].pk)); t["masterLDb"] = toDb(s.master[0].pk); t["masterRDb"] = toDb(s.master[1].pk); t["masterRmsDb"] = toDb(std::max(s.master[0].rms, s.master[1].rms));
    t["masterClip"] = s.master[0].clipN + s.master[1].clipN > 0; t["momentary"] = s.momentary; t["shortTerm"] = s.shortTerm; t["integrated"] = s.integrated; t["lra"] = s.lra; t["truePeak"] = s.truePeak; t["correlation"] = s.correlation; t["autoLevelDb"] = s.autoLevelDb;
    t["blocks"] = static_cast<qulonglong>(s.blocks); t["frames"] = static_cast<qulonglong>(s.frames); t["xruns"] = static_cast<qulonglong>(s.xruns);
    QVariantList bm; for (int k = 0; k < kBuses; ++k) { QVariantMap x; x["peakDb"] = toDb(s.buses[k].pk); x["rmsDb"] = toDb(s.buses[k].rms); x["holdDb"] = toDb(s.buses[k].hold); x["clip"] = s.buses[k].clipN > 0; bm << x; } busMeters_ = bm;
  } else {
    QVariantList bm; for (int k = 0; k < kBuses; ++k) { QVariantMap x; x["peakDb"] = -120.0; x["rmsDb"] = -120.0; x["holdDb"] = -120.0; x["clip"] = false; bm << x; } busMeters_ = bm;
    for (int i = 0; i < kStrips; ++i) { vu_[i] = 0; QVariantMap x; x["peakDb"] = -120.0; x["rmsDb"] = -120.0; x["holdDb"] = -120.0; x["clip"] = false; x["compGrDb"] = 0.0; x["gateDb"] = 0.0; x["inDb"] = -120.0; x["vuDb"] = -120.0; m << x; }
    vuMaster_ = 0; t["masterVuDb"] = -120.0; t["monitorLDb"] = -120.0; t["monitorRDb"] = -120.0;
    t["masterPeakDb"] = -120.0; t["masterLDb"] = -120.0; t["masterRDb"] = -120.0; t["masterRmsDb"] = -120.0; t["masterClip"] = false; t["momentary"] = -120.0; t["shortTerm"] = -120.0; t["integrated"] = -120.0; t["lra"] = 0.0; t["truePeak"] = -120.0; t["correlation"] = 0.0; t["autoLevelDb"] = 0.0; t["blocks"] = 0; t["frames"] = 0; t["xruns"] = 0;
  }
  meters_ = m; telemetry_ = t; emit metersChanged();
}

// ── persistence: the console as one JSON document (strips, sends, groups, buses, master). The same document is a SCENE; a strip's DSP part
// is a PRESET. QSettings keeps the current console, the 16 scenes and the user presets.
QJsonObject Engine::stripJson(const StripUi& s) const {
  const auto& p = s.dsp.params(); QJsonObject o; o["name"] = s.name; o["db"] = s.db; o["pan"] = s.pan; o["mute"] = s.mute; o["solo"] = s.solo;
  o["dsp"] = dspJson(p);
  QJsonArray sends; for (const SendUi& sd : s.sends) { QJsonObject e; e["db"] = sd.db; e["pre"] = sd.pre; sends << e; } o["sends"] = sends; o["dca"] = s.dca; o["muteGroups"] = static_cast<int>(s.muteGroups); o["color"] = s.color;
  QJsonArray pl; for (const PluginUi& p : s.plugins) { QJsonObject e; e["kind"] = p.kind; e["bypass"] = p.bypass; if (p.vst) e["state"] = QString::fromStdString(p.vst->state()); else if (!p.state.isEmpty()) e["state"] = p.state; pl << e; } o["plugins"] = pl; o["link"] = s.link; o["trimDb"] = s.trimDb; o["automix"] = s.automix; o["amWeight"] = s.amWeight;
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
void Engine::stripFromJson(StripUi& s, const QJsonObject& o) {
  if (o.contains("name")) s.name = o.value("name").toString().left(24); s.db = std::clamp(o.value("db").toDouble(0), -60.0, 10.0); s.pan = std::clamp(o.value("pan").toDouble(0), -1.0, 1.0); s.mute = o.value("mute").toBool(); s.solo = o.value("solo").toBool();
  s.dsp.set(dspFromJson(o.value("dsp").toObject()));
  const QJsonArray sends = o.value("sends").toArray(); for (int k = 0; k < kBuses; ++k) { if (k < sends.size()) { const QJsonObject e = sends[k].toObject(); s.sends[k].db = std::clamp(e.value("db").toDouble(-60), -60.0, 10.0); s.sends[k].pre = e.value("pre").toBool(); } else s.sends[k] = SendUi{}; }
  s.dca = std::clamp(o.value("dca").toInt(0), 0, kGroups); s.muteGroups = static_cast<unsigned>(std::clamp(o.value("muteGroups").toInt(0), 0, 255)); s.color = std::clamp(o.value("color").toInt(0), 0, 7);
  s.link = o.value("link").toBool(); s.trimDb = std::clamp(o.value("trimDb").toDouble(0), -24.0, 24.0); s.automix = o.value("automix").toBool(); s.amWeight = std::clamp(o.value("amWeight").toDouble(1), 0.25, 4.0);
  const QJsonArray pl = o.value("plugins").toArray(); for (int k = 0; k < kPluginSlots; ++k) { if (k < pl.size()) { const QJsonObject e = pl[k].toObject(); const QString kind = e.value("kind").toString().left(1024); if (s.plugins[k].kind != kind) s.plugins[k].vst.reset(); s.plugins[k].kind = kind; s.plugins[k].bypass = e.value("bypass").toBool(); s.plugins[k].state = e.value("state").toString(); } else s.plugins[k] = PluginUi{}; }
}
QJsonObject Engine::consoleJson() const {
  QJsonArray arr; for (const StripUi& s : strips_) arr << stripJson(s);
  QJsonArray buses; for (const BusUi& b : buses_) { QJsonObject o; o["name"] = b.name; o["db"] = b.db; o["returnDb"] = b.returnDb; o["mute"] = b.mute; o["fx"] = b.fxKind; o["fxMix"] = b.fxMix; o["fxBypass"] = b.fxBypass; QJsonObject fv; for (const auto& [id, v] : b.fxValues) fv[id] = v; o["fxValues"] = fv; buses << o; }
  QJsonArray dcas; for (const DcaUi& d : dcas_) { QJsonObject o; o["name"] = d.name; o["db"] = d.db; o["mute"] = d.mute; dcas << o; }
  QJsonArray mgs; for (const MuteGroupUi& g : muteGroups_) { QJsonObject o; o["name"] = g.name; o["active"] = g.active; mgs << o; }
  QJsonObject root; root["strips"] = arr; root["buses"] = buses; root["dcas"] = dcas; root["muteGroups"] = mgs; root["masterDb"] = masterDb_; root["masterLim"] = masterLim_; root["masterCeil"] = masterCeil_; root["autoLevelOn"] = autoOn_; root["autoLevelTarget"] = autoTarget_; root["autoLevelMax"] = autoMax_; root["width"] = width_; root["v"] = 2;
  QJsonObject mon; mon["source"] = mon_.source; mon["db"] = mon_.db; mon["dim"] = mon_.dim; mon["mono"] = mon_.mono; mon["mute"] = mon_.mute; root["monitor"] = mon;
  QJsonArray ir; for (int i = 0; i < kStrips; ++i) ir << inRoute_[i]; QJsonArray orr; for (int o = 0; o < kOutputs; ++o) orr << outRoute_[o]; root["inputRoutes"] = ir; root["outputRoutes"] = orr;
  return root;
}
// respectSafe: strips flagged RECALL SAFE keep what they have (a scene recall); false = the whole document (start-up)
void Engine::applyConsoleJson(const QJsonObject& root, bool respectSafe) {
  if (root.isEmpty()) return;
  masterDb_ = std::clamp(root.value("masterDb").toDouble(0), -60.0, 10.0); masterLim_ = root.value("masterLim").toBool(true); masterCeil_ = std::clamp(root.value("masterCeil").toDouble(-0.5), -12.0, 0.0);
  width_ = std::clamp(root.value("width").toDouble(1.0), 0.0, 2.0); autoOn_ = root.value("autoLevelOn").toBool(); autoTarget_ = std::clamp(root.value("autoLevelTarget").toDouble(-16), -30.0, -6.0); autoMax_ = std::clamp(root.value("autoLevelMax").toDouble(12), 0.0, 24.0);
  if (root.contains("inputRoutes")) { const QJsonArray ir = root.value("inputRoutes").toArray(); for (int i = 0; i < kStrips && i < ir.size(); ++i) inRoute_[i] = std::clamp(ir[i].toInt(i), -1, kStrips - 1); emit routingChanged(); }
  if (root.contains("outputRoutes")) { const QJsonArray orr = root.value("outputRoutes").toArray(); for (int o = 0; o < kOutputs && o < orr.size(); ++o) outRoute_[o] = std::clamp(orr[o].toInt(0), 0, am::Runtime::kSrcBus0L + 2 * kBuses - 1); emit routingChanged(); }
  if (root.contains("monitor")) { const QJsonObject mo = root.value("monitor").toObject(); mon_.source = std::clamp(mo.value("source").toInt(0), 0, kBuses); mon_.db = std::clamp(mo.value("db").toDouble(0), -60.0, 10.0); mon_.dim = mo.value("dim").toBool(); mon_.mono = mo.value("mono").toBool(); mon_.mute = false; emit monitorChanged(); }   // never recall a muted control room
  const QJsonArray arr = root.value("strips").toArray();
  for (int i = 0; i < kStrips && i < arr.size(); ++i) { if (respectSafe && ((recallSafe_ >> i) & 1u)) continue; stripFromJson(strips_[static_cast<std::size_t>(i)], arr[i].toObject()); }
  const QJsonArray buses = root.value("buses").toArray(); for (int k = 0; k < kBuses && k < buses.size(); ++k) { const QJsonObject o = buses[k].toObject(); if (o.contains("name")) buses_[k].name = o.value("name").toString().left(16); buses_[k].db = std::clamp(o.value("db").toDouble(0), -60.0, 10.0); buses_[k].returnDb = std::clamp(o.value("returnDb").toDouble(-60), -60.0, 10.0); buses_[k].mute = o.value("mute").toBool();
    buses_[k].fxKind = o.value("fx").toString(); buses_[k].fxMix = std::clamp(o.value("fxMix").toDouble(1), 0.0, 1.0); buses_[k].fxBypass = o.value("fxBypass").toBool(); buses_[k].fxValues.clear(); const QJsonObject fv = o.value("fxValues").toObject(); for (auto it = fv.begin(); it != fv.end(); ++it) buses_[k].fxValues[it.key()] = std::clamp(it.value().toDouble(), 0.0, 1.0); makeBusFx(k); }
  const QJsonArray dcas = root.value("dcas").toArray(); for (int g = 0; g < kGroups && g < dcas.size(); ++g) { const QJsonObject o = dcas[g].toObject(); if (o.contains("name")) dcas_[g].name = o.value("name").toString().left(16); dcas_[g].db = std::clamp(o.value("db").toDouble(0), -60.0, 10.0); dcas_[g].mute = o.value("mute").toBool(); }
  const QJsonArray mgs = root.value("muteGroups").toArray(); for (int g = 0; g < kGroups && g < mgs.size(); ++g) { const QJsonObject o = mgs[g].toObject(); if (o.contains("name")) muteGroups_[g].name = o.value("name").toString().left(16); muteGroups_[g].active = o.value("active").toBool(); }
  for (int i = 0; i < kStrips; ++i) for (int k = 0; k < kPluginSlots; ++k) { PluginUi& p = strips_[static_cast<std::size_t>(i)].plugins[k]; if (isVstKind(p.kind) && !p.vst) loadVst(i, k); else if (p.vst && !p.state.isEmpty()) { p.vst->setState(p.state.toStdString()); p.vst->setBypass(p.bypass); } }
  if (rt_) { for (auto& s : strips_) s.dsp.setSampleRate(s.dsp.sampleRate()); pushAll(); }
  emit stripsChanged(); emit busesChanged(); emit groupsChanged();
}
void Engine::save() {
  settings_.setValue("console/state", QString::fromUtf8(QJsonDocument(consoleJson()).toJson(QJsonDocument::Compact)));
  QJsonArray sc; for (const SceneUi& x : scenes_) { QJsonObject o; o["name"] = x.name; o["at"] = x.at; o["data"] = x.data; sc << o; }
  settings_.setValue("console/scenes", QString::fromUtf8(QJsonDocument(sc).toJson(QJsonDocument::Compact)));
  settings_.setValue("console/recallSafe", static_cast<qulonglong>(recallSafe_));
  QJsonArray pr; for (const PresetUi& x : userPresets_) { QJsonObject o; o["name"] = x.name; o["category"] = x.category; o["dsp"] = x.dsp; pr << o; }
  settings_.setValue("console/presets", QString::fromUtf8(QJsonDocument(pr).toJson(QJsonDocument::Compact)));
}
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
  recallSafe_ = static_cast<unsigned>(settings_.value("console/recallSafe", 0).toULongLong() & 0xffffffffu);
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
void Engine::setRecallSafe(int strip, bool on) { if (strip < 0 || strip >= kStrips) return; if (on) recallSafe_ |= 1u << strip; else recallSafe_ &= ~(1u << strip); save(); emit scenesChanged(); }
bool Engine::recallSafe(int strip) const { return strip >= 0 && strip < kStrips && ((recallSafe_ >> strip) & 1u); }

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
