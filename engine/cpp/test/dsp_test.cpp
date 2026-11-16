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
  // 8. compressor: 4:1 above a -20 dB threshold -> a -6 dB sine comes out near -16.5 dB; below the threshold untouched; knee is continuous; GR reported
  { Channel ch(fs); ch.setCompressor(true, -20, 4, 0, 1, 50, 0);
    const double out = sineGainDb(ch, fs, 1000) ; (void)out;   // the helper uses a full-scale sine: -20 + 20/4 = -15 dB
    CHECK(near(out, -15, 0.3), "compressor 4:1 on a 0 dB sine -> -15 dB");
    CHECK(ch.limiterGainReductionDb() < 0.01, "limiter idle"); CHECK(ch.compressor().gainReductionDb() < -14 && ch.compressor().gainReductionDb() > -16, "GR about -15 dB");
    Channel quiet(fs); quiet.setCompressor(true, -20, 4, 0, 1, 50, 0); { std::vector<float> x(48000); for (std::size_t i = 0; i < x.size(); ++i) x[i] = 0.01f * static_cast<float>(std::sin(2 * kPi * 1000 * static_cast<double>(i) / fs)); quiet.process(x.data(), x.size()); double pk = 0; for (std::size_t i = 24000; i < x.size(); ++i) pk = std::max(pk, std::fabs(static_cast<double>(x[i]))); CHECK(near(20 * std::log10(pk), -40, 0.2), "below threshold untouched"); }
    Channel knee(fs); knee.setCompressor(true, -20, 4, 12, 1, 50, 0); const auto& K = knee.compressor();
    CHECK(near(K.computeDb(-26), -26, 1e-9) && near(K.computeDb(-14), -20 + 6 / 4.0, 1e-9), "knee ends meet the straight lines");
    CHECK(std::fabs(K.computeDb(-20.001) - K.computeDb(-19.999)) < 0.01, "knee is continuous at the threshold");
    Channel mk(fs); mk.setCompressor(true, -20, 4, 0, 1, 50, 6); CHECK(near(sineGainDb(mk, fs, 1000), -9, 0.3), "make-up adds 6 dB"); }
  // 9. gate: -40 dB noise with a -30 dB threshold is attenuated by the range; a -20 dB signal passes; hysteresis closes 3 dB under the threshold
  { Channel g(fs); g.setGate(true, -30, -60, 1, 10, 20);
    std::vector<float> x(48000); for (std::size_t i = 0; i < x.size(); ++i) x[i] = 0.01f * static_cast<float>(std::sin(2 * kPi * 1000 * static_cast<double>(i) / fs));   // -40 dB
    g.process(x.data(), x.size()); double pk = 0; for (std::size_t i = 24000; i < x.size(); ++i) pk = std::max(pk, std::fabs(static_cast<double>(x[i])));
    CHECK(20 * std::log10(pk + 1e-12) < -95, "closed: -40 dB - 60 dB range"); CHECK(!g.gate().isOpen() && g.gate().gainDb() < -59, "gate reports closed");
    Channel o(fs); o.setGate(true, -30, -60, 1, 10, 20); CHECK(near(sineGainDb(o, fs, 1000), 0, 0.1), "a full-scale sine passes"); CHECK(o.gate().isOpen(), "gate reports open");
    CHECK(near(o.gate().computeDb(-20), -20, 1e-9) && near(o.gate().computeDb(-40), -100, 1e-9), "static curve: closed below the threshold");
    Channel off(fs); CHECK(off.gate().isOpen(), "a gate that is off is open"); }
  { // FAST dB MATH (the dynamics' per-sample level and gain): against the C library over the whole working range
    double worstDb = 0, worstLin = 0;
    for (double x = 1e-9; x < 20.0; x *= 1.0137) worstDb = std::max(worstDb, std::fabs(am::dsp::fastm::dbOf(x) - 20.0 * std::log10(x)));
    for (double d = -200; d <= 40; d += 0.0371) worstLin = std::max(worstLin, std::fabs(am::dsp::fastm::linOf(d) / std::pow(10.0, d / 20.0) - 1.0));
    std::printf("dsp_test: fast dB math, worst error %.2e dB in the level, %.2e (relative) in the gain\n", worstDb, worstLin);
    CHECK(worstDb < 2e-6, "fast level within 2e-6 dB"); CHECK(worstLin < 2e-7, "fast gain within 2e-7 (about 2e-6 dB)");
    CHECK(am::dsp::fastm::linOf(-50000) == 0.0 && am::dsp::fastm::linOf(0) == 1.0, "fast gain: off and unity exactly"); }
  { // EQ BANDS THAT ARE RUN: off and 0 dB bands are skipped; a band switched on (alone, as the CHANNEL page does) is heard at once
    am::dsp::Channel ch(48000); ch.setEqBand(3, {am::dsp::FilterType::Peaking, 1000, 1, 0});   // a bell at 0 dB: nothing to run
    CHECK(near(sineGainDb(ch, 48000, 1000), 0, 0.01), "a 0 dB bell is flat");
    am::dsp::Channel on(48000); on.setEqBand(3, {am::dsp::FilterType::Peaking, 1000, 1, 0}); on.setEqBand(3, {am::dsp::FilterType::Peaking, 1000, 1, 6});
    CHECK(near(sineGainDb(on, 48000, 1000), 6, 0.05), "the same band raised to +6 dB is heard");
    on.setEqBand(3, {am::dsp::FilterType::Off, 1000, 1, 6}); CHECK(near(sineGainDb(on, 48000, 1000), 0, 0.01), "switched off again: flat"); }
  { // IDLE CHANNEL: silence in rests the DSP only once nothing more can come out, the delay line included; sound wakes it at once
    am::dsp::Channel ch(48000); am::dsp::ChannelParams p = ch.params(); p.compOn = true; p.gateOn = true; p.delayMs = 50; p.eq[1] = {am::dsp::FilterType::Peaking, 1000, 1, 6}; ch.set(p);
    std::vector<float> b(256);
    for (std::size_t i = 0; i < b.size(); ++i) b[i] = 0.5f * static_cast<float>(std::sin(i * 0.1)); ch.process(b.data(), b.size());
    int restedAt = -1; bool delayedCameOut = false;
    for (int k = 0; k < 40; ++k) { std::fill(b.begin(), b.end(), 0.0f); ch.process(b.data(), b.size()); for (float v : b) if (std::fabs(v) > 1e-3f) delayedCameOut = true; if (ch.idle() && restedAt < 0) restedAt = k; }
    CHECK(delayedCameOut, "the 50 ms delay still plays out after the input went silent");
    CHECK(restedAt > 9, "not resting before the delay line (50 ms = 9.4 blocks) has emptied");
    CHECK(restedAt >= 0, "an idle channel rests");
    for (std::size_t i = 0; i < b.size(); ++i) b[i] = 0.5f; ch.process(b.data(), b.size()); CHECK(!ch.idle(), "sound wakes it in the same block"); }
  if (fails) { std::fprintf(stderr, "%d failure(s)\n", fails); return 1; }
  std::printf("dsp_test: ok\n"); return 0;
}
