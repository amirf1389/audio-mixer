// LOUDNESS MANAGER: the per-strip AUTO FADER rides toward the target within the range and the speed limits (never over +10 dB),
// AUTO ON/OFF closes after the hold and opens again within a block, a hand on the fader suspends the ride, the master target converges.
#include <cmath>
#include <cstdio>
#include <vector>

#include "audiomixer/engine.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;

namespace {
const double fs = 48000; const std::size_t n = 480;   // 10 ms blocks
struct Rig {
  Mixer m{2, fs}; std::vector<float> a, z, L, R; double ph = 0;
  Rig() : a(n), z(n, 0.f), L(n), R(n) { m.setLimiter(false); }
  // amp 0 = silence; returns the largest change of the ride per block
  double run(double sec, double amp, double* maxUp = nullptr, double* maxDown = nullptr) {
    const int blocks = static_cast<int>(sec * fs / n + 0.5);
    for (int b = 0; b < blocks; ++b) {
      for (std::size_t i = 0; i < n; ++i) { a[i] = static_cast<float>(amp * std::sin(ph)); ph += 2 * 3.14159265358979 * 1000 / fs; }
      const float* in[2] = {a.data(), z.data()}; const double before = m.ride(0).rideDb();
      m.process(in, n, L.data(), R.data());
      const double d = m.ride(0).rideDb() - before;
      if (maxUp && d > *maxUp) *maxUp = d;
      if (maxDown && -d > *maxDown) *maxDown = -d;
    }
    return m.ride(0).rideDb();
  }
};
double ampFor(double lufs) { return std::sqrt(2.0 * std::pow(10.0, (lufs + 0.691) / 10.0)); }   // a 1 kHz tone (K-weighting ~ +0.7 dB there)
}

int main() {
  const double dt = static_cast<double>(n) / fs;
  {  // rides up toward the target at <= 3 dB/s, and settles there
    Rig r; RideParams p; p.on = true; p.targetLufs = -18; r.m.setRide(0, p); r.m.setGainDb(0, 0);
    double up = 0, down = 0; r.run(8, ampFor(-24), &up, &down); const double st = r.m.ride(0).shortTerm(), rd = r.m.ride(0).rideDb();
    std::printf("ride up: input %.2f LUFS, ride %+.2f dB -> %.2f LUFS, max step up %.3f dB/s\n", st, rd, st + rd, up / dt);
    CHECK(std::fabs(st + rd - (-18)) < 0.3); CHECK(up / dt <= 3.0 + 1e-6);
  }
  {  // rides down at <= 6 dB/s
    Rig r; RideParams p; p.on = true; p.targetLufs = -18; r.m.setRide(0, p);
    double down = 0; r.run(6, ampFor(-12), nullptr, &down); const double st = r.m.ride(0).shortTerm(), rd = r.m.ride(0).rideDb();
    std::printf("ride down: input %.2f LUFS, ride %+.2f dB, max step down %.3f dB/s\n", st, rd, down / dt);
    CHECK(std::fabs(st + rd - (-18)) < 0.3); CHECK(down / dt <= 6.0 + 1e-6);
  }
  {  // never past the range, never above +10 dB with the fader
    Rig r; RideParams p; p.on = true; p.targetLufs = -18; p.rangeDb = 10; r.m.setRide(0, p);
    double up = 0; r.run(10, ampFor(-40), &up); CHECK(r.m.ride(0).rideDb() <= 10.0 + 1e-9 && r.m.ride(0).rideDb() > 9.9);
    r.m.setGainDb(0, 6); r.run(2, ampFor(-40)); std::printf("ride with the fader at +6: %+.2f dB\n", r.m.ride(0).rideDb()); CHECK(r.m.ride(0).rideDb() <= 4.0 + 1e-9);
    Rig q; p.rangeDb = 3; q.m.setRide(0, p); q.run(10, ampFor(-40)); CHECK(std::fabs(q.m.ride(0).rideDb() - 3.0) < 1e-6);
    Rig w; w.m.setRide(0, p); w.run(10, ampFor(-2)); CHECK(std::fabs(w.m.ride(0).rideDb() + 3.0) < 1e-6);
  }
  {  // a hand on the fader: the ride starts from 0 at the new position and holds for 5 s, then rides again
    Rig r; RideParams p; p.on = true; r.m.setRide(0, p); r.run(6, ampFor(-24)); CHECK(r.m.ride(0).rideDb() > 5);
    r.m.setGainDb(0, 2); r.m.rideTouch(0, 5); r.run(4.5, ampFor(-24)); std::printf("suspended: ride %+.2f dB\n", r.m.ride(0).rideDb());
    CHECK(std::fabs(r.m.ride(0).rideDb()) < 1e-9 && r.m.ride(0).suspended());
    r.run(4, ampFor(-24)); CHECK(!r.m.ride(0).suspended() && r.m.ride(0).rideDb() > 2.5);
  }
  {  // AUTO ON/OFF: closes after the hold (4 s) over ~1 s, opens again within a block
    Rig r; RideParams p; p.gate = true; r.m.setRide(0, p); r.run(2, ampFor(-30)); CHECK(!r.m.ride(0).gateClosed());
    r.run(3.9, 0); CHECK(!r.m.ride(0).gateClosed());
    r.run(0.5, 0); CHECK(r.m.ride(0).gateClosed() && r.m.ride(0).gateDb() > -79);
    r.run(1.0, 0); std::printf("gate after 5.4 s of silence: %.1f dB\n", r.m.ride(0).gateDb()); CHECK(r.m.ride(0).gateDb() <= -79.9);
    r.run(dt, ampFor(-30)); std::printf("gate one block after the signal returns: %.1f dB\n", r.m.ride(0).gateDb()); CHECK(r.m.ride(0).gateDb() == 0.0);
    float pk = 0; for (float v : r.L) pk = std::max(pk, std::fabs(v)); CHECK(pk > 0.01f);   // the block is ramped up, not held back
    r.m.setMute(0, true); r.run(8, 0); CHECK(!r.m.ride(0).gateClosed());   // a muted strip is left alone
  }
  {  // master target: -23 LUFS from a louder program, max 1 dB/s, at most -6 dB
    Mixer m(1, fs); m.setLimiter(true, -1.0, 100); m.setAutoLevel(true, -23, 6); m.setAutoLevelLimits(-6, 1, 1);
    std::vector<float> a(n), L(n), R(n); double ph = 0, prev = 0, maxStep = 0; const double amp = ampFor(-20) * 1.41;
    for (int b = 0; b < static_cast<int>(12 * fs / n); ++b) { for (std::size_t i = 0; i < n; ++i) { a[i] = static_cast<float>(amp * std::sin(ph)); ph += 2 * 3.14159265358979 * 1000 / fs; }
      const float* in[1] = {a.data()}; m.process(in, n, L.data(), R.data()); maxStep = std::max(maxStep, std::fabs(m.autoLevelGainDb() - prev)); prev = m.autoLevelGainDb(); }
    std::printf("master target -23: gain %+.2f dB, short-term %.2f LUFS, max %.3f dB/s\n", m.autoLevelGainDb(), m.loudness().shortTerm(), maxStep / dt);
    CHECK(maxStep / dt <= 1.0 + 1e-6); CHECK(m.autoLevelGainDb() >= -6.0 - 1e-9); CHECK(std::fabs(m.loudness().shortTerm() - (-23)) < 0.5 || m.autoLevelGainDb() <= -6 + 1e-6);
  }
  std::printf("loudness ride: ok\n");
  return 0;
}
