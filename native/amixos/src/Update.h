// UPDATES: the signed manifest check (releases/update.json: {"payload": "<json>", "signature": "<base64url r||s>"}). The signature is
// ECDSA P-256 over SHA-256 of the exact payload text with the vendor's public key (the same key the licence check trusts); only then is
// the payload parsed. Downloads come only from the project's own hosts (update_logic.hpp hostAllowed), are checked against the manifest's
// sha256 and are never run by the console: the person opens the installer. Nothing is sent but the GET; no identifiers, no telemetry.
#pragma once
#include <QNetworkAccessManager>
#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <functional>
#include <QSettings>
#include <QVariant>
#include <vector>
#include "license/license.hpp"

class Update : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString state READ state NOTIFY changed)              // idle | checking | upToDate | available | downloading | downloaded | error
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(QString latest READ latest NOTIFY changed)
  Q_PROPERTY(QString channel READ channel NOTIFY changed)
  Q_PROPERTY(QString released READ released NOTIFY changed)
  Q_PROPERTY(QStringList notes READ notes NOTIFY changed)
  Q_PROPERTY(QVariantMap file READ file NOTIFY changed)           // {key, name, url, size, sha256} for this platform, or empty
  Q_PROPERTY(QString downloaded READ downloaded NOTIFY changed)   // the verified local file after download()
  Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY changed)
  Q_PROPERTY(bool checkAtStart READ checkAtStart WRITE setCheckAtStart NOTIFY changed)
  Q_PROPERTY(QString lastCheck READ lastCheck NOTIFY changed)
  Q_PROPERTY(QString current READ current CONSTANT)
  Q_PROPERTY(int progress READ progress NOTIFY changed)
  // THE UPDATE NOTICE (am::upd::noticeFor): a banner in the console, a badge on SOFTWARE UPDATE in the menu, one Windows notification
  Q_PROPERTY(bool found READ found NOTIFY changed)                       // the signed manifest names a newer version than this one
  Q_PROPERTY(bool noticeBanner READ noticeBanner NOTIFY changed)
  Q_PROPERTY(bool noticeBadge READ noticeBadge NOTIFY changed)
  Q_PROPERTY(bool systemNotice READ systemNotice WRITE setSystemNotice NOTIFY changed)   // Windows notification when a new version is found (on by default)
  Q_PROPERTY(QString skippedVersion READ skippedVersion NOTIFY changed)
  Q_PROPERTY(QVariantList features READ features NOTIFY changed)   // the signed feature feed: [{version, date, title, items[]}], newest first
  Q_PROPERTY(int newFeatures READ newFeatures NOTIFY changed)
  Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)        // a verified installer is downloaded and this platform can install it and restart (Windows)
  Q_PROPERTY(QString justUpdated READ justUpdated NOTIFY changed)  // the version an update just brought (shown once at boot), else empty
  Q_PROPERTY(QString failedUpdate READ failedUpdate NOTIFY changed) // an INSTALL AND RESTART that came back on the old version
  // PLUG-IN DOWNLOADS: the plug-ins the vendor-signed list offers for this platform: [{id, name, version, note, file, size, installed, busy, progress}].
  // A download is https from one of the project's hosts, checked against the signed sha256 and size, and saved as one file into the console's
  // own plug-in folder; nothing is run. pluginMessage says what happened; pluginInstalled fires so the plug-in list is scanned again.
  Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
  Q_PROPERTY(QString pluginMessage READ pluginMessage NOTIFY pluginsChanged)
  Q_PROPERTY(bool autoDownload READ autoDownload WRITE setAutoDownload NOTIFY changed)   // fetch a found update by itself (the install still asks)      // entries newer than the last version the person looked at (the NEW badge)
 public:
  explicit Update(const QString& currentVersion, QObject* parent = nullptr);
  QString state() const { return state_; } QString message() const { return msg_; } QString latest() const { return latest_; } QString channel() const { return channel_; } QString released() const { return released_; }
  QStringList notes() const { return notes_; } QVariantMap file() const { return file_; } QString downloaded() const { return downloaded_; } QString current() const { return current_; } int progress() const { return progress_; }
  QString url() const { return s_.value("update/url", kDefaultUrl).toString(); } void setUrl(const QString& u);
  bool checkAtStart() const { return s_.value("update/checkAtStart", true).toBool(); } void setCheckAtStart(bool v) { s_.setValue("update/checkAtStart", v); emit changed(); }
  QString lastCheck() const { return s_.value("update/lastCheck").toString(); }
  Q_PROPERTY(QString licenseServer READ licenseServer WRITE setLicenseServer NOTIFY changed)   // where a product key is traded for the full key (https)
  Q_PROPERTY(QString redeemState READ redeemState NOTIFY changed)                              // "" | asking | done | the reason it failed
  QString licenseServer() const { return s_.value("licence/server", QStringLiteral("https://licence.amirfakhribadri.my")).toString(); }   /* the vendor's licence server unless another is typed */ void setLicenseServer(const QString& u);
  QString redeemState() const { return redeem_; }
  Q_INVOKABLE bool setServerLink(const QString& url);          // the update server link: any https host; its list must still be signed
  Q_INVOKABLE void redeemProductKey(const QString& code);
  // MANAGE LICENCE ONLINE: <licence server>/licence/?machine=<code>&edition=<plan> (the customer licence site), "" without a usable https server
  Q_INVOKABLE QString manageLicenceUrl(const QString& machine, const QString& edition) const;
  // ACTIVATION (Windows-style): a product key, licence key (AMIX1.) or edition token (AMFXE1.) + this computer's code -> activated(token)
  Q_INVOKABLE void activateOnline(const QString& text, const QString& machine);
  Q_INVOKABLE void startTrial(const QString& machine);       // a 30-day trial for this computer -> activated(token)
  Q_INVOKABLE void fetchRevocations();                       // GET <licence server>/revocations -> revocations(list)      // POST <licence server>/redeem; redeemed(key) when the server answers with a key
  Q_INVOKABLE void check();                                         // GET the manifest, verify, compare
  Q_INVOKABLE bool applyManifest(const QString& envelopeText);      // the same without the network (a file, the smoke test); false = rejected, with the reason in message
  Q_INVOKABLE void download();                                      // this platform's file into the data folder, sha256 checked
  Q_INVOKABLE void openDownloadFolder();
  // LOCAL (OFFLINE) UPDATE, like a firmware update from a USB stick: a folder (or the update.json in it, or the setup beside it) that holds
  // the vendor-signed update.json and this platform's setup. The signature and the setup's sha256 are checked exactly as for an update
  // from the internet; nothing unsigned or changed is ever offered. Then INSTALL AND RESTART installs it. An older version than this one
  // is refused unless allowOlder (going back on purpose).
  Q_INVOKABLE bool loadLocal(const QString& path, bool allowOlder = false);
  Q_INVOKABLE void clearLocal() { if (file_.value("url").toString().startsWith("file:")) { downloaded_.clear(); file_.clear(); latest_.clear(); notes_.clear(); progress_ = 0; } set("idle", QString()); }   // forget a verified local update
  Q_INVOKABLE QVariantList scanLocal();   // update bundles in Downloads, on the Desktop and at the top of every drive (USB sticks): [{path, version, ok, why, older}]
  bool loadLocalWith(const QString& path, bool allowOlder, const std::vector<am::lic::Jwk>& trusted);   // the same with other trusted keys (the tests)
  bool canInstall() const; QString justUpdated() const { return justUpdated_; } QString failedUpdate() const { return failed_; }
  bool autoDownload() const { return s_.value("update/autoDownload", true).toBool(); } void setAutoDownload(bool v) { s_.setValue("update/autoDownload", v); emit changed(); }
  Q_INVOKABLE bool installAndRestart();                             // re-checks the file's sha256, starts the setup silently with /RESTART and quits the console; the setup starts it again
  Q_INVOKABLE void acknowledgeUpdate() { justUpdated_.clear(); failed_.clear(); emit changed(); }
 signals:
  void restarting();
 public:
  QVariantList features() const { return features_; }
  int newFeatures() const;
  Q_INVOKABLE void markFeaturesSeen();                              // the badge goes out until a newer entry arrives
  Q_INVOKABLE static int compareVersions(const QString& a, const QString& b);
  static const char* kDefaultUrl;
 public:
  QVariantList plugins() const; QString pluginMessage() const { return pluginMsg_; }
  Q_INVOKABLE void downloadPlugin(const QString& id);
  Q_INVOKABLE bool removePlugin(const QString& id);                 // deletes the downloaded file (only one this list put there)
  int setPluginList(const QJsonArray& list);                        // from the signed manifest (and the self-test): returns how many entries were accepted
  QString pluginDir;                                                // the console's own plug-in folder (main sets it)
  bool found() const; bool noticeBanner() const; bool noticeBadge() const;
  bool systemNotice() const { return s_.value("update/systemNotice", true).toBool(); } void setSystemNotice(bool v) { s_.setValue("update/systemNotice", v); emit changed(); }
  QString skippedVersion() const { return s_.value("update/skipped").toString(); }
  Q_INVOKABLE void remindLater(int hours = 24);     // LATER: the banner comes back after that time (the badge stays)
  Q_INVOKABLE void skipThisVersion();               // SKIP THIS VERSION: nothing more about this one; the next version is announced again
  Q_INVOKABLE void unskip();                        // SOFTWARE UPDATE: "show me this version after all"
  Q_INVOKABLE void markUpdateSeen();                // SOFTWARE UPDATE was opened: the badge goes
  std::function<qint64()> clock;                    // tests: the time (ms); empty = the system's
 signals:
  void notifySystem(const QString& title, const QString& text);   // main shows it as a Windows notification
  void pluginsChanged();
  void pluginInstalled();
  void changed();
  void redeemed(const QString& key);
  void activated(const QString& activation);
  void revocations(const QString& list);
 private:
  void set(const QString& state, const QString& msg) { state_ = state; msg_ = msg; emit changed(); announce(); }
  void announce();   // a newer version found for the first time: one system notification (when switched on)
  QTimer laterTimer_, recheck_;
  static QString platformKey();
  QString redeem_;
  QVariantList pluginList_; QString pluginMsg_, pluginBusy_; int pluginProgress_ = 0;
  QSettings s_; QNetworkAccessManager net_; QString current_, state_ = "idle", msg_, latest_, channel_, released_, downloaded_; QStringList notes_; QVariantMap file_; QVariantList features_; QString justUpdated_, failed_; int progress_ = 0;
};
