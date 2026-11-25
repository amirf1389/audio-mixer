#include "ProTools.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

ProTools::ProTools(const QString& stateFile, QObject* parent) : QObject(parent), state_(stateFile) { load(); }
bool ProTools::tierAllows(const QString& e) { const QString u = e.toUpper(); return u == "PRO" || u == "STUDIO" || u == "MASTER"; }
QString ProTools::sessionName(const QDateTime& at) { return QStringLiteral("Session ") + at.toString(QStringLiteral("yyyy-MM-dd HH-mm-ss")); }
QString ProTools::makeSessionFolder(const QString& base) {
  if (base.isEmpty()) return {};
  QDir d(base); QString name = sessionName(QDateTime::currentDateTime()); for (int k = 2; d.exists(name) && k < 100; ++k) name = sessionName(QDateTime::currentDateTime()) + QStringLiteral(" (%1)").arg(k);
  if (!d.mkpath(name)) return {};
  session_ = d.filePath(name); emit markersChanged(); return session_; }
double ProTools::correlation(const QVector<double>& lr) {
  double lr2 = 0, ll = 0, rr = 0; for (int i = 0; i + 1 < lr.size(); i += 2) { lr2 += lr[i] * lr[i + 1]; ll += lr[i] * lr[i]; rr += lr[i + 1] * lr[i + 1]; }
  const double den = std::sqrt(ll * rr); return den < 1e-12 ? 0.0 : std::clamp(lr2 / den, -1.0, 1.0); }
static QVector<double> toVec(const QVariantList& l) { QVector<double> v; v.reserve(l.size()); for (const QVariant& x : l) v << x.toDouble(); return v; }
double ProTools::correlationOf(const QVariantList& lr) const { return correlation(toVec(lr)); }
QVariantList ProTools::goniometer(const QVariantList& lr) const {
  const QVector<double> v = toVec(lr); QVariantList out; out.reserve(v.size()); const double k = 0.70710678;
  for (int i = 0; i + 1 < v.size(); i += 2) { const double s = (v[i] - v[i + 1]) * k, m = (v[i] + v[i + 1]) * k; out << std::clamp(0.5 + s * 0.5, 0.0, 1.0) << std::clamp(0.5 - m * 0.5, 0.0, 1.0); }
  return out; }
void ProTools::pushLoudness(double m, double s) { hist_.push_back({std::max(-70.0, m), std::max(-70.0, s)}); if (hist_.size() > 600) hist_.remove(0, hist_.size() - 600); emit historyChanged(); }
QVariantList ProTools::loudHistory() const { QVariantList l; l.reserve(hist_.size() * 2); for (const auto& p : hist_) l << p.first << p.second; return l; }
QVariantList ProTools::markers() const { QVariantList l; for (const Marker& m : marks_) l << QVariantMap{{"id", m.id}, {"t", m.t}, {"label", m.label}}; return l; }
int ProTools::addMarker(double t, const QString& label) { Marker m{nextId_++, std::max(0.0, t), label.trimmed().isEmpty() ? QStringLiteral("Marker %1").arg(nextId_ - 1) : label.trimmed().left(120)};
  marks_.push_back(m); std::stable_sort(marks_.begin(), marks_.end(), [](const Marker& a, const Marker& b) { return a.t < b.t; }); persist(); emit markersChanged(); return m.id; }
bool ProTools::renameMarker(int id, const QString& label) { for (Marker& m : marks_) if (m.id == id) { m.label = label.trimmed().left(120); persist(); emit markersChanged(); return true; } return false; }
bool ProTools::removeMarker(int id) { for (int i = 0; i < marks_.size(); ++i) if (marks_[i].id == id) { marks_.remove(i); persist(); emit markersChanged(); return true; } return false; }
void ProTools::clearMarkers() { marks_.clear(); persist(); emit markersChanged(); }
void ProTools::setNotes(const QString& n) { if (n == notes_) return; notes_ = n.left(20000); persist(); emit markersChanged(); }
static QString csvField(QString s) { s.replace('"', QStringLiteral("\"\"")); if (!s.isEmpty() && QStringLiteral("=+-@").contains(s[0])) s.prepend('\''); return '"' + s + '"'; }   // no formula injection in a spreadsheet
QString ProTools::exportText() const {
  QString out = QStringLiteral("time_s,time,label\n");
  for (const Marker& m : marks_) { const int t = int(m.t); out += QStringLiteral("%1,%2:%3:%4,%5\n").arg(m.t, 0, 'f', 3).arg(t / 3600, 2, 10, QChar('0')).arg(t / 60 % 60, 2, 10, QChar('0')).arg(t % 60, 2, 10, QChar('0')).arg(csvField(m.label)); }
  if (!notes_.isEmpty()) out += QStringLiteral("\nnotes,") + csvField(notes_) + '\n';
  return out; }
bool ProTools::saveTo(const QString& folder) const { if (folder.isEmpty() || !QDir(folder).exists()) return false; QSaveFile f(QDir(folder).filePath(QStringLiteral("markers.csv")));
  if (!f.open(QIODevice::WriteOnly)) return false; f.write(exportText().toUtf8()); return f.commit(); }
void ProTools::persist() const { if (state_.isEmpty()) return; QJsonArray a; for (const Marker& m : marks_) a.append(QJsonObject{{"id", m.id}, {"t", m.t}, {"label", m.label}});
  QSaveFile f(state_); if (!f.open(QIODevice::WriteOnly)) return; f.write(QJsonDocument(QJsonObject{{"markers", a}, {"notes", notes_}, {"next", nextId_}}).toJson(QJsonDocument::Compact)); f.commit(); }
void ProTools::load() { if (state_.isEmpty()) return; QFile f(state_); if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) return; const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  for (const QJsonValue& v : o.value("markers").toArray()) { const QJsonObject m = v.toObject(); marks_.push_back({m.value("id").toInt(), m.value("t").toDouble(), m.value("label").toString().left(120)}); }
  notes_ = o.value("notes").toString().left(20000); nextId_ = std::max(o.value("next").toInt(1), 1); for (const Marker& m : marks_) nextId_ = std::max(nextId_, m.id + 1); }
