// Bit depth and transparency: integer <-> float conversion as every backend and file writer does it (audiomixer/sample_format.hpp),
// and the mixer at unity: is anything of a 16 / 24 / 32-bit signal lost on the way through?
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "audiomixer/fpu.hpp"
#include "audiomixer/runtime.hpp"
#include "audiomixer/sample_format.hpp"

using namespace am;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); ++fails; } } while (0)

int main() {
  // 16 bit: every value through float and back is the same value
  int bad16 = 0; for (int v = -32768; v <= 32767; ++v) if (fmt::toI16(fmt::fromI16(static_cast<std::int16_t>(v))) != v) ++bad16;
  CHECK(bad16 == 0, "16-bit round trip changed %d of 65536 values", bad16);
  // 24 bit: all 16,777,216 values, and the packed bytes
  long bad24 = 0; for (std::int32_t v = -8388608; v <= 8388607; ++v) if (fmt::toI24(fmt::fromI24(v)) != v) ++bad24;
  CHECK(bad24 == 0, "24-bit round trip changed %ld values", bad24);
  { std::uint8_t b[3]; for (std::int32_t v : {-8388608, -1, 0, 1, 8388607, 123456, -654321}) { fmt::pack24(b, v); CHECK(fmt::unpack24(b) == v, "pack / unpack 24 of %d", v); } }
  // 32 bit: a float32 engine carries 24 bits of mantissa: the error stays within that (relative 2^-24), never more, and the extremes hold
  std::mt19937 rng(9); std::uniform_int_distribution<std::int32_t> d(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()); double worstRel = 0; long long worstAbs = 0;
  for (int k = 0; k < 2000000; ++k) { const std::int32_t v = d(rng); const std::int32_t r = fmt::toI32(fmt::fromI32(v)); const long long e = std::llabs(static_cast<long long>(r) - v); worstAbs = std::max(worstAbs, e); if (v) worstRel = std::max(worstRel, static_cast<double>(e) / std::fabs(static_cast<double>(v))); }
  CHECK(worstRel <= std::ldexp(1.0, -24), "32-bit: relative error %.3g (more than float32's 2^-24)", worstRel);
  for (std::int32_t v : {std::numeric_limits<std::int32_t>::min(), -1, 0, 1, 255, -256}) CHECK(fmt::toI32(fmt::fromI32(v)) == v, "32-bit small / extreme value %d changed", v);
  // full scale, overs, NaN
  CHECK(fmt::toI16(1.0f) == 32767 && fmt::toI16(-1.0f) == -32768 && fmt::toI16(3.0f) == 32767 && fmt::toI16(std::nanf("")) == 0, "16-bit clamps / NaN");
  CHECK(fmt::toI24(1.0f) == 8388607 && fmt::toI24(-1.0f) == -8388608 && fmt::toI32(1.0f) == 2147483647 && fmt::toI32(-1.0f) == std::numeric_limits<std::int32_t>::min(), "24 / 32-bit clamps");
  CHECK(fmt::toI16(0.4f / 32768.0f) == 0 && fmt::toI16(0.6f / 32768.0f) == 1 && fmt::toI16(-0.6f / 32768.0f) == -1, "rounding to nearest (not truncation)");
  std::printf("format_test: 16-bit and 24-bit round trips bit-exact (all values), 32-bit within float32 (worst %lld LSB of 2^31, relative %.2g), clamps, rounding OK\n", worstAbs, worstRel);

  // THE MIXER AT UNITY: a 24-bit signal into strip 1 (fader 0 dB, panned hard left, master 0 dB, limiter off): the left output is the
  // input, bit for bit, at 24 bits
  { Runtime rt(2); DeviceConfig c; c.backend = "null"; c.frames = 256; std::string err; CHECK(rt.open(c, err), "open: %s", err.c_str());
    rt.setGainDb(0, 0); rt.setPan(0, -1); rt.setGainDb(1, -120); rt.setMasterDb(0); rt.setLimiter(false);
    const std::size_t n = 256; std::vector<float> in(n), q(n, 0.f), oL(n), oR(n); const float* ins[2] = {in.data(), q.data()}; float* outs[2] = {oL.data(), oR.data()};
    std::uniform_int_distribution<std::int32_t> d24(-4000000, 4000000); long changed = 0, total = 0; double worst = 0;
    for (int b = 0; b < 400; ++b) { for (auto& x : in) x = fmt::fromI24(d24(rng)); rt.onBlock(ins, outs, n);
      if (b < 200) continue;   // the gain ramps have settled
      for (std::size_t i = 0; i < n; ++i) { ++total; if (fmt::toI24(oL[i]) != fmt::toI24(in[i])) ++changed; worst = std::max(worst, std::fabs(static_cast<double>(oL[i]) - in[i])); } }
    std::printf("format_test: mixer at unity: %ld of %ld 24-bit samples changed (worst %.3g = %.2f LSB of 24 bit)\n", changed, total, worst, worst * 8388608.0);
    CHECK(changed == 0, "the mixer at unity is not bit-transparent at 24 bits (%ld samples changed)", changed); }
  // NaN GUARD: a block of NaN / inf on a strip (a broken plug-in) gives silence for that block, never a NaN at the device, and the
  // master recovers at once (the DC blocker and the limiter are not poisoned)
  { Runtime rt(2); DeviceConfig c; c.backend = "null"; c.frames = 256; std::string err; CHECK(rt.open(c, err), "open: %s", err.c_str());
    rt.setGainDb(0, 0); rt.setMasterDb(0); rt.setLimiter(true);
    const std::size_t n = 256; std::vector<float> in(n, 0.25f), q(n, 0.f), oL(n), oR(n); const float* ins[2] = {in.data(), q.data()}; float* outs[2] = {oL.data(), oR.data()};
    long bad = 0; double after = 0;
    for (int b = 0; b < 300; ++b) {
      for (std::size_t i = 0; i < n; ++i) in[i] = b == 100 ? (i % 2 ? std::numeric_limits<float>::infinity() : std::nanf("")) : 0.25f * std::sin(0.05f * static_cast<float>(i + b * n));
      rt.onBlock(ins, outs, n);
      for (std::size_t i = 0; i < n; ++i) { if (!std::isfinite(oL[i]) || !std::isfinite(oR[i])) ++bad; if (b > 150) after = std::max(after, static_cast<double>(std::fabs(oL[i]))); } }
    std::printf("format_test: NaN guard: %ld non-finite samples reached the output, %llu silenced, level after the burst %.3f\n", bad, static_cast<unsigned long long>(rt.nonFiniteSamples()), after);
    CHECK(bad == 0, "NaN / inf reached the device (%ld samples)", bad); CHECK(rt.nonFiniteSamples() > 0, "the guard did not count");
    CHECK(after > 0.05, "the master did not recover after the NaN burst (level %.3f)", after); }
  // DENORMALS: flushed to zero on the audio thread
  { am::denormalsOff(); volatile float tiny = 1e-30f; volatile float d = tiny * 1e-10f;
#ifdef AM_HAS_MXCSR
    CHECK(d == 0.0f, "denormals are not flushed (%g)", static_cast<double>(d));
#endif
    std::printf("format_test: denormals flushed to zero (%g)\n", static_cast<double>(d)); }
  std::printf(fails ? "format_test: %d failed\n" : "format_test: all passed\n", fails); return fails ? 1 : 0;
}
