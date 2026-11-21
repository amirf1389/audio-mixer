// kernel_bench: microseconds per 256-frame block (48 kHz) of each hot loop of the engine, one at a time, on the SIMD level the
// processor runs (the same dispatch as the engine; AM_SIMD=base for the baseline). Best of 7 runs. Shows where a callback's time goes before and after an optimisation:
//   kernel_bench [blocks, default 4000]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <vector>
#include "audiomixer/dsp.hpp"
#include "audiomixer/engine.hpp"
#include "audiomixer/fpu.hpp"
#include "audiomixer/simd.hpp"

namespace {
constexpr std::size_t F = 256;
double timeIt(int blocks, const std::function<void(int)>& f) {   // the best of 7 runs (a busy machine only ever adds time)
  for (int b = 0; b < 50; ++b) f(b);   // warm up
  double best = 1e30; const int per = blocks / 7 > 1 ? blocks / 7 : 1;
  for (int r = 0; r < 7; ++r) { const auto t0 = std::chrono::steady_clock::now(); for (int b = 0; b < per; ++b) f(r * per + b);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / per; best = us < best ? us : best; }
  return best;
}
}  // namespace

int main(int argc, char** argv) {
  am::denormalsOff();
  const int blocks = argc > 1 ? std::max(10, std::atoi(argv[1])) : 4000;
  std::mt19937 rng(7); std::normal_distribution<float> noise(0.f, 0.05f);
  std::vector<float> src(F * 64); for (std::size_t i = 0; i < src.size(); ++i) src[i] = noise(rng) + 0.2f * std::sin(0.01f * static_cast<float>(i) * 3);
  std::vector<float> buf(F);
  auto load = [&](int b) { std::copy(src.begin() + (b % 64) * F, src.begin() + (b % 64 + 1) * F, buf.begin()); };
  std::printf("kernel_bench: SIMD %s, us per %zu-frame block (one strip unless named)\n", am::simd::levelName(), F);
  auto row = [&](const char* name, double us) { std::printf("  %-34s %8.3f\n", name, us); };
  const double copy = timeIt(blocks, load);
  row("(input copy, subtracted below)", copy);
  { am::dsp::Channel ch; ch.setEqBand(0, {am::dsp::FilterType::LowShelf, 100, 0.7, 2}); ch.setEqBand(1, {am::dsp::FilterType::Peaking, 400, 1, -3}); ch.setEqBand(2, {am::dsp::FilterType::Peaking, 2500, 1, 2}); ch.setEqBand(3, {am::dsp::FilterType::HighShelf, 8000, 0.7, 1});
    row("channel EQ 4 bands", timeIt(blocks, [&](int b) { load(b); ch.process(buf.data(), F); }) - copy); }
  { am::dsp::Channel ch; ch.setCompressor(true, -18, 3, 6, 10, 120, 2); row("channel compressor", timeIt(blocks, [&](int b) { load(b); ch.process(buf.data(), F); }) - copy); }
  { am::dsp::Channel ch; ch.setGate(true, -50, -60, 1, 50, 120); row("channel gate", timeIt(blocks, [&](int b) { load(b); ch.process(buf.data(), F); }) - copy); }
  { am::dsp::Channel ch; ch.setCrossover(am::dsp::XoverType::LinkwitzRiley, 4, 80, 4, 12000); row("channel crossover LR4 hp+lp", timeIt(blocks, [&](int b) { load(b); ch.process(buf.data(), F); }) - copy); }
  { am::dsp::Channel ch; ch.setEqBand(0, {am::dsp::FilterType::LowShelf, 100, 0.7, 2}); ch.setEqBand(1, {am::dsp::FilterType::Peaking, 400, 1, -3}); ch.setEqBand(2, {am::dsp::FilterType::Peaking, 2500, 1, 2}); ch.setEqBand(3, {am::dsp::FilterType::HighShelf, 8000, 0.7, 1});
    ch.setCompressor(true, -18, 3, 6, 10, 120, 2); ch.setGate(true, -50, -60, 1, 50, 120);
    row("channel EQ+gate+comp (bench strip)", timeIt(blocks, [&](int b) { load(b); ch.process(buf.data(), F); }) - copy); }
  { am::Meter m; row("meter peak/RMS/clip", timeIt(blocks, [&](int b) { load(b); m.add(buf.data(), F); if (b % 8 == 0) m.end(b); }) - copy); }
  { am::TruePeak4x l, r; std::vector<double> x(F), pk(F), dl(F); row("true peak 4x (L+R)", timeIt(blocks, [&](int b) { load(b); for (std::size_t i = 0; i < F; ++i) x[i] = buf[i]; l.pushBlock(x.data(), F, pk.data(), dl.data()); r.pushBlock(x.data(), F, pk.data(), dl.data()); }) - copy); }
  { const int S = 64; am::Mixer mx(S, 48000); for (int s = 0; s < S; ++s) { mx.setGainDb(s, -12); mx.setPan(s, (s % 9 - 4) / 4.0); }
    mx.setLimiter(true, -1, 100); std::vector<std::vector<float>> in(S, std::vector<float>(F)); std::vector<const float*> ip(S); for (int s = 0; s < S; ++s) { for (std::size_t i = 0; i < F; ++i) in[s][i] = src[(i * 7 + s * 13) % src.size()]; ip[s] = in[s].data(); }
    std::vector<float> L(F), R(F); row("mixer 64 strips sum/gain/pan+meters", timeIt(blocks, [&](int) { mx.process(ip.data(), F, L.data(), R.data()); }));
    mx.setTruePeakLimiter(true); row("mixer 64 strips, true-peak limiter", timeIt(blocks, [&](int) { mx.process(ip.data(), F, L.data(), R.data()); })); }
  return 0;
}
