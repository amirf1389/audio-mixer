// The engine's SIMD kernels, written once over a small vector type V and compiled once per instruction-set level (simd_base.cpp:
// SSE2 / NEON / scalar, simd_avx2.cpp: AVX2 + FMA). Included inside an anonymous namespace after the file defines V:
//   V::T (L doubles), V::F (L floats), load / store / set, add sub mul div, fma(a, b, c) = a * b + c (one rounding only where the level
//   has FMA), V::M masks, gt(a, b) = a > b ? a : b per lane (NaN in a -> b), absv, masks (cmpgt, cmplt, cmple, sel, any), loadf / storef (float <->
//   double), accf (float add into memory), hsum, the bit helpers expField / mant1 (log2) and trunc / pow2i (exp2), and the scalar
//   fnma(a, b, c) = c - a * b, shiftIn(y, x) = {x, y0, .., y[L-2]}, last(y) = y[L-1], andm, and the scalar sfma(a, b, c) = a * b + c.
// RULES for this file: it may not call any inline function of the engine or of the standard library (in the AVX2 build such a call would
// be compiled with AVX2 and could be picked by the linker for every caller): plain arithmetic, the V helpers and memcpy only.
// Every kernel does what the scalar code it replaces did, in the same order per sample (see each one).

inline double fabs1(double v) { std::uint64_t b; std::memcpy(&b, &v, sizeof b); b &= 0x7fffffffffffffffull; std::memcpy(&v, &b, sizeof v); return v; }

// fastm::log2 / exp2 (dsp.hpp) over L lanes: the same series, the same evaluation order
inline V::T log2v(V::T x) {
  const V::T e = V::sub(V::expField(x), V::set(1023.0)), m = V::mant1(x), one = V::set(1.0);
  const V::T t = V::div(V::sub(m, one), V::add(m, one)), t2 = V::mul(t, t);
  V::T p = V::set(1.0 / 13);
  p = V::fma(t2, p, V::set(1.0 / 11)); p = V::fma(t2, p, V::set(1.0 / 9)); p = V::fma(t2, p, V::set(1.0 / 7)); p = V::fma(t2, p, V::set(1.0 / 5)); p = V::fma(t2, p, V::set(1.0 / 3)); p = V::fma(t2, p, one);
  return V::add(e, V::mul(V::mul(V::set(2.8853900817779268), t), p));
}
inline V::T exp2v(V::T y) {
  const V::M lo = V::cmplt(y, V::set(-1000.0)); y = V::sel(V::cmpgt(y, V::set(1000.0)), V::set(1000.0), y);
  V::T fl = V::trunc(V::sel(lo, V::set(0.0), y)); fl = V::sel(V::cmpgt(fl, y), V::sub(fl, V::set(1.0)), fl);
  const V::T f = V::mul(V::sub(y, fl), V::set(0.6931471805599453));
  V::T p = V::set(1.0 / 362880);
  p = V::fma(f, p, V::set(1.0 / 40320)); p = V::fma(f, p, V::set(1.0 / 5040)); p = V::fma(f, p, V::set(1.0 / 720)); p = V::fma(f, p, V::set(1.0 / 120)); p = V::fma(f, p, V::set(1.0 / 24));
  p = V::fma(f, p, V::set(1.0 / 6)); p = V::fma(f, p, V::set(0.5)); p = V::fma(f, p, V::set(1.0)); p = V::fma(f, p, V::set(1.0));
  return V::sel(lo, V::set(0.0), V::mul(p, V::pow2i(V::sel(lo, V::set(0.0), fl))));
}

// Biquad::tick through each section in turn, L sections at once as a WAVEFRONT: lane j holds section j of a group of L and works on
// the sample j steps behind lane 0, so a step moves every section of the group on by one sample (the input shifts one lane up, lane 0
// takes the next sample, the last lane gives the group's output). A group's state stays in registers for the whole block. The feedback
// is added as (b1 x + z2) - a1 y (a1 y last: the shortest path from one output to the next); a sample still sees every section in order.
void kCascade(double* const* sec, int count, double* x, std::size_t n) {
  if (n == 0) return;
  constexpr std::size_t L = V::L;
  for (int g0 = 0; g0 < count; g0 += static_cast<int>(L)) {
    alignas(64) double c[5][L], z[2][L], lane[L];
    for (std::size_t j = 0; j < L; ++j) {
      lane[j] = static_cast<double>(j);
      if (g0 + static_cast<int>(j) < count) { const double* s = sec[g0 + static_cast<int>(j)]; for (int q = 0; q < 5; ++q) c[q][j] = s[q]; z[0][j] = s[5]; z[1][j] = s[6]; }
      else { c[0][j] = 1; c[1][j] = c[2][j] = c[3][j] = c[4][j] = 0; z[0][j] = z[1][j] = 0; }   // a pass-through section fills the group
    }
    const V::T b0 = V::load(c[0]), b1 = V::load(c[1]), b2 = V::load(c[2]), a1 = V::load(c[3]), a2 = V::load(c[4]), idx = V::load(lane);
    V::T z1 = V::load(z[0]), z2 = V::load(z[1]), y = V::set(0.0);
    const std::size_t end = n + L - 1;   // steps
    auto edge = [&](std::size_t t) {     // a step where some lanes have no sample (the first and the last L - 1): their state is kept
      const V::T in = V::shiftIn(y, t < n ? x[t] : 0.0), yn = V::fma(b0, in, z1);
      const V::M on = V::andm(V::cmple(idx, V::set(static_cast<double>(t))), V::cmple(V::set(static_cast<double>(t) - static_cast<double>(n) + 1.0), idx));
      z1 = V::sel(on, V::fnma(a1, yn, V::fma(b1, in, z2)), z1); z2 = V::sel(on, V::fnma(a2, yn, V::mul(b2, in)), z2); y = yn;
      if (t + 1 >= L) x[t + 1 - L] = V::last(y);
    };
    std::size_t t = 0;
    for (; t + 1 < L && t < end; ++t) edge(t);
    for (; t < n; ++t) {   // every lane has a sample
      const V::T in = V::shiftIn(y, x[t]); y = V::fma(b0, in, z1);
      z1 = V::fnma(a1, y, V::fma(b1, in, z2)); z2 = V::fnma(a2, y, V::mul(b2, in));
      x[t + 1 - L] = V::last(y);
    }
    for (; t < end; ++t) edge(t);
    V::store(z[0], z1); V::store(z[1], z2);
    for (std::size_t j = 0; j < L && g0 + static_cast<int>(j) < count; ++j) { double* s = sec[g0 + static_cast<int>(j)]; s[5] = z[0][j]; s[6] = z[1][j]; }
  }
}

// Compressor::tick for each sample: the peak follower and the gain smoothing are serial (one sample needs the last), the level in dB,
// the static curve and the gain factor are not, so they run L samples at a time.
void kCompressor(simd::CompK* k, double* x, std::size_t n) {
  constexpr std::size_t kRun = 64;   // a multiple of every L
  alignas(64) double lvl[kRun], want[kRun], g[kRun];
  const double det = k->det, kneeLin = k->kneeLin, thr = k->thr, ratio = k->ratio, knee = k->knee, half = knee / 2, slope = 1.0 / ratio - 1.0, twoKnee = 2.0 * knee, att = k->att, rel = k->rel, makeup = k->makeup;
  double env = k->env, gr = k->gr, lastG = 0, lastF = 1;
  for (std::size_t base = 0; base < n; base += kRun) {
    const std::size_t m = n - base < kRun ? n - base : kRun, mv = (m + V::L - 1) / V::L * V::L; double* v = x + base;
    for (std::size_t i = 0; i < m; ++i) { const double a = fabs1(v[i]), fall = env + (a - env) * det; env = a > env ? a : fall; lvl[i] = env + 1e-9; }   // peak follower (both sides computed: no branch)
    for (std::size_t i = m; i < mv; ++i) lvl[i] = 1.0;
    for (std::size_t i = 0; i < mv; i += V::L) {   // want = computeDb(inDb) - inDb above the knee, else 0
      const V::T l = V::load(lvl + i), inDb = V::mul(V::set(6.020599913279624), log2v(l)), over = V::sub(inDb, V::set(thr));
      const V::T t = V::add(over, V::set(half)), soft = V::add(inDb, V::div(V::mul(V::mul(V::set(slope), t), t), V::set(twoKnee)));
      const V::T hard = V::sel(V::cmpgt(over, V::set(0.0)), V::add(V::set(thr), V::div(over, V::set(ratio))), inDb);
      const V::T curve = knee > 0 ? V::sel(V::cmple(V::absv(over), V::set(half)), soft, hard) : hard;
      V::store(want + i, V::sel(V::cmpgt(l, V::set(kneeLin)), V::sub(curve, inDb), V::set(0.0)));
    }
    for (std::size_t i = 0; i < m; ++i) {   // attack / release on the gain, settled = exactly the target
      const double w = want[i], c = w < gr ? att : rel; gr += (w - gr) * c; gr = fabs1(w - gr) < 1e-7 ? w : gr; g[i] = gr + makeup;
    }
    for (std::size_t i = m; i < mv; ++i) g[i] = 0.0;
    for (std::size_t i = 0; i < mv; i += V::L) V::store(g + i, exp2v(V::mul(V::load(g + i), V::set(0.16609640474436813))));   // linOf: the factor
    for (std::size_t i = 0; i < m; ++i) v[i] = v[i] * g[i];
    lastG = gr + makeup; lastF = g[m - 1];
  }
  if (n > 0) { k->gLinAt = lastG; k->gLin = lastF; }   // the factor of the last gain, as tick() leaves it
  k->env = env; k->gr = gr;
}

double kGainPeak(const float* in, std::size_t n, double g, float* out) {
  V::T pk = V::set(0.0); const V::T gv = V::set(g); std::size_t i = 0;
  for (; i + V::L <= n; i += V::L) { const V::T x = V::loadf(in + i); pk = V::gt(V::absv(x), pk); V::storef(out + i, V::mul(x, gv)); }
  alignas(64) double lanes[V::L]; V::store(lanes, pk); double p = 0; for (std::size_t j = 0; j < V::L; ++j) p = lanes[j] > p ? lanes[j] : p;
  for (; i < n; ++i) { const double x = static_cast<double>(in[i]), a = fabs1(x); if (a > p) p = a; out[i] = static_cast<float>(x * g); }
  return p;
}

void kMeter(const float* x, std::size_t n, double clipLin, double* pkOut, double* ssOut, int* runIo, int* maxRunIo) {
  V::T pk = V::set(*pkOut), ss = V::set(0.0); std::size_t i = 0;
  for (; i + V::L <= n; i += V::L) { const V::T v = V::loadf(x + i); pk = V::gt(V::absv(v), pk); ss = V::fma(v, v, ss); }
  alignas(64) double lanes[V::L]; V::store(lanes, pk); double p = *pkOut; for (std::size_t j = 0; j < V::L; ++j) p = lanes[j] > p ? lanes[j] : p;
  double s = V::hsum(ss);
  for (; i < n; ++i) { const double v = static_cast<double>(x[i]), a = fabs1(v); if (a > p) p = a; s = sfma(v, v, s); }
  *pkOut = p; *ssOut += s;
  if (n == 0) return;
  if (!(p >= clipLin)) { *runIo = 0; return; }   // no sample at the clip level (the usual case): the run ends, the longest stays
  int run = *runIo, maxRun = *maxRunIo;
  for (std::size_t j = 0; j < n; ++j) { if (fabs1(static_cast<double>(x[j])) >= clipLin) { if (++run > maxRun) maxRun = run; } else run = 0; }
  *runIo = run; *maxRunIo = maxRun;
}

void kPanAdd(const float* t, std::size_t n, double gl, double gr, float* L, float* R) {
  const V::T a = V::set(gl), b = V::set(gr); std::size_t i = 0;
  for (; i + V::L <= n; i += V::L) { const V::T v = V::loadf(t + i); V::accf(L + i, V::mul(v, a)); V::accf(R + i, V::mul(v, b)); }
  for (; i < n; ++i) { L[i] += static_cast<float>(static_cast<double>(t[i]) * gl); R[i] += static_cast<float>(static_cast<double>(t[i]) * gr); }
}

// TruePeak4x::push for each sample. The 3 interpolated points are FIR filters over the stream, so L output samples are made at once
// from a straight copy of the history (the last 12 samples) and the block; the sums run in the per-sample code's order.
void kTruePeak(double* h, int* wIo, const double* c, const double* x, std::size_t n, double* pk, double* delayed) {
  constexpr std::size_t kTaps = 12, kRun = 256;
  alignas(64) double buf[kTaps + kRun + V::L], out[kRun + V::L];
  const int w = *wIo; for (std::size_t k = 0; k < kTaps; ++k) buf[k] = h[static_cast<std::size_t>(w) + k];   // the window, oldest first
  for (std::size_t base = 0; base < n; base += kRun) {
    const std::size_t m = n - base < kRun ? n - base : kRun, mv = (m + V::L - 1) / V::L * V::L;
    for (std::size_t i = 0; i < m; ++i) buf[kTaps + i] = x[base + i];
    for (std::size_t i = m; i < mv; ++i) buf[kTaps + i] = 0.0;
    for (std::size_t i = 0; i < mv; i += V::L) {   // after pushing sample i the window is buf[i + 1 .. i + 12]
      const double* b = buf + i + 1; V::T p = V::absv(V::load(b + 5));
      for (std::size_t ph = 0; ph < 3; ++ph) {
        const double* cp = c + ph * kTaps; V::T acc = V::mul(V::load(b), V::set(cp[0]));
        for (std::size_t j = 1; j < kTaps; ++j) acc = V::fma(V::load(b + j), V::set(cp[j]), acc);
        p = V::gt(V::absv(acc), p);
      }
      V::store(out + i, p);
    }
    for (std::size_t i = 0; i < m; ++i) { pk[base + i] = out[i]; delayed[base + i] = buf[i + 6]; }
    for (std::size_t k = 0; k < kTaps; ++k) buf[k] = buf[m + k];   // the last 12 samples carry over
  }
  for (std::size_t k = 0; k < kTaps; ++k) h[k] = h[k + kTaps] = buf[k];
  *wIo = 0;
}

const simd::Kernels kTable = {kLevel, kName, kCascade, kCompressor, kGainPeak, kMeter, kPanAdd, kTruePeak};
