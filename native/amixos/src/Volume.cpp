// VOLUME CONTROL: tray, volume keys, per-app volume (Volume.h)
#include "Volume.h"

#include <QCoreApplication>
#include <QCursor>
#include <QIcon>
#include <QPointer>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <utility>

#include "Apps.h"
#include "Engine.h"
#include "audiomixer/runtime.hpp"

#ifdef AMIX_TRAY
#include <QAction>
#include <QMenu>
#include <QSystemTrayIcon>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
enum HotId { kHkUp = 0xA710, kHkDown, kHkMute, kMediaUp, kMediaDown, kMediaMute };
}

Volume::Volume(Engine* engine, Apps* apps, QObject* parent) : QObject(parent), engine_(engine), capture_(apps) {
  QSettings s;
  cfg_.target = s.value("volume/target", 0).toInt() == 1 ? am::vol::Target::Windows : am::vol::Target::Console;
  cfg_.stepDb = am::vol::clampStep(s.value("volume/stepDb", 2).toInt());
  cfg_.mediaKeys = s.value("volume/mediaKeys", false).toBool();   // off by default: the keyboard's volume keys stay with Windows
  cfg_.hotkeys = s.value("volume/hotkeys", true).toBool();
  tray_ = s.value("volume/tray", true).toBool();
  sendT_.setInterval(16); sendT_.setSingleShot(true); connect(&sendT_, &QTimer::timeout, this, &Volume::flush);
  listT_.setInterval(120); listT_.setSingleShot(true); connect(&listT_, &QTimer::timeout, this, &Volume::readList);   // a burst of "came / went" reports: one read
  if (capture_) connect(capture_, &Apps::changed, this, &Volume::notify);   // APP SOUND: which strips hear a program
  setLayer(am::vol::makeSystemLayer());
  QCoreApplication::instance()->installNativeEventFilter(this);
  QTimer::singleShot(0, this, [this] { if (!quiet_) { registerKeys(); makeTray(); } });
}

Volume::~Volume() {
  *alive_ = false;
  if (QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
  unregisterKeys();
  layer_.reset();
}

void Volume::quiet() { quiet_ = true; unregisterKeys();
#ifdef AMIX_TRAY
  if (icon_) icon_->hide();
#endif
}

void Volume::setLayer(std::unique_ptr<am::vol::SessionLayer> layer) {
  layer_ = std::move(layer); pendingVol_.clear(); list_ = am::vol::SessionList(); wire(); readList();
}

// Windows reports on its own threads: carried to this one (queued); a report after the console has gone is dropped
void Volume::wire() {
  if (!layer_) return;
  std::weak_ptr<bool> alive = alive_; QPointer<Volume> self(this);
  layer_->onListChanged = [alive, self] { if (alive.expired()) return; QMetaObject::invokeMethod(self, [self] { if (self && !self->listT_.isActive()) self->listT_.start(); }, Qt::QueuedConnection); };
  layer_->onVolume = [alive, self](const std::string& id, float v, bool m, bool ours) {
    if (alive.expired()) return;
    QMetaObject::invokeMethod(self, [self, id, v, m, ours] { if (self && self->list_.report(id, v, m, ours)) self->notify(); }, Qt::QueuedConnection); };
}

void Volume::readList() {
  if (!layer_ || !layer_->available()) { if (!list_.sessions().empty()) { list_.update({}); notify(); } return; }
  const am::vol::SessionList::Diff d = list_.update(layer_->list());
  if (!d.empty()) notify();
}
void Volume::refreshApps() { readList(); }

void Volume::notify() {
  QStringList ids; for (const am::vol::Session& x : list_.sessions()) ids << QString::fromStdString(x.id);
  if (ids != ids_) { ids_ = ids; emit appIdsChanged(); }
  emit appsChanged();
}
QVariantMap Volume::app(const QString& id) const { for (const QVariant& v : apps()) if (v.toMap().value("id").toString() == id) return v.toMap(); return {}; }

bool Volume::appsAvailable() const { return layer_ && layer_->available(); }

QVariantList Volume::apps() const {
  QVariantList caps = capture_ ? capture_->captures() : QVariantList();
  QVariantList out;
  for (const am::vol::Session& s : list_.sessions()) {
    QVariantMap m; m["id"] = QString::fromStdString(s.id); m["name"] = QString::fromStdString(s.name); m["pid"] = static_cast<qulonglong>(s.pid);
    m["volume"] = std::round(s.volume * 1000.0) / 10.0; m["mute"] = s.muted; m["system"] = s.system;
#ifdef _WIN32
    m["icon"] = s.pid && !s.system ? QStringLiteral("image://appwin/icon/%1").arg(s.pid) : QString();
#else
    m["icon"] = QString();
#endif
    QVariantList strips; int slot = -1;
    for (const QVariant& c : caps) { const QVariantMap cm = c.toMap(); if (s.pid && cm.value("pid").toULongLong() == s.pid) { strips = cm.value("strips").toList(); slot = cm.value("slot").toInt(); } }
    m["strips"] = strips; m["captureSlot"] = slot;   // APP SOUND: this program is also captured onto these strips
    out << m;
  }
  return out;
}

void Volume::setAppVolume(const QString& id, double percent) {
  if (!layer_ || !std::isfinite(percent)) return;
  const std::string sid = id.toStdString(); if (!list_.find(sid)) return;
  const float v = static_cast<float>(std::clamp(percent, 0.0, 100.0) / 100.0);
  list_.noteSet(sid, v); pendingVol_[id] = v; notify();
  if (!sendT_.isActive()) sendT_.start();
}
void Volume::flush() {
  sendT_.stop(); if (!layer_) { pendingVol_.clear(); return; }
  const QHash<QString, float> p = std::exchange(pendingVol_, {});
  for (auto it = p.begin(); it != p.end(); ++it) { layer_->setVolume(it.key().toStdString(), it.value()); ++writes_; }
}
void Volume::setAppMute(const QString& id, bool on) {
  if (!layer_) return; const std::string sid = id.toStdString(); if (!list_.find(sid)) return;
  list_.noteMute(sid, on); notify(); layer_->setMute(sid, on); ++writes_;
}

// ---- keys ----
QString Volume::dbText(double db) const { return QString::fromStdString(am::vol::dbText(db)); }
double Volume::stepped(double db, int dir) const { return am::vol::stepDb(db, dir, cfg_.stepDb); }

void Volume::stepMaster(int dir) {
  if (!engine_ || dir == 0) return;
  if (cfg_.target == am::vol::Target::Windows && engine_->windowsVolume() >= 0) {
    engine_->setWindowsVolume(static_cast<int>(std::lround(am::vol::stepScalar(static_cast<float>(engine_->windowsVolume() / 100.0), dir, cfg_.stepDb) * 100.0))); return; }
  if (engine_->masterMuted()) { if (dir < 0) return; engine_->setMasterMuted(false); }   // up from mute: the level comes back, then the step
  engine_->setMaster(am::vol::stepDb(engine_->masterDb(), dir, cfg_.stepDb));
}
void Volume::toggleMute() {
  if (!engine_) return;
  if (cfg_.target == am::vol::Target::Windows && engine_->windowsVolume() >= 0) { bool m = false; if (am::systemMuteGet(m)) am::systemMuteSet(!m); return; }
  engine_->setMasterMuted(!engine_->masterMuted());
}
bool Volume::press(int key, bool media) {
  am::vol::Action a; const am::vol::Key k = key == 0 ? am::vol::Key::Up : key == 1 ? am::vol::Key::Down : am::vol::Key::Mute;
  if (!am::vol::route(cfg_, k, media ? am::vol::Source::Media : am::vol::Source::Hotkey, a)) return false;
  if (a.toggleMute) toggleMute(); else stepMaster(a.dir);
  return true;
}

void Volume::save() const {
  QSettings s; s.setValue("volume/target", target()); s.setValue("volume/stepDb", cfg_.stepDb); s.setValue("volume/mediaKeys", cfg_.mediaKeys); s.setValue("volume/hotkeys", cfg_.hotkeys); s.setValue("volume/tray", tray_);
}
void Volume::setTarget(int t) { const auto n = t == 1 ? am::vol::Target::Windows : am::vol::Target::Console; if (n == cfg_.target) return; cfg_.target = n; save(); emit settingsChanged(); }
void Volume::setStepDb(int st) { st = am::vol::clampStep(st); if (st == cfg_.stepDb) return; cfg_.stepDb = st; save(); emit settingsChanged(); }
void Volume::setMediaKeys(bool on) { if (on == cfg_.mediaKeys) return; cfg_.mediaKeys = on; save(); registerKeys(); emit settingsChanged(); }
void Volume::setHotkeys(bool on) { if (on == cfg_.hotkeys) return; cfg_.hotkeys = on; save(); registerKeys(); emit settingsChanged(); }
void Volume::setTray(bool on) { if (on == tray_) return; tray_ = on; save(); makeTray(); emit settingsChanged(); }

void Volume::unregisterKeys() {
#ifdef _WIN32
  if (keysOn_) for (int id = kHkUp; id <= kMediaMute; ++id) UnregisterHotKey(nullptr, id);
#endif
  keysOn_ = false;
}
// the hotkeys belong to the whole system while the console runs; the keyboard's volume keys only when MEDIA KEYS is on (then Windows'
// own volume popup does not see them: the console's target takes them). A key another program holds already is said so, not forced.
void Volume::registerKeys() {
  unregisterKeys(); if (quiet_) return;
  QStringList got, refused;
#ifdef _WIN32
  auto reg = [&](int id, UINT mods, UINT vk, const char* name) { if (RegisterHotKey(nullptr, id, mods, vk)) got << name; else refused << name; };
  if (cfg_.hotkeys) { reg(kHkUp, MOD_CONTROL | MOD_ALT, VK_UP, "Ctrl+Alt+Up"); reg(kHkDown, MOD_CONTROL | MOD_ALT, VK_DOWN, "Ctrl+Alt+Down"); reg(kHkMute, MOD_CONTROL | MOD_ALT, 'M', "Ctrl+Alt+M"); }
  if (cfg_.mediaKeys) { reg(kMediaUp, 0, VK_VOLUME_UP, "Volume Up"); reg(kMediaDown, 0, VK_VOLUME_DOWN, "Volume Down"); reg(kMediaMute, 0, VK_VOLUME_MUTE, "Mute"); }
  keysOn_ = true;
  keysNote_ = (got.isEmpty() ? QString() : QStringLiteral("Keys: ") + got.join(", ")) + (refused.isEmpty() ? QString() : QStringLiteral("  ·  in use by another program: ") + refused.join(", "));
#else
  keysNote_ = (cfg_.hotkeys || cfg_.mediaKeys) ? QStringLiteral("System-wide keys are taken on Windows only.") : QString();
#endif
  emit settingsChanged();
}

bool Volume::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
#ifdef _WIN32
  if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
  const MSG* m = static_cast<const MSG*>(message);
  if (m->message == WM_HOTKEY && m->wParam >= kHkUp && m->wParam <= kMediaMute) {
    const int id = static_cast<int>(m->wParam); const bool media = id >= kMediaUp; const int key = (id - (media ? kMediaUp : kHkUp));
    press(key, media); if (result) *result = 0; return true; }
  // WM_APPCOMMAND to the console's own window (the volume keys while it has the focus): only when MEDIA KEYS is on, else Windows has them
  if (m->message == WM_APPCOMMAND && cfg_.mediaKeys) {
    const int cmd = GET_APPCOMMAND_LPARAM(m->lParam);
    const int key = cmd == APPCOMMAND_VOLUME_UP ? 0 : cmd == APPCOMMAND_VOLUME_DOWN ? 1 : cmd == APPCOMMAND_VOLUME_MUTE ? 2 : -1;
    if (key >= 0) { press(key, true); if (result) *result = TRUE; return true; } }
#else
  (void)type; (void)message; (void)result;
#endif
  return false;
}

// ---- tray ----
bool Volume::trayAvailable() const {
#ifdef AMIX_TRAY
  return QSystemTrayIcon::isSystemTrayAvailable();
#else
  return false;
#endif
}
void Volume::makeTray() {
#ifdef AMIX_TRAY
  if (!tray_ || quiet_ || !QSystemTrayIcon::isSystemTrayAvailable()) { if (icon_) icon_->hide(); return; }
  if (!icon_) {
    icon_ = new QSystemTrayIcon(QIcon(QStringLiteral(":/qt/qml/AmixOS/icons/amfx1.png")), this);
    menu_ = new QMenu();
    connect(this, &QObject::destroyed, menu_, [m = menu_] { delete m; });
    QAction* open = menu_->addAction(tr("Open console")); connect(open, &QAction::triggered, this, &Volume::openConsole);
    QAction* mute = menu_->addAction(tr("Mute master")); mute->setCheckable(true); connect(mute, &QAction::triggered, this, [this](bool on) { if (engine_) engine_->setMasterMuted(on); });
    connect(menu_, &QMenu::aboutToShow, this, [this, mute] { mute->setChecked(engine_ && engine_->masterMuted()); });
    menu_->addSeparator();
    QAction* quit = menu_->addAction(tr("Quit")); connect(quit, &QAction::triggered, this, &Volume::quitRequested);
    icon_->setContextMenu(menu_);
    connect(icon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
      if (r == QSystemTrayIcon::Trigger) { const QRect g = icon_->geometry(); const QPoint p = g.isValid() ? g.center() : QCursor::pos(); emit popupRequested(p.x(), p.y()); }
      else if (r == QSystemTrayIcon::DoubleClick) emit openConsole(); });
    auto tip = [this] { if (icon_ && engine_) icon_->setToolTip(QStringLiteral("AmF X1  ·  MASTER %1 dB%2").arg(dbText(engine_->masterDb()), engine_->masterMuted() ? QStringLiteral("  ·  MUTED") : QString())); };
    tip(); if (engine_) connect(engine_, &Engine::stripsChanged, icon_, tip);
  }
  icon_->show();
#endif
}
