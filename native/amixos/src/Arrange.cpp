#include "Arrange.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <cmath>

#include "Engine.h"

using namespace am;

Arrange::Arrange(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  load();
  engine_->setExtraSource(&player_);
  tick_.setInterval(33); connect(&tick_, &QTimer::timeout, this, [this] { const bool pl = player_.playing(); if (pl || wasPlaying_) emit transportChanged(); wasPlaying_ = pl; }); tick_.start();
}
Arrange::~Arrange() { engine_->setExtraSource(nullptr); save(); }

QString Arrange::projectFile() const { return QDir::homePath() + "/.amixos/arrangement.json"; }
void Arrange::note(const QString& m) { msg_ = m; emit messageChanged(); }
void Arrange::push() { player_.setProject(p_, bufs_); player_.setLoop(p_.loop); emit projectChanged(); }

bool Arrange::loadFile(const std::string& path) {
  if (bufs_.count(path)) return true;
  auto a = std::make_shared<arr::Audio>(); std::string err;
  if (!arr::readWav(path, *a, err)) { note(QString::fromStdString(err)); return false; }
  bufs_[path] = a; return true;
}
void Arrange::load() {
  QFile f(projectFile()); if (f.open(QIODevice::ReadOnly)) { bool ok = false; const json::Value v = json::Value::parse(f.readAll().toStdString(), ok); if (ok) p_ = arr::fromJson(v); }
  int missing = 0; for (const arr::Clip& c : p_.clips) if (!loadFile(c.file)) ++missing;
  if (missing) note(QStringLiteral("%1 clip file(s) could not be read: shown as MISSING, silent until the file is back.").arg(missing));
  push();
}
void Arrange::save() { QDir().mkpath(QFileInfo(projectFile()).path()); QFile f(projectFile()); if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QByteArray::fromStdString(arr::toJson(p_).dump() + "\n")); }

QVariantList Arrange::tracks() const {
  QVariantList l; const bool solo = arr::anySolo(p_);
  for (const arr::Track& t : p_.tracks) { QVariantMap m; m["id"] = t.id; m["name"] = QString::fromStdString(t.name); m["db"] = t.db; m["pan"] = t.pan; m["mute"] = t.mute; m["solo"] = t.solo; m["color"] = t.color; m["audible"] = arr::trackGain(t, solo) > 0;
    QVariantList a; for (const arr::AutoPoint& q : t.autoPts) { QVariantMap x; x["t"] = q.t; x["v"] = q.v; a << x; } m["auto"] = a; l << m; }
  return l;
}
QVariantList Arrange::clips() const {
  QVariantList l;
  for (const arr::Clip& c : p_.clips) { QVariantMap m; m["id"] = c.id; m["track"] = c.track; m["file"] = QString::fromStdString(c.file); m["name"] = QFileInfo(QString::fromStdString(c.file)).completeBaseName(); m["start"] = c.start; m["end"] = arr::clipEnd(c); m["offset"] = c.offset; m["db"] = c.db; m["missing"] = !bufs_.count(c.file); l << m; }
  return l;
}
QVariantList Arrange::markers() const { QVariantList l; for (const arr::Marker& m : p_.markers) { QVariantMap x; x["t"] = m.t; x["name"] = QString::fromStdString(m.name); l << x; } return l; }

void Arrange::setBpm(double v) { p_.bpm = arr::clampd(v, 20, 300); push(); }
void Arrange::setSnap(int v) { p_.snap = (v == 0 || v == 1 || v == 4 || v == 8 || v == 16) ? v : 4; push(); }
void Arrange::setLoop(bool v) { p_.loop = v; push(); }
void Arrange::setZoom(double v) { p_.zoom = arr::clampd(v, 5, 400); emit projectChanged(); }

void Arrange::play(double from) {
  if (!engine_->running()) { note(QStringLiteral("Start the engine on CONNECTIONS first: the arrangement plays through the master.")); return; }
  player_.play(from < 0 ? player_.position() : from); emit transportChanged();
}
void Arrange::stop() { player_.stop(); emit transportChanged(); }
void Arrange::toStart() { player_.stop(); player_.seek(0); emit transportChanged(); }
void Arrange::seek(double s) { player_.seek(std::max(0.0, s)); emit transportChanged(); }

int Arrange::addTrack(const QString& name) {
  arr::Track t; t.id = arr::nextId(p_); t.name = (name.isEmpty() ? QStringLiteral("Track %1").arg(p_.tracks.size() + 1) : name.left(80)).toStdString(); t.color = static_cast<int>(p_.tracks.size() % 8);
  p_.tracks.push_back(t); push(); save(); return t.id;
}
void Arrange::removeTrack(int id) {
  p_.tracks.erase(std::remove_if(p_.tracks.begin(), p_.tracks.end(), [&](const arr::Track& t) { return t.id == id; }), p_.tracks.end());
  p_.clips.erase(std::remove_if(p_.clips.begin(), p_.clips.end(), [&](const arr::Clip& c) { return c.track == id; }), p_.clips.end());
  push(); save();
}
void Arrange::setTrack(int id, const QString& key, const QVariant& value) {
  for (arr::Track& t : p_.tracks) if (t.id == id) {
    if (key == "db") t.db = arr::clampd(value.toDouble(), -60, 6); else if (key == "pan") t.pan = arr::clampd(value.toDouble(), -1, 1); else if (key == "mute") t.mute = value.toBool(); else if (key == "solo") t.solo = value.toBool();
    else if (key == "name") t.name = value.toString().left(80).toStdString(); else if (key == "color") t.color = std::clamp(value.toInt(), 0, 7);
  }
  push(); save();
}
QStringList Arrange::wavFilesIn(const QString& folder) const {
  QDir d(folder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation) : folder); QStringList out;
  for (const QFileInfo& fi : d.entryInfoList({"*.wav", "*.WAV"}, QDir::Files, QDir::Name)) out << fi.absoluteFilePath();
  return out;
}
int Arrange::importFile(const QString& path, int trackId) {
  const std::string p = QFileInfo(path).absoluteFilePath().toStdString();
  if (!loadFile(p)) return 0;
  if (!trackId) trackId = p_.tracks.empty() ? addTrack() : p_.tracks.back().id;
  bool has = false; for (const arr::Track& t : p_.tracks) if (t.id == trackId) has = true; if (!has) trackId = addTrack();
  double at = snapTime(player_.position()); for (const arr::Clip& c : p_.clips) if (c.track == trackId) at = std::max(at, arr::clipEnd(c));
  arr::Clip c; c.id = arr::nextId(p_); c.track = trackId; c.file = p; c.start = at; c.duration = bufs_[p]->duration(); p_.clips.push_back(c);
  sel_ = c.id; emit selectionChanged(); push(); save(); note(QStringLiteral("Imported %1 (%2 s) on track %3.").arg(QFileInfo(path).fileName()).arg(c.duration, 0, 'f', 2).arg(trackId));
  return c.id;
}
void Arrange::moveClip(int id, double start) { if (arr::placeClip(p_, id, snapTime(start))) { push(); save(); } }
void Arrange::moveClipToTrack(int id, int trackId) { bool has = false; for (const arr::Track& t : p_.tracks) if (t.id == trackId) has = true; if (!has) return; for (arr::Clip& c : p_.clips) if (c.id == id) { const double s = c.start; c.track = trackId; if (!arr::placeClip(p_, id, s)) { /* no room: placeClip left it; keep the new track anyway */ } } push(); save(); }
void Arrange::splitAtHead() { if (!sel_) return; const int nid = arr::nextId(p_); if (arr::splitClip(p_, sel_, player_.position(), nid)) { push(); save(); } }
void Arrange::deleteSelected() { if (!sel_) return; p_.clips.erase(std::remove_if(p_.clips.begin(), p_.clips.end(), [&](const arr::Clip& c) { return c.id == sel_; }), p_.clips.end()); sel_ = 0; emit selectionChanged(); push(); save(); }
void Arrange::setClipGain(int id, double db) { for (arr::Clip& c : p_.clips) if (c.id == id) c.db = arr::clampd(db, -60, 6); push(); save(); }
void Arrange::addMarker(const QString& name) { arr::Marker m; m.t = player_.position(); m.name = (name.isEmpty() ? QStringLiteral("Marker %1").arg(p_.markers.size() + 1) : name.left(80)).toStdString(); p_.markers.push_back(m); std::sort(p_.markers.begin(), p_.markers.end(), [](const arr::Marker& a, const arr::Marker& b) { return a.t < b.t; }); push(); save(); }
void Arrange::removeMarker(int i) { if (i >= 0 && i < static_cast<int>(p_.markers.size())) { p_.markers.erase(p_.markers.begin() + i); push(); save(); } }
void Arrange::addAutomation(int trackId, double t, double db) { for (arr::Track& tr : p_.tracks) if (tr.id == trackId) tr.autoPts = arr::addPoint(tr.autoPts, t, db); push(); save(); }
void Arrange::clearAutomation(int trackId) { for (arr::Track& tr : p_.tracks) if (tr.id == trackId) tr.autoPts.clear(); push(); save(); }
double Arrange::automationAt(int trackId, double t) const { for (const arr::Track& tr : p_.tracks) if (tr.id == trackId) return arr::autoAt(tr.autoPts, t); return 0; }
QString Arrange::exportWav(const QString& path) {
  const QString out = path.isEmpty() ? QDir::homePath() + "/amixos-arrangement.wav" : path; std::string err; double secs = 0;
  const double rate = engine_->runtime() ? engine_->runtime()->sampleRate() : 48000;
  if (!arr::exportWav(p_, bufs_, rate, out.toStdString(), err, &secs)) { note(QString::fromStdString(err)); return QString(); }
  note(QStringLiteral("Exported %1 s, 24-bit WAV at %2 Hz: %3").arg(secs, 0, 'f', 1).arg(rate).arg(out)); return out;
}
void Arrange::newProject() { player_.stop(); player_.seek(0); p_ = arr::Project(); sel_ = 0; emit selectionChanged(); push(); save(); note(QStringLiteral("New empty arrangement.")); }
