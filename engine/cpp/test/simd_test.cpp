// SIMD kernels (include/audiomixer/simd.hpp): every kernel, on every level this processor runs, against the scalar code it replaces
// (the per-sample Biquad / Compressor / TruePeak4x ticks and plain loops), on random and edge data (silence, denormals, full scale,
// clipping runs, NaN and inf) and block sizes 1..4097. Within 1e-6 relative (the baseline is mostly bit-exact; AVX2 + FMA rounds once
// per multiply-add). The run counters of the meter and the NaN handling must match exactly. Exit 0 = all good.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "audiomixer/dsp.hpp"
#include "audiomixer/engine.hpp"
#include "audiomixer/fpu.hpp"
#include "audiomixer/simd.hpp"

namespace {
int failures = 0;
void expect(bool ok, const std::string& what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what.c_str()); } else std::printf("ok   %s\n", what.c_str()); }
double worst = 0;   // the largest relative difference seen
bool close(double a, double b) {
  if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
  if (std::isinf(a) || std::isinf(b)) return a == b;
  const double d = std::fabs(a - b), m = std::max(std::fabs(a), std::fabs(b)), r = m > 1e-30 ? d / m : 0; if (r > worst && m > 1e-12) worst = r;
  return d <= 1e-6 * m + 1e-12;
}
template <class A, class B> bool allClose(const A& a, const B& b, std::size_t n) { for (std::size_t i = 0; i < n; ++i) if (!close(a[i], b[i])) { std::printf("     at %zu: %.17g vs %.17g\n", i, static_cast<double>(a[i]), static_cast<double>(b[i])); return false; } return true; }

const std::size_t kSizes[] = {1, 2, 3, 4, 5, 7, 8, 15, 31, 32, 33, 63, 64, 65, 127, 255, 256, 257, 511, 512, 513, 1000, 4096, 4097};
enum class Data { Noise, Loud, Denormal, Silence, Clip, Mixed };
const char* dataName(Data d) { switch (d) { case Data::Noise: return "noise"; case Data::Loud: return "loud"; case Data::Denormal: return "denormals"; case Data::Silence: return "silence"; case Data::Clip: return "clipping"; default: return "mixed"; } }
std::vector<float> make(Data d, std::size_t n, unsigned seed) {
  std::mt19937 rng(seed); std::normal_distribution<float> g(0.f, 0.1f); std::uniform_real_distribution<float> u(-1.f, 1.f); std::vector<float> x(n);
  for (std::size_t i = 0; i < n; ++i) switch (d) {
    case Data::Noise: x[i] = g(rng) + 0.3f * std::sin(0.03f * static_cast<float>(i)); break;
    case Data::Loud: x[i] = 0.99f * u(rng); break;
    case Data::Denormal: x[i] = 1e-39f * u(rng); break;   // below the smallest normal float
    case Data::Silence: x[i] = 0.f; break;
    case Data::Clip: x[i] = (i / 5) % 3 == 0 ? (i & 1 ? 1.f : -1.f) : 0.5f * u(rng); break;   // runs of full scale
    default: x[i] = i % 97 == 3 ? 1.2f : i % 41 == 0 ? 0.f : i % 13 == 0 ? 1e-40f : g(rng); break;
  }
  return x;
}
const Data kData[] = {Data::Noise, Data::Loud, Data::Denormal, Data::Silence, Data::Clip, Data::Mixed};

void testLevel(const am::simd::Kernels& k) {
  const std::string L = std::string("[") + k.name + "] ";
  const double fs = 48000;
  // 1. CASCADE: shelves, bells, a notch and LR4 crossover sections in series == Biquad::tick through each
  { bool ok = true;
    for (Data d : kData) for (std::size_t n : kSizes) {
      std::vector<am::dsp::Biquad> a(7); a[0].design(am::dsp::FilterType::LowShelf, fs, 100, 0.7, 6); a[1].design(am::dsp::FilterType::Peaking, fs, 40, 8, -12); a[2].design(am::dsp::FilterType::Peaking, fs, 2500, 1, 3);
      a[3].design(am::dsp::FilterType::HighShelf, fs, 8000, 0.7, -4); a[4].design(am::dsp::FilterType::Notch, fs, 60, 10, 0); a[5].design(am::dsp::FilterType::HighPass, fs, 30, 0.54, 0); a[6].design(am::dsp::FilterType::AllPass2, fs, 900, 0.7, 0);
      std::vector<am::dsp::Biquad> b = a; const std::vector<float> in = make(d, n, static_cast<unsigned>(n));
      std::vector<double> ref(n), got(n); for (std::size_t i = 0; i < n; ++i) { double v = in[i]; for (auto& q : a) v = q.tick(v); ref[i] = v; got[i] = in[i]; }
      std::vector<double*> sec; for (auto& q : b) sec.push_back(&q.b0);
      k.cascade(sec.data(), static_cast<int>(sec.size()), got.data(), n);
      ok = ok && allClose(got, ref, n) && close(a[6].z1, b[6].z1) && close(a[1].z2, b[1].z2);
      if (!ok) { std::printf("     %s, %zu frames\n", dataName(d), n); break; }
    }
    expect(ok, L + "biquad cascade == Biquad::tick in series (7 sections, sizes 1..4097, random and edge data)"); }
  // 2. COMPRESSOR, run block after block (the state carries over), hard and soft knee, == Compressor::tick
  { bool ok = true;
    for (double knee : {0.0, 6.0}) for (Data d : kData) for (std::size_t n : kSizes) {
      am::dsp::Compressor a, b; a.set(true, -18, 3, knee, 5, 120, 2, fs); b.set(true, -18, 3, knee, 5, 120, 2, fs);
      std::vector<double> ref, got;
      for (int rep = 0; rep < 3; ++rep) { const std::vector<float> in = make(d, n, static_cast<unsigned>(n * 3 + rep)); std::vector<double> blk(in.begin(), in.end());
        for (float v : in) ref.push_back(a.tick(v));
        b.process(blk.data(), blk.size()); got.insert(got.end(), blk.begin(), blk.end()); }
      ok = ok && allClose(got, ref, ref.size()) && close(a.gainReductionDb(), b.gainReductionDb());
      if (!ok) { std::printf("     knee %.0f, %s, %zu frames\n", knee, dataName(d), n); break; }
    }
    // NaN and inf in the input: the same as the per-sample code (the follower holds a NaN for good; the gain stays finite)
    am::dsp::Compressor a, b; a.set(true, -18, 3, 6, 5, 120, 2, fs); b.set(true, -18, 3, 6, 5, 120, 2, fs);
    std::vector<double> in = {0.5, std::numeric_limits<double>::infinity(), 0.2, 0.3, -0.7, 0.1, 0.0, 0.4}, ref(in.size()), got = in;
    for (std::size_t i = 0; i < in.size(); ++i) ref[i] = a.tick(in[i]);
    b.process(got.data(), got.size()); ok = ok && allClose(got, ref, in.size());
    in.assign({0.5, std::numeric_limits<double>::quiet_NaN(), 0.2, 0.3}); ref.assign(in.size(), 0); got = in;
    for (std::size_t i = 0; i < in.size(); ++i) ref[i] = a.tick(in[i]);
    b.process(got.data(), got.size()); ok = ok && allClose(got, ref, in.size()) && close(a.gainReductionDb(), b.gainReductionDb());
    expect(ok, L + "compressor (block kernel) == Compressor::tick: hard and soft knee, state across blocks, inf and NaN"); }
  // 3. GAIN + PEAK
  { bool ok = true;
    for (Data d : kData) for (std::size_t n : kSizes) {
      std::vector<float> in = make(d, n, static_cast<unsigned>(n + 11)); if (n > 3) in[n / 2] = std::numeric_limits<float>::quiet_NaN();
      std::vector<float> ref(n), got(n); double rp = 0; for (std::size_t i = 0; i < n; ++i) { const double x = in[i]; if (std::fabs(x) > rp) rp = std::fabs(x); ref[i] = static_cast<float>(x * 0.25118864315095801); }
      const double gp = k.gainPeak(in.data(), n, 0.25118864315095801, got.data());
      ok = ok && gp == rp && allClose(got, ref, n);
      if (!ok) { std::printf("     %s, %zu frames\n", dataName(d), n); break; }
    }
    expect(ok, L + "gain + input peak == the scalar loop (a NaN is not a peak)"); }
  // 4. METER: peak, sum of squares, clip runs over several adds
  { bool ok = true;
    for (Data d : kData) for (std::size_t n : kSizes) {
      double pk = 0, ss = 0, gpk = 0, gss = 0; int run = 0, maxRun = 0, grun = 0, gmax = 0; const double lin = std::pow(10.0, -0.1 / 20);
      for (int rep = 0; rep < 3; ++rep) { std::vector<float> x = make(d, n, static_cast<unsigned>(n * 7 + rep)); if (n > 9 && rep == 1) x[n - 2] = std::numeric_limits<float>::quiet_NaN();
        for (std::size_t i = 0; i < n; ++i) { const double v = x[i], a = std::fabs(v); if (a > pk) pk = a; ss += v * v; if (a >= lin) { if (++run > maxRun) maxRun = run; } else run = 0; }
        k.meter(x.data(), n, lin, &gpk, &gss, &grun, &gmax); }
      ok = ok && gpk == pk && run == grun && maxRun == gmax && close(gss, ss);
      if (!ok) { std::printf("     %s, %zu frames: pk %g/%g run %d/%d max %d/%d ss %g/%g\n", dataName(d), n, gpk, pk, grun, run, gmax, maxRun, gss, ss); break; }
    }
    expect(ok, L + "meter (peak, sum of squares, clip runs across adds) == the scalar loop"); }
  // 5. PAN SUM
  { bool ok = true;
    for (Data d : kData) for (std::size_t n : kSizes) {
      const std::vector<float> t = make(d, n, static_cast<unsigned>(n + 5)), l0 = make(Data::Noise, n, 1), r0 = make(Data::Loud, n, 2);
      std::vector<float> rl = l0, rr = r0, gl = l0, gr = r0; const double a = std::cos(0.3), b = std::sin(0.3);
      for (std::size_t i = 0; i < n; ++i) { rl[i] += static_cast<float>(t[i] * a); rr[i] += static_cast<float>(t[i] * b); }
      k.panAdd(t.data(), n, a, b, gl.data(), gr.data());
      ok = ok && allClose(gl, rl, n) && allClose(gr, rr, n);
      if (!ok) { std::printf("     %s, %zu frames\n", dataName(d), n); break; }
    }
    expect(ok, L + "pan sum into L / R == the scalar loop"); }
  // 6. TRUE PEAK 4x
  { bool ok = true;
    for (Data d : kData) for (std::size_t n : kSizes) {
      am::TruePeak4x a, b; std::vector<double> ref, rd, pk, dl;
      for (int rep = 0; rep < 2; ++rep) { const std::vector<float> f = make(d, n, static_cast<unsigned>(n + rep)); std::vector<double> x(f.begin(), f.end()), p(n), q(n);
        for (double v : x) { ref.push_back(a.push(v)); rd.push_back(a.delayed()); }
        b.pushBlock(x.data(), n, p.data(), q.data());   // the active level: this one
        pk.insert(pk.end(), p.begin(), p.end()); dl.insert(dl.end(), q.begin(), q.end()); }
      ok = ok && allClose(pk, ref, ref.size()) && allClose(dl, rd, rd.size());
      if (!ok) { std::printf("     %s, %zu frames\n", dataName(d), n); break; }
    }
    expect(ok, L + "true peak 4x (block kernel) == TruePeak4x::push"); }
  // 7. A WHOLE CHANNEL (delay, gate, LR4 crossover, 4 EQ bands, compressor, limiter) == the same stages one sample at a time
  { bool ok = true;
    for (Data d : {Data::Noise, Data::Loud, Data::Mixed}) for (std::size_t n : {1u, 7u, 256u, 257u, 4097u}) {
      am::dsp::Channel ch(fs); ch.setDelayMs(1.3); ch.setGate(true, -50, -60, 1, 50, 120); ch.setCrossover(am::dsp::XoverType::LinkwitzRiley, 4, 80, 2, 12000);
      ch.setEqBand(0, {am::dsp::FilterType::LowShelf, 100, 0.7, 2}); ch.setEqBand(1, {am::dsp::FilterType::Peaking, 400, 1, -3}); ch.setEqBand(2, {am::dsp::FilterType::Peaking, 2500, 1, 2}); ch.setEqBand(3, {am::dsp::FilterType::HighShelf, 8000, 0.7, 1});
      ch.setCompressor(true, -18, 3, 6, 10, 120, 2); ch.setLimiter(true, -3, 50);
      am::dsp::Gate g; g.set(true, -50, -60, 1, 50, 120, fs); am::dsp::Crossover hp, lp; hp.set(true, am::dsp::XoverType::LinkwitzRiley, 4, 80, fs); lp.set(false, am::dsp::XoverType::LinkwitzRiley, 2, 12000, fs);
      am::dsp::Biquad eq[4]; eq[0].design(am::dsp::FilterType::LowShelf, fs, 100, 0.7, 2); eq[1].design(am::dsp::FilterType::Peaking, fs, 400, 1, -3); eq[2].design(am::dsp::FilterType::Peaking, fs, 2500, 1, 2); eq[3].design(am::dsp::FilterType::HighShelf, fs, 8000, 0.7, 1);
      am::dsp::Compressor c; c.set(true, -18, 3, 6, 10, 120, 2, fs); am::dsp::Limiter lim; lim.set(true, -3, 50, fs);
      const double dly = 1.3 * 0.001 * fs; std::vector<double> line; std::vector<float> got, ref;
      for (int rep = 0; rep < 3; ++rep) { std::vector<float> x = make(d, n, static_cast<unsigned>(n + rep * 101)); if (rep == 2 && n > 8) x[3] = 1e-41f;
        for (float s : x) { line.push_back(s); const double rp = static_cast<double>(line.size() - 1) - dly; double v = 0;
          if (rp >= 0) { const std::size_t i0 = static_cast<std::size_t>(rp); const double fr = rp - static_cast<double>(i0); v = line[i0] + (line[i0 + 1] - line[i0]) * fr; }
          else if (rp > -1) v = line[0] * (rp + 1);   // between the ring's last (still silent) slot and the first sample

          v = g.tick(v); v = hp.tick(v); v = lp.tick(v); for (auto& q : eq) v = q.tick(v); v = c.tick(v); v = lim.tick(v); ref.push_back(static_cast<float>(v)); }
        ch.process(x.data(), n); got.insert(got.end(), x.begin(), x.end()); }
      ok = ok && allClose(got, ref, ref.size());
      if (!ok) { std::printf("     %s, %zu frames\n", dataName(d), n); break; }
    }
    expect(ok, L + "whole channel (delay, gate, crossover, EQ, compressor, limiter) == the stages one sample at a time"); }
}
}  // namespace

int main() {
  am::denormalsOff();   // as on the audio thread
  const am::simd::CpuFeatures& f = am::simd::cpu();
  std::printf("simd_test: CPU sse2 %d sse4.1 %d avx %d avx2 %d fma %d avx512f %d neon %d; active %s\n", f.sse2, f.sse41, f.avx, f.avx2, f.fma, f.avx512f, f.neon, am::simd::describe());
  int levels = 0;
  for (am::simd::Level l : {am::simd::Level::Scalar, am::simd::Level::Sse2, am::simd::Level::Neon, am::simd::Level::Avx2}) {
    const am::simd::Kernels* k = am::simd::kernelsFor(l); if (!k) continue; ++levels;
    am::simd::setLevel(l); testLevel(*k);
  }
  expect(levels >= 1, "at least the baseline level runs");
#if defined(__x86_64__) || defined(_M_X64)
  expect(am::simd::kernelsFor(am::simd::Level::Sse2) != nullptr, "x86-64: the SSE2 baseline is always there");
  if (f.avx2 && f.fma) expect(am::simd::kernelsFor(am::simd::Level::Avx2) != nullptr && std::strcmp(am::simd::levelName(), "AVX2+FMA") == 0, "AVX2 + FMA processor: the AVX2 kernels are available");
#endif
  std::printf("     largest relative difference from the scalar code: %.3g\n", worst);
  return failures ? 1 : 0;
}
