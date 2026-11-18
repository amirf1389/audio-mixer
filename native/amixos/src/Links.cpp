#include "Links.h"

#include "links/links.hpp"
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

static QVariantMap toMap(const am::links::Link& l) {
  QVariantMap m; m["ok"] = l.ok; m["service"] = QString::fromStdString(l.service); m["serviceName"] = QString::fromStdString(l.serviceName); m["kind"] = QString::fromStdString(l.kind);
  m["id"] = QString::fromStdString(l.id); m["web"] = QString::fromStdString(l.web); m["app"] = QString::fromStdString(l.app); m["error"] = QString::fromStdString(l.error); return m;
}

Links::Links(QObject* parent) : QObject(parent) {
  // the saved list is read like a pasted one: every address goes through the recogniser again, anything it refuses is dropped
  const QJsonArray a = QJsonDocument::fromJson(s_.value("links/list").toByteArray()).array();
  for (const QJsonValue& v : a) { if (list_.size() >= kMax) break; const QJsonObject o = v.toObject(); const am::links::Link l = am::links::parse(o["web"].toString().toStdString()); if (!l.ok) continue;
    QVariantMap m = toMap(l); m["name"] = o["name"].toString().left(80); list_ << m; }
}
QVariantMap Links::check(const QString& url) const { return toMap(am::links::parse(url.toStdString())); }
bool Links::add(const QString& url, const QString& name) {
  const am::links::Link l = am::links::parse(url.toStdString());
  if (!l.ok) { msg_ = QString::fromStdString(l.error); emit changed(); return false; }
  for (const QVariant& v : list_) if (v.toMap()["web"].toString() == QString::fromStdString(l.web)) { msg_ = QStringLiteral("This link is already in the list."); emit changed(); return false; }
  if (list_.size() >= kMax) { msg_ = QStringLiteral("The list is full (200 links): remove one first."); emit changed(); return false; }
  QString n; for (const QChar c : name.trimmed().left(80)) if (c.unicode() >= 0x20) n += c;   // no control characters in a name
  QVariantMap m = toMap(l); m["name"] = n; list_.prepend(m); msg_ = QString::fromStdString(l.serviceName + ": " + l.kind + " added"); save(); emit changed(); return true;
}
void Links::remove(int index) { if (index < 0 || index >= list_.size()) return; list_.removeAt(index); msg_.clear(); save(); emit changed(); }
bool Links::open(int index, bool inApp) {
  if (index < 0 || index >= list_.size()) return false; const QVariantMap m = list_[index].toMap();
  const am::links::Link l = am::links::parse(m["web"].toString().toStdString()); if (!l.ok) return false;   // checked again right before it leaves the program
  const QString target = QString::fromStdString(inApp && !l.app.empty() ? l.app : l.web); lastOpened_ = target;
  const bool ok = dryRun ? true : QDesktopServices::openUrl(QUrl(target));
  msg_ = ok ? QStringLiteral("Opened in %1: the sound plays there.").arg(QString::fromStdString(l.serviceName)) : QStringLiteral("Could not open %1: is its app or a browser installed?").arg(QString::fromStdString(l.serviceName));
  emit changed(); return ok;
}
void Links::save() { QJsonArray a; for (const QVariant& v : list_) { const QVariantMap m = v.toMap(); QJsonObject o; o["web"] = m["web"].toString(); o["name"] = m["name"].toString(); a.append(o); }
  s_.setValue("links/list", QJsonDocument(a).toJson(QJsonDocument::Compact)); }
