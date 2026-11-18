#include "Samples.h"

#include "Engine.h"
#include "arrange/decode.hpp"
#include "paths/paths.hpp"
#include "samples/samples.hpp"
#include "samples/sfx.hpp"
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <cmath>

Samples::Samples(Engine* engine, QObject* parent) : QObject(parent), engine_(engine), bank_(std::make_unique<am::pads::Bank>(am::paths::file("pads.json"))) {
  player_.gainDb.store(s_.value("samples/gainDb", -6.0).toDouble()); engine_->setSampleSource(&player_);
  connect(engine_, &Engine::stateChanged, this, [this] { if (engine_->running() && engine_->sampleRate() > 0 && engine_->sampleRate() != player_.rate()) prepare(engine_->sampleRate()); });
  prepare(engine_->sampleRate() > 0 ? engine_->sampleRate() : 48000);
  poll_.setInterval(100);
  connect(&poll_, &QTimer::timeout, this, [this] {
    if (player_.active.load() > 0 || player_.peak.load() > 0) emit changed();
    if (++watch_ % 10 == 0 && bank_->reloadIfChanged()) emit padsChanged();   // a phone changed a pad (the service saved the shared file)
  });
  poll_.start();
}
Samples::~Samples() { engine_->setSampleSource(nullptr); }

QVariantList Samples::list() const { QVariantList l; for (const auto& s : am::samples::list()) { QVariantMap m; m["id"] = s.id; m["name"] = QString::fromStdString(s.name); m["category"] = QString::fromStdString(s.category); m["seconds"] = s.seconds; l << m; } return l; }
QStringList Samples::categories() const { QStringList c; for (const auto& s : am::samples::list()) { const QString k = QString::fromStdString(s.category); if (!c.contains(k)) c << k; } return c; }
QStringList Samples::sfxFamilies() const { QStringList l; for (const auto& f : am::sfx::families()) l << QString::fromStdString(f); return l; }
void Samples::setGainDb(double db) { db = std::clamp(db, -40.0, 12.0); player_.gainDb.store(db); s_.setValue("samples/gainDb", db); emit changed(); }
double Samples::peakDb() const { const float p = player_.peak.load(); return p > 1e-6f ? 20 * std::log10(p) : -120; }
QString Samples::folder() const { return QString::fromStdString(am::paths::cacheDir()) + "/samples"; }

void Samples::prepare(double rate) {
  player_.prepare(rate); msg_ = QStringLiteral("100 samples ready at %1 Hz; effects and pads load when first played").arg(rate); emit changed();
}

bool Samples::playSource(const std::string& source, int padN, double padGainDb) {
  std::string err; if (!player_.play(source, padN, padGainDb, err)) { msg_ = QString::fromStdString(err); emit changed(); return false; }
  emit changed(); return true;
}
bool Samples::play(int id) {
  if (id < 1 || id > 100 || !player_.ready()) return false; last_ = id;
  if (!engine_->running() && remotePlay && remotePlay(id, player_.gainDb.load())) { emit changed(); return true; }   // the remote service has the sound card now
  return playSource("sample:" + std::to_string(id), 0, 0);
}
bool Samples::playPad(int n) {
  if (n < 1 || n > am::pads::kPads) return false; const am::pads::Pad p = bank_->at(n); lastPad_ = n;
  if (p.source.empty()) { msg_ = QStringLiteral("Pad %1 is empty: choose a sound for it").arg(n); emit changed(); return false; }
  if (!engine_->running() && remoteSend && remoteSend(QVariantMap{{"op", "pad"}, {"n", n}})) { emit changed(); return true; }
  return playSource(p.source, n, p.gainDb);
}
bool Samples::playSfx(int id) {
  if (id < 1 || id > am::sfx::kCount) return false;
  if (!engine_->running() && remoteSend && remoteSend(QVariantMap{{"op", "sfx"}, {"id", id}})) { emit changed(); return true; }
  return playSource("sfx:" + std::to_string(id), 0, 0);
}
void Samples::stopAll() { player_.stopAll(); if (remoteStop) remoteStop(); emit changed(); }

static QString kindOf(const std::string& src) { return src.empty() ? "empty" : src.rfind("sample:", 0) == 0 ? "sample" : src.rfind("sfx:", 0) == 0 ? "sfx" : "file"; }
QVariantMap Samples::pad(int n) const {
  const am::pads::Pad p = bank_->at(n); QVariantMap m; m["n"] = n; m["source"] = QString::fromStdString(p.source); m["name"] = QString::fromStdString(p.name); m["gainDb"] = p.gainDb;
  m["color"] = p.color; m["custom"] = !(p == am::pads::defaultPad(n)); m["kind"] = kindOf(p.source);
  if (m["kind"] == "file") { const QString f = QString::fromStdString(p.source.substr(5)); m["file"] = f; m["missing"] = !QFileInfo::exists(f); }
  return m;
}
QVariantList Samples::pads(int from, int to) const { QVariantList l; for (int n = std::max(1, from); n <= std::min(am::pads::kPads, to); ++n) l << pad(n); return l; }
QVariantList Samples::sfx(const QString& family) const {
  QVariantList l; for (int id = 1; id <= am::sfx::kCount; ++id) { const am::sfx::Info in = am::sfx::info(id); const QString f = QString::fromStdString(in.family); if (!family.isEmpty() && f != family) continue;
    QVariantMap m; m["id"] = id; m["name"] = QString::fromStdString(in.name); m["family"] = f; m["seconds"] = in.seconds; l << m; }
  return l;
}

bool Samples::saveBank() { std::string err; if (!bank_->save(err)) { msg_ = QString::fromStdString(err); emit changed(); return false; } emit padsChanged(); return true; }
bool Samples::setPad(int n, const QString& source, const QString& name, double gainDb, int color) {
  std::string err; if (!bank_->set(n, {source.toStdString(), name.toStdString(), gainDb, color}, true, err)) { msg_ = QString::fromStdString(err); emit changed(); return false; }
  msg_ = QStringLiteral("Pad %1: %2").arg(n).arg(QString::fromStdString(bank_->at(n).name)); return saveBank();
}
bool Samples::setPadFile(int n, const QString& pathOrUrl) {
  const QUrl u(pathOrUrl); const QString path = u.isLocalFile() ? u.toLocalFile() : pathOrUrl; const am::pads::Pad cur = bank_->at(n);
  return setPad(n, "file:" + QDir::fromNativeSeparators(path), QString(), cur.gainDb, cur.color);
}
bool Samples::resetPad(int n) { if (!bank_->resetPad(n)) return false; msg_ = QStringLiteral("Pad %1 back to %2").arg(n).arg(QString::fromStdString(bank_->at(n).name)); return saveBank(); }
QStringList Samples::audioFilters() const {
  QStringList ext; for (const auto& e : am::arr::audioExtensions()) ext << "*." + QString::fromStdString(e);
  return {QStringLiteral("Audio files (%1)").arg(ext.join(' ')), QStringLiteral("All files (*)")};
}

int Samples::exportAll() {
  QDir().mkpath(folder()); std::string err; const bool ok = am::samples::writeAll(folder().toStdString(), player_.rate() > 0 ? player_.rate() : 48000, err);
  const int n = QDir(folder()).entryList({"*.wav"}, QDir::Files).size(); msg_ = ok ? QStringLiteral("%1 WAV files in %2").arg(n).arg(folder()) : QString::fromStdString(err); emit changed(); return n;
}
