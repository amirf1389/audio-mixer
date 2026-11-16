// THE REMOTE'S RTA: a real-time analyzer of one strip or the master, for the remotes' EQ pages (the spectrum behind the EQ curve, the RTA EQ
// page, AUTO EQ). The audio thread only copies samples into a ring (no allocation, no lock); the spectrum is computed on request, outside
// the audio thread: the last 4096 samples, a Hann window, a radix-2 FFT, summed into 1/6-octave bands from 20 Hz to 20 kHz, in dBFS so that
// a full-scale sine reads 0 dB in its band. As a Runtime::Tap it passes every block on to the next tap (the recorder).
#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

#include "audiomixer/runtime.hpp"

namespace am {

class Rta final : public Runtime::Tap {
 public:
  static constexpr int kN = 4096;
  static constexpr int kOff = -2, kMaster = -1;   // the source: off, the master, or a strip (0..)
  std::atomic<int> source{kOff};
  std::atomic<Runtime::Tap*> next{nullptr};      // the recorder, while it records

  void onBlock(const float* const* strips, int nStrips, const float* L, const float* R, std::size_t frames, double rate) override {
    if (Runtime::Tap* t = next.load(std::memory_order_acquire)) t->onBlock(strips, nStrips, L, R, frames, rate);
    const int s = source.load(std::memory_order_relaxed); if (s == kOff) return;
    rate_.store(rate, std::memory_order_relaxed);
    const float* x = s >= 0 && s < nStrips && strips ? strips[s] : nullptr;
    unsigned w = w_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < frames; ++i) { ring_[w & (kRing - 1)] = x ? x[i] : (L && R ? 0.5f * (L[i] + R[i]) : 0.f); ++w; }
    w_.store(w, std::memory_order_release);
  }

  /** the band centres (Hz) of the 1/6-octave bands, 20 Hz .. 20 kHz */
  static const std::vector<double>& centres() {
    static const std::vector<double> c = [] { std::vector<double> v; for (double f = 20.0; f <= 20000.0 * 1.0001; f *= std::pow(2.0, 1.0 / 6.0)) v.push_back(f); return v; }();
    return c;
  }
  /** the spectrum of the last kN samples, one dBFS value per band (floor -120); false while there is not enough sound yet */
  bool bands(std::vector<double>& db) const {
    const double fs = rate_.load(std::memory_order_relaxed); const unsigned w = w_.load(std::memory_order_acquire);
    if (fs <= 0 || w < static_cast<unsigned>(kN)) return false;
    std::vector<std::complex<double>> a(kN);
    for (int i = 0; i < kN; ++i) { const double h = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (kN - 1)); a[static_cast<std::size_t>(i)] = ring_[(w - kN + static_cast<unsigned>(i)) & (kRing - 1)] * h; }
    fft(a);
    const std::vector<double>& c = centres(); db.assign(c.size(), -120.0);
    const double norm = 1.5 * (kN / 4.0) * (kN / 4.0);   // a Hann-windowed sine of amplitude A sums to 1.5 (A N / 4)^2 over its bins
    const double half = std::pow(2.0, 1.0 / 12.0), bin = fs / kN;
    for (std::size_t b = 0; b < c.size(); ++b) {
      const double lo = c[b] / half, hi = c[b] * half; int k0 = static_cast<int>(std::ceil(lo / bin)), k1 = static_cast<int>(std::floor(hi / bin));
      if (k1 < k0) { k0 = k1 = static_cast<int>(std::lround(c[b] / bin)); }   // a band narrower than one bin (the bass): the nearest bin
      k0 = std::max(1, k0); k1 = std::min(kN / 2 - 1, k1); double p = 0; for (int k = k0; k <= k1; ++k) p += std::norm(a[static_cast<std::size_t>(k)]);
      db[b] = p > 0 ? std::max(-120.0, 10.0 * std::log10(p / norm)) : -120.0;
    }
    return true;
  }

 private:
  static constexpr double kPi = 3.14159265358979323846;
  static constexpr unsigned kRing = 8192;
  static void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) { std::size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (std::size_t len = 2; len <= n; len <<= 1) {
      const std::complex<double> wl = std::polar(1.0, -2 * kPi / static_cast<double>(len));
      for (std::size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (std::size_t j = 0; j < len / 2; ++j) { const auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
  }
  std::array<float, kRing> ring_{}; std::atomic<unsigned> w_{0}; std::atomic<double> rate_{0};
};

}  // namespace am
