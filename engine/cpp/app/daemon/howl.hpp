// FEEDBACK GUARD (the remote service): a microphone that hears its own loudspeaker howls. The guard watches every strip that reads a device
// input and is heard (not muted, fader up); when one carries a howl it mutes that strip and the service reports it (state "feedback",
// a line on its output that the console shows). It mutes, it does not notch: a notching FEEDBACK KILLER (fx:antifeedback) is a plug-in the
// person puts in a rack; the guard is the safety net when nobody is at the console (the phones hold it).
//
// A HOWL, as the FEEDBACK KILLER finds it (fx.cpp): one narrow peak standing far above the rest of the spectrum, with no overtone, on the
// same frequency frame after frame. The guard adds what tells a howl from a held note: it GROWS (6 dB or more over the run, in at least
// 0.4 s), or it has saturated (a pure steady tone near full scale for 2 s). A steady test tone or a held note at a normal level is left
// alone.
//
// HowlDetector is the analysis of one strip (2048-point frames, hop 1024; pure, no allocation after construction; the guard thread runs it).
// HowlGuard is the Runtime tap: the audio thread only copies the armed strips into rings (no lock, no allocation); the guard thread reads them.
#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

#include "audiomixer/runtime.hpp"

namespace am {

class HowlDetector {
 public:
  static constexpr int kN = 2048, kHop = 1024;
  static constexpr double kStandDb = 20.0;     // the peak against the average of the other bins
  static constexpr double kFloorDb = -50.0;    // quieter than this is not looked at (0 dB = a full-scale sine)
  static constexpr double kGrowDb = 6.0, kGrowSeconds = 0.4;   // a run that grew this much, this long
  static constexpr double kHotDb = -6.0, kHotSeconds = 2.0;    // or a pure steady tone this loud this long
  HowlDetector() : win_(kN, 0.f), re_(kN), im_(kN), mag_(kN / 2) {}
  void reset() { std::fill(win_.begin(), win_.end(), 0.f); have_ = 0; lastBin_ = -1; streak_ = 0; startDb_ = lastDb_ = -200; }
  // one hop of kHop samples; true once when a howl is confirmed (hz() / levelDb() say where and how loud), then the run starts again
  bool push(const float* hop, double fs) {
    if (fs <= 0) return false;
    std::copy(win_.begin() + kHop, win_.end(), win_.begin()); std::copy(hop, hop + kHop, win_.end() - kHop);
    if (have_ < kN) { have_ += kHop; if (have_ < kN) return false; }
    analyse(fs);
    const int lo = std::max(2, static_cast<int>(100.0 * kN / fs)), hi = std::min(kN / 2 - 2, static_cast<int>(std::min(10000.0, fs * 0.45) * kN / fs));
    int pk = -1; double pv = 0, total = 0;
    for (int k = lo; k <= hi; ++k) { total += mag_[k]; if (mag_[k] > pv) { pv = mag_[k]; pk = k; } }
    if (pk < 0 || pv <= 0) { endRun(); return false; }
    const double peak = mag_[pk - 1] + mag_[pk] + mag_[pk + 1], rest = std::max(1e-24, (total - peak) / std::max(1, hi - lo - 2));
    const double standDb = 10.0 * std::log10(pv / rest), level = 10.0 * std::log10(peak / (1.5 * (kN / 4.0) * (kN / 4.0)) + 1e-24);
    const bool pure = 2 * pk + 1 > hi || mag_[2 * pk - 1] + mag_[2 * pk] + mag_[2 * pk + 1] < peak * 0.01;   // no overtone an octave up (-20 dB)
    if (standDb < kStandDb || level < kFloorDb || !pure) { endRun(); return false; }
    if (lastBin_ >= 0 && std::abs(pk - lastBin_) <= 1) ++streak_; else { lastBin_ = pk; streak_ = 1; startDb_ = level; }
    lastDb_ = level; hz_ = pk * fs / kN;
    const double secs = (streak_ - 1) * kHop / fs;
    if ((secs >= kGrowSeconds && level - startDb_ >= kGrowDb) || (secs >= kHotSeconds && level >= kHotDb)) { endRun(); return true; }
    return false;
  }
  double hz() const { return hz_; }
  double levelDb() const { return lastDb_; }
 private:
  void endRun() { lastBin_ = -1; streak_ = 0; }
  void analyse(double) {
    static constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < kN; ++i) { re_[i] = win_[static_cast<std::size_t>(i)] * (0.5 - 0.5 * std::cos(2.0 * kPi * i / (kN - 1))); im_[i] = 0; }
    for (int i = 1, j = 0; i < kN; ++i) { int bit = kN >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { std::swap(re_[i], re_[j]); std::swap(im_[i], im_[j]); } }
    for (int len = 2; len <= kN; len <<= 1) { const double ang = -2.0 * kPi / len, wr = std::cos(ang), wi = std::sin(ang);
      for (int i = 0; i < kN; i += len) { double cr = 1, ci = 0;
        for (int k = 0; k < len / 2; ++k) { const int a = i + k, b = a + len / 2; const double xr = re_[b] * cr - im_[b] * ci, xi = re_[b] * ci + im_[b] * cr;
          re_[b] = re_[a] - xr; im_[b] = im_[a] - xi; re_[a] += xr; im_[a] += xi; const double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } } }
    for (int k = 0; k < kN / 2; ++k) mag_[k] = re_[k] * re_[k] + im_[k] * im_[k];
  }
  std::vector<float> win_; std::vector<double> re_, im_, mag_; int have_ = 0, lastBin_ = -1, streak_ = 0; double startDb_ = -200, lastDb_ = -200, hz_ = 0;
};

class HowlGuard final : public Runtime::Tap {
 public:
  static constexpr int kStrips = 32;
  static constexpr unsigned kRing = 16384;
  std::atomic<Runtime::Tap*> next{nullptr};   // the RTA (and the recorder behind it)
  std::array<std::atomic<bool>, kStrips> armed{};
  void onBlock(const float* const* strips, int nStrips, const float* L, const float* R, std::size_t frames, double rate) override {
    if (Runtime::Tap* t = next.load(std::memory_order_acquire)) t->onBlock(strips, nStrips, L, R, frames, rate);
    rate_.store(rate, std::memory_order_relaxed);
    if (!strips) return;
    for (int s = 0; s < nStrips && s < kStrips; ++s) { if (!armed[s].load(std::memory_order_relaxed) || !strips[s]) continue;
      Ring& r = rings_[s]; unsigned w = r.w.load(std::memory_order_relaxed);
      for (std::size_t i = 0; i < frames; ++i) r.buf[(w++) & (kRing - 1)] = strips[s][i];
      r.w.store(w, std::memory_order_release); }
  }
  void onBuses(const float* const* busOut, int nBuses, const float* monL, const float* monR, std::size_t frames) override {
    if (Runtime::Tap* t = next.load(std::memory_order_acquire)) t->onBuses(busOut, nBuses, monL, monR, frames); }
  double rate() const { return rate_.load(std::memory_order_relaxed); }
  // the guard thread: the samples strip s got since `read` (moved on), one hop at a time; false when a whole hop is not there yet.
  // Fell behind by more than half the ring (a stalled thread): it skips to the newest samples.
  bool hop(int s, unsigned& read, float* out) const {
    const Ring& r = rings_[s]; const unsigned w = r.w.load(std::memory_order_acquire);
    if (w - read > kRing / 2) read = w - HowlDetector::kHop;
    if (w - read < static_cast<unsigned>(HowlDetector::kHop)) return false;
    for (int i = 0; i < HowlDetector::kHop; ++i) out[i] = r.buf[(read + static_cast<unsigned>(i)) & (kRing - 1)];
    read += HowlDetector::kHop; return true;
  }
  unsigned written(int s) const { return rings_[s].w.load(std::memory_order_acquire); }
 private:
  struct Ring { std::array<float, kRing> buf{}; std::atomic<unsigned> w{0}; };
  std::array<Ring, kStrips> rings_{}; std::atomic<double> rate_{0};
};

}  // namespace am
