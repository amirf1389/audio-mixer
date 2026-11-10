// I18n: the interface words in 80 languages. Two tables ship as resources: i18n/verified-22.json (the 22 languages of the retired web console,
// checked) and i18n/auto-58.json (58 more, machine translations marked "auto" in the language menu so a native speaker can correct them).
// QML asks t(key, lang) — the lang argument only makes the binding re-evaluate when the language changes; a missing word falls back to English.
#pragma once
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>

class I18n : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString lang READ lang WRITE setLang NOTIFY langChanged)
  Q_PROPERTY(bool rtl READ rtl NOTIFY langChanged)
  Q_PROPERTY(bool autoTranslated READ autoTranslated NOTIFY langChanged)
  Q_PROPERTY(QVariantList languages READ languages CONSTANT)
  Q_PROPERTY(int count READ count CONSTANT)
 public:
  explicit I18n(QObject* parent = nullptr);
  QString lang() const { return lang_; }
  void setLang(const QString& code);
  bool rtl() const { return meta_.value(lang_).rtl; }
  bool autoTranslated() const { return meta_.value(lang_).autoTr; }
  QVariantList languages() const;
  int count() const { return meta_.size(); }
  Q_INVOKABLE QString t(const QString& key, const QString& lang = QString()) const;
  Q_INVOKABLE int coverage(const QString& code) const;   // how many of the known keys this language has
  int keyCount() const { return keys_.size(); }
 signals:
  void langChanged();
 private:
  struct Meta { QString name; bool rtl = false; bool autoTr = false; };
  void loadVerified(); void loadAuto(); void loadUi();
  QHash<QString, Meta> meta_; QStringList order_;
  QHash<QString, QHash<QString, QString>> words_;   // key -> (lang -> text)
  QStringList keys_; QString lang_ = "en"; QSettings settings_;
};
