#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>
#include "../core/mixer.hpp"
using namespace amfb;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static double gainAt(const Biquad& c, double rate, double f) {   // steady-state gain of a sine through the filter, in dB
  BiquadState s; double sq = 0;
  for (int i = 0; i < 48000; ++i) { const double y = s.run(c, std::sin(2 * kPi * f * i / rate)); if (i >= 24000) sq += y * y; }
  return linToDb(std::sqrt(2 * sq / 24000));
}

int main() {
  {  // ring: order kept across the wrap, two threads, nothing lost or doubled
    SpscRing<float> r(1000);
    CHECK(r.capacity() == 1024, "capacity %zu", r.capacity());
    std::thread prod([&] { float v = 0; for (int sent = 0; sent < 200000;) { float b[37]; for (float& x : b) x = v++; std::size_t w = 0; while (w < 37) w += r.write(b + w, 37 - w); sent += 37; } });
    float expect = 0; bool ok = true; long got = 0;
    while (got < 200022) { float b[64]; const std::size_t n = r.read(b, 64); for (std::size_t i = 0; i < n; ++i) ok = ok && b[i] == expect++; got += long(n); }
    prod.join();
    CHECK(ok, "ring order broken");
    float one[2048] = {}; CHECK(r.write(one, 2048) == 1024, "a full ring must refuse the rest");
  }
  {  // biquads
    Biquad pk; pk.design(Biquad::Peak, 48000, 1000, 6, 1.0);
    CHECK(std::abs(gainAt(pk, 48000, 1000) - 6) < 0.05, "peak %.3f", gainAt(pk, 48000, 1000));
    CHECK(std::abs(gainAt(pk, 48000, 60)) < 0.2, "peak skirt %.3f", gainAt(pk, 48000, 60));
    Biquad ls; ls.design(Biquad::LowShelf, 48000, 200, -9, 0.707);
    CHECK(std::abs(gainAt(ls, 48000, 20) + 9) < 0.2 && std::abs(gainAt(ls, 48000, 8000)) < 0.1, "low shelf %.3f %.3f", gainAt(ls, 48000, 20), gainAt(ls, 48000, 8000));
    Biquad hs; hs.design(Biquad::HighShelf, 48000, 6000, 4, 0.707);
    CHECK(std::abs(gainAt(hs, 48000, 20000) - 4) < 0.2 && std::abs(gainAt(hs, 48000, 100)) < 0.1, "high shelf %.3f", gainAt(hs, 48000, 20000));
    Biquad flat; flat.design(Biquad::Peak, 48000, 1000, 0, 1); BiquadState s; CHECK(s.run(flat, 0.123456789) == 0.123456789, "0 dB must pass bit-exact");
  }
  {  // balance and width
    double l, r; balanceGains(0, l, r); CHECK(std::abs(l - 1) < 1e-12 && std::abs(r - 1) < 1e-12, "centre %.6f %.6f", l, r);
    balanceGains(-1, l, r); CHECK(std::abs(l - std::sqrt(2.0)) < 1e-12 && std::abs(r) < 1e-12, "left");
    balanceGains(0.4, l, r); CHECK(std::abs(l * l + r * r - 2) < 1e-12, "power must stay constant");
    double a = 0.7, b = -0.2; midSide(a, b, 1); CHECK(std::abs(a - 0.7) < 1e-15 && std::abs(b + 0.2) < 1e-15, "width 1");
    a = 0.7; b = -0.2; midSide(a, b, 0); CHECK(a == b && std::abs(a - 0.25) < 1e-15, "width 0 = mono");
  }
  {  // true peak: a sine at a quarter of the rate, 45 degrees off the samples, reads 3 dB low on a sample meter
    TruePeak4x tp; double sp = 0, t = 0;
    for (int i = 0; i < 4800; ++i) { const double x = std::sin(2 * kPi * 0.25 * i + kPi / 4); sp = std::max(sp, std::abs(x)); if (i > 100) t = std::max(t, tp.run(x)); else tp.run(x); }
    CHECK(std::abs(linToDb(sp) + 3.01) < 0.01, "sample peak %.3f", linToDb(sp));
    CHECK(std::abs(linToDb(t)) < 0.15, "true peak %.3f dB", linToDb(t));
  }
  {  // the mixer, end to end
    static SharedBlock blk; initBlock(&blk);
    HybridMixer mix(&blk, 48000, 1);
    std::vector<float> inL(480), inR(480), oL(480), oR(480), src(960);
    const float* in[2] = {inL.data(), inR.data()}; float* out[2] = {oL.data(), oR.data()};
    auto run = [&](int blocks, bool feed) {
      double pk = 0;
      for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 480; ++i) { const float v = float(0.5 * std::sin(2 * kPi * 1000.0 * (b * 480 + i) / 48000.0)); inL[i] = inR[i] = v; src[2 * i] = v; src[2 * i + 1] = -v; }
        if (feed) mix.ring(5).write(src.data(), 960);
        mix.process(in, 2, out, 2, 480);
        if (b > blocks / 2) for (int i = 0; i < 480; ++i) pk = std::max(pk, double(std::max(std::abs(oL[i]), std::abs(oR[i]))));
      }
      return pk;
    };
    CHECK(std::abs(run(40, false) - 0.5) < 1e-6, "unity path %.7f", run(40, false));
    {  // unity, flat EQ, centre: the device strip passes bit-exact
      run(4, false); bool exact = true; for (int i = 0; i < 480; ++i) exact = exact && oL[i] == inL[i] && oR[i] == inR[i];
      CHECK(exact, "unity must be bit-exact");
    }
    run(300, false);                                                     // let the 300 ms RMS window settle
    CHECK(std::abs(linToDb(blk.m[0].rmsL) + 9.03) < 0.1 && blk.m[0].phase > 0.99f, "rms %.2f phase %.2f", linToDb(blk.m[0].rmsL), blk.m[0].phase.load());
    CHECK(blk.m[0].active == 1 && blk.m[5].active == 0, "active flags");
    blk.p[0].mute = 1; blk.paramGen++;
    CHECK(run(20, false) < 1e-9, "mute");
    blk.p[0].mute = 0; blk.p[0].gainDb = -6; blk.p[0].pan = -1; blk.paramGen++;
    run(20, false);
    CHECK(std::abs(blk.m[kStrips].peakL - 0.5 * dbToLin(-6) * std::sqrt(2.0)) < 1e-4 && blk.m[kStrips].peakR < 1e-9, "gain and pan %.5f %.5f", blk.m[kStrips].peakL.load(), blk.m[kStrips].peakR.load());
    blk.p[0].mute = 1; blk.paramGen++;
    run(30, true);                                                       // strip 6: out-of-phase source through its ring
    CHECK(blk.m[5].active == 1 && std::abs(blk.m[5].peakL - 0.5f) < 1e-4f && blk.m[5].phase < -0.99f, "ring strip peak %.4f phase %.2f", blk.m[5].peakL.load(), blk.m[5].phase.load());
    blk.p[5].width = 0; blk.paramGen++;
    CHECK(run(20, true) < 1e-6, "width 0 of an out-of-phase source is silence");
    blk.p[5].width = 1; blk.p[5].solo = 1; blk.p[0].mute = 0; blk.p[5].eqGain[3] = 6; blk.paramGen++;
    run(40, true);
    CHECK(std::abs(linToDb(blk.m[5].peakL / 0.5) - 6) < 0.1, "eq +6 dB at 1 kHz: %.2f", linToDb(blk.m[5].peakL / 0.5));
    CHECK(blk.m[0].peakL < 1e-9, "solo must silence the others");
    const auto u = blk.underruns.load(); run(5, false); CHECK(blk.underruns.load() >= u && blk.m[5].active == 0, "a dry ring gives silence");
    CHECK(blk.heartbeat.load() > 0, "heartbeat");
    {  // compressor: level 0 is bit-exact off; level 5 on a loud strip pulls the peak down and reports the reduction
      blk.p[5].solo = 0; blk.p[5].eqGain[3] = 0; blk.p[0].gainDb = 0; blk.p[0].pan = 0; blk.p[0].compLevel = 0; blk.paramGen++;
      run(4, false); bool exact = true; for (int i = 0; i < 480; ++i) exact = exact && oL[i] == inL[i];
      CHECK(exact, "compressor level 0 must be off");
      blk.p[0].compLevel = 5; blk.paramGen++;
      run(60, false);
      const double gr = blk.m[0].reductionDb.load();
      CHECK(gr < -3 && gr > -12, "reduction %.2f dB at level 5 on -6 dBFS", gr);
      CHECK(blk.m[0].peakL > 0.3f && blk.m[0].peakL < 0.9f, "compressed peak %.3f", blk.m[0].peakL.load());
      blk.p[0].compLevel = 0; blk.paramGen++;
    }
  }
  std::printf(fails ? "core_test: %d FAILED\n" : "core_test: all passed\n", fails);
  return fails ? 1 : 0;
}
