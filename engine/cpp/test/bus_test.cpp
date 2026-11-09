// AUX SENDS: pre / post sends, bus level, mute, return into the master, the bus's own output.  bus_test  (exit 0 = all good)
#include "audiomixer/engine.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
static int fails = 0;
#define CHECK(c, w) do { if (!(c)) { std::fprintf(stderr, "FAIL %s (%d)\n", w, __LINE__); ++fails; } } while (0)
static bool near(double a, double b, double t) { return std::fabs(a - b) <= t; }
int main() {
  const double fs = 48000; const std::size_t n = 4800;
  std::vector<float> s0(n), s1(n, 0.0f), L(n), R(n), bL(n), bR(n), zL(n), zR(n);
  for (std::size_t i = 0; i < n; ++i) s0[i] = 0.5f * static_cast<float>(std::sin(2 * 3.14159265358979 * 1000 * static_cast<double>(i) / fs));   // -6 dB sine
  const float* in[2] = {s0.data(), s1.data()}; float* bus[2 * am::kMaxBuses] = {}; bus[0] = bL.data(); bus[1] = bR.data(); bus[2] = zL.data(); bus[3] = zR.data();
  auto peak = [](const std::vector<float>& v) { double p = 0; for (float x : v) p = std::max(p, std::fabs(static_cast<double>(x))); return p; };
  const double c45 = std::cos(3.14159265358979 / 4);
  // 1. no sends: buses silent, master as before
  { am::Mixer m(2, fs); m.process(in, n, L.data(), R.data(), bus); CHECK(peak(bL) == 0 && peak(bR) == 0, "no sends -> silent buses"); CHECK(near(peak(L), 0.5 * c45, 1e-3), "master unchanged"); }
  // 2. a pre-fader send at 0 dB ignores the strip gain and mute; a post-fader send follows both
  { am::Mixer m(2, fs); m.setGainDb(0, -6); m.setSend(0, 0, 0, true); m.setSend(0, 1, 0, false);
    m.process(in, n, L.data(), R.data(), bus);
    CHECK(near(peak(bL), 0.5 * c45, 1e-3), "pre send: input level, panned centre"); CHECK(near(peak(zL), 0.25 * c45, 2e-3), "post send: after the -6 dB fader");
    CHECK(near(m.bus(0).meter.state().pk, 0.5 * c45, 1e-3), "bus meter reads the bus");
    m.setMute(0, true); m.process(in, n, L.data(), R.data(), bus);
    CHECK(near(peak(bL), 0.5 * c45, 1e-3), "pre send survives the mute"); CHECK(peak(zL) == 0, "post send is muted with the strip"); CHECK(peak(L) == 0, "master muted"); }
  // 3. bus level, bus mute, return into the master
  { am::Mixer m(2, fs); m.setSend(0, 0, 0, true); m.setBusLevel(0, -6); m.process(in, n, L.data(), R.data(), bus);
    CHECK(near(peak(bL), 0.25 * c45, 2e-3), "bus level -6 dB");
    m.setBusMute(0, true); m.process(in, n, L.data(), R.data(), bus); CHECK(peak(bL) == 0, "bus mute");
    m.setBusMute(0, false); m.setBusLevel(0, 0); m.setMute(0, true); m.setBusReturn(0, 0); m.process(in, n, L.data(), R.data(), bus);
    CHECK(near(peak(L), 0.5 * c45, 1e-3), "a muted strip is still heard through the bus RETURN"); }
  // 4. a send below -60 dB is off; the bus output pointers may be null
  { am::Mixer m(2, fs); m.setSend(0, 0, -70, true); m.process(in, n, L.data(), R.data(), nullptr); CHECK(m.bus(0).meter.state().pk == 0, "-70 dB send is off"); }
  if (fails) return 1; std::printf("bus_test: ok\n"); return 0;
}
