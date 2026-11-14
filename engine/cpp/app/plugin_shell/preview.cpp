#include "plugin_shell/preview.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "arrange/decode.hpp"
#include "samples/samples.hpp"

namespace am::shell {
namespace {
long long nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

Preview::Preview() { setSample(1); }
Preview::~Preview() { stop(); delete chain_.exchange(nullptr); }

bool Preview::start(std::string& err, const std::string& backend, double rate, unsigned frames) {
  stop(); be_ = createBackend(backend == "auto" ? (availableBackends().empty() ? "null" : availableBackends().front()) : backend);
  if (!be_) { err = "no audio backend " + backend; return false; }
  DeviceConfig c; c.backend = backend; c.sampleRate = rate; c.frames = frames; c.outChannels = 2;
  c.inChannels = source_.load() == Microphone ? 1 : 0; c.inDevice = c.inChannels ? "" : "-"; c.nullInput = nullIn_;
  rate_ = rate; retire(chain_.exchange(nullptr)); setDesign(design_);   // the chain built again at this rate
  const bool ok = be_->open(c, [this](const float* const* in, float* const* out, std::size_t n) {
      float* L = out[0]; float* R = out[1]; render(in, L, R, n); }, err) && be_->start(err);
  if (!ok) { be_.reset(); return false; }
  running_ = true; return true;
}
void Preview::stop() { if (be_) { be_->stop(); be_.reset(); } running_ = false; sweepGraves(); }

void Preview::setDesign(const Design& d) {
  design_ = d; std::vector<std::string> kinds; for (const auto& b : d.blocks) kinds.push_back(b.kind);
  auto apply = [&](Chain& c) {   // fixed knobs, then the exposed ones at their design value
    for (std::size_t i = 0; i < d.blocks.size() && i < c.fx.size(); ++i) for (const auto& [k, v] : d.blocks[i].set) c.fx[i]->set(k, v);
    for (const auto& p : d.params) if (p.block >= 0 && p.block < static_cast<int>(c.fx.size())) c.fx[static_cast<std::size_t>(p.block)]->set(p.id, p.def); };
  Chain* cur = chain_.load();
  if (cur && cur->kinds == kinds) { apply(*cur); return; }   // the same blocks: the knobs move live, nothing is rebuilt
  auto c = std::make_unique<Chain>(); c->kinds = kinds;
  for (const auto& k : kinds) if (auto e = am::fx::make(k, rate_)) c->fx.push_back(std::move(e));
  apply(*c); retire(chain_.exchange(c.release()));
}
void Preview::retire(Chain* c) { if (!c) return; std::lock_guard<std::mutex> g(graveM_); grave_.emplace_back(std::unique_ptr<Chain>(c), nowMs()); sweepGraves(); }
void Preview::sweepGraves() {   // freed a second after they left the audio thread's hands (it reads the pointer once per block)
  const long long t = nowMs();
  grave_.erase(std::remove_if(grave_.begin(), grave_.end(), [&](const auto& g) { return !running_ || t - g.second > 1000; }), grave_.end());
  clipGrave_.erase(std::remove_if(clipGrave_.begin(), clipGrave_.end(), [&](const auto& g) { return !running_ || t - g.second > 1000; }), clipGrave_.end());
}
void Preview::setClip(std::unique_ptr<Clip> c) {
  std::lock_guard<std::mutex> g(graveM_); const Clip* old = clip_.exchange(c.get());
  if (old && clipOwn_.get() == old) clipGrave_.emplace_back(std::move(clipOwn_), nowMs());
  clipOwn_ = std::move(c); sweepGraves();
}
bool Preview::setSample(int id) {
  auto c = std::make_unique<Clip>(); if (!am::samples::render(id, rate_, c->L, c->R) || c->L.empty()) return false;
  setClip(std::move(c)); source_ = Sample; return true;
}
bool Preview::setFile(const std::string& path, std::string& err) {
  am::arr::Audio a; if (!am::arr::readAudio(path, a, err) || a.L.empty()) { if (err.empty()) err = "the file has no audio"; return false; }
  auto c = std::make_unique<Clip>();
  if (std::fabs(a.rate - rate_) < 0.5) { c->L = std::move(a.L); c->R = a.R.empty() ? c->L : std::move(a.R); }
  else {   // to the device rate (linear: a preview, not a master)
    const double step = a.rate / rate_; const std::size_t n = static_cast<std::size_t>(a.L.size() / step); c->L.resize(n); c->R.resize(n);
    const std::vector<float>& R = a.R.empty() ? a.L : a.R;
    for (std::size_t i = 0; i < n; ++i) { const double x = i * step; const std::size_t k = static_cast<std::size_t>(x); const float f = static_cast<float>(x - k); const std::size_t k1 = std::min(k + 1, a.L.size() - 1);
      c->L[i] = a.L[k] + (a.L[k1] - a.L[k]) * f; c->R[i] = R[k] + (R[k1] - R[k]) * f; } }
  setClip(std::move(c)); source_ = File; return true;
}
void Preview::setSource(Source s) { source_ = s; }

void Preview::render(const float* const* in, float* L, float* R, std::size_t n) {
  const int src = source_.load(); float ip = 0;
  if (src == Sample || src == File) { const Clip* c = clip_.load();
    for (std::size_t i = 0; i < n; ++i) { if (!c || c->L.empty()) { L[i] = R[i] = 0; continue; } if (pos_ >= c->L.size()) pos_ = 0; L[i] = c->L[pos_]; R[i] = c->R.empty() ? c->L[pos_] : c->R[pos_]; ++pos_; } }
  else if (src == PinkNoise) for (std::size_t i = 0; i < n; ++i) {   // Paul Kellet's pink filter on white noise, about -18 dBFS
    rng_ = rng_ * 1664525u + 1013904223u; const float w = (static_cast<float>(rng_ >> 8) / 16777216.f - 0.5f) * 0.5f;
    pink_[0] = 0.99886f * pink_[0] + w * 0.0555179f; pink_[1] = 0.99332f * pink_[1] + w * 0.0750759f; pink_[2] = 0.969f * pink_[2] + w * 0.153852f;
    pink_[3] = 0.8665f * pink_[3] + w * 0.3104856f; pink_[4] = 0.55f * pink_[4] + w * 0.5329522f; pink_[5] = -0.7616f * pink_[5] - w * 0.016898f;
    const float p = (pink_[0] + pink_[1] + pink_[2] + pink_[3] + pink_[4] + pink_[5] + pink_[6] + w * 0.5362f) * 0.25f; pink_[6] = w * 0.115926f; L[i] = R[i] = p; }
  else if (src == Sweep) for (std::size_t i = 0; i < n; ++i) {   // 20 Hz to 20 kHz in 8 s, -12 dBFS
    const double f = 20.0 * std::pow(1000.0, std::fmod(sweepT_, 8.0) / 8.0); sweepT_ += 1.0 / rate_; phase_ += 2 * 3.14159265358979 * f / rate_; if (phase_ > 6.283185307) phase_ -= 6.283185307;
    L[i] = R[i] = static_cast<float>(0.25 * std::sin(phase_)); }
  else for (std::size_t i = 0; i < n; ++i) L[i] = R[i] = in && in[0] ? in[0][i] : 0.f;   // the microphone (mono, to both sides)
  for (std::size_t i = 0; i < n; ++i) ip = std::max(ip, std::fabs(L[i]));
  if (!bypass_.load()) if (Chain* c = chain_.load()) for (auto& e : c->fx) e->process(L, R, n);
  const float g = gain_.load(); float op = 0;
  for (std::size_t i = 0; i < n; ++i) { L[i] = std::clamp(L[i] * g, -1.f, 1.f); R[i] = std::clamp(R[i] * g, -1.f, 1.f); op = std::max(op, std::max(std::fabs(L[i]), std::fabs(R[i]))); }
  if (ip > inPk_.load()) inPk_ = ip; if (op > outPk_.load()) outPk_ = op; ++blocks_;
}

}  // namespace am::shell
