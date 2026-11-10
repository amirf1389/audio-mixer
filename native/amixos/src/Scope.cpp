#include "Scope.h"

#include <algorithm>
#include <cmath>
#include <complex>

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

void ScopeTap::spectrum(int ch, std::vector<float>& bands) const {
  ch = std::clamp(ch, 0, 2); constexpr std::size_t N = 4096; const std::size_t h = head_.load(std::memory_order_acquire); const std::vector<float>& r = ring_[ch];
  std::vector<std::complex<double>> x(N);
  for (std::size_t i = 0; i < N; ++i) { const double w = 0.5 - 0.5 * std::cos(2 * M_PI * static_cast<double>(i) / N); x[i] = r[(h - N + i) & (kRing - 1)] * w; }
  for (std::size_t i = 1, j = 0; i < N; ++i) { std::size_t bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(x[i], x[j]); }   // radix-2, in place
  for (std::size_t len = 2; len <= N; len <<= 1) { const std::complex<double> wl = std::polar(1.0, -2 * M_PI / static_cast<double>(len)); for (std::size_t i = 0; i < N; i += len) { std::complex<double> w = 1; for (std::size_t k = 0; k < len / 2; ++k) { const std::complex<double> u = x[i + k], v = x[i + k + len / 2] * w; x[i + k] = u + v; x[i + k + len / 2] = u - v; w *= wl; } } }
  bands.assign(kBands, -120.f); const double scale = 2.0 / (N * 0.5);   // Hann coherent gain 0.5
  for (int b = 0; b < kBands; ++b) {
    const double fc = 20.0 * std::pow(2.0, b / 3.0), lo = fc / std::pow(2.0, 1.0 / 6), hi = fc * std::pow(2.0, 1.0 / 6);
    std::size_t k0 = static_cast<std::size_t>(std::floor(lo * N / fs_)), k1 = static_cast<std::size_t>(std::ceil(hi * N / fs_)); k0 = std::max<std::size_t>(1, k0); k1 = std::min(k1, N / 2 - 1); if (k1 < k0) k1 = k0;
    double p = 0; for (std::size_t k = k0; k <= k1; ++k) { const double a = std::abs(x[k]) * scale; p += a * a; }
    bands[static_cast<std::size_t>(b)] = static_cast<float>(p > 1e-12 ? 10 * std::log10(p) : -120.0);
  }
}

bool ScopeTap::roundTrip(double& ms, double& returnDb) const {
  const std::size_t span = static_cast<std::size_t>(std::min<double>(fs_, kRing / 2.0)); const std::size_t h = head_.load(std::memory_order_acquire), start = h - span;
  const std::vector<float>& m = ring_[0]; const std::vector<float>& s = ring_[2];
  std::size_t onset = 0; float mpk = 0; for (std::size_t i = 0; i + static_cast<std::size_t>(fs_ * 0.5) < span; ++i) { const float v = std::fabs(m[(start + i) & (kRing - 1)]); if (v > mpk * 1.5f && v > 0.05f) { mpk = v; onset = i; } }   // the first clear pulse of the window with half a second after it
  if (mpk < 0.05f) return false;
  const std::size_t win = static_cast<std::size_t>(fs_ * 0.5); std::size_t echo = 0; float spk = 0;
  for (std::size_t i = onset + 1; i < onset + win && i < span; ++i) { const float v = std::fabs(s[(start + i) & (kRing - 1)]); if (v > spk) { spk = v; echo = i; } }
  if (spk < mpk / 40.f) return false;
  for (std::size_t i = onset + 1; i < echo; ++i) { const float v = std::fabs(s[(start + i) & (kRing - 1)]); if (v > spk * 0.5f) { echo = i; break; } }   // the echo's onset, not its peak
  ms = static_cast<double>(echo - onset) * 1000.0 / fs_; returnDb = 20 * std::log10(spk); return true;
}
