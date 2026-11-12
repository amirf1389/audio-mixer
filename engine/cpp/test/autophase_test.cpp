// AUTO PHASE: two microphones on one source, the second wired the other way round. With AUTO PHASE on the second strip the
// engine turns it round within a few seconds, the mix stops cancelling, and it stays turned round. A strip already in phase is left alone.
#include "audiomixer/runtime.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

static int failures = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }

int main() {
  am::Runtime rt(4); am::DeviceConfig cfg; cfg.backend = "null"; cfg.sampleRate = 48000; cfg.frames = 256; cfg.inChannels = 3; cfg.outChannels = 2;
  cfg.nullInput = [](float* const* x, std::size_t n, std::uint64_t f0) {
    for (std::size_t i = 0; i < n; ++i) {
      const double t = static_cast<double>(f0 + i) / 48000.0;
      const float v = static_cast<float>(0.3 * std::sin(2 * 3.14159265358979 * 220.0 * t) + 0.1 * std::sin(2 * 3.14159265358979 * 1330.0 * t));
      x[0][i] = v; x[1][i] = -0.8f * v; x[2][i] = 0.6f * v;     // input 2 is the reversed microphone, input 3 is in phase
    }
  };
  std::string err;
  if (!rt.open(cfg, err)) { std::printf("FAIL open: %s\n", err.c_str()); return 1; }
  rt.setInputRoute(0, 0); rt.setInputRoute(1, 1); rt.setInputRoute(2, 2);
  expect(rt.start(err), "start");
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  const am::Snapshot before = rt.snapshot();
  rt.setAutoPolarity(1, true); rt.setAutoPolarity(2, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(3500));
  const am::Snapshot after = rt.snapshot();
  std::this_thread::sleep_for(std::chrono::milliseconds(4000));
  const am::Snapshot later = rt.snapshot();
  rt.setAutoPolarity(1, false);
  rt.stop();
  std::printf("master before %.3f, after %.3f, later %.3f; strip 2 inverted %d (corr %.2f), strip 3 inverted %d (corr %.2f)\n",
              before.master[0].pk, after.master[0].pk, later.master[0].pk, after.strips[1].polInv, after.strips[1].polCorr, after.strips[2].polInv, after.strips[2].polCorr);
  expect(after.strips[1].polInv, "the reversed microphone is turned round");
  expect(!after.strips[2].polInv, "the microphone in phase is left alone");
  expect(after.master[0].pk > before.master[0].pk * 2.5, "the mix stops cancelling");
  expect(later.strips[1].polInv && !later.strips[2].polInv, "it stays that way (no flapping)");
  expect(after.strips[1].polCorr > 0.5 && after.strips[2].polCorr > 0.5, "both strips now agree with the mix");
  std::printf(failures ? "autophase_test: %d FAILED\n" : "autophase_test: all passed\n", failures);
  return failures ? 1 : 0;
}
