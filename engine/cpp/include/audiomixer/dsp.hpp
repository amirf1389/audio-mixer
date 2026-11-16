// A mix OS channel DSP (C++20): the processing of one channel strip before it reaches the mixer, in the order a car / studio processor applies it.
//   polarity -> delay (time alignment, 0..50 ms, sub-sample by linear interpolation) -> noise gate (threshold with 3 dB hysteresis, attack /
//   hold / release, range) -> high-pass / low-pass crossover (Butterworth, Linkwitz-Riley or Bessel, 6..48 dB/octave) -> parametric EQ (kEqBands
//   biquads: peaking, low / high shelf, low / high pass, notch, all-pass 1st / 2nd order) -> compressor (feed-forward peak detector, soft knee,
//   attack / release in the dB domain, make-up) -> peak limiter (instant attack, one-pole release)
// Real-time rules: no allocation, no locks, no exceptions and no I/O in process(); every buffer is a fixed array sized at compile time; parameter changes
// recompute coefficients in the caller's thread (set*) and are applied at the next block, so a change never tears a filter in the middle of a block.
// All arithmetic in double (the "64-bit summing" of the Pro tier); the audio arrives and leaves as float.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace am::dsp {

// FAST dB MATH for the per-sample dynamics (compressor, gate): the level in dB and the gain back as a factor, every sample of every channel.
// The C library's log10 / pow cost about half of the engine's time, and more on processors without FMA (older AMD and Intel), where they
// take a slow generic path. These use plain arithmetic any x86-64 (SSE2) runs fast: under 2e-6 dB from the exact values (inaudible).
namespace fastm {
// log2(x) for x > 0 (normal numbers): exponent from the bits, the mantissa m in [1, 2) through atanh's series in t = (m-1)/(m+1) (t < 1/3)
inline double log2(double x) noexcept {
  std::uint64_t b; std::memcpy(&b, &x, sizeof b);
  const int e = static_cast<int>((b >> 52) & 0x7ff) - 1023; b = (b & 0x000fffffffffffffull) | 0x3ff0000000000000ull; double m; std::memcpy(&m, &b, sizeof m);
  const double t = (m - 1.0) / (m + 1.0), t2 = t * t;
  return e + 2.8853900817779268 * t * (1.0 + t2 * (1.0 / 3 + t2 * (1.0 / 5 + t2 * (1.0 / 7 + t2 * (1.0 / 9 + t2 * (1.0 / 11 + t2 * (1.0 / 13)))))));   // 2/ln 2
}
// 2^y: the whole part into the exponent bits, the fraction (in [0, 1)) by e^(f ln 2) to the 9th power term
inline double exp2(double y) noexcept {
  if (y < -1000.0) return 0.0; if (y > 1000.0) y = 1000.0;
  double fl = static_cast<double>(static_cast<std::int64_t>(y)); if (fl > y) fl -= 1.0;   // floor without a library call (SSE2 has no rounding instruction)
  const double f = (y - fl) * 0.6931471805599453;
  const double p = 1.0 + f * (1.0 + f * (0.5 + f * (1.0 / 6 + f * (1.0 / 24 + f * (1.0 / 120 + f * (1.0 / 720 + f * (1.0 / 5040 + f * (1.0 / 40320 + f * (1.0 / 362880)))))))));
  const std::uint64_t b = static_cast<std::uint64_t>(static_cast<std::int64_t>(fl) + 1023) << 52; double s; std::memcpy(&s, &b, sizeof s); return p * s;
}
inline double dbOf(double lin) noexcept { return 6.020599913279624 * log2(lin); }    // 20 log10(x), x > 0
inline double linOf(double db) noexcept { return exp2(db * 0.16609640474436813); }   // 10^(db/20)
}  // namespace fastm

constexpr double kPi = 3.14159265358979323846;
constexpr int kEqBands = 13;        // 0..9: the ten bands of the MIC EQ / parametric rack; 10..12: the CHANNEL EQ (low shelf, mid, high shelf)
constexpr int kChannelEqFirst = 10;  // the first of the three channel EQ bands
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

// feed-forward compressor: a smoothed peak detector, a soft-knee gain computer in dB, attack / release on the gain
class Compressor {
 public:
  void set(bool on, double thresholdDb, double ratio, double kneeDb, double attackMs, double releaseMs, double makeupDb, double fs) noexcept {
    on_ = on; thr_ = thresholdDb; ratio_ = ratio < 1 ? 1 : ratio; knee_ = kneeDb < 0 ? 0 : kneeDb; makeup_ = makeupDb;
    kneeLin_ = std::pow(10.0, (thr_ - knee_ / 2) / 20.0); gLinAt_ = 1e300;   // below the knee nothing is computed in dB
    att_ = 1.0 - std::exp(-1.0 / ((attackMs < 0.05 ? 0.05 : attackMs) * 0.001 * fs)); rel_ = 1.0 - std::exp(-1.0 / ((releaseMs < 1 ? 1 : releaseMs) * 0.001 * fs));
    det_ = 1.0 - std::exp(-1.0 / (0.02 * fs));   // peak detector: instant rise, 20 ms fall, so a steady sine reads its peak
  }
  // the static transfer: output level for an input level (dB), without make-up; what the page draws
  [[nodiscard]] double computeDb(double inDb) const noexcept {
    const double over = inDb - thr_;
    if (knee_ > 0 && std::fabs(over) <= knee_ / 2) { const double t = over + knee_ / 2; return inDb + (1.0 / ratio_ - 1.0) * t * t / (2.0 * knee_); }
    return over > 0 ? thr_ + over / ratio_ : inDb;
  }
  [[nodiscard]] double tick(double x) noexcept {
    if (!on_) return x;
    const double a = std::fabs(x); env_ = a > env_ ? a : env_ + (a - env_) * det_;   // peak follower
    double want = 0.0; if (env_ + 1e-9 > kneeLin_) { const double inDb = fastm::dbOf(env_ + 1e-9); want = computeDb(inDb) - inDb; }   // <= 0: how much to turn down
    gr_ += (want - gr_) * (want < gr_ ? att_ : rel_); if (std::fabs(want - gr_) < 1e-7) gr_ = want;   // settled: the same gain as before, exactly
    const double g = gr_ + makeup_; if (g != gLinAt_) { gLinAt_ = g; gLin_ = fastm::linOf(g); }      // the factor only when the gain moved
    return x * gLin_;
  }
  [[nodiscard]] double gainReductionDb() const noexcept { return on_ ? gr_ : 0; }
  [[nodiscard]] bool on() const noexcept { return on_; }
  void reset() noexcept { env_ = 0; gr_ = 0; }
 private:
  bool on_ = false; double thr_ = -20, ratio_ = 4, knee_ = 6, makeup_ = 0, att_ = 0.01, rel_ = 0.001, det_ = 0.05, env_ = 0, gr_ = 0, kneeLin_ = 0.07, gLinAt_ = 1e300, gLin_ = 1;
};

// noise gate: opens above the threshold, closes 3 dB below it (hysteresis) after the hold time; attenuates by `range` when closed
class Gate {
 public:
  void set(bool on, double thresholdDb, double rangeDb, double attackMs, double holdMs, double releaseMs, double fs) noexcept {
    on_ = on; thr_ = thresholdDb; range_ = rangeDb > 0 ? 0 : rangeDb < -90 ? -90 : rangeDb; holdSamples_ = static_cast<std::uint32_t>((holdMs < 0 ? 0 : holdMs) * 0.001 * fs);
    thrLin_ = std::pow(10.0, thr_ / 20.0); closeLin_ = std::pow(10.0, (thr_ - 3.0) / 20.0); gLinAt_ = 1e300;   // the gate compares levels as plain numbers
    att_ = 1.0 - std::exp(-1.0 / ((attackMs < 0.05 ? 0.05 : attackMs) * 0.001 * fs)); rel_ = 1.0 - std::exp(-1.0 / ((releaseMs < 1 ? 1 : releaseMs) * 0.001 * fs));
    det_ = 1.0 - std::exp(-1.0 / (0.02 * fs));
  }
  // the static transfer for the page: in -> out (dB), closed below the threshold by `range`
  [[nodiscard]] double computeDb(double inDb) const noexcept { return inDb >= thr_ ? inDb : inDb + range_; }
  [[nodiscard]] double tick(double x) noexcept {
    if (!on_) return x;
    const double a = std::fabs(x); env_ = a > env_ ? a : env_ + (a - env_) * det_;
    const double lvl = env_ + 1e-9;
    if (lvl >= thrLin_) { open_ = true; hold_ = holdSamples_; }
    else if (open_ && lvl < closeLin_) { if (hold_ > 0) --hold_; else open_ = false; }
    const double target = open_ ? 0.0 : range_;
    g_ += (target - g_) * (target > g_ ? att_ : rel_); if (std::fabs(target - g_) < 1e-7) g_ = target;   // settled: exactly open or closed
    if (g_ != gLinAt_) { gLinAt_ = g_; gLin_ = fastm::linOf(g_); }
    return x * gLin_;
  }
  [[nodiscard]] double gainDb() const noexcept { return on_ ? g_ : 0; }
  [[nodiscard]] bool isOpen() const noexcept { return !on_ || open_; }
  void reset() noexcept { env_ = 0; g_ = 0; open_ = false; hold_ = 0; }
 private:
  bool on_ = false, open_ = false; double thr_ = -50, range_ = -80, att_ = 0.1, rel_ = 0.001, det_ = 0.05, env_ = 0, g_ = 0, thrLin_ = 0.003, closeLin_ = 0.002, gLinAt_ = 1e300, gLin_ = 1; std::uint32_t holdSamples_ = 0, hold_ = 0;
};

struct ChannelParams {
  bool polarityInvert = false;
  double delayMs = 0;                                   // 0..50
  XoverType xoverType = XoverType::LinkwitzRiley;
  int hpOrder = 0, lpOrder = 0;                         // 0 = off; 1..8 = 6..48 dB/oct
  double hpFreq = 80, lpFreq = 4000;
  std::array<EqBand, kEqBands> eq{};
  bool limiterOn = false; double limiterCeilDb = -1, limiterReleaseMs = 80;
  bool compOn = false; double compThresholdDb = -20, compRatio = 4, compKneeDb = 6, compAttackMs = 10, compReleaseMs = 100, compMakeupDb = 0;
  bool gateOn = false; double gateThresholdDb = -50, gateRangeDb = -80, gateAttackMs = 1, gateHoldMs = 50, gateReleaseMs = 100;
};

class Channel {
 public:
  explicit Channel(double fs = 48000) noexcept { setSampleRate(fs); }
  void setSampleRate(double fs) noexcept { fs_ = fs > 0 ? fs : 48000; apply(); reset(); }
  [[nodiscard]] double sampleRate() const noexcept { return fs_; }
  [[nodiscard]] const ChannelParams& params() const noexcept { return p_; }
  void set(const ChannelParams& p) noexcept { p_ = p; apply(); }
  void setEqBand(int i, const EqBand& b) noexcept { if (i >= 0 && i < kEqBands) { p_.eq[static_cast<std::size_t>(i)] = b; eqList(i); } }   // the list of bands that run follows
  void setDelayMs(double ms) noexcept { p_.delayMs = ms < 0 ? 0 : ms > 50 ? 50 : ms; delaySamples_ = p_.delayMs * 0.001 * fs_; if (delaySamples_ > static_cast<double>(kMaxDelayFrames - 2)) delaySamples_ = static_cast<double>(kMaxDelayFrames - 2); }
  void setPolarity(bool invert) noexcept { p_.polarityInvert = invert; }
  void setCrossover(XoverType t, int hpOrder, double hpFreq, int lpOrder, double lpFreq) noexcept { p_.xoverType = t; p_.hpOrder = hpOrder; p_.hpFreq = hpFreq; p_.lpOrder = lpOrder; p_.lpFreq = lpFreq; hp_.set(true, t, hpOrder, hpFreq, fs_); lp_.set(false, t, lpOrder, lpFreq, fs_); }
  void setLimiter(bool on, double ceilDb, double releaseMs) noexcept { p_.limiterOn = on; p_.limiterCeilDb = ceilDb; p_.limiterReleaseMs = releaseMs; lim_.set(on, ceilDb, releaseMs, fs_); }
  void setCompressor(bool on, double thr, double ratio, double knee, double att, double rel, double makeup) noexcept { p_.compOn = on; p_.compThresholdDb = thr; p_.compRatio = ratio; p_.compKneeDb = knee; p_.compAttackMs = att; p_.compReleaseMs = rel; p_.compMakeupDb = makeup; comp_.set(on, thr, ratio, knee, att, rel, makeup, fs_); }
  void setGate(bool on, double thr, double range, double att, double hold, double rel) noexcept { p_.gateOn = on; p_.gateThresholdDb = thr; p_.gateRangeDb = range; p_.gateAttackMs = att; p_.gateHoldMs = hold; p_.gateReleaseMs = rel; gate_.set(on, thr, range, att, hold, rel, fs_); }
  [[nodiscard]] const Compressor& compressor() const noexcept { return comp_; }
  [[nodiscard]] const Gate& gate() const noexcept { return gate_; }
  [[nodiscard]] bool active() const noexcept {
    if (p_.polarityInvert || p_.delayMs > 0 || hp_.order() || lp_.order() || p_.limiterOn || p_.compOn || p_.gateOn) return true;
    for (const auto& b : p_.eq) if (b.type != FilterType::Off) return true;
    return false;
  }
  // in-place, `n` frames; real-time safe
  void process(float* x, std::size_t n) noexcept {
    if (!active()) return;
    // IDLE: digital silence in, for longer than the delay line holds, and the output already died away (under -140 dBFS): nothing can
    // come out, so the EQ and dynamics rest (their state is kept) until sound comes back. A muted channel is not idle: its meter and
    // pre-fader sends still need it.
    { bool silent = true; for (std::size_t i = 0; i < n && silent; ++i) silent = x[i] == 0.0f;
      if (!silent) zeroRun_ = 0; else { zeroRun_ += n; if (lastOut_ < 1e-7 && zeroRun_ > static_cast<std::size_t>(delaySamples_) + 2 * n) { idle_ = true; return; } } }
    idle_ = false; double outPk = 0;
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
      v = gate_.tick(v);
      v = hp_.tick(v); v = lp_.tick(v);
      for (int k = 0; k < nEqOn_; ++k) v = eq_[static_cast<std::size_t>(eqOn_[static_cast<std::size_t>(k)])].tick(v);   // only the bands that change the sound
      v = comp_.tick(v);
      v = lim_.tick(v);
      x[i] = static_cast<float>(v); const double av = v < 0 ? -v : v; if (av > outPk) outPk = av;
    }
    lastOut_ = outPk;
  }
  [[nodiscard]] bool idle() const noexcept { return idle_; }   // resting this block (diagnostics, tests)
  // the whole channel's magnitude response at f (EQ + crossover), for the curves
  [[nodiscard]] double gainDbAt(double f) const noexcept { double g = hp_.gainDbAt(fs_, f) + lp_.gainDbAt(fs_, f); for (const auto& b : eq_) g += b.gainDbAt(fs_, f); return g; }
  [[nodiscard]] double limiterGainReductionDb() const noexcept { return lim_.gainReductionDb(); }
  void reset() noexcept { for (auto& b : eq_) b.reset(); hp_.reset(); lp_.reset(); lim_.reset(); comp_.reset(); gate_.reset(); buf_.fill(0); w_ = 0; }

 private:
  void apply() noexcept {
    eqList();
    hp_.set(true, p_.xoverType, p_.hpOrder, p_.hpFreq, fs_); lp_.set(false, p_.xoverType, p_.lpOrder, p_.lpFreq, fs_);
    lim_.set(p_.limiterOn, p_.limiterCeilDb, p_.limiterReleaseMs, fs_);
    comp_.set(p_.compOn, p_.compThresholdDb, p_.compRatio, p_.compKneeDb, p_.compAttackMs, p_.compReleaseMs, p_.compMakeupDb, fs_);
    gate_.set(p_.gateOn, p_.gateThresholdDb, p_.gateRangeDb, p_.gateAttackMs, p_.gateHoldMs, p_.gateReleaseMs, fs_);
    setDelayMs(p_.delayMs);
  }
  // designs every EQ band and lists the ones that change the sound (the only ones run per sample)
  void eqList(int only = -1) noexcept {   // only: the one band that changed (the others keep their design)
    nEqOn_ = 0;
    for (int i = 0; i < kEqBands; ++i) { const EqBand& b = p_.eq[static_cast<std::size_t>(i)]; Biquad& q = eq_[static_cast<std::size_t>(i)]; if (only < 0 || only == i) q.design(b.type, fs_, b.freq, b.q, b.gainDb);
      // a band that is off, or a bell / shelf at exactly 0 dB, passes the sound unchanged: it is not run (its state is cleared, so turning it on starts clean)
      const bool flat = (b.type == FilterType::Peaking || b.type == FilterType::LowShelf || b.type == FilterType::HighShelf) && b.gainDb == 0.0;
      const bool pass = (q.b0 == 1 && q.b1 == 0 && q.b2 == 0 && q.a1 == 0 && q.a2 == 0) || flat;
      if (pass) q.reset(); else eqOn_[static_cast<std::size_t>(nEqOn_++)] = i; }
  }
  double fs_ = 48000; ChannelParams p_{};
  std::array<Biquad, kEqBands> eq_{}; Crossover hp_, lp_; Limiter lim_; Compressor comp_; Gate gate_;
  std::array<double, kMaxDelayFrames> buf_{}; std::size_t w_ = 0; double delaySamples_ = 0;
  std::size_t zeroRun_ = 0; double lastOut_ = 1.0; bool idle_ = false;
  std::array<int, kEqBands> eqOn_{}; int nEqOn_ = 0;   // the EQ bands that are run, in order   // IDLE: samples of silence in a row, the last block's output peak
};

}  // namespace am::dsp
