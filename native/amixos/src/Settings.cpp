#include "Settings.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QRegularExpression>
#include <QDateTime>

#include "Engine.h"
#include "vst3/vst3_host.hpp"
#include "vst2/vst2_host.hpp"

Settings::Settings(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  if (engine_) { engine_->setClipThreshold(clipDb()); engine_->setPeakHold(peakHoldMs()); }
  crashed_ = s_.value("session/open", false).toBool(); s_.setValue("session/open", true); s_.sync();   // cleared by sessionClosed(); still set at the next start = the last run died
  autosave_.setInterval(60000); connect(&autosave_, &QTimer::timeout, this, [this] { if (!engine_) return; QDir().mkpath(dataFolder()); if (!exportConsole(dataFolder() + "/autosave.json").isEmpty()) emit autosaveChanged(); }); autosave_.start();
}
void Settings::sessionClosed() { s_.setValue("session/open", false); s_.sync(); }
QVariantMap Settings::autosave() const { QVariantMap m; QFileInfo fi(dataFolder() + "/autosave.json"); m["exists"] = fi.exists(); m["path"] = fi.filePath(); m["at"] = fi.exists() ? fi.lastModified().toString("yyyy-MM-dd HH:mm:ss") : QString(); m["bytes"] = fi.exists() ? static_cast<qlonglong>(fi.size()) : 0; return m; }
bool Settings::restoreAutosave() { const QString p = dataFolder() + "/autosave.json"; if (!QFileInfo::exists(p)) { note("No autosave yet"); return false; } exportConsole(dataFolder() + "/before-restore.json"); const bool ok = importConsole(p); if (ok) note("Console restored from the autosave; the console before it is in before-restore.json"); return ok; }
QString Settings::saveText(const QString& name, const QString& text) {
  QString n = name; n.replace(QRegularExpression("[^A-Za-z0-9._-]"), "_"); if (n.isEmpty()) n = "export.txt"; QDir().mkpath(dataFolder()); const QString p = dataFolder() + "/" + n;
  QFile f(p); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { note(QStringLiteral("Cannot write %1").arg(p)); return QString(); } f.write(text.toUtf8()); note(QStringLiteral("Saved %1").arg(p)); return p;
}
void Settings::setPeakHoldMs(int v) { set("audio/peakHoldMs", v); if (engine_) engine_->setPeakHold(v); }
void Settings::setClipDb(double v) { v = std::clamp(v, -12.0, 0.0); set("audio/clipDb", v); if (engine_) engine_->setClipThreshold(v); }

QStringList Settings::vstDefaultDirs() const { QStringList l; for (const auto& d : am::vst3::defaultDirs()) l << QString::fromStdString(d) + "  (VST3)"; for (const auto& d : am::vst2::defaultDirs()) l << QString::fromStdString(d) + "  (VST2)"; return l; }
int Settings::addVstDir(const QString& dir) { QStringList l = vstDirs(); const QString d = dir.trimmed(); if (!d.isEmpty() && !l.contains(d)) l << d; s_.setValue("vst3/dirs", l); s_.sync(); emit changed(); return rescanVst(); }
int Settings::removeVstDir(const QString& dir) { QStringList l = vstDirs(); l.removeAll(dir); s_.setValue("vst3/dirs", l); s_.sync(); emit changed(); return rescanVst(); }
int Settings::rescanVst() { const int n = engine_ ? engine_->rescanVst(vstDirs()) : 0; note(QStringLiteral("%1 VST3 / VST2 plug-in(s) found in %2 folder(s).").arg(n).arg(vstDefaultDirs().size() + vstDirs().size())); return n; }
QString Settings::dataFolder() const { return QDir::homePath() + "/.amixos"; }
QVariantList Settings::dataFiles() const {
  QVariantList l;
  auto add = [&](const QString& name, const QString& path, const QString& what) { QFileInfo fi(path); QVariantMap m; m["name"] = name; m["path"] = path; m["exists"] = fi.exists(); m["bytes"] = fi.exists() ? static_cast<qlonglong>(fi.size()) : 0; m["what"] = what; l << m; };
  add("Settings + console", s_.fileName(), "every option here, the console state, scenes, presets (QSettings)");
  add("Arrangement", dataFolder() + "/arrangement.json", "ARRANGE: tracks, clips (by file path), markers, automation");
  add("Licence key", dataFolder() + "/license.json", "the activated key (0600); removed by DEACTIVATE on LICENSE");
  add("Edition activation", dataFolder() + "/edition.json", "a paid edition bound to this computer");
  add("Driver locks", dataFolder() + "/driver-lock.json", "AI driver locks and the Realtek pin opt-out");
  add("Daemon token", dataFolder() + "/daemon.token", "the amixos-daemon API token for the phone remote (0600)");
  return l;
}

void Settings::rememberDevice(const QString& backend, const QString& in, const QString& out, double rate, int frames, int inCh, int outCh) {
  QVariantMap m; m["backend"] = backend; m["in"] = in; m["out"] = out; m["rate"] = rate; m["frames"] = frames; m["inChannels"] = inCh; m["outChannels"] = outCh; set("audio/lastDevice", m);
}
bool Settings::startLastDevice() {
  const QVariantMap d = lastDevice();
  if (d.isEmpty() && engine_ && autoAsio()) { const bool ok = engine_->autoStart(defaultFrames()); note(ok ? engine_->lastRoute() : engine_->status()); return ok; }
  if (d.isEmpty() || !engine_) { note(QStringLiteral("No device remembered yet: start one on CONNECTIONS once, or turn AUTO DETECT on.")); return false; }
  const bool ok = smartStart() ? engine_->openSmart(d["backend"].toString(), d["in"].toString(), d["out"].toString(), d["rate"].toDouble(), d["frames"].toInt(), d["inChannels"].toInt(), d["outChannels"].toInt())
                               : engine_->open(d["backend"].toString(), d["in"].toString(), d["out"].toString(), d["rate"].toDouble(), d["frames"].toInt(), d["inChannels"].toInt(), d["outChannels"].toInt());
  note(ok ? QStringLiteral("Started the last device: %1").arg(engine_->status()) : QStringLiteral("Could not start the last device: %1").arg(engine_->status()));
  return ok;
}

QString Settings::exportConsole(const QString& path) {
  const QString out = path.isEmpty() ? QDir::homePath() + "/amixos-console.json" : path;
  QJsonObject root; root["app"] = "AmF X1"; root["kind"] = "console-export"; root["v"] = 1; root["console"] = engine_->consoleExport(); root["scenes"] = engine_->scenesExport(); root["presets"] = engine_->presetsExport();
  QFile f(out); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { note(QStringLiteral("Cannot write %1").arg(out)); return QString(); }
  f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)); note(QStringLiteral("Console exported: %1").arg(out)); return out;
}
bool Settings::importConsole(const QString& path) {
  QFile f(path); if (!f.open(QIODevice::ReadOnly)) { note(QStringLiteral("Cannot read %1").arg(path)); return false; }
  const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
  if (root.value("kind").toString() != "console-export" || !root.contains("console")) { note(QStringLiteral("%1 is not an A mix OS console export.").arg(QFileInfo(path).fileName())); return false; }
  engine_->consoleImport(root.value("console").toObject(), root.value("scenes").toArray(), root.value("presets").toArray());
  note(QStringLiteral("Console imported from %1 (%2 strips, %3 scenes).").arg(QFileInfo(path).fileName()).arg(root.value("console").toObject().value("strips").toArray().size()).arg(root.value("scenes").toArray().size()));
  return true;
}
bool Settings::openDataFolder() { QDir().mkpath(dataFolder()); return QDesktopServices::openUrl(QUrl::fromLocalFile(dataFolder())); }

QStringList Settings::factoryResetPreview() const {
  return {QStringLiteral("All options on this page (back to defaults)"), QStringLiteral("The console: names, faders, DSP, sends, groups, routing, monitor, plugin slots"), QStringLiteral("All 16 scenes and your user presets (factory presets stay)"),
          QStringLiteral("The arrangement project (the audio files themselves are not touched)"), QStringLiteral("AI driver locks and the Realtek pin choice"), QStringLiteral("Kept: the licence key, the edition activation, the daemon token, the language")};
}
void Settings::factoryReset() {
  const QString lang = s_.value("ui/lang").toString();
  engine_->stop(); engine_->factoryReset();
  s_.clear(); if (!lang.isEmpty()) s_.setValue("ui/lang", lang); s_.sync();
  for (const QString& f : {dataFolder() + "/arrangement.json", dataFolder() + "/driver-lock.json"}) QFile::remove(f);
  if (engine_) { engine_->setClipThreshold(clipDb()); engine_->setPeakHold(peakHoldMs()); }
  emit changed(); note(QStringLiteral("Factory reset done. The licence, the edition and the daemon token were kept."));
}
