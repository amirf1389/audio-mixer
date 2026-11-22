// DUAL LISTEN in the control room: the A / B mix math, the monitor-only EQ (never the master / show outputs), the monitor analysis bands,
// AI EQ AUTO from those bands (the ACOUSTICS Auto EQ), and the capture / restore of the monitor's EQ mode.
#include "audiomixer/runtime.hpp"
#include "measure/measure.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int fails = 0;
static void expect(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }
static float rms(const std::vector<float>& v) { double a = 0; for (float x : v) a += double(x) * x; return static_cast<float>(std::sqrt(a / static_cast<double>(v.size()))); }
static float peak(const std::vector<float>& v) { float p = 0; for (float x : v) p = std::max(p, std::fabs(x)); return p; }

int main() {
  { double a = 0, b = 0;   // the dual mix math
    am::Runtime::dualGains(0, 0, a, b); expect(a == 1 && b == 0, "blend 0: A alone");
    am::Runtime::dualGains(1, 0, a, b); expect(a == 0 && std::fabs(b - 1) < 1e-12, "blend 1: B alone");
    am::Runtime::dualGains(0.5, 0, a, b); expect(std::fabs(a - b) < 1e-12 && std::fabs(a * a + b * b - 1) < 1e-12, "blend 0.5: equal power, the same loudness");
    bool power = true; for (int i = 0; i <= 20; ++i) { am::Runtime::dualGains(i / 20.0, 0, a, b); power = power && std::fabs(a * a + b * b - 1) < 1e-9; } expect(power, "every blend keeps a^2 + b^2 = 1");
    am::Runtime::dualGains(0.3, 1, a, b); expect(a == 1 && b == 0, "A/B switch on A");
    am::Runtime::dualGains(0.3, 2, a, b); expect(a == 0 && b == 1, "A/B switch on B"); }
  { am::Runtime rt(2); am::DeviceConfig c; c.backend = "null"; c.frames = 64; std::string err; expect(rt.open(c, err), "open null");
    const std::size_t n = 64; std::vector<float> tone(n); for (std::size_t i = 0; i < n; ++i) tone[i] = 0.5f * ((i / 8) % 2 ? 1.f : -1.f);   // a 3 kHz square
    std::vector<float> o0(n), o1(n); const float* ins[2] = {tone.data(), tone.data()}; float* outs[2] = {o0.data(), o1.data()};
    rt.setGainDb(0, 0); rt.setGainDb(1, 0); rt.setPan(0, -1); rt.setPan(1, 1); rt.setOutputRoute(1, am::Runtime::kSrcMasterL);   // out 0 = monitor L, out 1 = master L (the show)
    auto run = [&](int k) { for (int i = 0; i < k; ++i) rt.onBlock(ins, outs, n); };
    run(20); const float plain = peak(o0), plainRms = rms(o0), show = peak(o1); const double masterPk = rt.snapshot().master[0].pk;
    rt.setMonitorDual(true, 2, 0.5, 2); run(4); expect(peak(o0) < 1e-6f, "DUAL LISTEN on B (an empty bus): the control room is silent");
    expect(std::fabs(peak(o1) - show) < 1e-6f, "the show output does not follow the monitor's A/B");
    rt.setMonitorDual(true, 2, 0.5, 1); run(4); expect(std::fabs(peak(o0) - plain) < 1e-5f, "A/B switch on A: the master as before");
    rt.setMonitorDual(true, 2, 0.5, 0); run(4); expect(std::fabs(peak(o0) / plain - 0.70710678f) < 1e-3f, "blend 0.5 of the master and an empty bus: -3 dB");
    rt.setMonitorDual(true, 0, 0.5, 0); run(4); expect(std::fabs(peak(o0) / plain - 1.41421356f) < 2e-3f, "the same source on A and B sums (equal power: +3 dB)");
    rt.setMonitorDual(true, 2, 0.0, 0); run(400);
    const am::Snapshot s = rt.snapshot(); int top = 0; for (int k = 1; k < am::Runtime::kMonBands; ++k) if (s.monitorBandDb[k] > s.monitorBandDb[top]) top = k;
    expect(top == 6 || top == 7, "the monitor analysis hears the 3 kHz tone in the 2 k / 4 k octave");
    expect(s.monitorBandDb[0] < s.monitorBandDb[top] - 20, "and little at 31 Hz");
    // the monitor EQ: a deep cut at the tone changes the control room only
    rt.setMonitorEqFilter(0, 3000, 1.0, -15); rt.setMonitorEqOn(true); run(40);
    expect(rms(o0) < plainRms * 0.6f, "the monitor EQ cuts the control room");
    expect(std::fabs(peak(o1) - show) < 1e-6f, "the monitor EQ leaves the show output alone");
    expect(std::fabs(rt.snapshot().master[0].pk - masterPk) < 0.02, "the monitor EQ leaves the master meter alone");
    rt.setMonitorEqOn(false); rt.setMonitorDual(false, 2, 0.5, 0); run(40); expect(std::fabs(peak(o0) - plain) < 1e-5f, "DUAL LISTEN and the EQ off: the monitor is the master again"); }
  { double bands[am::Runtime::kMonBands]; for (double& b : bands) b = -30; bands[3] = -20;   // +10 dB at 250 Hz
    const auto f = am::measure::monitorAutoEq(bands, am::Runtime::kMonBands);
    expect(!f.empty() && f[0].gainDb < -3 && f[0].freq > 180 && f[0].freq < 350, "AI EQ AUTO cuts the 250 Hz bump");
    bool gentle = true; for (const auto& x : f) gentle = gentle && x.gainDb <= 4 + 1e-9 && x.gainDb >= -8 - 1e-9; expect(gentle, "AI EQ AUTO stays gentle (+4 / -8 dB)");
    double silent[am::Runtime::kMonBands]; for (double& b : silent) b = -140; expect(am::measure::monitorAutoEq(silent, am::Runtime::kMonBands).empty(), "nothing heard: no filters");
    for (double& b : bands) b = -30; expect(am::measure::monitorAutoEq(bands, am::Runtime::kMonBands).empty(), "a balanced mix: no filters"); bands[3] = -20;
    am::measure::MonitorEqMode m; m.mode = am::measure::MonitorEqMode::Manual; m.filters = {{100, 1.0, -3.0}, {8000, 2.0, 2.0}};
    m.engage(); expect(m.mode == am::measure::MonitorEqMode::AiAuto && m.filters.empty() && m.captured, "DUAL LISTEN captures the EQ mode as AI EQ AUTO");
    m.learn(bands, am::Runtime::kMonBands); expect(!m.filters.empty() && m.eqOn(), "AI EQ AUTO learns from the bands");
    m.engage(); expect(m.savedMode == am::measure::MonitorEqMode::Manual && m.savedFilters.size() == 2, "a second engage keeps the first capture");
    m.reset(); expect(m.filters.empty() && m.mode == am::measure::MonitorEqMode::AiAuto, "reset: flat, still AI EQ AUTO");
    m.release(); expect(m.mode == am::measure::MonitorEqMode::Manual && m.filters.size() == 2 && m.filters[0].freq == 100 && m.filters[1].gainDb == 2.0 && !m.captured, "off: the previous EQ mode and filters come back exactly");
    m.release(); expect(m.mode == am::measure::MonitorEqMode::Manual && m.filters.size() == 2, "a second release changes nothing");
    am::measure::MonitorEqMode off; off.engage(); off.learn(bands, am::Runtime::kMonBands); off.release(); expect(off.mode == am::measure::MonitorEqMode::Off && !off.eqOn(), "from OFF back to OFF"); }
  std::printf(fails ? "monitor_test: %d FAILED\n" : "monitor_test: all passed\n", fails); return fails ? 1 : 0;
}
