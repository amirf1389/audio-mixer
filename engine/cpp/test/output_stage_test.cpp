// The master output stage: the DC blocker removes an offset, 16-bit TPDF dither quantises to the 16-bit grid, float out leaves the signal
// alone; the Runtime profiler reports block time and load on the null backend.
#include <cmath>
#include <cstdio>
#include <set>
#include <thread>
#include <vector>

#include "audiomixer/engine.hpp"
#include "audiomixer/runtime.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;

int main() {
  const double fs = 48000; const std::size_t n = 256; Mixer m(1, fs); m.setLimiter(false); m.setDcBlock(true);
  std::vector<float> in(n), L(n), R(n); const float* ins[1] = {in.data()};
  for (std::size_t i = 0; i < n; ++i) in[i] = 0.3f + 0.2f * std::sin(2 * 3.14159265 * 1000 * i / fs);   // +0.3 DC on a tone
  for (int k = 0; k < 400; ++k) m.process(ins, n, L.data(), R.data());   // 2 s: the 5 Hz blocker has settled
  double mean = 0; for (float v : L) mean += v; mean /= n; CHECK(std::fabs(mean) < 0.005);
  float pk = 0; for (float v : L) pk = std::max(pk, std::fabs(v)); CHECK(pk > 0.13f && pk < 0.16f);   // the tone itself (0.2 * -3 dB pan) is intact
  m.setOutputBits(16); m.process(ins, n, L.data(), R.data()); m.process(ins, n, L.data(), R.data());
  std::set<long> grid; for (float v : L) grid.insert(std::lround(v * 32768.0)); int offGrid = 0; for (float v : L) if (std::fabs(v * 32768.0 - std::lround(v * 32768.0)) > 1e-3) ++offGrid;
  CHECK(offGrid == 0 && grid.size() > 10);   // every sample on the 16-bit grid, and not all the same value
  m.setOutputBits(0); m.process(ins, n, L.data(), R.data()); offGrid = 0;
  m.setLimiter(true, -6.0, 50); for (int k = 0; k < 100; ++k) m.process(ins, n, L.data(), R.data()); pk = 0; for (float v : L) pk = std::max(pk, std::fabs(v)); CHECK(pk <= 0.5012f + 1e-3f);   // with the blocker in front, the limiter ceiling still holds
  m.setLimiter(false); for (float v : L) if (std::fabs(v * 32768.0 - std::lround(v * 32768.0)) > 1e-3) ++offGrid; CHECK(offGrid > 0);
  Runtime rt(2); DeviceConfig c; c.backend = "null"; c.frames = 128; std::string err; CHECK(rt.open(c, err) && rt.start(err));
  std::this_thread::sleep_for(std::chrono::milliseconds(400)); const Snapshot s = rt.snapshot(); rt.stop();
  std::printf("profiler: block %.1f us (max %.1f), load %.2f %%, jitter %.1f us over %llu blocks\n", s.blockUs, s.maxBlockUs, s.loadPct, s.jitterUs, static_cast<unsigned long long>(s.blocks));
  CHECK(s.blocks > 50 && s.blockUs >= 0 && s.maxBlockUs >= s.blockUs && s.loadPct >= 0 && s.loadPct < 100);
  std::printf("output_stage_test: DC blocker, 16-bit TPDF dither, float out, profiler OK\n");
  return 0;
}
