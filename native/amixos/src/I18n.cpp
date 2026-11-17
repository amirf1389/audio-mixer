#include <QSet>
#include <QRegularExpression>
#include "I18n.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static QJsonDocument readJson(const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return QJsonDocument::fromJson(f.readAll()); }

I18n::I18n(QObject* parent) : QObject(parent) {
  loadVerified(); loadAuto(); loadUi();
  const QString saved = settings_.value("ui/lang", "en").toString();
  lang_ = offered(saved) ? saved : "en";
}

void I18n::loadVerified() {
  // the 22 languages of the web console, in its column order
  struct L { const char* code; const char* name; bool rtl; };
  static const L k[] = {{"en", "English", false}, {"fa", "فارسی", true}, {"ar", "العربية", true}, {"ur", "اردو", true}, {"es", "Español", false}, {"fr", "Français", false}, {"de", "Deutsch", false}, {"pt", "Português", false},
    {"ru", "Русский", false}, {"tr", "Türkçe", false}, {"hi", "हिन्दी", false}, {"zh", "中文", false}, {"ja", "日本語", false}, {"ko", "한국어", false}, {"ms", "Bahasa Melayu", false}, {"id", "Bahasa Indonesia", false},
    {"it", "Italiano", false}, {"nl", "Nederlands", false}, {"pl", "Polski", false}, {"vi", "Tiếng Việt", false}, {"th", "ไทย", false}, {"uk", "Українська", false}};
  for (const L& l : k) { meta_.insert(l.code, {l.name, l.rtl, false}); order_ << l.code; }
  const QJsonObject o = readJson(":/qt/qml/AmixOS/i18n/verified-22.json").object();
  for (auto it = o.begin(); it != o.end(); ++it) {
    const QJsonObject row = it.value().toObject(); QHash<QString, QString> h;
    for (auto jt = row.begin(); jt != row.end(); ++jt) h.insert(jt.key(), jt.value().toString());
    words_.insert(it.key(), h); if (!keys_.contains(it.key())) keys_ << it.key();
  }
}

void I18n::loadAuto() {
  const QJsonObject o = readJson(":/qt/qml/AmixOS/i18n/auto-58.json").object();
  const QJsonArray keys = o.value("keys").toArray(); QStringList ks; for (const QJsonValue& v : keys) ks << v.toString();
  for (const QString& key : ks) { if (!words_.contains(key)) words_.insert(key, {}); if (!keys_.contains(key)) keys_ << key; }
  const QJsonObject langs = o.value("languages").toObject();
  for (auto it = langs.begin(); it != langs.end(); ++it) {
    const QJsonObject l = it.value().toObject();
    if (!meta_.contains(it.key())) { meta_.insert(it.key(), {l.value("name").toString(it.key()), l.value("rtl").toBool(), true}); order_ << it.key(); }
    const QJsonArray words = l.value("words").toArray();
    for (int i = 0; i < ks.size() && i < words.size(); ++i) { const QString w = words[i].toString(); if (!w.isEmpty() && !words_[ks[i]].contains(it.key())) words_[ks[i]].insert(it.key(), w); }
  }
}

void I18n::loadUi() {
  // the interface strings (English text = key), one file per language under i18n/ui/; a missing word falls back to English
  const QStringList langs = {"fa", "ar", "ur", "es", "fr", "de", "pt", "ru", "tr", "hi", "zh", "ja", "ko", "ms", "id", "it", "nl", "pl", "vi", "th", "uk"};
  for (const QString& l : langs) { const QJsonObject o = readJson(":/qt/qml/AmixOS/i18n/ui/" + l + ".json").object(); if (o.isEmpty()) continue;
    for (auto it = o.begin(); it != o.end(); ++it) { if (!words_.contains(it.key())) { words_.insert(it.key(), {}); keys_ << it.key(); } words_[it.key()].insert(l, it.value().toString()); } }
}
// OFFERED LANGUAGES: all of them. The 21 with full interface tables are translated throughout; the 58 machine-translated ones ("auto")
// translate the page names and the words they have, and show English for the rest until their tables are filled.
bool I18n::offered(const QString& code) const { return meta_.contains(code); }
void I18n::setLang(const QString& code) { if (!meta_.contains(code) || !offered(code) || code == lang_) return; lang_ = code; settings_.setValue("ui/lang", code); emit langChanged(); }

QVariantList I18n::languages() const {
  QVariantList l;
  for (const QString& c : order_) { if (!offered(c)) continue; const Meta& m = meta_.value(c); QVariantMap x; x["code"] = c; x["name"] = m.name; x["rtl"] = m.rtl; x["auto"] = m.autoTr; x["coverage"] = coverage(c); l << x; }
  return l;
}

QString I18n::t(const QString& key, const QString& lang) const {
  const QString code = lang.isEmpty() ? lang_ : lang;
  const auto it = words_.constFind(key);
  if (it == words_.constEnd() || it->value(code).isEmpty()) {   // AMIX_I18N_MISSING=<file>: every string shown untranslated in this language is written there once (a check tool)
    static const QString miss = qEnvironmentVariable("AMIX_I18N_MISSING");
    if (!miss.isEmpty() && code != "en" && key.contains(QRegularExpression("[A-Za-z]{2,}"))) { static QSet<QString> seen; if (!seen.contains(key)) { seen.insert(key); QFile f(miss); if (f.open(QIODevice::Append | QIODevice::Text)) f.write((key + "\u001e\n").toUtf8()); } }
    if (it == words_.constEnd()) return key; }
  const QString w = it->value(code); return w.isEmpty() ? it->value("en", key) : w;
}

int I18n::coverage(const QString& code) const { int n = 0; for (const QString& k : keys_) if (!words_.value(k).value(code).isEmpty()) ++n; return n; }
