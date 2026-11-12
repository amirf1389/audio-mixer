// ACCOUNT / ACCESS / ADMIN: operator roles with a permission matrix (what each role may touch, which pages it sees), a 4-digit PIN that
// locks the fader surface and guards the ADMIN page and the FOH MASTER role, and an append-only audit log in the data folder.
// The PIN is stored as SHA-256(salt + pin), never in clear; the default 1234 applies only until a PIN is set and the pages say so.
#pragma once
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariant>

class Engine;

class Access : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString role READ role NOTIFY changed)                       // master | monitor | broadcast | guest
  Q_PROPERTY(QVariantList roles READ roles CONSTANT)                      // [{id, name, blurb}]
  Q_PROPERTY(QStringList perms READ perms CONSTANT)                       // faders dsp routing loudness archive system
  Q_PROPERTY(QVariantMap matrix READ matrix NOTIFY changed)               // {role: {perm: bool}}
  Q_PROPERTY(bool surfaceLocked READ surfaceLocked NOTIFY changed)
  Q_PROPERTY(bool adminMode READ adminMode NOTIFY changed)                // PIN verified for ADMIN; cleared by lock() and after 30 min
  Q_PROPERTY(bool pinIsDefault READ pinIsDefault NOTIFY changed)
  Q_PROPERTY(bool pinAtStart READ pinAtStart WRITE setPinAtStart NOTIFY changed)   // the console starts locked: faders, mutes and solos wait for the PIN
  Q_PROPERTY(bool consumerEdition READ consumerEdition WRITE setConsumerEdition NOTIFY changed)   // hides ACCOUNT / ACCESS / ADMIN / DIAGNOSTICS
  Q_PROPERTY(QVariantList audit READ audit NOTIFY auditChanged)           // newest last: [{at, who, text}]
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
  Q_PROPERTY(int failedPins READ failedPins NOTIFY changed)
 public:
  explicit Access(Engine* engine, QObject* parent = nullptr);
  QString role() const { return s_.value("access/role", "master").toString(); }
  QVariantList roles() const;
  QStringList perms() const { return {"faders", "dsp", "routing", "loudness", "archive", "system"}; }
  QVariantMap matrix() const;
  bool surfaceLocked() const { return locked_; }
  bool adminMode() const;
  bool pinIsDefault() const { return !s_.contains("access/pinHash"); }
  bool pinAtStart() const { return s_.value("access/pinAtStart", false).toBool() && !pinIsDefault(); } void setPinAtStart(bool on);
  bool consumerEdition() const { return s_.value("access/consumer", false).toBool(); } void setConsumerEdition(bool v);
  QVariantList audit() const;
  QString message() const { return msg_; }
  int failedPins() const { return failed_; }
  Q_INVOKABLE bool can(const QString& perm) const;                       // the current role, and the surface lock for "faders"
  Q_INVOKABLE bool canPage(const QString& pageId) const;                 // which pages the current role sees
  Q_INVOKABLE QString permOfPage(const QString& pageId) const;
  Q_INVOKABLE bool setRole(const QString& id, const QString& pin);       // a role with more rights than the current one needs the PIN
  Q_INVOKABLE void setPerm(const QString& role, const QString& perm, bool on);   // ADMIN only (adminMode)
  Q_INVOKABLE void resetMatrix();
  Q_INVOKABLE void lock();                                               // faders, mutes and solos stop reacting until unlock(pin)
  Q_INVOKABLE bool unlock(const QString& pin);
  Q_INVOKABLE bool enterAdmin(const QString& pin);
  Q_INVOKABLE void leaveAdmin();
  Q_INVOKABLE bool setPin(const QString& oldPin, const QString& newPin);   // 4..8 digits
  Q_INVOKABLE void log(const QString& text);                              // one audit line, stamped with the time and the role
  Q_INVOKABLE void clearAudit();
  Q_INVOKABLE QString exportAudit();                                      // CSV copy in the data folder; returns its path
 signals:
  void changed();
  void auditChanged();
  void messageChanged();
 private:
  bool checkPin(const QString& pin);
  static QString hashPin(const QString& pin);
  QString auditPath() const;
  void note(const QString& m) { msg_ = m; emit messageChanged(); }
  Engine* engine_; QSettings s_; bool locked_ = false; qint64 adminAt_ = 0; int failed_ = 0; QString msg_;
};
