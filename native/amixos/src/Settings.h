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
#include <QVariant>

class Engine;

class Settings : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString startPage READ startPage WRITE setStartPage NOTIFY changed)
  Q_PROPERTY(bool confirmDestructive READ confirmDestructive WRITE setConfirmDestructive NOTIFY changed)
  Q_PROPERTY(bool tooltips READ tooltips WRITE setTooltips NOTIFY changed)
  Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY changed)
  Q_PROPERTY(QVariantMap lastDevice READ lastDevice NOTIFY changed)              // backend, in, out, rate, frames, inChannels, outChannels
  Q_PROPERTY(int defaultRate READ defaultRate WRITE setDefaultRate NOTIFY changed)
  Q_PROPERTY(int defaultFrames READ defaultFrames WRITE setDefaultFrames NOTIFY changed)
  Q_PROPERTY(bool smartStart READ smartStart WRITE setSmartStart NOTIFY changed)
  Q_PROPERTY(double clipDb READ clipDb WRITE setClipDb NOTIFY changed)
  Q_PROPERTY(int peakHoldMs READ peakHoldMs WRITE setPeakHoldMs NOTIFY changed)
  Q_PROPERTY(bool vuDefault READ vuDefault WRITE setVuDefault NOTIFY changed)
  Q_PROPERTY(bool monitorPanelDefault READ monitorPanelDefault WRITE setMonitorPanelDefault NOTIFY changed)
  Q_PROPERTY(bool rackDefault READ rackDefault WRITE setRackDefault NOTIFY changed)
  Q_PROPERTY(bool channelPanelDefault READ channelPanelDefault WRITE setChannelPanelDefault NOTIFY changed)
  Q_PROPERTY(QVariantList dataFiles READ dataFiles NOTIFY changed)              // [{name, path, exists, bytes}]
  Q_PROPERTY(QString dataFolder READ dataFolder CONSTANT)
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)

 public:
  explicit Settings(Engine* engine, QObject* parent = nullptr);
  QString startPage() const { return s_.value("general/startPage", "home").toString(); } void setStartPage(const QString& v) { set("general/startPage", v); }
  bool confirmDestructive() const { return s_.value("general/confirm", true).toBool(); } void setConfirmDestructive(bool v) { set("general/confirm", v); }
  bool tooltips() const { return s_.value("general/tooltips", true).toBool(); } void setTooltips(bool v) { set("general/tooltips", v); }
  bool autoStart() const { return s_.value("audio/autoStart", false).toBool(); } void setAutoStart(bool v) { set("audio/autoStart", v); }
  QVariantMap lastDevice() const { return s_.value("audio/lastDevice").toMap(); }
  int defaultRate() const { return s_.value("audio/rate", 48000).toInt(); } void setDefaultRate(int v) { set("audio/rate", v); }
  int defaultFrames() const { return s_.value("audio/frames", 256).toInt(); } void setDefaultFrames(int v) { set("audio/frames", v); }
  bool smartStart() const { return s_.value("audio/smartStart", true).toBool(); } void setSmartStart(bool v) { set("audio/smartStart", v); }
  double clipDb() const { return s_.value("audio/clipDb", -0.1).toDouble(); } void setClipDb(double v);
  int peakHoldMs() const { return s_.value("audio/peakHoldMs", 1400).toInt(); } void setPeakHoldMs(int v);
  bool vuDefault() const { return s_.value("mixer/vu", false).toBool(); } void setVuDefault(bool v) { set("mixer/vu", v); }
  bool monitorPanelDefault() const { return s_.value("mixer/monitor", true).toBool(); } void setMonitorPanelDefault(bool v) { set("mixer/monitor", v); }
  bool rackDefault() const { return s_.value("mixer/rack", true).toBool(); } void setRackDefault(bool v) { set("mixer/rack", v); }
  bool channelPanelDefault() const { return s_.value("mixer/channelPanel", true).toBool(); } void setChannelPanelDefault(bool v) { set("mixer/channelPanel", v); }
  QVariantList dataFiles() const;
  QString dataFolder() const;
  QString message() const { return msg_; }

  Q_INVOKABLE void rememberDevice(const QString& backend, const QString& in, const QString& out, double rate, int frames, int inCh, int outCh);
  Q_INVOKABLE bool startLastDevice();                 // what auto-start does: opens the remembered device (SMART START when enabled)
  Q_INVOKABLE QString exportConsole(const QString& path);   // the whole console + scenes + presets as one JSON file ("" = ~/amixos-console.json)
  Q_INVOKABLE bool importConsole(const QString& path);
  Q_INVOKABLE bool openDataFolder();
  Q_INVOKABLE QStringList factoryResetPreview() const;      // what FACTORY RESET removes
  Q_INVOKABLE void factoryReset();                           // settings, console, scenes, presets, arrangement, driver locks; the licence and the token stay
  Q_INVOKABLE QString settingsFile() const { return s_.fileName(); }

 signals:
  void changed();
  void messageChanged();

 private:
  void set(const QString& k, const QVariant& v) { if (s_.value(k) == v) return; s_.setValue(k, v); s_.sync(); emit changed(); }
  void note(const QString& m) { msg_ = m; emit messageChanged(); }
  Engine* engine_; QSettings s_; QString msg_;
};
