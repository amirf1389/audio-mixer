#include "Apps.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QThread>
#include <cmath>
#include <thread>

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
    if (i.used) { m["name"] = QString::fromStdString(i.label); m["pid"] = static_cast<qulonglong>(i.pid); m["state"] = QString::fromStdString(i.state); m["test"] = i.test; m["gaps"] = static_cast<qulonglong>(i.gaps);
      m["peakDb"] = i.peak > 1e-6 ? 20.0 * std::log10(i.peak) : -120.0;
      m["message"] = i.state == "failed" ? QString::fromStdString(i.error) : i.state == "silent" ? QStringLiteral("captured, but the program plays nothing now (or its stream is protected)") : i.state == "opening" ? QStringLiteral("opening") : QString(); }
    else { m["name"] = waiting_[k]; m["pid"] = 0; m["state"] = "waiting"; m["test"] = false; m["gaps"] = 0; m["peakDb"] = -120.0; m["message"] = QStringLiteral("waiting for the program to play again"); }
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
QVariantMap Apps::captureTest(const QString& name, double hz, int strip, bool stereo) {
  if (strip < 0 || strip >= engine_->strips() || !engine_->runtime()) { note_ = QStringLiteral("Start the sound card first (SOUND CARD), then pick the program."); emit changed(); return QVariantMap{{"ok", false}, {"slot", -1}, {"message", note_}}; }
  std::string err; const int slot = cap().openTest(name.toStdString(), hz, engine_->runtime()->sampleRate(), err);
  return finish(slot, name, strip, stereo, err);
}
void Apps::release(int slot) {
  if (slot < 0 || slot >= Capture::kSlots) return;
  placing_ = true; const QVariantList routes = engine_->inputRoutes();
  for (int s = 0; s < routes.size(); ++s) if (slotOfInput(routes[s].toInt()) == slot) engine_->setInputRoute(s, -1);
  placing_ = false; waiting_[slot].clear(); if (cap_) cap_->close(slot); save(); emit changed();
}
// a slot no strip listens to is given back
void Apps::prune() {
  bool heard[Capture::kSlots] = {}; const QVariantList routes = engine_->inputRoutes();
  for (const QVariant& r : routes) { const int k = slotOfInput(r.toInt()); if (k >= 0) heard[k] = true; }
  bool any = false;
  for (int k = 0; k < Capture::kSlots; ++k) { if (heard[k]) continue; if (cap_ && cap_->info(k).used) { cap_->close(k); any = true; } if (!waiting_[k].isEmpty()) { waiting_[k].clear(); any = true; } }
  if (any) save();
}
// once a second: a program that closed leaves its slot waiting; a waiting slot looks for its program among those that play now
void Apps::poll() {
  bool wait = false; QString sig;
  for (int k = 0; k < Capture::kSlots; ++k) {
    if (cap_) { const Capture::Info i = cap_->info(k);
      if (i.used && i.state == "failed") { waiting_[k] = QString::fromStdString(i.label); cap_->close(k); }
      if (i.used) sig += QString::fromStdString(i.state) + QLatin1Char('|'); }
    if (!waiting_[k].isEmpty()) { wait = true; sig += QStringLiteral("w|"); } }
  if (sig != lastSig_) { lastSig_ = sig; emit changed(); }
  if (!wait || looking_ || !engine_->runtime()) return;
  auto take = [this](const std::vector<std::pair<quint32, QString>>& now) {
    looking_ = false; if (!engine_->runtime()) return; bool got = false;
    for (int k = 0; k < Capture::kSlots; ++k) { if (waiting_[k].isEmpty()) continue;
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
  QJsonArray a; for (int k = 0; k < Capture::kSlots; ++k) { QString n = waiting_[k]; if (cap_) { const Capture::Info i = cap_->info(k); if (i.used && !i.test && i.pid != 0) n = QString::fromStdString(i.label); else if (i.used) n.clear(); }
    if (!n.isEmpty()) a << QJsonObject{{"slot", k}, {"name", n}}; }
  s_.setValue("apps/captures", QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)));
}
void Apps::load() {
  const QJsonArray a = QJsonDocument::fromJson(s_.value("apps/captures").toString().toUtf8()).array();
  for (const QJsonValue& v : a) { const QJsonObject o = v.toObject(); const int k = o.value("slot").toInt(-1); QString n = o.value("name").toString().left(80); n.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f<>]")));
    if (k >= 0 && k < Capture::kSlots && !n.isEmpty()) waiting_[k] = n; }
  prune();   // a slot the saved console no longer listens to is not waited for
}
