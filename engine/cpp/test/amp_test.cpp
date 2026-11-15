// THE AMPLIFIER STAGE: off is bit-exact; gain, polarity, mute and delay do what they say; the limiter holds the ceiling; a crossover
// leg passes its band and cuts the other
#include <cmath>
#include <cstdio>
#include <vector>

#include "audiomixer/amp.hpp"

using am::AmpChannel; using am::AmpParams;
static std::vector<float> sine(double f, double fs, int n, double a) { std::vector<float> v(static_cast<std::size_t>(n)); for (int i = 0; i < n; ++i) v[static_cast<std::size_t>(i)] = static_cast<float>(a * std::sin(2 * 3.141592653589793 * f * i / fs)); return v; }
static double peakTail(const std::vector<float>& v) { double p = 0; for (std::size_t i = v.size() / 2; i < v.size(); ++i) p = std::max(p, static_cast<double>(std::fabs(v[i]))); return p; }
#define CHECK(c, ...) do { if (!(c)) { std::fprintf(stderr, "amp_test: " __VA_ARGS__); std::fprintf(stderr, "\n"); return 1; } } while (0)
int main() {
  const double fs = 48000;
  { AmpChannel a; a.prepare(fs); auto x = sine(1000, fs, 4800, 0.5); const auto y0 = x; a.process(x.data(), 4800); CHECK(x == y0, "off is not bit-exact"); }
  { AmpChannel a; a.prepare(fs); AmpParams p; p.on = true; p.gainDb = -6.0206; p.invert = true; a.set(p); std::vector<float> x(64, 0.5f); a.process(x.data(), 64); CHECK(std::fabs(x[10] + 0.25f) < 1e-4, "gain/polarity: %f", x[10]); }
  { AmpChannel a; a.prepare(fs); AmpParams p; p.on = true; p.mute = true; a.set(p); std::vector<float> x(64, 0.5f); a.process(x.data(), 64); CHECK(x[5] == 0.f, "mute"); }
  { AmpChannel a; a.prepare(fs); AmpParams p; p.on = true; p.delayMs = 10; a.set(p); std::vector<float> x(1000, 0.f); x[0] = 1.f; a.process(x.data(), 1000); CHECK(x[480] == 1.f && x[0] == 0.f, "10 ms delay is not 480 samples"); }
  { AmpChannel a; a.prepare(fs); AmpParams p; p.on = true; p.gainDb = 12; p.limitOn = true; p.limitDb = -6; a.set(p); auto x = sine(1000, fs, 9600, 0.9); a.process(x.data(), 9600);
    CHECK(peakTail(x) <= std::pow(10.0, -6.0 / 20) + 1e-6, "limiter let %f through", peakTail(x)); CHECK(a.gainReductionDb() > 10, "no gain reduction shown (%f)", a.gainReductionDb()); }
  { AmpChannel a; a.prepare(fs); AmpParams p; p.on = true; p.lpOrder = 4; p.lpHz = 100; a.set(p);   // a sub output: 40 Hz passes, 2 kHz is cut
    auto lo = sine(40, fs, 48000, 0.5); a.process(lo.data(), 48000); AmpChannel b; b.prepare(fs); b.set(p); auto hi = sine(2000, fs, 48000, 0.5); b.process(hi.data(), 48000);
    CHECK(peakTail(lo) > 0.4 && peakTail(hi) < 0.005, "low-pass leg: 40 Hz %f, 2 kHz %f", peakTail(lo), peakTail(hi)); }
  std::printf("amplifier stage: bypass, gain, polarity, mute, delay, limiter and crossover OK\n");
  return 0;
}
