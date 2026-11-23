// The settings system: typed options backed by QSettings (the same store the console state uses), with sane defaults.
//   general   start page, confirm destructive actions, tooltips
//   audio     auto-start the last device on launch (and what that device was), default sample rate / buffer, SMART START as the default start,
//             clip threshold (dBFS), peak hold time
//   mixer     defaults for the MIXER page (VU faces, monitor panel, plugin rack, channel panel), 0 VU reference lives on the Engine
//   files     where everything is kept, export / import of the whole console as JSON, open the data folder, FACTORY RESET
// Changing a value saves it at once; nothing here phones home.
#pragma once
#include <QObject>
#include <algorithm>
#include <cmath>
#include <QSettings>
#include <QTimer>
#include <QVariant>

class Engine;

class Settings : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString startPage READ startPage WRITE setStartPage NOTIFY changed)
  Q_PROPERTY(bool confirmDestructive READ confirmDestructive WRITE setConfirmDestructive NOTIFY changed)
  Q_PROPERTY(bool tooltips READ tooltips WRITE setTooltips NOTIFY changed)
  Q_PROPERTY(bool hideFromCapture READ hideFromCapture WRITE setHideFromCapture NOTIFY changed)   // HIDE FROM SCREENSHOTS & SCREEN SHARING (Windows, CaptureGuard.h): on by default (also blocks OBS / TikTok LIVE Studio), applies at once
  Q_PROPERTY(bool captureOverridden READ captureOverridden CONSTANT)   // AMIX_ALLOW_CAPTURE=1 (the repository's tooling): the setting is kept but not applied
  Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY changed)
  Q_PROPERTY(QString colorTheme READ colorTheme WRITE setColorTheme NOTIFY changed)   // the colour theme: "" (the console's own), chocolate, ocean, forest, plum
  Q_PROPERTY(bool colorMotion READ colorMotion WRITE setColorMotion NOTIFY changed)   // the AURORA colour behind the pages moves (off: it stays still, for slow computers)   // the console's look: dark (stage) or light (daylight); Theme.qml and the Qt palette follow
  Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY changed)   // the system is auto on: the remembered device, else AUTO DETECT, at every start (default on)
  Q_PROPERTY(QVariantMap lastDevice READ lastDevice NOTIFY changed)              // backend, in, out, rate, frames, inChannels, outChannels
  Q_PROPERTY(int defaultRate READ defaultRate WRITE setDefaultRate NOTIFY changed)
  Q_PROPERTY(int defaultFrames READ defaultFrames WRITE setDefaultFrames NOTIFY changed)
  Q_PROPERTY(bool smartStart READ smartStart WRITE setSmartStart NOTIFY changed)
  Q_PROPERTY(bool autoAsio READ autoAsio WRITE setAutoAsio NOTIFY changed)        // AUTO DETECT the best device (ASIO first) when none is remembered
  Q_PROPERTY(double clipDb READ clipDb WRITE setClipDb NOTIFY changed)
  Q_PROPERTY(int peakHoldMs READ peakHoldMs WRITE setPeakHoldMs NOTIFY changed)
  Q_PROPERTY(int uiZoom READ uiZoom WRITE setUiZoom NOTIFY changed)   // INTERFACE ZOOM: 0 = AUTO (fit the screen), else % of the Windows scale; at the next start (AutoZoom.h)
  Q_PROPERTY(QString zoomNote READ zoomNote CONSTANT)
  Q_PROPERTY(double liveZoom READ liveZoom WRITE setLiveZoom NOTIFY changed)   // ZOOM IN / OUT at once (Ctrl + / - / 0, Ctrl + wheel): 0.5 .. 2.0 on top of the interface zoom
  Q_PROPERTY(bool vuDefault READ vuDefault WRITE setVuDefault NOTIFY changed)
  Q_PROPERTY(QString vuStyle READ vuStyle WRITE setVuStyle NOTIFY changed)   // the meters on every fader page: "digital" (bars) or "analog" (needle faces)
  Q_PROPERTY(bool bootAnimation READ bootAnimation WRITE setBootAnimation NOTIFY changed)   // the opening animation
  Q_PROPERTY(bool bootSound READ bootSound WRITE setBootSound NOTIFY changed)               // its chord on the master (only when the engine is already running)
  Q_PROPERTY(bool monitorPanelDefault READ monitorPanelDefault WRITE setMonitorPanelDefault NOTIFY changed)
  Q_PROPERTY(bool rackDefault READ rackDefault WRITE setRackDefault NOTIFY changed)
  Q_PROPERTY(bool spatialEnabled READ spatialEnabled WRITE setSpatialEnabled NOTIFY changed)   // 360 AUDIO: an option, off by default; off = its page is out of the menu and no channel is placed in 360
  Q_PROPERTY(QString mixerLayout READ mixerLayout WRITE setMixerLayout NOTIFY changed)   // EDIT LAYOUT on the mixer: what shows and the strip width, as JSON ("" = the layout it ships with)
  Q_PROPERTY(bool mixerSimple READ mixerSimple WRITE setMixerSimple NOTIFY changed)   // MIXER strips: SIMPLE (name, tone, pan, MUTE / SOLO, fader) or FULL (every key and readout)
  Q_PROPERTY(bool rackCompactDefault READ rackCompactDefault WRITE setRackCompactDefault NOTIFY changed)
  Q_PROPERTY(bool channelPanelDefault READ channelPanelDefault WRITE setChannelPanelDefault NOTIFY changed)
  Q_PROPERTY(QVariantList dataFiles READ dataFiles NOTIFY changed)              // [{name, path, exists, bytes}]
  Q_PROPERTY(QStringList vstDirs READ vstDirs NOTIFY changed)                      // extra VST3 folders besides the platform ones
  Q_PROPERTY(QStringList vstDefaultDirs READ vstDefaultDirs CONSTANT)
  Q_PROPERTY(QString dataFolder READ dataFolder CONSTANT)
  Q_PROPERTY(QVariantMap folders READ folders CONSTANT)   // {os, program, data, cache, prefetch, temp, downloads, legacy}: where this build keeps its files
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
  Q_PROPERTY(bool setupDone READ setupDone NOTIFY changed)                       // the factory setup (first run) was completed
  Q_PROPERTY(bool lastSessionCrashed READ lastSessionCrashed CONSTANT)          // the previous run did not close cleanly
  Q_PROPERTY(QVariantMap autosave READ autosave NOTIFY autosaveChanged)          // {exists, path, at, bytes}: the console written every 60 s

 public:
  explicit Settings(Engine* engine, QObject* parent = nullptr);
  QString startPage() const { return s_.value("general/startPage", "home").toString(); } void setStartPage(const QString& v) { set("general/startPage", v); }
  bool confirmDestructive() const { return s_.value("general/confirm", true).toBool(); } void setConfirmDestructive(bool v) { set("general/confirm", v); }
  bool tooltips() const { return s_.value("general/tooltips", true).toBool(); } void setTooltips(bool v) { set("general/tooltips", v); }
  bool hideFromCapture() const { return s_.value("general/hideFromCapture", true).toBool(); } void setHideFromCapture(bool v) { set("general/hideFromCapture", v); }
  bool captureOverridden() const { return qEnvironmentVariable("AMIX_ALLOW_CAPTURE") == QLatin1String("1"); }
  bool darkTheme() const { return s_.value("ui/dark", true).toBool(); } void setDarkTheme(bool v) { set("ui/dark", v); }
  QString colorTheme() const { const QString t = s_.value("ui/colorTheme").toString(); return t == "chocolate" || t == "ocean" || t == "forest" || t == "plum" ? t : QString(); }
  void setColorTheme(const QString& v) { set("ui/colorTheme", v == "chocolate" || v == "ocean" || v == "forest" || v == "plum" ? v : QString()); }
  bool colorMotion() const { return s_.value("ui/colorMotion", true).toBool(); } void setColorMotion(bool v) { set("ui/colorMotion", v); }
  bool autoStart() const { return s_.value("audio/autoStart", true).toBool(); } void setAutoStart(bool v) { set("audio/autoStart", v); }
  QVariantMap lastDevice() const { return s_.value("audio/lastDevice").toMap(); }
  int defaultRate() const { return s_.value("audio/rate", 48000).toInt(); } void setDefaultRate(int v) { set("audio/rate", v); }
  int defaultFrames() const { return s_.value("audio/frames", 256).toInt(); } void setDefaultFrames(int v) { set("audio/frames", v); }
  bool autoAsio() const { return false; }   /* automatic selection was removed: the device is always chosen by hand */ void setAutoAsio(bool v) { set("audio/autoDetect", v); }
  bool smartStart() const { return false; }   /* the driver ladder was removed: the chosen device is opened, and no other in its place */ void setSmartStart(bool v) { set("audio/smartStart", v); }
  double clipDb() const { return s_.value("audio/clipDb", -0.1).toDouble(); } void setClipDb(double v);
  int peakHoldMs() const { return s_.value("audio/peakHoldMs", 1400).toInt(); } void setPeakHoldMs(int v);
  int uiZoom() const { return s_.value("ui/zoom", 0).toInt(); } void setUiZoom(int v) { set("ui/zoom", v); }
  double liveZoom() const { const double z = s_.value("ui/liveZoom", 1.0).toDouble(); return z >= 0.5 && z <= 2.0 ? z : 1.0; }
  void setLiveZoom(double z) { z = std::round(std::clamp(z, 0.5, 2.0) * 20.0) / 20.0; if (std::abs(z - liveZoom()) < 1e-9) return; set("ui/liveZoom", z); }
  Q_INVOKABLE void zoomIn() { setLiveZoom(liveZoom() + 0.1); }
  Q_INVOKABLE void zoomOut() { setLiveZoom(liveZoom() - 0.1); }
  Q_INVOKABLE void zoomReset() { setLiveZoom(1.0); }
  QString zoomNote() const { return zoomNote_; } void setZoomNote(const QString& n) { zoomNote_ = n; }
 private: QString zoomNote_;
 public:
  bool bootAnimation() const { return s_.value("ui/bootAnimation", true).toBool(); } void setBootAnimation(bool v) { set("ui/bootAnimation", v); }
  bool bootSound() const { return s_.value("ui/bootSound", true).toBool(); } void setBootSound(bool v) { set("ui/bootSound", v); }
  bool vuDefault() const { return s_.value("mixer/vu", false).toBool(); } void setVuDefault(bool v) { set("mixer/vu", v); }
  QString vuStyle() const { return vuDefault() ? QStringLiteral("analog") : QStringLiteral("digital"); } void setVuStyle(const QString& v) { setVuDefault(v == QLatin1String("analog")); }   // one setting: ANALOG = the VU faces
  bool monitorPanelDefault() const { return s_.value("mixer/monitor", true).toBool(); } void setMonitorPanelDefault(bool v) { set("mixer/monitor", v); }
  bool rackCompactDefault() const { return s_.value("mixer/rackCompact", false).toBool(); } void setRackCompactDefault(bool v) { set("mixer/rackCompact", v); }
  bool rackDefault() const { return s_.value("mixer/rack", true).toBool(); } void setRackDefault(bool v) { set("mixer/rack", v); }
  bool spatialEnabled() const { return s_.value("spatial/enabled", false).toBool(); } void setSpatialEnabled(bool v) { set("spatial/enabled", v); }
  QString mixerLayout() const { return s_.value("mixer/layout").toString().left(2000); } void setMixerLayout(const QString& v) { set("mixer/layout", v.left(2000)); }
  bool mixerSimple() const { return s_.value("mixer/simple", true).toBool(); } void setMixerSimple(bool v) { set("mixer/simple", v); }
  bool channelPanelDefault() const { return s_.value("mixer/channelPanel", true).toBool(); } void setChannelPanelDefault(bool v) { set("mixer/channelPanel", v); }
  QVariantList dataFiles() const;
  QStringList vstDirs() const { return s_.value("vst3/dirs").toStringList(); }
  QStringList vstDefaultDirs() const;   // VST3 and VST2 platform folders
  Q_INVOKABLE int addVstDir(const QString& dir);      // adds and rescans; returns the plug-in count
  Q_INVOKABLE int removeVstDir(const QString& dir);
  Q_INVOKABLE int rescanVst();
  Q_INVOKABLE bool openPluginBuilder();   // starts "AmF X1 Plugin Builder" next to the console (a separate program); its exports show up here after RESCAN
  Q_INVOKABLE bool openDiagnostics();     // the separate AmF X1 Diagnostics program (speakers, headphones, microphones, drivers)
  Q_INVOKABLE QString vstUserFolder();                // the console's own plug-in folder (<data>/vst): created, and saved among the scanned folders, the first time it is asked for
  // EXPORT: the console's own things (its saved state with the scenes, the sample pads, the arrangement) in one .zip in the signed-in user's
  // Downloads folder. Only those files: no licence, no network tokens, no log. toDir = "" means that Downloads folder.
  // Returns {ok, path, folder, user, files, bytes, error}.
  Q_INVOKABLE QVariantMap exportZip(const QString& toDir = QString());
  Q_INVOKABLE QString systemUser() const;             // the name of the user signed in to this computer (shown beside the export)
  Q_INVOKABLE QString userDownloads() const;          // that user's Downloads folder
  Q_INVOKABLE bool openVstFolder(const QString& dir = QString());   // shows a plug-in folder in the system's file manager; "" = the console's own
  QString dataFolder() const; QVariantMap folders() const;
  QString message() const { return msg_; }

  Q_INVOKABLE void rememberDevice(const QString& backend, const QString& in, const QString& out, double rate, int frames, int inCh, int outCh);
  Q_INVOKABLE bool startLastDevice();                 // what auto-start does: opens the remembered device (SMART START when enabled)
  Q_INVOKABLE QString exportConsole(const QString& path);   // the whole console + scenes + presets as one JSON file ("" = ~/amixos-console.json)
  Q_INVOKABLE bool importConsole(const QString& path);
  Q_INVOKABLE bool openDataFolder();
  Q_INVOKABLE QStringList factoryResetPreview() const;      // what FACTORY RESET removes
  Q_INVOKABLE void factoryReset();                           // settings, console, scenes, presets, arrangement, driver locks; the licence and the token stay
  Q_INVOKABLE QString settingsFile() const { return s_.fileName(); }
  bool setupDone() const { return s_.value("general/setupDone", true).toBool(); }   // written at the first start by decideSetup(); a FACTORY RESET or RERUN SETUP sets it to false
  // A NEW INSTALLATION STARTS IN THE FACTORY SETUP. Decided once, at the very first start, and written down at once: closing the window
  // in the middle of the setup does not get past it (the next start shows it again). An installation that already has settings (an
  // update over a console in use) is not sent back to the setup. The self-tests run on empty settings and are not a new installation.
  static bool decideSetup(QSettings& s, bool testRun) {
    if (s.contains(QStringLiteral("general/setupDone"))) return s.value(QStringLiteral("general/setupDone")).toBool();
    const bool done = testRun || !s.allKeys().isEmpty(); s.setValue(QStringLiteral("general/setupDone"), done); s.sync(); return done; }
  Q_INVOKABLE void finishSetup() { set("general/setupDone", true); }
  Q_INVOKABLE void rerunSetup() { set("general/setupDone", false); }   // the wizard shows again at the next start (or at once: Main.qml watches it)
  bool lastSessionCrashed() const { return crashed_; }
  QVariantMap autosave() const;
  Q_INVOKABLE bool restoreAutosave();                        // the console as it was at the last autosave (a backup of the current one is written first)
  Q_INVOKABLE QString saveText(const QString& name, const QString& text);   // writes a text file into the data folder; returns its path ("" on failure)
  Q_INVOKABLE void sessionClosed();                          // called on a clean quit

 signals:
  void changed();
  void messageChanged();
  void autosaveChanged();

 private:
  void set(const QString& k, const QVariant& v) { if (s_.value(k) == v) return; s_.setValue(k, v); s_.sync(); emit changed(); }
  void note(const QString& m) { msg_ = m; emit messageChanged(); }
  Engine* engine_; QSettings s_; QString msg_; bool crashed_ = false; QTimer autosave_;
};
