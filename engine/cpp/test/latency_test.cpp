// LOW LATENCY SOUND DETECTION: the verdict and advice for the usual Windows setups, and the ROUND TRIP click found at the exact delay
// through noise, a quieter return and an inverted cable.
#include "audiomixer/latency.hpp"
#include "audiomixer/onevoice.hpp"
#include "audiomixer/runtime.hpp"
#include <vector>

#include <cmath>
#include <cstdio>
#include <random>

using namespace am::latency;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static bool has(const Report& r, const char* w) { for (const auto& a : r.advice) if (a.find(w) != std::string::npos) return true; return false; }

int main() {
  // ASIO interface at 64 frames, driver says 96 in / 112 out: LOW
  { Input i; i.backend = "asio"; i.rate = 48000; i.blockFrames = 64; i.inFrames = 96; i.outFrames = 112; Report r = assess(i);
    CHECK(r.reported); CHECK(r.level == Level::Low); CHECK(std::fabs(r.totalMs - 4.333) < 0.01); CHECK(r.levelName == "LOW LATENCY"); CHECK(has(r, "Nothing to change"));
    std::printf("latency_test: ASIO 64: %s\n", r.verdict.c_str()); }
  // SYSTEM EFFECTS: on the shared path the APOs are named (and linked); ASIO and WASAPI exclusive bypass them, so nothing is said there
  { Input i; i.backend = "wasapi"; i.blockFrames = 480; i.apoActive = true; Report r = assess(i); CHECK(r.apoAdvice); CHECK(has(r, "APOs"));
    i.exclusive = true; r = assess(i); CHECK(!r.apoAdvice); CHECK(!has(r, "APOs"));
    i.backend = "asio"; i.exclusive = false; r = assess(i); CHECK(!r.apoAdvice);
    i.backend = "wasapi"; i.apoActive = false; r = assess(i); CHECK(!r.apoAdvice); }
  // WASAPI shared at 480 frames, Windows reports 10 ms + 480 buffer each way: HIGH, advice exclusive / ASIO and a smaller buffer
  { Input i; i.backend = "wasapi"; i.blockFrames = 480; i.inFrames = 960; i.outFrames = 960; Report r = assess(i);
    CHECK(r.level == Level::High); CHECK(std::fabs(r.totalMs - 40) < 0.01); CHECK(has(r, "EXCLUSIVE")); CHECK(has(r, "smaller buffer")); }
  // WASAPI exclusive at 128: driver silent -> estimate one block each way = 5.3 ms: LOW
  { Input i; i.backend = "wasapi"; i.exclusive = true; i.blockFrames = 128; Report r = assess(i); CHECK(!r.reported); CHECK(r.level == Level::Low); CHECK(!has(r, "EXCLUSIVE")); }
  // DirectSound: always advise WASAPI / ASIO
  { Input i; i.backend = "dsound"; i.blockFrames = 256; i.inFrames = 768; i.outFrames = 768; Report r = assess(i); CHECK(r.level == Level::High); CHECK(r.advice.front().find("DirectSound") == 0); }
  // Bluetooth output: HIGH whatever the driver says, and that advice first
  { Input i; i.backend = "wasapi"; i.exclusive = true; i.blockFrames = 128; i.outFrames = 128; i.hasInput = false; i.bluetoothOut = true; Report r = assess(i);
    CHECK(r.level == Level::High); CHECK(r.advice.front().find("Bluetooth") != std::string::npos); CHECK(r.verdict.find("to the output") != std::string::npos); }
  // dropouts: never advise a smaller buffer, say to raise it
  { Input i; i.backend = "asio"; i.blockFrames = 256; i.inFrames = 600; i.outFrames = 600; i.xruns = 3; Report r = assess(i); CHECK(!has(r, "smaller buffer")); CHECK(has(r, "Raise it")); }
  // hybrid processor with fast cores off
  { Input i; i.backend = "asio"; i.blockFrames = 64; i.inFrames = 64; i.outFrames = 64; i.hybrid = true; i.fastCores = false; Report r = assess(i); CHECK(has(r, "FAST CORES")); }
  // a 1024-frame WASAPI shared buffer (21 ms): the driver type in words and the plain buffer advice
  { Input i; i.backend = "wasapi"; i.blockFrames = 1024; Report r = assess(i); CHECK(r.driver == "WASAPI shared"); CHECK(has(r, "Buffer above 10 ms")); CHECK(has(r, "Windows mixer buffer")); }
  CHECK(driverLabel("asio", false) == "ASIO" && driverLabel("wasapi", true) == "WASAPI exclusive" && driverLabel("dsound", false) == "DirectSound");
  CHECK(levelOf(9.9) == Level::Low && levelOf(10) == Level::Ok && levelOf(20) == Level::Ok && levelOf(20.1) == Level::High);

  // ROUND TRIP: the click back after 517 frames (10.77 ms at 48 kHz), at a quarter of the level, with noise; then through an inverted cable
  const double rate = 48000; const std::vector<float> click = testClick(rate); CHECK(click.size() == 1920);
  std::mt19937 g(7); std::normal_distribution<float> noise(0.f, 0.01f);
  for (int invert = 0; invert < 2; ++invert) {
    std::vector<float> rec(24000); for (auto& x : rec) x = noise(g);
    const long lag = 517; for (std::size_t i = 0; i < click.size(); ++i) rec[lag + i] += (invert ? -0.25f : 0.25f) * click[i];
    double conf = 0; const long found = findDelay(click.data(), click.size(), rec.data(), rec.size(), &conf);
    CHECK(found == lag); CHECK(conf > 0.8);
    std::printf("latency_test: round trip %s: found %ld frames (%.2f ms), confidence %.2f\n", invert ? "inverted" : "normal", found, found * 1000.0 / rate, conf);
  }
  // ROUND TRIP in ms: a whole synthetic recording, click played at 0.1 s, back 12.34 ms later through noise: right within 1 ms; silence: -1
  for (double r2 : {44100.0, 48000.0, 96000.0}) {
    const std::vector<float> ck = testClick(r2); std::vector<float> rec(static_cast<std::size_t>(0.8 * r2)); for (auto& x : rec) x = noise(g);
    const std::size_t at = static_cast<std::size_t>(0.1 * r2), d = static_cast<std::size_t>(0.01234 * r2); for (std::size_t i = 0; i < ck.size(); ++i) rec[at + d + i] += 0.3f * ck[i];
    double conf = 0; const double got = roundTripMs(rec.data(), rec.size(), at, r2, &conf); CHECK(std::fabs(got - 12.34) < 1.0); CHECK(conf > 0.8);
    std::vector<float> quiet(rec.size()); for (auto& x : quiet) x = noise(g); CHECK(roundTripMs(quiet.data(), quiet.size(), at, r2, &conf) < 0);
    std::printf("latency_test: round trip %.0f Hz: %.2f ms (sent 12.34 ms)\n", r2, got); }
  // nothing came back (no loop cable): low confidence, not trusted
  { std::vector<float> rec(24000); for (auto& x : rec) x = noise(g); double conf = 1; findDelay(click.data(), click.size(), rec.data(), rec.size(), &conf); CHECK(conf < 0.3); }
  { double c = 1; CHECK(findDelay(click.data(), click.size(), click.data(), 10, &c) == -1 && c == 0); }
  // ONE VOICE: the same voice on inputs 1 and 2 is found after 1.5 s; a real stereo pair, silence or a quiet hum on one side never is
  { using am::Runtime; using am::DeviceConfig; using am::OneVoice;
    auto run = [&](auto genL, auto genR, double seconds) {
      Runtime rt(2); DeviceConfig c; c.backend = "null"; c.frames = 256; c.inChannels = 2; std::string err; if (!rt.open(c, err)) return std::pair<am::Snapshot, int>{};
      const std::size_t n = 256; std::vector<float> a(n), b(n), o0(n), o1(n); const float* ins[2] = {a.data(), b.data()}; float* outs[2] = {o0.data(), o1.data()};
      OneVoice ov; int firedAt = -1; std::size_t t = 0;
      for (int k = 0; k < static_cast<int>(seconds * 48000 / n); ++k) { for (std::size_t i = 0; i < n; ++i, ++t) { a[i] = genL(t); b[i] = genR(t); } rt.onBlock(ins, outs, n); if (firedAt < 0 && ov.update(rt.snapshot())) firedAt = k; }
      return std::pair<am::Snapshot, int>{rt.snapshot(), firedAt}; };
    std::mt19937 g2(7); std::normal_distribution<float> nz(0.f, 0.1f);
    std::vector<float> voice(48000 * 3); for (std::size_t i = 0; i < voice.size(); ++i) voice[i] = 0.3f * std::sin(2 * 3.14159 * 180 * i / 48000.0) * (0.6f + 0.4f * std::sin(2 * 3.14159 * 3 * i / 48000.0)) + 0.2f * nz(g2);
    auto v = [&](std::size_t t) { return voice[t % voice.size()]; };
    const auto same = run(v, v, 2.5);
    CHECK(same.first.in12Corr > 0.999 && std::fabs(same.first.in12DiffDb) < 0.01 && same.first.in12LevelDb > -40 && same.second > 0 && same.second * 256 / 48000.0 <= 2.0);
    std::vector<float> other(48000 * 3); for (auto& x : other) x = 0.3f * nz(g2);
    const auto stereo = run(v, [&](std::size_t t) { return 0.7f * voice[t % voice.size()] + 0.7f * other[t % other.size()]; }, 3.0);
    CHECK(stereo.second < 0 && stereo.first.in12Corr < 0.995);
    const auto quiet = run([](std::size_t) { return 0.f; }, [](std::size_t) { return 0.f; }, 3.0); CHECK(quiet.second < 0);
    const auto louder = run(v, [&](std::size_t t) { return 0.5f * voice[t % voice.size()]; }, 3.0); CHECK(louder.second < 0 && std::fabs(louder.first.in12DiffDb - 6.02) < 0.1);   // one side 6 dB down: a different gain, not the same capsule twice
    std::printf("latency_test: ONE VOICE same mic found after %.1f s (corr %.4f); stereo corr %.2f, silence and a 6 dB difference left alone\n", same.second * 256 / 48000.0 + 0.5, same.first.in12Corr, stereo.first.in12Corr); }
  std::printf(fails ? "latency_test: %d FAILED\n" : "latency_test: all passed\n", fails);
  return fails ? 1 : 0;
}
