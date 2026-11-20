// DSP MODE FAST: the 4x true-peak detector and limiter, TRUE BYPASS (bit-exact), the worker pool (bit-identical to one thread),
// silent strips, the block suggestion. Exit 0 = all good.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "audiomixer/latency.hpp"
#include "audiomixer/runtime.hpp"

static int failures = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
constexpr double kPi = 3.14159265358979323846;

namespace {
struct Gain2 : am::Runtime::Insert {   // an insert that doubles the signal: if TRUE BYPASS ever called it, the output would change
  bool off = true; int calls = 0;
  void process(float* x, std::size_t n) override { ++calls; for (std::size_t i = 0; i < n; ++i) x[i] *= 2.0f; }
  bool bypassed() const override { return off; }
};
struct OnePole : am::Runtime::Insert {   // a typical light insert with state
  float z = 0;
  void process(float* x, std::size_t n) override { for (std::size_t i = 0; i < n; ++i) { z += 0.2f * (x[i] - z); x[i] = 0.7f * x[i] + 0.3f * z; } }
};
std::unique_ptr<am::Runtime> make(int strips, std::size_t frames) {
  auto rt = std::make_unique<am::Runtime>(strips); am::DeviceConfig c; c.backend = "null"; c.sampleRate = 48000; c.frames = static_cast<int>(frames); c.inChannels = strips; c.outChannels = 2; std::string err;
  if (!rt->open(c, err)) { std::printf("open: %s\n", err.c_str()); std::exit(1); }
  return rt;
}
// runs `blocks` blocks of noise + tones into every strip, returns the master L/R of all blocks
std::vector<float> render(am::Runtime& rt, int strips, std::size_t frames, int blocks, unsigned seed, int silentFrom = 1 << 30) {
  std::mt19937 rng(seed); std::normal_distribution<float> nz(0.f, 0.05f);
  std::vector<std::vector<float>> in(static_cast<std::size_t>(strips), std::vector<float>(frames)); std::vector<float> l(frames), r(frames), all;
  std::vector<const float*> ip(static_cast<std::size_t>(strips)); for (int s = 0; s < strips; ++s) ip[static_cast<std::size_t>(s)] = in[static_cast<std::size_t>(s)].data(); float* op[2] = {l.data(), r.data()};
  for (int b = 0; b < blocks; ++b) {
    for (int s = 0; s < strips; ++s) for (std::size_t i = 0; i < frames; ++i) in[static_cast<std::size_t>(s)][i] = s >= silentFrom ? 0.0f : nz(rng) + 0.2f * static_cast<float>(std::sin(0.013 * static_cast<double>(b * frames + i) * (s + 1)));
    rt.onBlock(ip.data(), op, frames); all.insert(all.end(), l.begin(), l.end()); all.insert(all.end(), r.begin(), r.end());
  }
  return all;
}
void typical(am::Runtime& rt, int strips) {
  for (int s = 0; s < strips; ++s) { rt.setEqBand(s, 0, am::dsp::FilterType::LowShelf, 100, 0.7, 2); rt.setEqBand(s, 1, am::dsp::FilterType::Peaking, 2500, 1, -3);
    rt.setCompressor(s, true, -18, 3, 6, 10, 120, 2); rt.setGate(s, true, -50, -60, 1, 50, 120); rt.setGainDb(s, -12); rt.setPan(s, (s % 7 - 3) / 3.0); }
  rt.setLimiter(true, -1.0, 100);
}
}  // namespace

int main() {
  // 1. the detector: a tone at a quarter of the rate, 45 degrees off the sample grid: every sample is +-0.7071, the waveform between them peaks at 1.0
  {
    am::TruePeak4x tp; double samplePk = 0, truePk = 0;
    for (int n = 0; n < 400; ++n) { const double x = std::sin(kPi / 2 * n + kPi / 4); samplePk = std::max(samplePk, std::fabs(x)); const double p = tp.push(x); if (n > 20) truePk = std::max(truePk, p); }
    std::printf("     fs/4 tone at 45 deg: sample peak %.4f, true peak %.4f (exact 1.0)\n", samplePk, truePk);
    expect(std::fabs(samplePk - 0.70711) < 1e-3, "the samples peak at 0.7071 (-3.01 dBFS)");
    expect(truePk > 0.97 && truePk < 1.03, "the 4x detector finds the inter-sample peak of 1.0 (0 dBTP) within 0.25 dB");
    am::TruePeak4x d; double last = 0; for (int n = 0; n < 20; ++n) { d.push(n == 3 ? 1.0 : 0.0); if (n == 3 + am::TruePeak4x::kDelay) last = d.delayed(); }
    expect(last == 1.0, "delayed() is the input kDelay samples later");
  }
  // 2. the master limiter at -1.0 dBTP: sample peak alone lets the inter-sample peaks through, the true-peak detector holds them
  {
    auto tpOut = [](bool tp) {
      am::Mixer m(1, 48000); m.setLimiter(true, -1.0, 100); m.setTruePeakLimiter(tp); m.setPan(0, 0); const std::size_t n = 4800;
      std::vector<float> x(n), l(n), r(n); for (std::size_t i = 0; i < n; ++i) x[i] = static_cast<float>(2.0 * std::sin(kPi / 2 * i + kPi / 4));   // after the centre pan: samples at 0 dBFS, the waveform at +3 dBTP
      const float* in[1] = {x.data()}; m.process(in, n, l.data(), r.data());
      am::TruePeak4x d; double pk = 0; for (std::size_t i = 0; i < n; ++i) { const double p = d.push(l[i]); if (i > 600) pk = std::max(pk, p); }
      return 20 * std::log10(pk);
    };
    const double samplePeakOnly = tpOut(false), truePeak = tpOut(true);
    std::printf("     limiter at -1.0: output true peak %.2f dBTP with sample-peak detection, %.2f dBTP with 4x true peak\n", samplePeakOnly, truePeak);
    expect(samplePeakOnly > -0.5, "a sample-peak limiter lets inter-sample peaks over the ceiling (the problem)");
    expect(truePeak <= -0.9, "the true-peak limiter keeps the output at -1.0 dBTP (0.1 dB tolerance)");
  }
  const std::size_t F = 256;
  // 3. TRUE BYPASS: a switched-off insert and flat EQ bands are not run at all: bit-exact with a strip that has neither
  {
    auto a = make(4, F), b = make(4, F); auto g = std::make_shared<Gain2>();
    a->setInserts(1, std::make_shared<const am::Runtime::InsertChain>(am::Runtime::InsertChain{g}));
    a->setEqBand(1, 0, am::dsp::FilterType::Peaking, 1000, 1, 0.0); a->setEqBand(2, 3, am::dsp::FilterType::HighShelf, 8000, 0.7, 0.0);
    a->setFast(true); b->setFast(true);
    const auto oa = render(*a, 4, F, 100, 5), ob = render(*b, 4, F, 100, 5);
    expect(oa.size() == ob.size() && std::memcmp(oa.data(), ob.data(), oa.size() * sizeof(float)) == 0, "TRUE BYPASS: bit-exact pass-through, no added delay");
    expect(g->calls == 0, "TRUE BYPASS: the switched-off insert is never called");
    g->off = false; render(*a, 4, F, 2, 5); expect(g->calls == 2, "switched back on, the insert runs again");
    // FAST with no limiter and nothing silent is the same mix as DIGITAL, bit for bit
    auto c = make(8, F), d = make(8, F); typical(*c, 8); typical(*d, 8); c->setLimiter(false, -1, 100); d->setLimiter(false, -1, 100); c->setFast(true);
    const auto oc = render(*c, 8, F, 200, 9), od = render(*d, 8, F, 200, 9);
    expect(std::memcmp(oc.data(), od.data(), oc.size() * sizeof(float)) == 0, "FAST (limiter off) mixes exactly like DIGITAL");
  }
  // 4. MULTI-CORE: 64 strips with EQ, dynamics and an insert on 3 helpers + the caller == the caller alone, bit for bit
  {
    const int S = 64; auto one = make(S, F), many = make(S, F); typical(*one, S); typical(*many, S);
    for (int s = 0; s < S; ++s) { one->setInserts(s, std::make_shared<const am::Runtime::InsertChain>(am::Runtime::InsertChain{std::make_shared<OnePole>()})); many->setInserts(s, std::make_shared<const am::Runtime::InsertChain>(am::Runtime::InsertChain{std::make_shared<OnePole>()})); }
    one->setFastHelpers(0); many->setFastHelpers(3); one->setFast(true); many->setFast(true);
    const auto o1 = render(*one, S, F, 400, 11, 48), o2 = render(*many, S, F, 400, 11, 48);
    std::printf("     helpers: %d, blocks shared out: %llu of 400\n", many->fastHelpers(), static_cast<unsigned long long>(many->fastSharedRuns()));
    expect(many->fastHelpers() == 3 && many->fastSharedRuns() > 0, "the worker pool ran");
    expect(o1.size() == o2.size() && std::memcmp(o1.data(), o2.data(), o1.size() * sizeof(float)) == 0, "multi-thread output == single-thread output, bit for bit");
    many->setFast(false); expect(many->fastHelpers() == 0 && !many->fast(), "leaving FAST ends the helpers");
  }
  // 5. SILENT STRIPS: under -120 dBFS for a second, nothing ringing: skipped (meter at rest, no DSP); sound brings it straight back
  {
    auto rt = make(2, F); rt->setFast(true); rt->setEqBand(0, 0, am::dsp::FilterType::Peaking, 1000, 1, 3);
    std::vector<float> q(F, 1e-7f), z(F, 0.f), l(F), r(F); const float* ip[2] = {q.data(), z.data()}; float* op[2] = {l.data(), r.data()};
    for (int b = 0; b < 400; ++b) rt->onBlock(ip, op, F);
    float pk = 0; for (float v : l) pk = std::max(pk, std::fabs(v)); expect(pk == 0.0f, "a strip under -120 dBFS for a second adds nothing");
    std::vector<float> loud(F, 0.25f); ip[0] = loud.data(); rt->onBlock(ip, op, F); pk = 0; for (float v : l) pk = std::max(pk, std::fabs(v)); expect(pk > 0.01f, "sound again: the strip is back in the same block");
  }
  // 6. TRUE BUFFER: the suggestion
  {
    am::latency::Input in; in.backend = "asio"; in.blockFrames = 256; in.maxBlockUs = 200;   // 0.78 us a frame
    const int b = am::latency::suggestFastBlock(in); std::printf("     ASIO, 256 frames, worst block 200 us: suggests %d frames\n", b);
    expect(b >= 32 && b <= 128, "a fast machine on ASIO is offered a small block");
    in.xruns = 3; expect(am::latency::suggestFastBlock(in) >= 256, "after dropouts it never suggests a smaller block");
    in.xruns = 0; in.backend = "wasapi"; in.exclusive = false; expect(am::latency::suggestFastBlock(in) >= 480, "WASAPI shared: not under its 10 ms period");
  }
  std::printf(failures ? "fast_test: %d failure(s)\n" : "fast_test: all good\n", failures);
  return failures ? 1 : 0;
}
