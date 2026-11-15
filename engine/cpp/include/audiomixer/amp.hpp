#pragma once
// THE AMPLIFIER STAGE (system processor): one per device output, after the output matrix and before the device. What a loudspeaker
// processor does in front of a power amplifier: gain, mute, polarity, alignment delay, a high-pass and a low-pass crossover leg (so one
// output feeds the sub and another the tops), and a peak limiter that keeps the amplifier and the driver inside their limits.
// Everything runs on the audio thread without allocation: the delay line is sized once in prepare().
#include <algorithm>
#include <cmath>
#include <vector>

#include "audiomixer/dsp.hpp"

namespace am {

struct AmpParams {
  bool on = false;          // off: the output passes untouched (bit-exact)
  double gainDb = 0;        // -60 .. +12
  bool mute = false, invert = false;
  double delayMs = 0;       // 0 .. 100 (speaker alignment: about 2.9 ms per metre)
  int hpOrder = 0;          // 0 = off, 1 .. 8 (12 dB/oct per 2)
  double hpHz = 80;
  int lpOrder = 0;
  double lpHz = 18000;
  dsp::XoverType type = dsp::XoverType::LinkwitzRiley;
  bool limitOn = false;
  double limitDb = -1;      // ceiling in dBFS, -30 .. 0
  double releaseMs = 200;   // 10 .. 2000
};

class AmpChannel {
 public:
  static constexpr double kMaxDelayMs = 100.0;
  void prepare(double fs) {
    fs_ = fs > 0 ? fs : 48000.0;
    line_.assign(static_cast<std::size_t>(fs_ * kMaxDelayMs / 1000.0) + 2, 0.f); pos_ = 0; env_ = 1.0; pk_ = 0; gr_ = 0;
    apply();
  }
  void set(const AmpParams& p) { p_ = p; apply(); }
  const AmpParams& params() const { return p_; }
  // in place; frames of one output
  void process(float* x, int frames) noexcept {
    if (!p_.on) { double pk = 0; for (int i = 0; i < frames; ++i) pk = std::max(pk, static_cast<double>(std::fabs(x[i]))); pk_ = std::max(pk, pk_ * 0.8); gr_ = 0; return; }
    const std::size_t n = line_.size(); double pk = 0, minG = 1.0;
    for (int i = 0; i < frames; ++i) {
      double s = x[i];
      if (n > 0 && delay_ > 0) { line_[pos_] = static_cast<float>(s); std::size_t rd = pos_ + n - delay_; if (rd >= n) rd -= n; s = line_[rd]; if (++pos_ >= n) pos_ = 0; }
      if (hp_.order() > 0) s = hp_.tick(s);
      if (lp_.order() > 0) s = lp_.tick(s);
      s *= gain_;
      if (p_.limitOn) {   // peak limiter: instant attack to the ceiling, smooth release back to unity
        const double a = std::fabs(s), want = a > ceil_ ? ceil_ / a : 1.0;
        env_ = want < env_ ? want : env_ + (1.0 - env_) * rel_;
        s *= env_; if (env_ < minG) minG = env_;
        if (s > ceil_) s = ceil_; else if (s < -ceil_) s = -ceil_;   // the last guard: nothing above the ceiling leaves
      }
      if (!std::isfinite(s)) s = 0;
      x[i] = static_cast<float>(s); pk = std::max(pk, std::fabs(s));
    }
    pk_ = std::max(pk, pk_ * 0.8); gr_ = minG < 1.0 ? -20.0 * std::log10(minG) : gr_ * 0.8;
  }
  double peak() const noexcept { return pk_; }
  double gainReductionDb() const noexcept { return gr_; }
  // the response of the crossover legs in dB (for the page's curve)
  double responseDbAt(double f) const noexcept { return (hp_.order() > 0 ? hp_.gainDbAt(fs_, f) : 0.0) + (lp_.order() > 0 ? lp_.gainDbAt(fs_, f) : 0.0); }

 private:
  void apply() {
    const double g = p_.mute ? 0.0 : std::pow(10.0, std::clamp(p_.gainDb, -60.0, 12.0) / 20.0);
    gain_ = p_.invert ? -g : g;
    delay_ = static_cast<std::size_t>(std::clamp(p_.delayMs, 0.0, kMaxDelayMs) * fs_ / 1000.0 + 0.5);
    if (!line_.empty() && delay_ >= line_.size()) delay_ = line_.size() - 1;
    hp_.set(true, p_.type, p_.hpOrder, std::clamp(p_.hpHz, 10.0, 20000.0), fs_);
    lp_.set(false, p_.type, p_.lpOrder, std::clamp(p_.lpHz, 20.0, 24000.0), fs_);
    ceil_ = std::pow(10.0, std::clamp(p_.limitDb, -30.0, 0.0) / 20.0);
    rel_ = 1.0 - std::exp(-1.0 / (std::clamp(p_.releaseMs, 10.0, 2000.0) * 0.001 * fs_));
  }
  AmpParams p_; double fs_ = 48000, gain_ = 1, ceil_ = 1, rel_ = 0.001, env_ = 1, pk_ = 0, gr_ = 0;
  std::vector<float> line_; std::size_t pos_ = 0, delay_ = 0;
  dsp::Crossover hp_, lp_;
};

}  // namespace am
