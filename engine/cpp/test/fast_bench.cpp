// fast_bench: DSP MODE DIGITAL against FAST on the same console. 64 strips with a 4-band EQ, compressor, gate and one insert each
// (48 playing, 16 plugged in but silent), the master limiter at -1.0 dB, 48 kHz, 256 frames. The runtime's own callback is clocked
// directly (no sound card). Run once per mode (the peak memory of a process only grows):
//   fast_bench digital|fast [seconds of audio, default 20] [paced|flat] [helpers]
// paced: each block starts on its real-time deadline (as a sound card clocks it), so idle helpers sleep as they would; otherwise back to back.
// Prints wall time and CPU time (all threads) per block, the share of the real-time budget and the peak resident memory.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
#include "audiomixer/runtime.hpp"

namespace {
struct OnePole : am::Runtime::Insert { float z = 0; void process(float* x, std::size_t n) override { for (std::size_t i = 0; i < n; ++i) { z += 0.2f * (x[i] - z); x[i] = 0.7f * x[i] + 0.3f * z; } } };
double cpuSeconds() {
#if defined(_WIN32)
  FILETIME a, b, k, u; GetProcessTimes(GetCurrentProcess(), &a, &b, &k, &u);
  auto t = [](FILETIME f) { return (static_cast<double>(f.dwHighDateTime) * 4294967296.0 + f.dwLowDateTime) * 1e-7; }; return t(k) + t(u);
#else
  rusage r{}; getrusage(RUSAGE_SELF, &r); return r.ru_utime.tv_sec + r.ru_stime.tv_sec + (r.ru_utime.tv_usec + r.ru_stime.tv_usec) * 1e-6;
#endif
}
double peakRssMb() {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS m{}; GetProcessMemoryInfo(GetCurrentProcess(), &m, sizeof m); return m.PeakWorkingSetSize / 1048576.0;
#else
  rusage r{}; getrusage(RUSAGE_SELF, &r); return r.ru_maxrss / 1024.0;   // Linux: kilobytes
#endif
}
}  // namespace

int main(int argc, char** argv) {
  const bool fast = argc > 1 && std::strcmp(argv[1], "fast") == 0; const double seconds = argc > 2 ? std::max(1.0, std::atof(argv[2])) : 20.0;
  const int S = 64, playing = 48; const std::size_t F = 256; const double rate = 48000, budgetUs = F * 1e6 / rate; const int blocks = static_cast<int>(seconds * rate / F); const bool paced = argc > 3 && std::strcmp(argv[3], "paced") == 0;
  am::Runtime rt(S); am::DeviceConfig cfg; cfg.backend = "null"; cfg.sampleRate = rate; cfg.frames = F; cfg.inChannels = S; cfg.outChannels = 2; std::string err;
  if (!rt.open(cfg, err)) { std::fprintf(stderr, "fast_bench: %s\n", err.c_str()); return 1; }
  for (int s = 0; s < S; ++s) {
    rt.setEqBand(s, 0, am::dsp::FilterType::LowShelf, 100, 0.7, 2); rt.setEqBand(s, 1, am::dsp::FilterType::Peaking, 400, 1, -3);
    rt.setEqBand(s, 2, am::dsp::FilterType::Peaking, 2500, 1, 2); rt.setEqBand(s, 3, am::dsp::FilterType::HighShelf, 8000, 0.7, 1);
    rt.setCompressor(s, true, -18, 3, 6, 10, 120, 2); rt.setGate(s, true, -50, -60, 1, 50, 120); rt.setGainDb(s, -12);
    if (s < playing) rt.setInserts(s, std::make_shared<const am::Runtime::InsertChain>(am::Runtime::InsertChain{std::make_shared<OnePole>()}));   // the playing strips carry an insert
  }
  rt.setLimiter(true, -1.0, 100); if (argc > 4) rt.setFastHelpers(std::atoi(argv[4]));   // [helpers]: -1 = cores - 1 (the default)
  rt.setFast(fast);
  std::vector<std::vector<float>> in(S, std::vector<float>(F)), out(2, std::vector<float>(F)); std::vector<const float*> ip(S); for (int s = 0; s < S; ++s) ip[s] = in[s].data(); float* op[2] = {out[0].data(), out[1].data()};
  std::mt19937 rng(7); std::normal_distribution<float> noise(0.f, 0.05f);
  // the inputs are made before the clock starts: the bench times the engine, not the noise generator
  std::vector<std::vector<float>> src(S, std::vector<float>(F * 64)); for (int s = 0; s < S; ++s) for (std::size_t i = 0; i < src[s].size(); ++i) src[s][i] = s < playing ? noise(rng) + 0.2f * std::sin(0.01f * static_cast<float>(i) * (s + 1)) : 1e-7f * noise(rng);   // the silent ones: a converter's floor, about -160 dBFS
  const double c0 = cpuSeconds(); const auto t0 = std::chrono::steady_clock::now(); double worst = 0; std::vector<double> took(static_cast<std::size_t>(blocks));
  for (int b = 0; b < blocks; ++b) {
    for (int s = 0; s < S; ++s) std::memcpy(in[s].data(), src[s].data() + (b % 64) * F, F * sizeof(float));
    if (paced) std::this_thread::sleep_until(t0 + std::chrono::microseconds(static_cast<long long>(b * budgetUs)));
    const auto a = std::chrono::steady_clock::now(); rt.onBlock(ip.data(), op, F); took[static_cast<std::size_t>(b)] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - a).count(); worst = std::max(worst, took[static_cast<std::size_t>(b)]);
  }
  double sum = 0; for (double t : took) sum += t; const double wallUs = sum / blocks; std::sort(took.begin(), took.end()); const double p99 = took[static_cast<std::size_t>(0.99 * (blocks - 1))]; const double cpuUs = (cpuSeconds() - c0) * 1e6 / blocks;
  std::printf("fast_bench %-7s %s 64 strips (48 playing, 16 at the noise floor), EQ+comp+gate (+ insert on the playing), limiter -1 dB, 48 kHz / %zu frames, %d blocks, helpers %d\n", fast ? "FAST" : "DIGITAL", paced ? "paced" : "flat-out", F, blocks, rt.fastHelpers());
  std::printf("  callback %.1f us/block (%.1f%% of real time), p99 %.1f us, worst %.1f us, CPU (all threads) %.1f us/block, peak RSS %.1f MB\n", wallUs, 100 * wallUs / budgetUs, p99, worst, cpuUs, peakRssMb());
  rt.setFast(false);
  return 0;
}
