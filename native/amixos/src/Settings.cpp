#include <QCoreApplication>
#include <QProcess>
#include <algorithm>
#include "Settings.h"
#include "paths/paths.hpp"

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
int Settings::addVstDir(const QString& dir) { QStringList l = vstDirs(); QString d = dir.trimmed(); if (d.size() >= 2 && d.startsWith('"') && d.endsWith('"')) d = d.mid(1, d.size() - 2).trimmed();   /* "Copy as path" on Windows puts the path in quotes */ while (d.size() > 3 && (d.endsWith('/') || d.endsWith('\\'))) d.chop(1); if (!d.isEmpty() && !l.contains(d)) l << d; s_.setValue("vst3/dirs", l); s_.sync(); emit changed(); return rescanVst(); }
int Settings::removeVstDir(const QString& dir) { QStringList l = vstDirs(); l.removeAll(dir); s_.setValue("vst3/dirs", l); s_.sync(); emit changed(); return rescanVst(); }
QString Settings::vstUserFolder() {
  const QString d = QDir::toNativeSeparators(dataFolder() + "/vst"); QDir().mkpath(d);
  QStringList l = vstDirs(); if (!l.contains(d)) { l << d; s_.setValue("vst3/dirs", l); s_.sync(); emit changed(); }   // saved: scanned at every start from now on
  return d;
}
bool Settings::openVstFolder(const QString& dir) {
  const QString d = dir.trimmed().isEmpty() ? vstUserFolder() : dir.trimmed();
  if (!QDir(d).exists()) { note(QStringLiteral("The folder %1 does not exist.").arg(d)); return false; }
  return QDesktopServices::openUrl(QUrl::fromLocalFile(d));
}
int Settings::rescanVst() { const int n = engine_ ? engine_->rescanVst(vstDirs()) : 0; note(QStringLiteral("%1 VST3 / VST2 plug-in(s) found in %2 folder(s).").arg(n).arg(vstDefaultDirs().size() + vstDirs().size())); return n; }
QString Settings::dataFolder() const { return QString::fromStdString(am::paths::dataDir()); }
QVariantMap Settings::folders() const {
  QVariantMap m; m["os"] = am::paths::osName(); m["program"] = QString::fromStdString(am::paths::programDir()); m["data"] = QString::fromStdString(am::paths::dataDir()); m["cache"] = QString::fromStdString(am::paths::cacheDir());
  m["prefetch"] = QString::fromStdString(am::paths::prefetchDir()); m["temp"] = QString::fromStdString(am::paths::tempDir()); m["downloads"] = QString::fromStdString(am::paths::downloadsDir()); m["legacy"] = QString::fromStdString(am::paths::legacyHome()); return m;
}
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
  // ONCE, ON THE FIRST START OF THIS BUILD: AUTO becomes WASAPI (the system's default speakers and microphone, which every Windows
  // computer has and which carry sound), every WASAPI device is switched ON, and an ASIO driver that AUTO had remembered is forgotten.
  // An ASIO driver that is not running reports no channels, so AUTO used to land on an empty one ("Audio Mixer": no input, no output).
  // A device the person picks by hand afterwards, ASIO included, is remembered and opened as before.
  if (!s_.value("audio/autoWasapiV2", false).toBool() && engine_) {
    QSettings q; q.setValue("audio/autoPrefer", QStringLiteral("wasapi"));
    QStringList off = q.value("audio/offDevices").toStringList(); off.erase(std::remove_if(off.begin(), off.end(), [](const QString& k) { return k.startsWith(QStringLiteral("wasapi|")); }), off.end()); q.setValue("audio/offDevices", off);
    if (lastDevice().value("backend").toString() == QLatin1String("asio")) set("audio/lastDevice", QVariantMap());
    set("audio/autoWasapiV2", true); engine_->refreshDevices();
  }
  const QVariantMap d = lastDevice();
  // NO DEVICE YET (a new installation, or after a factory reset): AUTO DETECT the sound card (the system's default WASAPI speakers and
  // microphone; another preference on SOUND CARD), start it and remember it, so the console comes up working; MANUAL selection opens nothing by itself
  if (d.isEmpty() && engine_) {
    if (engine_->autoPrefer() == QLatin1String("manual")) { note(QStringLiteral("No device remembered yet: MANUAL selection is on, so pick one on CONNECTIONS.")); return false; }
    const QVariantMap a = engine_->autoDetect(); const int fr = defaultFrames() > 0 ? defaultFrames() : 256;
    const bool ok = !a.isEmpty() && engine_->autoStart(fr);
    if (ok) rememberDevice(a.value("backend").toString(), a.value("in").toString() == "-" ? QString() : a.value("in").toString(), a.value("out").toString() == "-" ? QString() : a.value("out").toString(),
                           a.value("rate").toDouble() > 0 ? a.value("rate").toDouble() : a.value("defaultRate").toDouble() > 0 ? a.value("defaultRate").toDouble() : 48000.0, fr, std::min(32, a.value("inChannels").toInt()), std::min(32, a.value("outChannels").toInt()));
    note(ok ? engine_->lastRoute() : (a.isEmpty() ? QStringLiteral("No sound card found: plug one in, or pick one on CONNECTIONS.") : engine_->status()));
    return ok;
  }
  if (!engine_) return false;
  // ENGINE ON never opens a Windows microphone by itself (it fed the speakers at once): outputs only, unless MIC AT ENGINE ON is on
  const bool keepIn = engine_->keepInputAtStart(d["backend"].toString(), d["in"].toString().isEmpty() ? QStringLiteral("default") : d["in"].toString()) || d["inChannels"].toInt() == 0;
  const QString in = keepIn ? d["in"].toString() : QStringLiteral("-"); const int inCh = keepIn ? d["inChannels"].toInt() : 0;
  const bool ok = smartStart() ? engine_->openSmart(d["backend"].toString(), in, d["out"].toString(), d["rate"].toDouble(), d["frames"].toInt(), inCh, d["outChannels"].toInt())
                               : engine_->open(d["backend"].toString(), in, d["out"].toString(), d["rate"].toDouble(), d["frames"].toInt(), inCh, d["outChannels"].toInt());
  if (ok && !keepIn) engine_->noteOutputsOnly();
  note(ok ? QStringLiteral("Started the last device: %1").arg(engine_->status()) : QStringLiteral("Could not start the last device: %1").arg(engine_->status()));
  return ok;
}

QString Settings::exportConsole(const QString& path) {
  const QString out = path.isEmpty() ? QDir::homePath() + "/amixos-console.json" : path;
  QJsonObject root; root["app"] = "AmF X1"; root["kind"] = "console-export"; root["v"] = 1; root["console"] = engine_->consoleExport(); root["scenes"] = engine_->scenesExport(); root["presets"] = engine_->presetsExport();
  QFile f(out); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { note(QStringLiteral("Cannot write %1").arg(out)); return QString(); }
  f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)); note(QStringLiteral("Console exported: %1").arg(out)); return out;
}
bool Settings::importConsole(const QString& given) {
  const QString path = given.isEmpty() ? QDir::homePath() + "/amixos-console.json" : given;   // the same default file exportConsole("") writes
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
  engine_->stop(); engine_->factoryReset();
  s_.clear(); s_.setValue("general/setupDone", false); s_.sync();   // everything back to the factory state, the language too: the factory setup runs once, right after it, and asks for it again
  for (const QString& f : {dataFolder() + "/arrangement.json", dataFolder() + "/driver-lock.json", dataFolder() + "/daemon.token", dataFolder() + "/daemon.tokens.json"}) QFile::remove(f);   // the network tokens go too: every phone has to be let in again
  if (engine_) { engine_->setClipThreshold(clipDb()); engine_->setPeakHold(peakHoldMs()); }
  emit changed(); note(QStringLiteral("Factory reset done: everything is as it left the factory. Only the licence and the edition were kept."));
}

bool Settings::openDiagnostics() {
#ifdef _WIN32
  const QString exe = QCoreApplication::applicationDirPath() + "/AmF X1 Diagnostics.exe";
#else
  const QString exe = QCoreApplication::applicationDirPath() + "/AmF X1 Diagnostics";
#endif
  if (!QFileInfo::exists(exe)) { note(QStringLiteral("The Diagnostics app is not installed next to the console (run the setup again and tick it).")); return false; }
  const bool ok = QProcess::startDetached(exe, {}); note(ok ? QStringLiteral("Diagnostics opened.") : QStringLiteral("The Diagnostics app did not start.")); return ok;
}
bool Settings::openPluginBuilder() {
#ifdef _WIN32
  const QString exe = QCoreApplication::applicationDirPath() + "/AmF X1 Plugin Builder.exe";
#else
  const QString exe = QCoreApplication::applicationDirPath() + "/AmF X1 Plugin Builder";
#endif
  if (!QFileInfo::exists(exe)) { note(QStringLiteral("The Plug-in Builder is not installed next to the console.")); return false; }
  const bool ok = QProcess::startDetached(exe, {}); note(ok ? QStringLiteral("Plug-in Builder opened: export a plug-in there, then RESCAN on PLUGINS.") : QStringLiteral("The Plug-in Builder did not start."));
  return ok;
}
