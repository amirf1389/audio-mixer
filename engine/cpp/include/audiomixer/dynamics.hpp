// Compression target: how far apart the loud and the quiet moments of a channel are, and the compression that would bring that spread
// to a chosen target. It only measures and suggests; applying is the user's choice (nothing here changes a setting).
//   push(levelDb) about 10 times a second with the channel's input level (before EQ and dynamics), 10 s of history
//   spread   = the 95th minus the 10th percentile of the levels that carry signal (above -60 dBFS)
//   ratio    = spread / target, 1 .. 8; threshold = the median level minus 3 dB (so the body of the signal is what gets held)
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace am {

struct CompSuggestion {
  bool ready = false;            // enough signal heard (at least 3 s of the last 10)
  double spreadDb = 0, medianDb = -120, targetDb = 6;
  double thresholdDb = -20, ratio = 1;   // for a threshold / ratio compressor
  int level = 0;                 // for the hybrid mixer's 0..10 level (0 = no compression needed)
  const char* verdict = "listening";   // "listening" | "on target" | "too dynamic" | "already squashed"
};

class DynamicsTracker {
 public:
  static constexpr std::size_t kLen = 100;            // 10 s at 10 per second
  void push(double levelDb) { h_[pos_] = levelDb; pos_ = (pos_ + 1) % kLen; if (n_ < kLen) ++n_; }
  void clear() { n_ = 0; pos_ = 0; }
  std::size_t size() const { return n_; }

  CompSuggestion suggest(double targetDb) const {
    CompSuggestion s; s.targetDb = std::clamp(targetDb, 2.0, 30.0);
    std::array<double, kLen> v{}; std::size_t k = 0;
    for (std::size_t i = 0; i < n_; ++i) if (h_[i] > -60.0) v[k++] = h_[i];
    if (k < 30) return s;                                // under 3 s of signal: keep listening
    std::sort(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k));
    const auto at = [&](double p) { return v[std::min(k - 1, static_cast<std::size_t>(p * static_cast<double>(k - 1) + 0.5))]; };
    s.ready = true; s.spreadDb = at(0.95) - at(0.10); s.medianDb = at(0.50);
    s.ratio = std::clamp(s.spreadDb / s.targetDb, 1.0, 8.0);
    s.thresholdDb = std::clamp(s.medianDb - 3.0, -50.0, -3.0);
    s.verdict = s.spreadDb < s.targetDb * 0.6 ? "already squashed" : s.spreadDb <= s.targetDb * 1.25 ? "on target" : "too dynamic";
    if (s.ratio < 1.2) { s.ratio = 1.0; s.level = 0; }
    else s.level = std::clamp(static_cast<int>(std::lround(1.0 + (s.ratio - 1.5) * 9.0 / 6.5)), 1, 10);   // the hybrid level whose ratio is closest
    return s;
  }

 private:
  std::array<double, kLen> h_{}; std::size_t pos_ = 0, n_ = 0;
};

}  // namespace am
