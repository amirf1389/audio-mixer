// LOW LATENCY SOUND DETECTION: the verdict and advice for the usual Windows setups, and the ROUND TRIP click found at the exact delay
// through noise, a quieter return and an inverted cable.
#include "audiomixer/latency.hpp"

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
  // nothing came back (no loop cable): low confidence, not trusted
  { std::vector<float> rec(24000); for (auto& x : rec) x = noise(g); double conf = 1; findDelay(click.data(), click.size(), rec.data(), rec.size(), &conf); CHECK(conf < 0.3); }
  { double c = 1; CHECK(findDelay(click.data(), click.size(), click.data(), 10, &c) == -1 && c == 0); }
  std::printf(fails ? "latency_test: %d FAILED\n" : "latency_test: all passed\n", fails);
  return fails ? 1 : 0;
}
