// Runtime test: the mixer on the null device (timed, no hardware) and, where ALSA is built in and offers a "null" device, on ALSA. Control from another thread, consistent snapshots,
// the meters of the audio thread. Exit 0 = all good.
#include "audiomixer/runtime.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

static int failures = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }

static void run(const char* backend, const char* in, const char* out, bool nullDevice) {
  am::Runtime rt(4); am::DeviceConfig cfg; cfg.backend = backend; cfg.inDevice = in; cfg.outDevice = out; cfg.sampleRate = 48000; cfg.frames = 256; cfg.inChannels = 2; cfg.outChannels = 2;
  cfg.nullInput = [](float* const* x, std::size_t n, std::uint64_t f0) { for (std::size_t i = 0; i < n; ++i) { const float v = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(f0 + i) / 48000.0)); x[0][i] = v; x[1][i] = v; } };
  std::string err;
  if (!rt.open(cfg, err)) { std::printf("SKIP %s: %s\n", backend, err.c_str()); return; }
  expect(rt.start(err), "start");
  rt.setPan(0, -1); rt.setPan(1, 1); rt.setGainDb(1, -6);                                   // from this thread, while the audio thread runs
  std::this_thread::sleep_for(std::chrono::milliseconds(700));
  const am::Snapshot a = rt.snapshot();
  rt.setMute(0, true); rt.setGainDb(1, -20);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const am::Snapshot b = rt.snapshot();
  rt.stop();
  std::printf("%s: %llu blocks, %llu frames, xruns %llu; strip 0 pk %.3f, strip 1 pk %.3f; master L %.3f R %.3f; momentary %.2f LUFS\n", rt.backendName().c_str(), static_cast<unsigned long long>(a.blocks),
              static_cast<unsigned long long>(a.frames), static_cast<unsigned long long>(a.xruns), a.strips[0].pk, a.strips[1].pk, a.master[0].pk, a.master[1].pk, a.momentary);
  expect(a.blocks > 20, "the audio thread ran");
  // the test thread is not real-time: a busy machine can hold it past the 4-block margin once or twice, which is the scheduler, not the engine
  expect(a.xruns <= 2 || !nullDevice, "at most 2 late blocks on the timed device");
  if (!nullDevice) { expect(a.master[0].pk == 0.0 && b.blocks > a.blocks, "ALSA null device: silence in, silence out, the loop keeps running"); return; }     // no tone reaches the mixer from ALSA's null device
  expect(a.frames > 48000 * 0.45 && a.frames < 48000 * 1.0, "the device clock: about 0.7 s of audio in 0.7 s");
  expect(std::fabs(a.strips[0].pk - 0.5) < 0.02, "strip 0 pk 0.5 (the input sine)");
  expect(std::fabs(a.strips[1].pk - 0.25) < 0.02, "strip 1 pk 0.25 (-6 dB) from the thread's control");
  expect(a.master[0].pk > 0.3 && a.master[1].pk > 0.15, "master has both strips: panned left and right");
  expect(std::fabs(a.strips[2].pk) < 1e-9 && std::fabs(a.strips[3].pk) < 1e-9, "strips without an input are silent");
  expect(b.blocks > a.blocks && b.strips[1].pk < 0.1 && b.master[0].pk < 0.3, "mute and gain from the control thread arrive");
  expect(std::isfinite(a.momentary) && a.momentary > -20 && a.momentary < 0, "loudness is measured");
  expect(a.correlation == a.correlation, "phase is a number");
}

int main() {
  const auto names = am::availableBackends();
  std::printf("backends built in:"); for (const auto& n : names) std::printf(" %s", n.c_str()); std::printf("\n");
  expect(names.back() == "null", "null is always there");
  run("null", "", "", true);
  { std::string unknown = "nonexistent"; am::Runtime rt(2); am::DeviceConfig c; c.backend = unknown; std::string err; expect(!rt.open(c, err) && !err.empty(), "an unknown backend is refused with a reason"); }
#ifdef AM_HAVE_ALSA
  run("alsa", "null", "null", false);                                                     // ALSA's own null device: no hardware needed (skipped where it is not configured)
#endif
  std::printf(failures ? "runtime: %d failed\n" : "runtime: all good\n", failures);
  return failures ? 1 : 0;
}
