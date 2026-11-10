// UPDATES: the signed manifest check (releases/update.json: {"payload": "<json>", "signature": "<base64url r||s>"}). The signature is
// ECDSA P-256 over SHA-256 of the exact payload text with the vendor's public key (the same key the licence check trusts); only then is
// the payload parsed. Downloads come only from the project's own hosts (update_logic.hpp hostAllowed), are checked against the manifest's
// sha256 and are never run by the console: the person opens the installer. Nothing is sent but the GET; no identifiers, no telemetry.
#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QVariant>

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
 public:
  explicit Update(const QString& currentVersion, QObject* parent = nullptr);
  QString state() const { return state_; } QString message() const { return msg_; } QString latest() const { return latest_; } QString channel() const { return channel_; } QString released() const { return released_; }
  QStringList notes() const { return notes_; } QVariantMap file() const { return file_; } QString downloaded() const { return downloaded_; } QString current() const { return current_; } int progress() const { return progress_; }
  QString url() const { return s_.value("update/url", kDefaultUrl).toString(); } void setUrl(const QString& u);
  bool checkAtStart() const { return s_.value("update/checkAtStart", true).toBool(); } void setCheckAtStart(bool v) { s_.setValue("update/checkAtStart", v); emit changed(); }
  QString lastCheck() const { return s_.value("update/lastCheck").toString(); }
  Q_INVOKABLE void check();                                         // GET the manifest, verify, compare
  Q_INVOKABLE bool applyManifest(const QString& envelopeText);      // the same without the network (a file, the smoke test); false = rejected, with the reason in message
  Q_INVOKABLE void download();                                      // this platform's file into the data folder, sha256 checked
  Q_INVOKABLE void openDownloadFolder();
  Q_INVOKABLE static int compareVersions(const QString& a, const QString& b);
  static const char* kDefaultUrl;
 signals:
  void changed();
 private:
  void set(const QString& state, const QString& msg) { state_ = state; msg_ = msg; emit changed(); }
  static QString platformKey();
  QSettings s_; QNetworkAccessManager net_; QString current_, state_ = "idle", msg_, latest_, channel_, released_, downloaded_; QStringList notes_; QVariantMap file_; int progress_ = 0;
};
