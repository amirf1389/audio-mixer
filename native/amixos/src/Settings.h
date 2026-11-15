// The settings system: typed options backed by QSettings (the same store the console state uses), with sane defaults.
//   general   start page, confirm destructive actions, tooltips
//   audio     auto-start the last device on launch (and what that device was), default sample rate / buffer, SMART START as the default start,
//             clip threshold (dBFS), peak hold time
//   mixer     defaults for the MIXER page (VU faces, monitor panel, plugin rack, channel panel), 0 VU reference lives on the Engine
//   files     where everything is kept, export / import of the whole console as JSON, open the data folder, FACTORY RESET
// Changing a value saves it at once; nothing here phones home.
#pragma once
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariant>

class Engine;

class Settings : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString startPage READ startPage WRITE setStartPage NOTIFY changed)
  Q_PROPERTY(bool confirmDestructive READ confirmDestructive WRITE setConfirmDestructive NOTIFY changed)
  Q_PROPERTY(bool tooltips READ tooltips WRITE setTooltips NOTIFY changed)
  Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY changed)
  Q_PROPERTY(bool colorMotion READ colorMotion WRITE setColorMotion NOTIFY changed)   // the AURORA colour behind the pages moves (off: it stays still, for slow computers)   // the console's look: dark (stage) or light (daylight); Theme.qml and the Qt palette follow
  Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY changed)   // the system is auto on: the remembered device, else AUTO DETECT, at every start (default on)
  Q_PROPERTY(QVariantMap lastDevice READ lastDevice NOTIFY changed)              // backend, in, out, rate, frames, inChannels, outChannels
  Q_PROPERTY(int defaultRate READ defaultRate WRITE setDefaultRate NOTIFY changed)
  Q_PROPERTY(int defaultFrames READ defaultFrames WRITE setDefaultFrames NOTIFY changed)
  Q_PROPERTY(bool smartStart READ smartStart WRITE setSmartStart NOTIFY changed)
  Q_PROPERTY(bool autoAsio READ autoAsio WRITE setAutoAsio NOTIFY changed)        // AUTO DETECT the best device (ASIO first) when none is remembered
  Q_PROPERTY(double clipDb READ clipDb WRITE setClipDb NOTIFY changed)
  Q_PROPERTY(int peakHoldMs READ peakHoldMs WRITE setPeakHoldMs NOTIFY changed)
  Q_PROPERTY(bool vuDefault READ vuDefault WRITE setVuDefault NOTIFY changed)
  Q_PROPERTY(bool bootAnimation READ bootAnimation WRITE setBootAnimation NOTIFY changed)   // the opening animation
  Q_PROPERTY(bool bootSound READ bootSound WRITE setBootSound NOTIFY changed)               // its chord on the master (only when the engine is already running)
  Q_PROPERTY(bool monitorPanelDefault READ monitorPanelDefault WRITE setMonitorPanelDefault NOTIFY changed)
  Q_PROPERTY(bool rackDefault READ rackDefault WRITE setRackDefault NOTIFY changed)
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
  bool darkTheme() const { return s_.value("ui/dark", true).toBool(); } void setDarkTheme(bool v) { set("ui/dark", v); }
  bool colorMotion() const { return s_.value("ui/colorMotion", true).toBool(); } void setColorMotion(bool v) { set("ui/colorMotion", v); }
  bool autoStart() const { return s_.value("audio/autoStart", true).toBool(); } void setAutoStart(bool v) { set("audio/autoStart", v); }
  QVariantMap lastDevice() const { return s_.value("audio/lastDevice").toMap(); }
  int defaultRate() const { return s_.value("audio/rate", 48000).toInt(); } void setDefaultRate(int v) { set("audio/rate", v); }
  int defaultFrames() const { return s_.value("audio/frames", 256).toInt(); } void setDefaultFrames(int v) { set("audio/frames", v); }
  bool autoAsio() const { return false; }   /* automatic selection was removed: the device is always chosen by hand */ void setAutoAsio(bool v) { set("audio/autoDetect", v); }
  bool smartStart() const { return false; }   /* the driver ladder was removed: the chosen device is opened, and no other in its place */ void setSmartStart(bool v) { set("audio/smartStart", v); }
  double clipDb() const { return s_.value("audio/clipDb", -0.1).toDouble(); } void setClipDb(double v);
  int peakHoldMs() const { return s_.value("audio/peakHoldMs", 1400).toInt(); } void setPeakHoldMs(int v);
  bool bootAnimation() const { return s_.value("ui/bootAnimation", true).toBool(); } void setBootAnimation(bool v) { set("ui/bootAnimation", v); }
  bool bootSound() const { return s_.value("ui/bootSound", true).toBool(); } void setBootSound(bool v) { set("ui/bootSound", v); }
  bool vuDefault() const { return s_.value("mixer/vu", false).toBool(); } void setVuDefault(bool v) { set("mixer/vu", v); }
  bool monitorPanelDefault() const { return s_.value("mixer/monitor", true).toBool(); } void setMonitorPanelDefault(bool v) { set("mixer/monitor", v); }
  bool rackCompactDefault() const { return s_.value("mixer/rackCompact", false).toBool(); } void setRackCompactDefault(bool v) { set("mixer/rackCompact", v); }
  bool rackDefault() const { return s_.value("mixer/rack", true).toBool(); } void setRackDefault(bool v) { set("mixer/rack", v); }
  bool channelPanelDefault() const { return s_.value("mixer/channelPanel", true).toBool(); } void setChannelPanelDefault(bool v) { set("mixer/channelPanel", v); }
  QVariantList dataFiles() const;
  QStringList vstDirs() const { return s_.value("vst3/dirs").toStringList(); }
  QStringList vstDefaultDirs() const;   // VST3 and VST2 platform folders
  Q_INVOKABLE int addVstDir(const QString& dir);      // adds and rescans; returns the plug-in count
  Q_INVOKABLE int removeVstDir(const QString& dir);
  Q_INVOKABLE int rescanVst();
  Q_INVOKABLE bool openPluginBuilder();   // starts "AmF X1 Plugin Builder" next to the console (a separate program); its exports show up here after RESCAN
  Q_INVOKABLE QString vstUserFolder();                // the console's own plug-in folder (<data>/vst): created, and saved among the scanned folders, the first time it is asked for
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
  bool setupDone() const { return s_.value("general/setupDone", true).toBool(); }   // true unless a FACTORY RESET asked for the setup: a new installation goes straight to the console
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
