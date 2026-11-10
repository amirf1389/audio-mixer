#include "Scope.h"

#include <algorithm>
#include <cmath>

ScopeTap::ScopeTap() { for (auto& r : ring_) r.assign(kRing, 0.f); }

void ScopeTap::onBlock(const float* const* strips, int nStrips, const float* L, const float* R, std::size_t frames, double) {
  const int s = strip_.load(); const float* st = (strips && s >= 0 && s < nStrips) ? strips[s] : nullptr; std::size_t h = head_.load(std::memory_order_relaxed);
  for (std::size_t i = 0; i < frames; ++i) { const std::size_t k = (h + i) & (kRing - 1); ring_[0][k] = L[i]; ring_[1][k] = R[i]; ring_[2][k] = st ? st[i] : 0.f; }
  head_.store(h + frames, std::memory_order_release);
}

void ScopeTap::window(int ch, std::size_t span, int points, bool trigger, std::vector<float>& out) const {
  ch = std::clamp(ch, 0, 2); span = std::clamp<std::size_t>(span, 16, kRing / 2); points = std::max(2, points); out.assign(static_cast<std::size_t>(points), 0.f);
  const std::size_t h = head_.load(std::memory_order_acquire); std::size_t start = h - span;   // the newest window
  if (trigger) {   // look back up to one more span for a rising zero crossing of the master L, so the picture does not drift
    const std::vector<float>& t = ring_[0]; bool found = false;
    for (std::size_t back = span; back < 2 * span && !found; ++back) { const std::size_t k = (h - back) & (kRing - 1), k1 = (h - back + 1) & (kRing - 1); if (t[k] <= 0.f && t[k1] > 0.f) { start = h - back; found = true; } }
  }
  const std::vector<float>& r = ring_[ch];
  for (int p = 0; p < points; ++p) { const std::size_t i = start + static_cast<std::size_t>(static_cast<double>(p) * static_cast<double>(span) / points); out[static_cast<std::size_t>(p)] = r[i & (kRing - 1)]; }
}
void ScopeTap::pairs(int points, std::vector<float>& lr) const {
  points = std::clamp(points, 2, 4096); lr.assign(static_cast<std::size_t>(points) * 2, 0.f); const std::size_t h = head_.load(std::memory_order_acquire);
  for (int p = 0; p < points; ++p) { const std::size_t k = (h - static_cast<std::size_t>(points) + static_cast<std::size_t>(p)) & (kRing - 1); lr[static_cast<std::size_t>(2 * p)] = ring_[0][k]; lr[static_cast<std::size_t>(2 * p + 1)] = ring_[1][k]; }
}
ScopeTap::Figures ScopeTap::figures(int ch, std::size_t span) const {
  ch = std::clamp(ch, 0, 2); span = std::clamp<std::size_t>(span, 16, kRing / 2); const std::size_t h = head_.load(std::memory_order_acquire); const std::vector<float>& r = ring_[ch];
  float mn = 1e9f, mx = -1e9f; double sq = 0; int crossings = 0; float prev = r[(h - span) & (kRing - 1)]; std::size_t firstX = 0, lastX = 0;
  for (std::size_t i = 1; i < span; ++i) { const float v = r[(h - span + i) & (kRing - 1)]; mn = std::min(mn, v); mx = std::max(mx, v); sq += static_cast<double>(v) * v; if (prev <= 0.f && v > 0.f) { if (!crossings) firstX = i; lastX = i; ++crossings; } prev = v; }
  Figures f; f.vpp = mx - mn; f.vrms = std::sqrt(sq / static_cast<double>(span)); f.crest = f.vrms > 1e-9 ? 20 * std::log10(std::max(std::fabs(mn), std::fabs(mx)) / f.vrms) : 0;
  f.freq = crossings >= 2 ? static_cast<double>(crossings - 1) * fs_ / static_cast<double>(lastX - firstX) : 0;
  return f;
}
