// Runtime test: the mixer on the null device (timed, no hardware) and, where ALSA is built in and offers a "null" device, on ALSA. Control from another thread, consistent snapshots,
// the meters of the audio thread. Exit 0 = all good.
#include "audiomixer/runtime.hpp"

#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <vector>
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
  {   // the monitor section: -20 dB dim halves nothing on the master meters but the monitor peak drops by 10x; mute silences the monitor only
    am::Runtime rt(2); am::DeviceConfig c; c.backend = "null"; c.frames = 64; std::string err; expect(rt.open(c, err), "open null for the monitor test");
    const std::size_t n = 64; std::vector<float> tone(n); for (std::size_t i = 0; i < n; ++i) tone[i] = 0.5f * ((i / 8) % 2 ? 1.f : -1.f);
    std::vector<float> o0(n), o1(n); const float* ins[2] = {tone.data(), tone.data()}; float* outs[2] = {o0.data(), o1.data()};
    rt.setGainDb(0, 0); rt.setGainDb(1, 0); rt.setPan(0, -1); rt.setPan(1, 1);
    for (int k = 0; k < 20; ++k) rt.onBlock(ins, outs, n);
    const am::Snapshot plain = rt.snapshot(); const float lPlain = *std::max_element(o0.begin(), o0.end());
    rt.setMonitor(0, 0, true, false, false); for (int k = 0; k < 20; ++k) rt.onBlock(ins, outs, n);
    const am::Snapshot dim = rt.snapshot(); const float lDim = *std::max_element(o0.begin(), o0.end());
    expect(std::fabs(dim.master[0].pk - plain.master[0].pk) < 0.02, "dim leaves the master meter alone");
    expect(lDim > 0 && std::fabs(lDim / lPlain - 0.1f) < 0.01f, "dim is -20 dB on the outputs");
    expect(std::fabs(dim.monitorPk[0] / plain.monitorPk[0] - 0.1) < 0.02, "the monitor meter follows the dim");
    rt.setMonitor(0, 0, false, true, false); for (int k = 0; k < 20; ++k) rt.onBlock(ins, outs, n);
    expect(std::fabs(*std::max_element(o0.begin(), o0.end()) - *std::max_element(o1.begin(), o1.end())) < 1e-5f, "mono: both outputs carry the same sum");
    rt.setMonitor(0, 0, false, false, true); for (int k = 0; k < 20; ++k) rt.onBlock(ins, outs, n);
    expect(*std::max_element(o0.begin(), o0.end()) == 0.f && rt.snapshot().master[0].pk > 0.1, "mute silences the outputs, the master meter still shows signal");
    rt.setMonitor(1, 0, false, false, false); rt.setSend(0, 0, 0, false); rt.setBusLevel(0, 0); for (int k = 0; k < 20; ++k) rt.onBlock(ins, outs, n);
    expect(*std::max_element(o0.begin(), o0.end()) > 0.1f, "monitor source = bus 1 hears the send");
    // input trim: -6 dB halves the strip's post-fader peak, +6 dB doubles it (fader at 0 dB)
    rt.setMonitor(0, 0, false, false, false); rt.setInputRoute(0, 0); rt.setInputRoute(1, 1); rt.setSend(0, 0, -120, false);
    rt.setTrimDb(0, -6.0206); for (int k = 0; k < 800; ++k) rt.onBlock(ins, outs, n); expect(std::fabs(rt.snapshot().strips[0].pk - 0.25) < 0.02, "trim -6 dB halves the strip peak");
    rt.setTrimDb(0, 0); for (int k = 0; k < 800; ++k) rt.onBlock(ins, outs, n); expect(std::fabs(rt.snapshot().strips[0].pk - 0.5) < 0.02, "trim back to 0 dB");
    // the routing matrix: strip 0 fed from input 1 (silence here), output 0 carrying bus 1 L directly, output 1 off
    rt.setMonitor(0, 0, false, false, false); rt.setSend(0, 0, -120, false);
    std::vector<float> quiet(n, 0.0f); const float* ins2[2] = {tone.data(), quiet.data()};
    rt.setInputRoute(0, 1); rt.setInputRoute(1, -1); for (int k = 0; k < 800; ++k) rt.onBlock(ins2, outs, n);   // ~1 s: the peak meters fall
    expect(rt.snapshot().strips[0].pk < 1e-3 && rt.snapshot().strips[1].pk < 1e-3, "input routing: strip 0 reads the quiet input 1, strip 1 reads nothing");
    rt.setInputRoute(0, 0); rt.setInputRoute(1, 0); for (int k = 0; k < 20; ++k) rt.onBlock(ins2, outs, n);
    expect(rt.snapshot().strips[1].pk > 0.3, "input 0 can feed two strips");
    rt.setOutputRoute(0, am::Runtime::kSrcBus0L); rt.setOutputRoute(1, am::Runtime::kSrcOff); rt.setSend(0, 0, 0, false); for (int k = 0; k < 20; ++k) rt.onBlock(ins2, outs, n);   // strip 0 is panned left: its send lands on bus L
    expect(*std::max_element(o0.begin(), o0.end()) > 0.1f, "output routing: out 0 = bus 1 L");
    expect(*std::max_element(o1.begin(), o1.end()) == 0.f, "output routing: out 1 = off");
    rt.setOutputRoute(0, am::Runtime::kSrcMasterL); rt.setOutputRoute(1, am::Runtime::kSrcMasterL); rt.setMonitor(0, 0, false, false, true); for (int k = 0; k < 20; ++k) rt.onBlock(ins2, outs, n);
    expect(*std::max_element(o1.begin(), o1.end()) > 0.3f, "master L on two outputs is not affected by the monitor mute");
  }
  { std::string unknown = "nonexistent"; am::Runtime rt(2); am::DeviceConfig c; c.backend = unknown; std::string err; expect(!rt.open(c, err) && !err.empty(), "an unknown backend is refused with a reason"); }
#ifdef AM_HAVE_ALSA
  run("alsa", "null", "null", false);                                                     // ALSA's own null device: no hardware needed (skipped where it is not configured)
#endif
  { // a burst far larger than the control ring while the audio runs: nothing is lost, and the LAST change (detach the effect) arrives
    struct Count : am::BusProcessor { std::atomic<long> n{0}; void process(float*, float*, std::size_t) override { n.fetch_add(1); } } fx;
    am::Runtime rt(8); am::DeviceConfig cfg; cfg.backend = "null"; cfg.frames = 256; std::string err;
    expect(rt.open(cfg, err) && rt.start(err), "burst: null device starts");
    rt.setBusProcessor(0, &fx); std::this_thread::sleep_for(std::chrono::milliseconds(60)); expect(fx.n.load() > 0, "burst: the effect runs");
    for (int i = 0; i < 40000; ++i) rt.setGainDb(i % 8, -(i % 30));
    rt.setMasterDb(-7.0); rt.setBusProcessor(0, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(80)); const long after = fx.n.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(80)); expect(fx.n.load() == after, "burst: the detach at the end of 40 000 changes was not dropped");
    rt.stop(); }
  std::printf(failures ? "runtime: %d failed\n" : "runtime: all good\n", failures);
  return failures ? 1 : 0;
}
