// A mix OS channel DSP (C++20): the processing of one channel strip before it reaches the mixer, in the order a car / studio processor applies it.
//   polarity -> delay (time alignment, 0..50 ms, sub-sample by linear interpolation) -> high-pass / low-pass crossover (Butterworth, Linkwitz-Riley or
//   Bessel, 6..48 dB/octave) -> parametric EQ (kEqBands biquads: peaking, low / high shelf, low / high pass, notch, all-pass 1st / 2nd order) ->
//   peak limiter (instant attack, one-pole release)
// Real-time rules: no allocation, no locks, no exceptions and no I/O in process(); every buffer is a fixed array sized at compile time; parameter changes
// recompute coefficients in the caller's thread (set*) and are applied at the next block, so a change never tears a filter in the middle of a block.
// All arithmetic in double (the "64-bit summing" of the Pro tier); the audio arrives and leaves as float.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace am::dsp {

constexpr double kPi = 3.14159265358979323846;
constexpr int kEqBands = 10;
constexpr int kMaxXoverOrder = 8;          // 48 dB/octave
constexpr std::size_t kMaxDelayFrames = 9600;   // 50 ms at 192 kHz

enum class FilterType : std::uint8_t { Off, Peaking, LowShelf, HighShelf, LowPass, HighPass, Notch, AllPass1, AllPass2 };
enum class XoverType : std::uint8_t { Butterworth, LinkwitzRiley, Bessel };

// one transposed direct form II biquad
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   // normalised: a0 = 1
  double z1 = 0, z2 = 0;
  void reset() noexcept { z1 = z2 = 0; }
  [[nodiscard]] double tick(double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
  void identity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }
  // RBJ cookbook coefficients. q is the quality (peaking / notch / pass / allpass2), gainDb the band gain (peaking / shelves); shelves use slope 1.
  void design(FilterType t, double fs, double f, double q, double gainDb) noexcept {
    if (t == FilterType::Off || fs <= 0 || f <= 0 || f >= fs * 0.5) { identity(); return; }
    const double w0 = 2.0 * kPi * f / fs, cw = std::cos(w0), sw = std::sin(w0), A = std::pow(10.0, gainDb / 40.0);
    const double Q = q > 0.01 ? q : 0.01, alpha = sw / (2.0 * Q);
    double a0 = 1, _b0, _b1, _b2, _a1, _a2;
    switch (t) {
      case FilterType::Peaking: _b0 = 1 + alpha * A; _b1 = -2 * cw; _b2 = 1 - alpha * A; a0 = 1 + alpha / A; _a1 = -2 * cw; _a2 = 1 - alpha / A; break;
      case FilterType::LowShelf: { const double s = 2 * std::sqrt(A) * alpha; _b0 = A * ((A + 1) - (A - 1) * cw + s); _b1 = 2 * A * ((A - 1) - (A + 1) * cw); _b2 = A * ((A + 1) - (A - 1) * cw - s); a0 = (A + 1) + (A - 1) * cw + s; _a1 = -2 * ((A - 1) + (A + 1) * cw); _a2 = (A + 1) + (A - 1) * cw - s; break; }
      case FilterType::HighShelf: { const double s = 2 * std::sqrt(A) * alpha; _b0 = A * ((A + 1) + (A - 1) * cw + s); _b1 = -2 * A * ((A - 1) + (A + 1) * cw); _b2 = A * ((A + 1) + (A - 1) * cw - s); a0 = (A + 1) - (A - 1) * cw + s; _a1 = 2 * ((A - 1) - (A + 1) * cw); _a2 = (A + 1) - (A - 1) * cw - s; break; }
      case FilterType::LowPass: _b0 = (1 - cw) / 2; _b1 = 1 - cw; _b2 = (1 - cw) / 2; a0 = 1 + alpha; _a1 = -2 * cw; _a2 = 1 - alpha; break;
      case FilterType::HighPass: _b0 = (1 + cw) / 2; _b1 = -(1 + cw); _b2 = (1 + cw) / 2; a0 = 1 + alpha; _a1 = -2 * cw; _a2 = 1 - alpha; break;
      case FilterType::Notch: _b0 = 1; _b1 = -2 * cw; _b2 = 1; a0 = 1 + alpha; _a1 = -2 * cw; _a2 = 1 - alpha; break;
      case FilterType::AllPass2: _b0 = 1 - alpha; _b1 = -2 * cw; _b2 = 1 + alpha; a0 = 1 + alpha; _a1 = -2 * cw; _a2 = 1 - alpha; break;
      case FilterType::AllPass1: { const double k = std::tan(w0 / 2), c = (k - 1) / (k + 1); _b0 = c; _b1 = 1; _b2 = 0; a0 = 1; _a1 = c; _a2 = 0; break; }   // 1st order: 90 deg at f
      default: identity(); return;
    }
    b0 = _b0 / a0; b1 = _b1 / a0; b2 = _b2 / a0; a1 = _a1 / a0; a2 = _a2 / a0;
  }
  // magnitude response in dB at frequency f (for the page's curves)
  [[nodiscard]] double gainDbAt(double fs, double f) const noexcept {
    const double w = 2.0 * kPi * f / fs, c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
    const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
    const double m2 = (nr * nr + ni * ni) / ((dr * dr + di * di) + 1e-30);
    return 10.0 * std::log10(m2 + 1e-30);
  }
};

struct EqBand { FilterType type = FilterType::Off; double freq = 1000, q = 1.0, gainDb = 0; };

// a crossover leg (high-pass or low-pass) of order 1..8 as cascaded biquads (odd orders end with a first-order section)
class Crossover {
 public:
  void set(bool highPass, XoverType type, int order, double freq, double fs) noexcept {
    order = order < 0 ? 0 : order > kMaxXoverOrder ? kMaxXoverOrder : order;
    if (type == XoverType::LinkwitzRiley && (order & 1)) ++order;        // LR exists in even orders only
    if (order > kMaxXoverOrder) order = kMaxXoverOrder;
    order_ = order; hp_ = highPass;
    for (auto& s : s_) s.identity();
    if (order == 0 || fs <= 0 || freq <= 0 || freq >= fs * 0.5) { order_ = 0; return; }
    const FilterType ft = highPass ? FilterType::HighPass : FilterType::LowPass;
    int sec = 0;
    if (type == XoverType::LinkwitzRiley) {                                   // LR-2n = two cascaded Butterworth-n
      const int n = order / 2;
      for (int pass = 0; pass < 2; ++pass) sec = butterworth(ft, n, freq, fs, sec);
    } else if (type == XoverType::Bessel) {
      sec = bessel(ft, order, freq, fs, sec);
    } else {
      sec = butterworth(ft, order, freq, fs, sec);
    }
    nsec_ = sec;
  }
  [[nodiscard]] int order() const noexcept { return order_; }
  [[nodiscard]] double tick(double x) noexcept { for (int i = 0; i < nsec_; ++i) x = s_[static_cast<std::size_t>(i)].tick(x); return x; }
  void reset() noexcept { for (auto& s : s_) s.reset(); }
  [[nodiscard]] double gainDbAt(double fs, double f) const noexcept { double g = 0; for (int i = 0; i < nsec_; ++i) g += s_[static_cast<std::size_t>(i)].gainDbAt(fs, f); return g; }

 private:
  // Butterworth of order n: second-order sections with Q = 1 / (2 cos(theta_k)), plus one first-order section when n is odd
  int butterworth(FilterType ft, int n, double f, double fs, int sec) noexcept {
    for (int k = 0; k < n / 2; ++k) { const double q = 1.0 / (2.0 * std::cos(kPi * (2.0 * k + 1) / (2.0 * n))); s_[static_cast<std::size_t>(sec++)].design(ft, fs, f, q, 0); }
    if (n & 1) sec = firstOrder(ft, f, fs, sec);
    return sec;
  }
  // Bessel (normalised to -3 dB at f): pole Q and frequency scaling per second-order section, tables for orders 2..8
  int bessel(FilterType ft, int n, double f, double fs, int sec) noexcept {
    // {frequency factor, Q} per section (odd orders: the last entry is the real pole, Q = 0)
    static constexpr double T[9][4][2] = {
      {}, {{1.0, 0}}, {{1.2736, 0.5773}}, {{1.4524, 0.6910}, {1.3270, 0}}, {{1.4192, 0.5219}, {1.5912, 0.8055}},
      {{1.5611, 0.5635}, {1.7607, 0.9165}, {1.5069, 0}}, {{1.6060, 0.5103}, {1.6913, 0.6112}, {1.9071, 1.0234}},
      {{1.7174, 0.5324}, {1.8235, 0.6608}, {2.0507, 1.1262}, {1.6853, 0}}, {{1.7837, 0.5060}, {1.8376, 0.5596}, {1.9591, 0.7109}, {2.1953, 1.2258}}};
    for (int k = 0; k < (n + 1) / 2; ++k) {
      const double fk = T[n][k][0], q = T[n][k][1], fc = ft == FilterType::LowPass ? f * fk : f / fk;
      if (q > 0) s_[static_cast<std::size_t>(sec++)].design(ft, fs, fc, q, 0); else sec = firstOrder(ft, fc, fs, sec);
    }
    return sec;
  }
  int firstOrder(FilterType ft, double f, double fs, int sec) noexcept {
    const double k = std::tan(kPi * f / fs), a0 = 1 + k; Biquad& b = s_[static_cast<std::size_t>(sec++)];
    if (ft == FilterType::LowPass) { b.b0 = k / a0; b.b1 = k / a0; } else { b.b0 = 1 / a0; b.b1 = -1 / a0; }
    b.b2 = 0; b.a1 = (k - 1) / a0; b.a2 = 0; return sec;
  }
  std::array<Biquad, kMaxXoverOrder> s_{}; int nsec_ = 0, order_ = 0; bool hp_ = true;
};

// brick-wall peak limiter: the gain never lets a sample above the ceiling; one-pole release
class Limiter {
 public:
  void set(bool on, double ceilingDb, double releaseMs, double fs) noexcept { on_ = on; ceil_ = std::pow(10.0, ceilingDb / 20.0); rel_ = 1.0 - std::exp(-1.0 / ((releaseMs < 1 ? 1 : releaseMs) * 0.001 * fs)); }
  [[nodiscard]] double tick(double x) noexcept {
    if (!on_) return x;
    const double pk = std::fabs(x), need = pk > ceil_ ? ceil_ / pk : 1.0;
    if (need < g_) g_ = need; else g_ += (1.0 - g_) * rel_;
    if (g_ > need) g_ = need;
    return x * g_;
  }
  [[nodiscard]] double gainReductionDb() const noexcept { return 20.0 * std::log10(g_ + 1e-12); }
  void reset() noexcept { g_ = 1; }
 private:
  bool on_ = false; double ceil_ = 1, rel_ = 0.001, g_ = 1;
};

struct ChannelParams {
  bool polarityInvert = false;
  double delayMs = 0;                                   // 0..50
  XoverType xoverType = XoverType::LinkwitzRiley;
  int hpOrder = 0, lpOrder = 0;                         // 0 = off; 1..8 = 6..48 dB/oct
  double hpFreq = 80, lpFreq = 4000;
  std::array<EqBand, kEqBands> eq{};
  bool limiterOn = false; double limiterCeilDb = -1, limiterReleaseMs = 80;
};

class Channel {
 public:
  explicit Channel(double fs = 48000) noexcept { setSampleRate(fs); }
  void setSampleRate(double fs) noexcept { fs_ = fs > 0 ? fs : 48000; apply(); reset(); }
  [[nodiscard]] double sampleRate() const noexcept { return fs_; }
  [[nodiscard]] const ChannelParams& params() const noexcept { return p_; }
  void set(const ChannelParams& p) noexcept { p_ = p; apply(); }
  void setEqBand(int i, const EqBand& b) noexcept { if (i >= 0 && i < kEqBands) { p_.eq[static_cast<std::size_t>(i)] = b; eq_[static_cast<std::size_t>(i)].design(b.type, fs_, b.freq, b.q, b.gainDb); } }
  void setDelayMs(double ms) noexcept { p_.delayMs = ms < 0 ? 0 : ms > 50 ? 50 : ms; delaySamples_ = p_.delayMs * 0.001 * fs_; if (delaySamples_ > static_cast<double>(kMaxDelayFrames - 2)) delaySamples_ = static_cast<double>(kMaxDelayFrames - 2); }
  void setPolarity(bool invert) noexcept { p_.polarityInvert = invert; }
  void setCrossover(XoverType t, int hpOrder, double hpFreq, int lpOrder, double lpFreq) noexcept { p_.xoverType = t; p_.hpOrder = hpOrder; p_.hpFreq = hpFreq; p_.lpOrder = lpOrder; p_.lpFreq = lpFreq; hp_.set(true, t, hpOrder, hpFreq, fs_); lp_.set(false, t, lpOrder, lpFreq, fs_); }
  void setLimiter(bool on, double ceilDb, double releaseMs) noexcept { p_.limiterOn = on; p_.limiterCeilDb = ceilDb; p_.limiterReleaseMs = releaseMs; lim_.set(on, ceilDb, releaseMs, fs_); }
  [[nodiscard]] bool active() const noexcept {
    if (p_.polarityInvert || p_.delayMs > 0 || hp_.order() || lp_.order() || p_.limiterOn) return true;
    for (const auto& b : p_.eq) if (b.type != FilterType::Off) return true;
    return false;
  }
  // in-place, `n` frames; real-time safe
  void process(float* x, std::size_t n) noexcept {
    if (!active()) return;
    const double pol = p_.polarityInvert ? -1.0 : 1.0; const bool dl = delaySamples_ > 0;
    for (std::size_t i = 0; i < n; ++i) {
      double v = static_cast<double>(x[i]) * pol;
      if (dl) {
        buf_[w_] = v;
        double rp = static_cast<double>(w_) - delaySamples_; if (rp < 0) rp += static_cast<double>(kMaxDelayFrames);
        const std::size_t i0 = static_cast<std::size_t>(rp), i1 = (i0 + 1) % kMaxDelayFrames; const double fr = rp - static_cast<double>(i0);
        v = buf_[i0] + (buf_[i1] - buf_[i0]) * fr;
        w_ = (w_ + 1) % kMaxDelayFrames;
      }
      v = hp_.tick(v); v = lp_.tick(v);
      for (auto& b : eq_) v = b.tick(v);
      v = lim_.tick(v);
      x[i] = static_cast<float>(v);
    }
  }
  // the whole channel's magnitude response at f (EQ + crossover), for the curves
  [[nodiscard]] double gainDbAt(double f) const noexcept { double g = hp_.gainDbAt(fs_, f) + lp_.gainDbAt(fs_, f); for (const auto& b : eq_) g += b.gainDbAt(fs_, f); return g; }
  [[nodiscard]] double limiterGainReductionDb() const noexcept { return lim_.gainReductionDb(); }
  void reset() noexcept { for (auto& b : eq_) b.reset(); hp_.reset(); lp_.reset(); lim_.reset(); buf_.fill(0); w_ = 0; }

 private:
  void apply() noexcept {
    for (int i = 0; i < kEqBands; ++i) { const EqBand& b = p_.eq[static_cast<std::size_t>(i)]; eq_[static_cast<std::size_t>(i)].design(b.type, fs_, b.freq, b.q, b.gainDb); }
    hp_.set(true, p_.xoverType, p_.hpOrder, p_.hpFreq, fs_); lp_.set(false, p_.xoverType, p_.lpOrder, p_.lpFreq, fs_);
    lim_.set(p_.limiterOn, p_.limiterCeilDb, p_.limiterReleaseMs, fs_);
    setDelayMs(p_.delayMs);
  }
  double fs_ = 48000; ChannelParams p_{};
  std::array<Biquad, kEqBands> eq_{}; Crossover hp_, lp_; Limiter lim_;
  std::array<double, kMaxDelayFrames> buf_{}; std::size_t w_ = 0; double delaySamples_ = 0;
};

}  // namespace am::dsp
