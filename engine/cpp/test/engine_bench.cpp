// engine_bench: what one audio block costs the engine, on this processor, in the cases a console meets (CPU budget for older machines).
//   engine_bench [blocks]   48 kHz, 256 frames, 32 channels with a 4-band EQ, compressor and gate on each; prints microseconds per block
//   and the share of the real-time budget (5.33 ms per block). The runtime's own callback is clocked directly (no sound card).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include "audiomixer/runtime.hpp"

namespace {
using Clock = std::chrono::steady_clock;
struct Case { const char* name; int playing; int muted; };
double run(am::Runtime& rt, const Case& c, int blocks, std::vector<std::vector<float>>& in, std::vector<std::vector<float>>& out, std::size_t frames) {
  std::mt19937 rng(7); std::normal_distribution<float> noise(0.f, 0.05f);
  for (int s = 0; s < 32; ++s) { std::fill(in[s].begin(), in[s].end(), 0.f); rt.setMute(s, s >= 32 - c.muted); }
  std::vector<const float*> ip(32); std::vector<float*> op(2); for (int s = 0; s < 32; ++s) ip[s] = in[s].data(); op[0] = out[0].data(); op[1] = out[1].data();
  double best = 1e30;
  for (int rep = 0; rep < 3; ++rep) {   // best of three: the cost of the engine, not of the machine's other work
    const auto t0 = Clock::now();
    for (int b = 0; b < blocks; ++b) {
      for (int s = 0; s < c.playing; ++s) for (std::size_t i = 0; i < frames; ++i) in[s][i] = noise(rng) + 0.2f * std::sin(0.01f * static_cast<float>(i + s));
      rt.onBlock(ip.data(), op.data(), frames);
    }
    best = std::min(best, std::chrono::duration<double, std::micro>(Clock::now() - t0).count() / blocks);
  }
  return best;
}
}  // namespace

int main(int argc, char** argv) {
  const int blocks = argc > 1 ? std::max(50, std::atoi(argv[1])) : 2000; const std::size_t frames = 256; const double budgetUs = frames * 1e6 / 48000.0;
  am::Runtime rt(32); am::DeviceConfig cfg; cfg.backend = "null"; cfg.sampleRate = 48000; cfg.frames = frames; cfg.inChannels = 32; cfg.outChannels = 2; std::string err;
  if (!rt.open(cfg, err)) { std::fprintf(stderr, "engine_bench: %s\n", err.c_str()); return 1; }   // opened, not started: this program is the clock
  for (int s = 0; s < 32; ++s) {
    rt.setEqBand(s, 0, am::dsp::FilterType::LowShelf, 100, 0.7, 2); rt.setEqBand(s, 1, am::dsp::FilterType::Peaking, 400, 1, -3);
    rt.setEqBand(s, 2, am::dsp::FilterType::Peaking, 2500, 1, 2); rt.setEqBand(s, 3, am::dsp::FilterType::HighShelf, 8000, 0.7, 1);
    rt.setCompressor(s, true, -18, 3, 6, 10, 120, 2); rt.setGate(s, true, -50, -60, 1, 50, 120); rt.setGainDb(s, -6);
  }
  std::vector<std::vector<float>> in(32, std::vector<float>(frames)), out(2, std::vector<float>(frames));
  { std::vector<const float*> ip(32); std::vector<float*> op{out[0].data(), out[1].data()}; for (int s = 0; s < 32; ++s) ip[s] = in[s].data(); rt.onBlock(ip.data(), op.data(), frames); }   // the settings above land
  const Case cases[] = {{"32 channels playing", 32, 0}, {"4 playing, 28 silent", 4, 0}, {"4 playing, 28 muted", 4, 28}};
  std::printf("engine_bench: 48 kHz, %zu frames, 32 channels with EQ + compressor + gate (budget %.0f us per block)\n", frames, budgetUs);
  for (const Case& c : cases) { const double us = run(rt, c, blocks, in, out, frames); std::printf("  %-22s %8.1f us per block  %5.2f%% of real time\n", c.name, us, 100.0 * us / budgetUs); }
  return 0;
}
