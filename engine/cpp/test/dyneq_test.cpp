// AmF DYNAMIC EQ: the band math against the reference biquad (fx::Biquad) and against a measured sine, the Butterworth cuts, the
// dynamic gain reduction, M / S, no NaN / denormals; and the FabFilter quick-load's vendor detection on a fake plug-in list.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "fx/fx.hpp"
#include "vst3/vst3_host.hpp"

using namespace am;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s: ", __FILE__, __LINE__, #c); std::fprintf(stderr, " " __VA_ARGS__); std::fprintf(stderr, "\n"); ++fails; } } while (0)

static double refDb(const fx::Biquad& q, double fs, double f) { fx::DynEq::Coef c; c.b0 = q.b0; c.b1 = q.b1; c.b2 = q.b2; c.a1 = q.a1; c.a2 = q.a2; return fx::DynEq::magnitudeDb(&c, 1, fs, f); }
static double nFreq(double hz) { return std::log(hz / 20) / std::log(1000.0); }
static double nQ(double q) { return std::log(q / 0.1) / std::log(300.0); }
static double nGain(double db) { return db / 60 + 0.5; }
// rms in dB of the second half of a sine through the effect
static double sineGainDb(fx::DynEq& e, double fs, double hz, double amp, double secs = 0.5) {
  const int n = static_cast<int>(fs * secs); std::vector<float> L(n), R(n); for (int i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(amp * std::sin(2 * 3.14159265358979 * hz * i / fs));
  std::vector<float> in = L; for (int i = 0; i < n; i += 256) e.process(L.data() + i, R.data() + i, static_cast<std::size_t>(std::min(256, n - i)));
  double a = 0, b = 0; for (int i = n / 2; i < n; ++i) { a += double(L[i]) * L[i]; b += double(in[i]) * in[i]; } return 10 * std::log10(a / b);
}

int main() {
  const double fs = 48000;
  { // band math: bell / shelves / notch / band pass equal the reference biquad's response
    const struct { int type; fx::Biquad::Kind ref; } pairs[] = {{fx::DynEq::Bell, fx::Biquad::Peak}, {fx::DynEq::LowShelf, fx::Biquad::LowShelf}, {fx::DynEq::HighShelf, fx::Biquad::HighShelf}, {fx::DynEq::BandPass, fx::Biquad::BandPass}};
    for (const auto& p : pairs) for (double f0 : {60.0, 1000.0, 9000.0}) for (double g : {-12.0, 6.0}) {
      fx::DynEq::Coef c[8]; const int u = fx::DynEq::design(p.type, fs, f0, 1.4, g, 24, c); fx::Biquad q; q.design(p.ref, fs, f0, 1.4, p.ref == fx::Biquad::BandPass ? 0 : g);
      double worst = 0; for (double f = 20; f < 20000; f *= 1.07) worst = std::max(worst, std::fabs(fx::DynEq::magnitudeDb(c, u, fs, f) - refDb(q, fs, f)));
      CHECK(u == 1 && worst < 1e-6, "type %d f %.0f g %.0f: %.2e dB off", p.type, f0, g, worst); }
    // a bell measured with a sine: +6 dB at the centre, the curve the editor draws says the same
    fx::DynEq e(fs); e.setBand(0, fx::DynEq::On, 1); e.setBand(0, fx::DynEq::Freq, nFreq(1000)); e.setBand(0, fx::DynEq::Gain, nGain(6)); e.setBand(0, fx::DynEq::Q, nQ(1));
    const double m = sineGainDb(e, fs, 1000, 0.25); CHECK(std::fabs(m - 6) < 0.1, "bell measured %.2f dB", m); CHECK(std::fabs(e.responseDb(1000, false) - 6) < 0.05, "curve %.2f", e.responseDb(1000, false));
    // the Butterworth cuts: -3 dB at the corner, slope x octaves below (order n: 10 log10(1 + 2^(2n)))
    for (int slope : {6, 12, 18, 24, 48, 96}) { fx::DynEq::Coef c[8]; const int u = fx::DynEq::design(fx::DynEq::LowCut, fs, 1000, 1, 0, slope, c); const int n = slope / 6;
      const double at = fx::DynEq::magnitudeDb(c, u, fs, 1000), half = fx::DynEq::magnitudeDb(c, u, fs, 500), want = -10 * std::log10(1 + std::pow(2.0, 2 * n));
      CHECK(std::fabs(at + 3.0103) < 0.05 && std::fabs(half - want) < 0.6 && std::fabs(fx::DynEq::magnitudeDb(c, u, fs, 15000)) < 0.05, "low cut %d: %.2f at fc, %.2f (want %.2f) an octave below", slope, at, half, want);
      const int uh = fx::DynEq::design(fx::DynEq::HighCut, fs, 1000, 1, 0, slope, c); CHECK(std::fabs(fx::DynEq::magnitudeDb(c, uh, fs, 1000) + 3.0103) < 0.05 && std::fabs(fx::DynEq::magnitudeDb(c, uh, fs, 50)) < 0.05, "high cut %d", slope); }
    { fx::DynEq::Coef c[8]; const int u = fx::DynEq::design(fx::DynEq::Notch, fs, 2000, 4, 0, 24, c); CHECK(fx::DynEq::magnitudeDb(c, u, fs, 2000) < -60 && std::fabs(fx::DynEq::magnitudeDb(c, u, fs, 200)) < 0.1, "notch"); }
    std::printf("dyneq_test: bell / shelves / band pass = reference biquad, sine-measured bell, Butterworth cuts 6 .. 96 dB/oct, notch OK\n"); }
  { // dynamic gain reduction: 4:1 above -30 dB, at most 12 dB down; under the threshold the band stays flat
    auto make = [&] { auto e = std::make_unique<fx::DynEq>(fs); e->setBand(0, fx::DynEq::On, 1); e->setBand(0, fx::DynEq::Freq, nFreq(1000)); e->setBand(0, fx::DynEq::Q, nQ(1)); e->setBand(0, fx::DynEq::Dyn, 1);
      e->setBand(0, fx::DynEq::Thresh, 1 - 30.0 / 60); e->setBand(0, fx::DynEq::Ratio, std::log(4.0) / std::log(20.0)); e->setBand(0, fx::DynEq::Range, nGain(-12)); return e; };
    { auto e = make(); const double g = sineGainDb(*e, fs, 1000, 0.5, 1.0); CHECK(std::fabs(g + 12) < 0.6, "loud: %.2f dB (want -12, the range)", g); CHECK(std::fabs(e->dynamicDb(0) + 12) < 0.3, "dyn %.2f", e->dynamicDb(0)); }
    { auto e = make(); const double g = sineGainDb(*e, fs, 1000, 0.01, 1.0); CHECK(std::fabs(g) < 0.1, "quiet: %.2f dB (want 0)", g); }
    { auto e = make(); e->setBand(0, fx::DynEq::Range, nGain(-30)); const double g = sineGainDb(*e, fs, 1000, 0.1, 1.0);   // -20 dB peak: 10 dB over, 7.5 dB down
      CHECK(std::fabs(g + 7.5) < 0.8, "ratio: %.2f dB (want -7.5)", g); }
    { auto e = make(); e->setBand(0, fx::DynEq::Range, nGain(+6)); e->setBand(0, fx::DynEq::Gain, nGain(-6)); const double g = sineGainDb(*e, fs, 1000, 0.5, 1.0); CHECK(std::fabs(g) < 0.6, "upward: %.2f dB (want -6 + 6)", g); }
    { auto e = make(); e->setBand(0, fx::DynEq::Stereo, 2.0 / 4); const int n = 24000; std::vector<float> L(n), R(n);   // SIDE only: a centred (mono) signal passes untouched
      for (int i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(0.5 * std::sin(2 * 3.14159265 * 1000 * i / fs)); const std::vector<float> L0 = L; e->setBand(0, fx::DynEq::Gain, nGain(-12));
      e->process(L.data(), R.data(), n); double d = 0; for (int i = 0; i < n; ++i) d = std::max(d, static_cast<double>(std::fabs(L[i] - L0[i]))); CHECK(d < 1e-5, "side band touched the mid: %.2e", d); }
    std::printf("dyneq_test: dynamic band: range cap, under threshold flat, ratio, upward, SIDE leaves the mid OK\n"); }
  { // no NaN, no runaway, no denormals: random settings, noise then a long silence
    std::mt19937 rng(3); std::uniform_real_distribution<double> U(0, 1), N(-1, 1);
    for (int run = 0; run < 20; ++run) { fx::DynEq e(run % 2 ? 44100 : 96000); for (int b = 0; b < fx::DynEq::kBands; ++b) for (int k = 0; k < fx::DynEq::kKeys; ++k) e.setBand(b, k, U(rng));
      std::vector<float> L(8192), R(8192); bool ok = true, sub = false;
      for (int blk = 0; blk < 60; ++blk) { for (int i = 0; i < 8192; ++i) { L[i] = blk < 6 ? static_cast<float>(N(rng)) : blk == 6 && i == 0 ? 1e-38f : 0.f; R[i] = blk < 6 ? static_cast<float>(N(rng)) : 0.f; }
        e.process(L.data(), R.data(), 8192); for (int i = 0; i < 8192; ++i) { ok = ok && std::isfinite(L[i]) && std::isfinite(R[i]) && std::fabs(L[i]) < 1e4f; if (blk > 30) sub = sub || std::fpclassify(L[i]) == FP_SUBNORMAL || std::fpclassify(R[i]) == FP_SUBNORMAL; } }
      CHECK(ok && !sub, "run %d: finite %d subnormal %d", run, ok, sub);
      float sp[64]; e.spectrum(true, sp, 64, 20, 20000); for (float v : sp) CHECK(std::isfinite(v)); }
    std::printf("dyneq_test: 20 random 24-band settings: finite, no denormals in the silence after OK\n"); }
  { // state ids: "b<N>_<key>" round trip and texts
    fx::DynEq e(fs); CHECK(e.params().size() == 24 * 13 + 1); e.set("b24_freq", 0.75); CHECK(std::fabs(e.band(23, fx::DynEq::Freq) - 0.75) < 1e-6); e.set("b25_freq", 0.1); e.set("bx_freq", 0.1); e.set("b3_nope", 0.1);
    CHECK(e.text("b1_type", 3.0 / 6) == "LOW CUT" && e.text("b1_slope", 1) == "96 dB/oct" && e.text("b2_stereo", 0.25) == "MID"); }
  { // FabFilter quick-load: vendor detection on a fake list (no plug-in is loaded)
    std::vector<vst3::ClassInfo> list = {{"0", "Pro-Q", "Other Vendor", "", "", ""}, {"1", "FabFilter Pro-C 2", "FabFilter", "", "", ""}, {"2", "Pro-Q 3", "FabFilter", "", "", ""},
      {"3", "FabFilter Pro-Q 4", "FabFilter Software Instruments", "", "", ""}, {"4", "Pro-Quality", "fabfilter", "", "", ""}, {"5", "AmF DYNAMIC EQ", "AMFB Audio", "", "", ""}};
    CHECK(vst3::isFabFilter("FabFilter") && vst3::isFabFilter("fabfilter software") && !vst3::isFabFilter("Fab Filter") && !vst3::isFabFilter(""));
    CHECK(vst3::proQVersion("Other Vendor", "Pro-Q") == 0 && vst3::proQVersion("FabFilter", "Pro-C 2") == 0 && vst3::proQVersion("FabFilter", "Pro-Q 3") == 3 && vst3::proQVersion("FabFilter", "Pro-Q") == 1 && vst3::proQVersion("fabfilter", "Pro-Quality") == 0);
    CHECK(vst3::bestProQ(list) == 3); list.erase(list.begin() + 3); CHECK(vst3::bestProQ(list) == 2); list.erase(list.begin() + 2); CHECK(vst3::bestProQ(list) == -1); CHECK(vst3::bestProQ({}) == -1);
    std::printf("dyneq_test: FabFilter / Pro-Q vendor detection on a fake list OK\n"); }
  if (fails) { std::printf("dyneq_test: %d failed\n", fails); return 1; }
  return 0;
}
