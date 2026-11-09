// A mix OS channel DSP: numbers a filter designer would check.  dsp_test   (exit 0 = all good)
#include "audiomixer/dsp.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace am::dsp;
static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s (%s:%d)\n", what, __FILE__, __LINE__); ++fails; } } while (0)
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// RMS of a steady sine after `f` has been run through the channel (the transient skipped)
static double sineGainDb(Channel& ch, double fs, double f) {
  const std::size_t n = static_cast<std::size_t>(fs);   // one second
  std::vector<float> x(n); for (std::size_t i = 0; i < n; ++i) x[i] = static_cast<float>(std::sin(2 * kPi * f * static_cast<double>(i) / fs));
  ch.reset(); ch.process(x.data(), n);
  double s = 0; const std::size_t from = n / 2; for (std::size_t i = from; i < n; ++i) s += static_cast<double>(x[i]) * x[i];
  return 20 * std::log10(std::sqrt(s / static_cast<double>(n - from)) * std::sqrt(2.0) + 1e-12);
}

int main() {
  const double fs = 48000;
  // 1. a peaking band of +6 dB at 1 kHz: +6 at 1 kHz, about 0 far away; the analytic response agrees with the measured one
  { Channel ch(fs); ch.setEqBand(0, {FilterType::Peaking, 1000, 1.0, 6});
    CHECK(near(ch.gainDbAt(1000), 6, 0.01), "peaking +6 dB at centre"); CHECK(near(ch.gainDbAt(50), 0, 0.1), "peaking flat far below"); CHECK(near(ch.gainDbAt(15000), 0, 0.2), "peaking flat far above");
    CHECK(near(sineGainDb(ch, fs, 1000), 6, 0.05), "measured +6 dB"); CHECK(near(sineGainDb(ch, fs, 100), 0, 0.1), "measured flat at 100 Hz"); }
  // 2. shelves
  { Channel ch(fs); ch.setEqBand(0, {FilterType::LowShelf, 200, 0.707, -4}); ch.setEqBand(1, {FilterType::HighShelf, 6000, 0.707, 3});
    CHECK(near(ch.gainDbAt(20), -4, 0.1), "low shelf -4 dB in the bass"); CHECK(near(ch.gainDbAt(20000), 3, 0.15), "high shelf +3 dB in the treble"); CHECK(near(ch.gainDbAt(1500), 0, 0.3), "shelves meet near 0 in the middle"); }
  // 3. crossovers: Linkwitz-Riley is -6 dB at the corner (so HP + LP sum flat), Butterworth -3 dB, slopes match the order
  for (int order : {2, 4, 8}) {
    Channel hp(fs); hp.setCrossover(XoverType::LinkwitzRiley, order, 1000, 0, 0);
    CHECK(near(hp.gainDbAt(1000), -6.02, 0.1), "LR -6 dB at the corner");
    const double slope = hp.gainDbAt(125) - hp.gainDbAt(250);   // dB per octave, well below the corner
    CHECK(near(slope, -6.02 * order, 0.6), "LR slope = 6 dB/oct per order");
    Channel lp(fs); lp.setCrossover(XoverType::LinkwitzRiley, 0, 0, order, 1000);
    // the two legs sum to unity magnitude at the corner (in phase): check at the corner by adding the outputs of both legs
    const std::size_t n = 48000; std::vector<float> a(n), b(n);
    for (std::size_t i = 0; i < n; ++i) a[i] = b[i] = static_cast<float>(std::sin(2 * kPi * 1000 * static_cast<double>(i) / fs));
    hp.reset(); lp.reset(); hp.process(a.data(), n); lp.process(b.data(), n);
    // LR-2 legs are 180 degrees apart at the corner (one driver is wired inverted in practice); LR-4 and LR-8 are in phase
    const double sign = (order % 4 == 2) ? -1.0 : 1.0;
    double s = 0; for (std::size_t i = n / 2; i < n; ++i) { const double v = a[i] + sign * b[i]; s += v * v; }
    CHECK(near(20 * std::log10(std::sqrt(s / (n / 2.0)) * std::sqrt(2.0)), 0, 0.1), "LR high + low sums flat at the corner");
  }
  { Channel bw(fs); bw.setCrossover(XoverType::Butterworth, 4, 1000, 0, 0); CHECK(near(bw.gainDbAt(1000), -3.01, 0.1), "Butterworth -3 dB at the corner");
    Channel bw1(fs); bw1.setCrossover(XoverType::Butterworth, 1, 1000, 0, 0); CHECK(near(bw1.gainDbAt(1000), -3.01, 0.1), "1st order -3 dB"); CHECK(near(bw1.gainDbAt(125) - bw1.gainDbAt(250), -6.02, 0.3), "1st order 6 dB/oct");
    Channel be(fs); be.setCrossover(XoverType::Bessel, 4, 1000, 0, 0); CHECK(near(be.gainDbAt(1000), -3.01, 0.6), "Bessel about -3 dB at the corner"); CHECK(be.gainDbAt(125) < -20, "Bessel 4th order attenuates well below");
    Channel lr3(fs); lr3.setCrossover(XoverType::LinkwitzRiley, 3, 1000, 0, 0); CHECK(lr3.params().hpOrder == 3 && near(lr3.gainDbAt(125) - lr3.gainDbAt(250), -6.02 * 4, 0.6), "LR odd order rounds up to the next even order"); }
  // 4. delay: 1.5 ms moves a step by 72 samples at 48 kHz (sub-sample positions interpolate)
  { Channel ch(fs); ch.setDelayMs(1.5); std::vector<float> x(200, 0.0f); for (std::size_t i = 100; i < 200; ++i) x[i] = 1.0f;
    ch.process(x.data(), x.size()); std::size_t first = 0; for (std::size_t i = 0; i < 200; ++i) if (x[i] > 0.5f) { first = i; break; }
    CHECK(first == 172, "step delayed by 72 samples"); CHECK(x[171] < 0.01f, "silence before the delayed step");
    ch.setDelayMs(1.5 + 0.5 / 48.0); std::vector<float> y(200, 0.0f); for (std::size_t i = 100; i < 200; ++i) y[i] = 1.0f; ch.reset(); ch.process(y.data(), 200);
    CHECK(near(y[172], 0.5, 0.01), "half a sample of delay: linear interpolation gives 0.5 at the edge"); }
  // 5. polarity, all-pass (gain 1, phase shifted), limiter
  { Channel ch(fs); ch.setPolarity(true); float v[3] = {0.5f, -0.25f, 1.0f}; ch.process(v, 3); CHECK(v[0] == -0.5f && v[1] == 0.25f && v[2] == -1.0f, "polarity inverts");
    Channel ap(fs); ap.setEqBand(0, {FilterType::AllPass2, 500, 0.7, 0}); ap.setEqBand(1, {FilterType::AllPass1, 2000, 0.7, 0});
    CHECK(near(ap.gainDbAt(100), 0, 0.01) && near(ap.gainDbAt(500), 0, 0.01) && near(ap.gainDbAt(8000), 0, 0.01), "all-pass is flat in magnitude");
    Channel lim(fs); lim.setLimiter(true, -6, 50); std::vector<float> loud(4800, 0.9f); lim.process(loud.data(), loud.size());
    float mx = 0; for (float s : loud) mx = std::max(mx, std::fabs(s)); CHECK(mx <= 0.5012f + 1e-4f, "limiter never above -6 dBFS"); CHECK(lim.limiterGainReductionDb() < -4.5, "gain reduction reported"); }
  // 6. an idle channel is a no-op and reports inactive; a channel at a wrong rate recomputes
  { Channel ch(fs); CHECK(!ch.active(), "fresh channel inactive"); float v[2] = {0.3f, -0.3f}; ch.process(v, 2); CHECK(v[0] == 0.3f && v[1] == -0.3f, "inactive = untouched");
    ch.setEqBand(2, {FilterType::Peaking, 1000, 1, 6}); ch.setSampleRate(96000); CHECK(near(ch.gainDbAt(1000), 6, 0.01), "coefficients recomputed for 96 kHz"); }
  // 7. a band above Nyquist or at 0 Hz is ignored, never NaN
  { Channel ch(fs); ch.setEqBand(0, {FilterType::Peaking, 30000, 1, 6}); ch.setEqBand(1, {FilterType::LowPass, 0, 1, 0}); float v[4] = {1, 1, 1, 1}; ch.process(v, 4); CHECK(std::isfinite(v[3]) && near(v[3], 1, 1e-6), "impossible bands = identity"); }
  if (fails) { std::fprintf(stderr, "%d failure(s)\n", fails); return 1; }
  std::printf("dsp_test: ok\n"); return 0;
}
