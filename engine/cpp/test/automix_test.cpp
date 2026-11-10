// AUTOMIX gain sharing and AUTO LEVEL on the Mixer: a loud and a quiet member share one unity gain; the quiet one's share falls;
// auto level raises a -30 dBFS-ish tone toward the target without exceeding max gain, and the limiter still holds the ceiling.
#include <cmath>
#include <cstdio>
#include <vector>

#include "audiomixer/engine.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;

int main() {
  const double fs = 48000; const std::size_t n = 256;
  Mixer m(4, fs); m.setLimiter(false);
  std::vector<float> loud(n), quiet(n), L(n), R(n);
  for (std::size_t i = 0; i < n; ++i) { loud[i] = 0.5f * std::sin(2 * 3.14159265 * 440 * i / fs); quiet[i] = 0.05f * std::sin(2 * 3.14159265 * 660 * i / fs); }
  const float* in[4] = {loud.data(), quiet.data(), nullptr, nullptr}; std::vector<float> z(n, 0.f); in[2] = z.data(); in[3] = z.data();
  m.setAutomix(0, true, 1.0); m.setAutomix(1, true, 1.0);
  for (int k = 0; k < 200; ++k) m.process(in, n, L.data(), R.data());
  const double g0 = m.automixGainDb(0), g1 = m.automixGainDb(1);
  std::printf("automix: loud %.1f dB, quiet %.1f dB\n", g0, g1);
  CHECK(g0 > -1.0 && g1 < -12.0 && g1 > -40.5);
  m.setAutomix(1, true, 4.0); for (int k = 0; k < 200; ++k) m.process(in, n, L.data(), R.data()); CHECK(m.automixGainDb(1) > g1 + 6);   // weight lifts the quiet one
  m.setAutomix(0, false, 1); m.setAutomix(1, false, 1); CHECK(std::fabs(m.automixGainDb(0)) < 1e-9);
  // auto level: a quiet tone on strip 2 only, target -16 LUFS, max +12 dB
  in[0] = z.data(); in[1] = z.data(); std::vector<float> soft(n); for (std::size_t i = 0; i < n; ++i) soft[i] = 0.02f * std::sin(2 * 3.14159265 * 1000 * i / fs); in[2] = soft.data();
  m.setLimiter(true, -1.0, 100); m.setAutoLevel(true, -16, 12);
  for (int k = 0; k < 48000 * 8 / static_cast<int>(n); ++k) m.process(in, n, L.data(), R.data());   // 8 s: the gain climbs at 1 dB/s
  const double a = m.autoLevelGainDb(); std::printf("auto level after 8 s: %+.1f dB, short-term %.1f LUFS\n", a, m.loudness().shortTerm());
  CHECK(a > 5.0 && a <= 12.0 + 1e-9);
  float pk = 0; for (float v : L) pk = std::max(pk, std::fabs(v)); CHECK(pk <= 0.8913f + 1e-3f);   // the limiter's -1 dBTP ceiling holds
  m.setAutoLevel(false, -16, 12); m.process(in, n, L.data(), R.data()); CHECK(std::fabs(m.autoLevelGainDb()) < 1e-9);
  std::printf("automix_test: gain sharing, weights, auto level toward -16 LUFS under the limiter OK\n");
  return 0;
}
