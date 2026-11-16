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
  // GAIN MODE's output trim (the Windows volume as a trim after the master): it is left out of process(), so the loudness meter and
  // AUTO LEVEL see the mix as before and AUTO LEVEL does not climb to undo it; applyOutputTrim() lowers what goes to the device
  { std::vector<float> mid(n); for (std::size_t i = 0; i < n; ++i) mid[i] = 0.1f * std::sin(2 * 3.14159265 * 1000 * i / fs); in[2] = mid.data();
    Mixer a1(4, fs), a2(4, fs); a1.setLimiter(true, -1.0, 100); a2.setLimiter(true, -1.0, 100); a1.setAutoLevel(true, -16, 12); a2.setAutoLevel(true, -16, 12);
    a2.setOutputTrimDb(-12.0); std::vector<float> L2(n), R2(n);
    for (int k = 0; k < 48000 * 8 / static_cast<int>(n); ++k) { a1.process(in, n, L.data(), R.data()); a2.process(in, n, L2.data(), R2.data()); float* ch[2] = {L2.data(), R2.data()}; a2.applyOutputTrim(ch, 2, n); }
    std::printf("output trim -12 dB: auto level %+.2f dB (no trim %+.2f), short-term %.2f vs %.2f LUFS\n", a2.autoLevelGainDb(), a1.autoLevelGainDb(), a2.loudness().shortTerm(), a1.loudness().shortTerm());
    CHECK(std::fabs(a2.autoLevelGainDb() - a1.autoLevelGainDb()) < 1e-9 && std::fabs(a2.loudness().shortTerm() - a1.loudness().shortTerm()) < 1e-9);
    CHECK(std::fabs(L2[n / 2] - L[n / 2] * 0.25119f) < 1e-5f);   // the device gets the mix 12 dB down
    CHECK(std::fabs(a2.outputTrimDb() + 12.0) < 1e-9); a2.setOutputTrimDb(-120); CHECK(a2.outputTrimDb() <= -119.0); }
  std::printf("automix_test: gain sharing, weights, auto level toward -16 LUFS under the limiter, output trim after the meters OK\n");
  return 0;
}
