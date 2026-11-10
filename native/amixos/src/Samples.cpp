#include "Samples.h"

#include "Engine.h"
#include "paths/paths.hpp"
#include "samples/samples.hpp"
#include <QDir>
#include <QStandardPaths>
#include <cmath>

void SamplePlayer::render(float* L, float* R, std::size_t frames, double fs) {
  (void)fs; for (std::size_t i = 0; i < frames; ++i) { L[i] = 0.f; R[i] = 0.f; }
  const auto* bank = bank_.load(); if (!bank) return;
  if (stop_.exchange(false)) for (Voice& v : voices_) v.id = 0;
  while (r_.load() != w_.load()) { const int id = ring_[r_.load() % kRing]; r_.store(r_.load() + 1); if (id < 1 || id > kCount) continue;
    int slot = -1; for (int k = 0; k < kVoices; ++k) if (voices_[k].id == 0) { slot = k; break; }
    if (slot < 0) { slot = static_cast<int>(next_++ % kVoices); }   // steal round-robin
    voices_[slot].id = id; voices_[slot].pos = 0; }
  const float g = static_cast<float>(std::pow(10.0, gainDb.load() / 20.0)); float pk = 0; int act = 0;
  for (Voice& v : voices_) { if (v.id == 0) continue; const Clip& c = (*bank)[v.id - 1]; const std::size_t n = c.L.size(); ++act;
    for (std::size_t i = 0; i < frames && v.pos < n; ++i, ++v.pos) { L[i] += c.L[v.pos] * g; R[i] += c.R[v.pos] * g; }
    if (v.pos >= n) v.id = 0; }
  for (std::size_t i = 0; i < frames; ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
  peak.store(pk); active.store(act);
}

Samples::Samples(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  player_.gainDb.store(s_.value("samples/gainDb", -6.0).toDouble()); engine_->setSampleSource(&player_);
  connect(engine_, &Engine::stateChanged, this, [this] { if (engine_->running() && engine_->sampleRate() > 0 && engine_->sampleRate() != rate_) prepare(engine_->sampleRate()); });
  if (engine_->sampleRate() > 0) prepare(engine_->sampleRate()); else prepare(48000);
  poll_.setInterval(100); connect(&poll_, &QTimer::timeout, this, [this] { if (player_.active.load() > 0 || player_.peak.load() > 0) emit changed(); }); poll_.start();
}
Samples::~Samples() { engine_->setSampleSource(nullptr); }
QVariantList Samples::list() const { QVariantList l; for (const auto& s : am::samples::list()) { QVariantMap m; m["id"] = s.id; m["name"] = QString::fromStdString(s.name); m["category"] = QString::fromStdString(s.category); m["seconds"] = s.seconds; l << m; } return l; }
QStringList Samples::categories() const { QStringList c; for (const auto& s : am::samples::list()) { const QString k = QString::fromStdString(s.category); if (!c.contains(k)) c << k; } return c; }
void Samples::setGainDb(double db) { db = std::clamp(db, -40.0, 12.0); player_.gainDb.store(db); s_.setValue("samples/gainDb", db); emit changed(); }
double Samples::peakDb() const { const float p = player_.peak.load(); return p > 1e-6f ? 20 * std::log10(p) : -120; }
QString Samples::folder() const { return QString::fromStdString(am::paths::cacheDir()) + "/samples"; }
void Samples::prepare(double rate) {
  auto bank = std::make_shared<std::array<SamplePlayer::Clip, SamplePlayer::kCount>>();
  for (int id = 1; id <= SamplePlayer::kCount; ++id) am::samples::render(id, rate, (*bank)[id - 1].L, (*bank)[id - 1].R);
  player_.setBank(bank); rate_ = rate; ready_ = true; msg_ = QStringLiteral("100 samples ready at %1 Hz").arg(rate); emit changed();
}
bool Samples::play(int id) { if (id < 1 || id > SamplePlayer::kCount || !ready_) return false; last_ = id; const bool ok = player_.trigger(id); emit changed(); return ok; }
void Samples::stopAll() { player_.stopAll(); emit changed(); }
int Samples::exportAll() {
  QDir().mkpath(folder()); std::string err; const bool ok = am::samples::writeAll(folder().toStdString(), rate_ > 0 ? rate_ : 48000, err);
  const int n = QDir(folder()).entryList({"*.wav"}, QDir::Files).size(); msg_ = ok ? QStringLiteral("%1 WAV files in %2").arg(n).arg(folder()) : QString::fromStdString(err); emit changed(); return n;
}
