#include "arrange/decode.hpp"
#include "Media.h"
static QStringList audioPatterns() { QStringList l; for (const std::string& e : am::arr::audioExtensions()) { l << "*." + QString::fromStdString(e) << "*." + QString::fromStdString(e).toUpper(); } return l; }   // WAV, FLAC, AIFF, M4A, MP3 ...

#include <QDir>
#include <QFileInfo>
#include <cmath>

#include "Engine.h"
#include "arrange/arrange.hpp"

void MediaSource::set(std::shared_ptr<const Clip> c) { std::lock_guard<std::mutex> g(m_); clip_ = std::move(c); }
void MediaSource::render(float* L, float* R, std::size_t frames, double) {
  for (std::size_t i = 0; i < frames; ++i) { L[i] = 0.f; R[i] = 0.f; }
  if (!playing.load()) { peak.store(0); return; }
  std::shared_ptr<const Clip> c; { std::lock_guard<std::mutex> g(m_); c = clip_; }
  if (!c || c->L.empty()) { playing.store(false); return; }
  const float g = static_cast<float>(std::pow(10.0, gainDb.load() / 20.0)); const std::size_t n = c->L.size(); std::size_t idx = static_cast<std::size_t>(std::max(0.0, pos.load()) * c->rate); float pk = 0;
  for (std::size_t i = 0; i < frames; ++i) {
    if (idx >= n) { if (loop.load()) idx = 0; else { playing.store(false); ended.store(true); pos.store(0); break; } }
    const float l = c->L[idx] * g, r = c->R[idx] * g; L[i] = l; R[i] = r; pk = std::max(pk, std::max(std::fabs(l), std::fabs(r))); ++idx;
  }
  if (playing.load()) pos.store(static_cast<double>(idx) / c->rate);
  peak.store(pk);
}

Media::Media(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  src_.loop.store(s_.value("media/loop", false).toBool()); src_.gainDb.store(s_.value("media/gainDb", -6.0).toDouble());
  for (const QString& p : s_.value("media/playlist").toStringList()) { QVariantMap m; m["path"] = p; m["name"] = QFileInfo(p).fileName(); m["seconds"] = 0.0; m["ok"] = QFileInfo::exists(p); m["error"] = QFileInfo::exists(p) ? QString() : QStringLiteral("file not found"); list_ << m; }
  engine_->setMediaSource(&src_);
  connect(engine_, &Engine::stateChanged, this, [this] { reloadForRate(); });   // a device at another rate: resample the loaded clip
  tick_.setInterval(100); connect(&tick_, &QTimer::timeout, this, [this] { if (src_.ended.exchange(false)) { if (autoNext() && current_ + 1 < list_.size()) { next(); play(); } } emit transportChanged(); }); tick_.start();
}
Media::~Media() { engine_->setMediaSource(nullptr); }
double Media::peakDb() const { const float p = src_.peak.load(); return p > 1e-6f ? 20 * std::log10(p) : -120; }
void Media::setGainDb(double db) { src_.gainDb.store(std::clamp(db, -60.0, 12.0)); s_.setValue("media/gainDb", src_.gainDb.load()); emit transportChanged(); }
void Media::save() { QStringList l; for (const QVariant& v : list_) l << v.toMap()["path"].toString(); s_.setValue("media/playlist", l); }
int Media::add(const QString& path) {
  const QString p = path.trimmed(); if (p.isEmpty()) return 0; int added = 0; QStringList files;
  if (QFileInfo(p).isDir()) { for (const QFileInfo& fi : QDir(p).entryInfoList(audioPatterns(), QDir::Files, QDir::Name)) files << fi.filePath(); } else files << p;
  for (const QString& f : files) { QVariantMap m; m["path"] = f; m["name"] = QFileInfo(f).fileName(); m["seconds"] = 0.0; m["ok"] = QFileInfo::exists(f); m["error"] = QFileInfo::exists(f) ? QString() : QStringLiteral("file not found"); list_ << m; ++added; }
  if (added) { save(); msg_ = QStringLiteral("%1 file(s) added").arg(added); if (current_ < 0) select(0); emit playlistChanged(); emit transportChanged(); }
  else msg_ = QStringLiteral("no WAV at %1").arg(p);
  return added;
}
void Media::remove(int i) { if (i < 0 || i >= list_.size()) return; list_.removeAt(i); if (current_ == i) { stop(); current_ = -1; src_.set(nullptr); duration_ = 0; } else if (current_ > i) --current_; save(); emit playlistChanged(); emit transportChanged(); }
void Media::clear() { stop(); list_.clear(); current_ = -1; src_.set(nullptr); duration_ = 0; save(); emit playlistChanged(); emit transportChanged(); }
bool Media::load(int i, QString& err) {
  const QString path = list_[i].toMap()["path"].toString(); am::arr::Audio a; std::string e;
  if (!am::arr::readAudio(path.toStdString(), a, e)) { err = QString::fromStdString(e); return false; }
  const double fs = engine_->running() ? engine_->sampleRate() : (engine_->sampleRate() > 0 ? engine_->sampleRate() : 48000);
  auto clip = std::make_shared<MediaSource::Clip>(); clip->rate = fs;
  if (std::fabs(a.rate - fs) > 1) { clip->L = am::arr::resampleLinear(a.L, a.rate, fs); clip->R = am::arr::resampleLinear(a.R, a.rate, fs); } else { clip->L = a.L; clip->R = a.R; }
  duration_ = a.duration(); loadedRate_ = fs; src_.set(clip); QVariantMap m = list_[i].toMap(); m["seconds"] = duration_; m["ok"] = true; m["error"] = QString(); list_[i] = m;
  return true;
}
bool Media::select(int i) {
  if (i < 0 || i >= list_.size()) return false; const bool was = playing(); stop(); QString err;
  if (!load(i, err)) { QVariantMap m = list_[i].toMap(); m["ok"] = false; m["error"] = err; list_[i] = m; msg_ = QStringLiteral("%1: %2").arg(m["name"].toString(), err); emit playlistChanged(); emit transportChanged(); return false; }
  current_ = i; src_.pos.store(0); msg_ = list_[i].toMap()["name"].toString() + QStringLiteral("  %1 s").arg(duration_, 0, 'f', 1); if (was) play(); emit playlistChanged(); emit transportChanged(); return true;
}
bool Media::play(int i) { if (i >= 0 && i != current_ && !select(i)) return false; if (current_ < 0) { if (list_.isEmpty() || !select(0)) return false; } src_.ended.store(false); src_.playing.store(true); emit transportChanged(); return true; }
void Media::pause() { src_.playing.store(false); emit transportChanged(); }
void Media::stop() { src_.playing.store(false); src_.pos.store(0); emit transportChanged(); }
void Media::next() { if (list_.isEmpty()) return; select((current_ + 1) % list_.size()); }
void Media::previous() { if (list_.isEmpty()) return; select((current_ - 1 + list_.size()) % list_.size()); }
void Media::seek(double sec) { src_.pos.store(std::clamp(sec, 0.0, std::max(0.0, duration_))); emit transportChanged(); }
void Media::reloadForRate() { if (current_ >= 0 && engine_->running() && std::fabs(engine_->sampleRate() - loadedRate_) > 1) { const double at = src_.pos.load(); QString err; if (load(current_, err)) src_.pos.store(std::min(at, duration_)); } }
