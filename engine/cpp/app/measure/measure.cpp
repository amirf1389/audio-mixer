#include "measure/measure.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <sstream>

namespace am::measure {
void fftInPlace(std::complex<double>* x, std::size_t N) {
  constexpr double pi = 3.14159265358979323846;
  for (std::size_t i = 1, j = 0; i < N; ++i) { std::size_t bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(x[i], x[j]); }
  for (std::size_t len = 2; len <= N; len <<= 1) { const double ang = -2 * pi / static_cast<double>(len); const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < N; i += len) { std::complex<double> w(1, 0); for (std::size_t k = 0; k < len / 2; ++k) { const std::complex<double> u = x[i + k], v = x[i + k + len / 2] * w; x[i + k] = u + v; x[i + k + len / 2] = u - v; w *= wl; } } }
}
namespace {
constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;
std::size_t pow2(std::size_t n) { std::size_t p = 1; while (p < n) p <<= 1; return p; }
void fft(std::vector<cd>& x, bool inverse) {   // in place; x.size() a power of two (the inverse by conjugation around the shared forward one)
  const std::size_t N = x.size(); if (inverse) for (auto& v : x) v = std::conj(v);
  fftInPlace(x.data(), N);
  if (inverse) for (auto& v : x) v = std::conj(v) / static_cast<double>(N);
}
double db(double a) { return a > 1e-12 ? 20 * std::log10(a) : -240; }
double dbp(double p) { return p > 1e-24 ? 10 * std::log10(p) : -240; }
std::vector<double> logGrid(int ppo, double fLo, double fHi) { std::vector<double> f; const double step = std::pow(2.0, 1.0 / std::max(1, ppo)); for (double x = fLo; x <= fHi * 1.0000001; x *= step) f.push_back(x); return f; }
// magnitude / phase of a real signal on a log grid, smoothed to the grid's own octave fraction (power average over each point's band)
void spectrumOnGrid(const std::vector<double>& x, double fs, const std::vector<double>& grid, int ppo, std::vector<double>& magDb, std::vector<double>* phaseDeg) {
  const std::size_t N = pow2(std::max<std::size_t>(x.size(), 8)); std::vector<cd> X(N); for (std::size_t i = 0; i < x.size(); ++i) X[i] = x[i]; fft(X, false);
  const double binHz = fs / static_cast<double>(N), half = std::pow(2.0, 0.5 / std::max(1, ppo)); magDb.assign(grid.size(), -240); if (phaseDeg) phaseDeg->assign(grid.size(), 0);
  for (std::size_t g = 0; g < grid.size(); ++g) { const double f = grid[g]; std::size_t k0 = static_cast<std::size_t>(std::floor(f / half / binHz)), k1 = static_cast<std::size_t>(std::ceil(f * half / binHz)); if (f >= fs * 0.5 || k0 > N / 2 - 1) continue;   // a point above half the sample rate (a harmonic of a high tone) has no bin: it stays at -240 dB instead of reading past the spectrum
    k0 = std::max<std::size_t>(1, k0); k1 = std::min(k1, N / 2 - 1); if (k1 < k0) k1 = k0;
    double p = 0; for (std::size_t k = k0; k <= k1; ++k) p += std::norm(X[k]); magDb[g] = dbp(p / static_cast<double>(k1 - k0 + 1));
    if (phaseDeg) { const std::size_t kc = std::min(N / 2 - 1, std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(f / binHz)))); (*phaseDeg)[g] = std::arg(X[kc]) * 180 / kPi; } }
}
double interp(const std::vector<double>& fx, const std::vector<double>& fy, double f) {   // linear in log f
  if (fx.empty()) return 0;
  if (f <= fx.front()) return fy.front();
  if (f >= fx.back()) return fy.back();
  std::size_t i = 1; while (i < fx.size() && fx[i] < f) ++i; const double t = (std::log(f) - std::log(fx[i - 1])) / (std::log(fx[i]) - std::log(fx[i - 1])); return fy[i - 1] + t * (fy[i] - fy[i - 1]);
}
}  // namespace

Sweep makeSweep(double fs, double f1, double f2, double seconds, double amplitude) {
  Sweep s; s.fs = fs; s.f1 = f1; s.f2 = f2; s.seconds = seconds; const std::size_t n = static_cast<std::size_t>(seconds * fs), lead = static_cast<std::size_t>(s.leadSec * fs), tail = static_cast<std::size_t>(s.tailSec * fs);
  const double R = std::log(f2 / f1), K = 2 * kPi * f1 * seconds / R; s.signal.assign(lead + n + tail, 0.f); s.inverse.assign(n, 0.0);
  for (std::size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / fs; double v = std::sin(K * (std::exp(t * R / seconds) - 1));
    const double fadeIn = std::min(1.0, t / 0.02), fadeOut = std::min(1.0, (seconds - t) / 0.02); v *= std::min(fadeIn, fadeOut); s.signal[lead + i] = static_cast<float>(amplitude * v); }
  // the inverse: the sweep reversed with a -6 dB / octave envelope, scaled so sweep * inverse peaks at 1
  for (std::size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / fs; s.inverse[i] = static_cast<double>(s.signal[lead + n - 1 - i]) / amplitude * std::exp(-t * R / seconds); }
  { const std::size_t N = pow2(2 * n); std::vector<cd> A(N), B(N); for (std::size_t i = 0; i < n; ++i) { A[i] = s.signal[lead + i]; B[i] = s.inverse[i]; } fft(A, false); fft(B, false);
    // normalise so that sweep * inverse is 0 dB across the band (median of |S I| between 2 f1 and f2 / 2)
    const double binHz = fs / static_cast<double>(N); std::vector<double> m; for (std::size_t k = static_cast<std::size_t>(2 * f1 / binHz); k < N / 2 && static_cast<double>(k) * binHz < f2 / 2; ++k) m.push_back(std::abs(A[k] * B[k]));
    if (!m.empty()) { std::nth_element(m.begin(), m.begin() + static_cast<long>(m.size() / 2), m.end()); const double med = m[m.size() / 2]; if (med > 0) for (auto& v : s.inverse) v /= med; } }
  return s;
}

Impulse deconvolve(const std::vector<float>& recorded, const Sweep& sweep, int harmonics) {
  Impulse ir; ir.fs = sweep.fs; const std::size_t n = sweep.inverse.size(); if (recorded.empty() || n == 0) return ir;
  const std::size_t N = pow2(recorded.size() + n); std::vector<cd> A(N), B(N); for (std::size_t i = 0; i < recorded.size(); ++i) A[i] = recorded[i]; for (std::size_t i = 0; i < n; ++i) B[i] = sweep.inverse[i]; fft(A, false); fft(B, false); for (std::size_t i = 0; i < N; ++i) A[i] *= B[i]; fft(A, true);
  std::vector<double> full(N); for (std::size_t i = 0; i < N; ++i) full[i] = A[i].real();
  // the linear response peaks at (lead + n - 1) + the acoustic delay; search from there on
  const std::size_t base = static_cast<std::size_t>(sweep.leadSec * sweep.fs) + n - 1; std::size_t pk = base; double best = 0;
  for (std::size_t i = base > 2048 ? base - 2048 : 0; i < std::min(N, base + static_cast<std::size_t>(sweep.fs * 2)); ++i) if (std::fabs(full[i]) > best) { best = std::fabs(full[i]); pk = i; }
  ir.peak = pk; const std::size_t len = std::min(N - pk, static_cast<std::size_t>(sweep.fs * std::min(sweep.tailSec + 1.0, 3.0))); ir.h.assign(full.begin() + static_cast<long>(pk), full.begin() + static_cast<long>(pk + len));
  { const std::size_t np = std::min<std::size_t>(pk, static_cast<std::size_t>(sweep.fs * 0.02)); ir.pre.assign(full.begin() + static_cast<long>(pk - np), full.begin() + static_cast<long>(pk)); }
  const double R = std::log(sweep.f2 / sweep.f1);
  for (int k = 2; k <= harmonics; ++k) {   // the k-th harmonic's impulse sits T ln(k) / R before the linear one; its window ends half-way to the next lower harmonic
    const double dt = sweep.seconds * std::log(static_cast<double>(k)) / R, dtPrev = sweep.seconds * std::log(static_cast<double>(k - 1)) / R; const std::size_t at = static_cast<std::size_t>(std::llround(dt * sweep.fs));
    if (at >= pk) break;
    const std::size_t start = pk - at, win = std::max<std::size_t>(64, static_cast<std::size_t>((dt - dtPrev) * 0.5 * sweep.fs)); std::vector<double> h(full.begin() + static_cast<long>(start > win / 8 ? start - win / 8 : 0), full.begin() + static_cast<long>(std::min(N, start + win)));
    ir.harmonics.push_back(std::move(h)); ir.harmonicAt.push_back(start); }
  return ir;
}

Response response(const Impulse& ir, int ppo, double fLo, double fHi, double windowSec) {
  Response r; if (ir.h.empty()) return r; r.freq = logGrid(ppo, fLo, fHi); const std::size_t n = std::min(ir.h.size(), static_cast<std::size_t>(windowSec * ir.fs)); std::vector<double> w(ir.h.begin(), ir.h.begin() + static_cast<long>(n)); const std::size_t np = ir.pre.size();
  for (std::size_t i = n / 2; i < n; ++i) w[i] *= 0.5 + 0.5 * std::cos(kPi * static_cast<double>(i - n / 2) / static_cast<double>(n - n / 2));   // half Hann tail
  if (np) { for (std::size_t i = 0; i < np; ++i) { const double g = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / static_cast<double>(np)); w.insert(w.begin() + static_cast<long>(i), ir.pre[i] * g); } }   // the pre-ringing, faded in
  spectrumOnGrid(w, ir.fs, r.freq, ppo, r.magDb, &r.phaseDeg);
  if (np) for (std::size_t i = 0; i < r.freq.size(); ++i) { double ph = r.phaseDeg[i] + 360.0 * r.freq[i] * static_cast<double>(np) / ir.fs; ph = std::fmod(ph + 180.0, 360.0); if (ph < 0) ph += 360.0; r.phaseDeg[i] = ph - 180.0; }   // take the pre-roll's delay out of the phase
  return r;
}
std::vector<double> thdPercent(const Impulse& ir, const std::vector<double>& freq) {
  std::vector<double> out(freq.size(), 0.0); if (ir.h.empty() || ir.harmonics.empty()) return out;
  std::vector<double> fund; { std::vector<double> ph; std::vector<double> grid; for (double f : freq) for (int k = 1; k <= static_cast<int>(ir.harmonics.size()) + 1; ++k) grid.push_back(f * k); std::sort(grid.begin(), grid.end()); grid.erase(std::unique(grid.begin(), grid.end()), grid.end());
    const std::size_t n = std::min(ir.h.size(), static_cast<std::size_t>(0.1 * ir.fs)); std::vector<double> w(ir.h.begin(), ir.h.begin() + static_cast<long>(n)); std::vector<double> m1; spectrumOnGrid(w, ir.fs, grid, 12, m1, nullptr);
    std::vector<std::vector<double>> mk; for (const auto& h : ir.harmonics) { std::vector<double> m; spectrumOnGrid(h, ir.fs, grid, 12, m, nullptr); mk.push_back(m); }
    for (std::size_t g = 0; g < freq.size(); ++g) { const double f = freq[g]; const double a1 = std::pow(10.0, interp(grid, m1, f) / 20); if (a1 <= 1e-9) continue; double sum = 0;
      for (std::size_t k = 0; k < mk.size(); ++k) { const double fk = f * static_cast<double>(k + 2); if (fk > ir.fs * 0.45) break; const double ak = std::pow(10.0, interp(grid, mk[k], fk) / 20); sum += ak * ak; }
      out[g] = 100.0 * std::sqrt(sum) / a1; } }
  return out;
}
bool parseCalibration(const std::string& text, Calibration& c) {
  c = Calibration(); std::istringstream in(text); std::string line;
  while (std::getline(in, line)) { std::size_t p = line.find_first_not_of(" \t\r"); if (p == std::string::npos) continue; const char ch = line[p]; if (ch == '#' || ch == '*' || ch == ';' || ch == '"') continue; for (auto& x : line) if (x == ',' || x == '\t') x = ' ';
    double f = 0, d = 0; if (std::sscanf(line.c_str(), "%lf %lf", &f, &d) == 2 && f > 0) { c.freq.push_back(f); c.db.push_back(d); } }
  return c.freq.size() >= 2;
}
void applyCalibration(Response& r, const Calibration& c) { if (c.freq.size() < 2) return; for (std::size_t i = 0; i < r.freq.size(); ++i) r.magDb[i] -= interp(c.freq, c.db, r.freq[i]); }
void applySplOffset(Response& r, double offsetDb) { for (auto& m : r.magDb) m += offsetDb; }

std::vector<float> tone(double fs, double freq, double seconds, double amplitude) { std::vector<float> v(static_cast<std::size_t>(fs * seconds)); for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(amplitude * std::sin(2 * kPi * freq * static_cast<double>(i) / fs)); return v; }
std::vector<float> smpteTones(double fs, double seconds, double amplitude) { std::vector<float> v(static_cast<std::size_t>(fs * seconds)); for (std::size_t i = 0; i < v.size(); ++i) { const double t = static_cast<double>(i) / fs; v[i] = static_cast<float>(amplitude * (0.8 * std::sin(2 * kPi * 60 * t) + 0.2 * std::sin(2 * kPi * 7000 * t))); } return v; }
namespace { double goertzel(const std::vector<float>& x, std::size_t from, std::size_t n, double fs, double f) { const double w = 2 * kPi * f / fs, c = 2 * std::cos(w); double s0 = 0, s1 = 0, s2 = 0; for (std::size_t i = 0; i < n; ++i) { const double win = 0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / static_cast<double>(n)); s0 = x[from + i] * win + c * s1 - s2; s2 = s1; s1 = s0; } const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w); return 2 * std::sqrt(re * re + im * im) / (0.5 * static_cast<double>(n)); } }
Stepped analyseTone(const std::vector<float>& x, double fs, double freq) {
  Stepped s; s.freq = freq; if (x.size() < 1024) return s; const std::size_t n = std::min(x.size() - x.size() / 10, static_cast<std::size_t>(fs)), from = x.size() / 10 < x.size() - n ? x.size() / 10 : 0;
  const double a1 = goertzel(x, from, n, fs, freq); double h = 0; for (int k = 2; k <= 5; ++k) { if (freq * k > fs * 0.45) break; const double ak = goertzel(x, from, n, fs, freq * k); h += ak * ak; }
  double tot = 0; for (std::size_t i = 0; i < n; ++i) { const double win = 0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / static_cast<double>(n)); const double v = x[from + i] * win; tot += v * v; } tot /= 0.375 * static_cast<double>(n);   // Hann power correction: the mean square of x
  s.amplitude = a1; s.thdPercent = a1 > 1e-9 ? 100 * std::sqrt(h) / a1 : 0;
  const double rest = std::max(0.0, tot - a1 * a1 / 2);   // everything but the fundamental (harmonics + noise), as power
  s.thdnPercent = a1 > 1e-9 ? 100 * std::sqrt(2 * rest) / a1 : 0; return s;
}
double analyseSmpte(const std::vector<float>& x, double fs) {
  if (x.size() < 4096) return 0;
  const std::size_t n = std::min(x.size(), static_cast<std::size_t>(fs)); const double c = goertzel(x, 0, n, fs, 7000);
  if (c <= 1e-9) return 0;
  double s = 0;
  for (int k = 1; k <= 3; ++k) { const double a = goertzel(x, 0, n, fs, 7000 + 60 * k), b = goertzel(x, 0, n, fs, 7000 - 60 * k); s += a * a + b * b; } return 100 * std::sqrt(s) / c;
}

Rta RtaAverager::analyse(const float* x, std::size_t n, double fs, const RtaOptions& o) {
  Rta r; r.freq = logGrid(o.bandsPerOctave, o.fLo, o.fHi); const std::size_t N = pow2(std::max<std::size_t>(o.fftSize, 1024)); std::vector<cd> X(N); const std::size_t m = std::min(n, N);
  for (std::size_t i = 0; i < m; ++i) { const double w = 0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / static_cast<double>(m)); X[i] = x[n - m + i] * w; } fft(X, false);
  const double binHz = fs / static_cast<double>(N), half = std::pow(2.0, 0.5 / o.bandsPerOctave), scale = 2.0 / (0.5 * static_cast<double>(m)); if (acc_.size() != r.freq.size()) acc_.assign(r.freq.size(), 0.0);
  r.db.resize(r.freq.size());
  for (std::size_t g = 0; g < r.freq.size(); ++g) { const double f = r.freq[g]; std::size_t k0 = std::max<std::size_t>(1, static_cast<std::size_t>(std::floor(f / half / binHz))), k1 = std::min(N / 2 - 1, static_cast<std::size_t>(std::ceil(f * half / binHz))); if (k1 < k0) k1 = k0;
    double p = 0; for (std::size_t k = k0; k <= k1; ++k) { const double a = std::abs(X[k]) * scale; p += a * a; }
    acc_[g] = o.averaging > 0 ? acc_[g] * o.averaging + p * (1 - o.averaging) : p; r.db[g] = dbp(acc_[g]); }
  return r;
}
double RtaAverager::alignTo(double referenceDb) const { double pk = -240; for (double p : acc_) pk = std::max(pk, dbp(p)); return pk > -200 ? referenceDb - pk : 0; }

Decay decay(const Impulse& ir, double seconds) {
  Decay d; if (ir.h.empty()) return d; const std::size_t n = std::min(ir.h.size(), static_cast<std::size_t>(seconds * ir.fs)); d.seconds = static_cast<double>(n) / ir.fs; std::vector<double> e(n); double pk = 0; for (std::size_t i = 0; i < n; ++i) { e[i] = ir.h[i] * ir.h[i]; pk = std::max(pk, e[i]); } if (pk <= 0) return d;
  d.etcDb.resize(n); for (std::size_t i = 0; i < n; ++i) d.etcDb[i] = dbp(e[i] / pk);
  std::vector<double> s(n); double acc = 0; for (std::size_t i = n; i-- > 0;) { acc += e[i]; s[i] = acc; } d.schroederDb.resize(n); for (std::size_t i = 0; i < n; ++i) d.schroederDb[i] = dbp(s[i] / s[0]);
  auto fit = [&](double a, double b, double& r2) {   // least squares over the range [a, b] dB below the start; returns the RT60 the slope implies
    std::size_t i0 = 0, i1 = 0; while (i0 < n && d.schroederDb[i0] > -a) ++i0; i1 = i0; while (i1 < n && d.schroederDb[i1] > -b) ++i1; if (i1 <= i0 + 10 || i1 >= n) { r2 = 0; return 0.0; }
    double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0; const double m = static_cast<double>(i1 - i0); for (std::size_t i = i0; i < i1; ++i) { const double x = static_cast<double>(i) / ir.fs, y = d.schroederDb[i]; sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y; }
    const double den = m * sxx - sx * sx; if (std::fabs(den) < 1e-12) { r2 = 0; return 0.0; } const double slope = (m * sxy - sx * sy) / den; const double ssTot = syy - sy * sy / m, ssRes = ssTot - slope * (sxy - sx * sy / m); r2 = ssTot > 0 ? 1 - ssRes / ssTot : 0; return slope < 0 ? -60.0 / slope : 0.0; };
  double r20, r30; d.t20 = fit(5, 25, r20); d.t30 = fit(5, 35, r30); double bestR = 0; for (double b : {15.0, 20.0, 25.0, 30.0, 35.0, 40.0, 45.0}) { double r; const double t = fit(5, b, r); if (t > 0 && r > bestR) { bestR = r; d.topt = t; d.toptRange = b; } }
  return d;
}
double delaySeconds(const std::vector<float>& rec, const std::vector<float>& ref, double fs, double maxSeconds) {
  if (rec.empty() || ref.empty()) return 0;
  const std::size_t N = pow2(rec.size() + ref.size()); std::vector<cd> A(N), B(N); for (std::size_t i = 0; i < rec.size(); ++i) A[i] = rec[i]; for (std::size_t i = 0; i < ref.size(); ++i) B[i] = ref[i]; fft(A, false); fft(B, false); for (std::size_t i = 0; i < N; ++i) A[i] *= std::conj(B[i]); fft(A, true);
  const std::size_t lim = std::min(N, static_cast<std::size_t>(maxSeconds * fs)); std::size_t best = 0; double bv = -1; for (std::size_t i = 0; i < lim; ++i) if (A[i].real() > bv) { bv = A[i].real(); best = i; } return static_cast<double>(best) / fs;
}

Grid waterfall(const Impulse& ir, int slices, double windowSec, double stepSec, int ppo, double fLo, double fHi) {
  Grid g; if (ir.h.empty()) return g; g.x = logGrid(ppo, fLo, fHi); const std::size_t win = static_cast<std::size_t>(windowSec * ir.fs), step = static_cast<std::size_t>(stepSec * ir.fs);
  for (int s = 0; s < slices; ++s) { const std::size_t from = static_cast<std::size_t>(s) * step; if (from + 64 >= ir.h.size()) break; const std::size_t n = std::min(win, ir.h.size() - from); std::vector<double> w(n); for (std::size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / static_cast<double>(n); w[i] = ir.h[from + i] * (t < 0.5 ? 1.0 : 0.5 + 0.5 * std::cos(kPi * (t - 0.5) / 0.5)); }   // rising window: flat then a half cosine
    std::vector<double> m; spectrumOnGrid(w, ir.fs, g.x, ppo, m, nullptr); g.y.push_back(static_cast<double>(from) / ir.fs); g.db.push_back(m); }
  return g;
}
Grid spectrogram(const float* x, std::size_t n, double fs, std::size_t fftSize, std::size_t hop, int ppo, double fLo, double fHi) {
  Grid g; g.x = logGrid(ppo, fLo, fHi); const std::size_t N = pow2(fftSize); for (std::size_t from = 0; from + N <= n; from += hop) { std::vector<double> w(N); for (std::size_t i = 0; i < N; ++i) w[i] = x[from + i] * (0.5 - 0.5 * std::cos(2 * kPi * static_cast<double>(i) / static_cast<double>(N))); std::vector<double> m; spectrumOnGrid(w, fs, g.x, ppo, m, nullptr); g.y.push_back(static_cast<double>(from) / fs); g.db.push_back(m); }
  return g;
}

std::vector<Mode> roomModes(const Room& r, double fMax, double c) {
  std::vector<Mode> out; const int maxN = 12;
  for (int nx = 0; nx <= maxN; ++nx) for (int ny = 0; ny <= maxN; ++ny) for (int nz = 0; nz <= maxN; ++nz) { if (nx + ny + nz == 0) continue; const double f = c / 2 * std::sqrt(std::pow(nx / r.L, 2) + std::pow(ny / r.W, 2) + std::pow(nz / r.H, 2)); if (f > fMax) continue; const int k = (nx > 0) + (ny > 0) + (nz > 0); out.push_back({nx, ny, nz, f, k == 1 ? "axial" : k == 2 ? "tangential" : "oblique"}); }
  std::sort(out.begin(), out.end(), [](const Mode& a, const Mode& b) { return a.freq < b.freq; }); return out;
}
Response roomResponse(const Room& r, int ppo, double fLo, double fHi, double c) {
  Response out; out.freq = logGrid(ppo, fLo, fHi); out.magDb.assign(out.freq.size(), 0); out.phaseDeg.assign(out.freq.size(), 0);
  const auto modes = roomModes(r, fHi * 1.5, c); const double delta = 6.91 / std::max(0.05, r.rt60);   // modal damping from the decay time
  auto shape = [](int n, double x, double L) { return std::cos(n * kPi * x / L); };
  // each mode's weight (source and listener coupling) and its squared angular frequency do not depend on the frequency: worked out once
  std::vector<double> weight(modes.size()), wm2(modes.size());
  for (std::size_t k = 0; k < modes.size(); ++k) { const Mode& m = modes[k]; const double wm = 2 * kPi * m.freq; wm2[k] = wm * wm;
    const double src = shape(m.nx, r.sx, r.L) * shape(m.ny, r.sy, r.W) * shape(m.nz, r.sz, r.H), lis = shape(m.nx, r.lx, r.L) * shape(m.ny, r.ly, r.W) * shape(m.nz, r.lz, r.H);
    weight[k] = src * lis * (m.nx ? 2 : 1) * (m.ny ? 2 : 1) * (m.nz ? 2 : 1); }
  for (std::size_t g = 0; g < out.freq.size(); ++g) { const double f = out.freq[g], w = 2 * kPi * f, im = 2 * delta * w; double re = 0, ii = 0;
    for (std::size_t k = 0; k < modes.size(); ++k) { const double a = wm2[k] - w * w, d = a * a + im * im; re += weight[k] * a / d; ii -= weight[k] * im / d; }   // weight / (a + i im)
    const cd p(re, ii); out.magDb[g] = db(std::abs(p) * 1e4); out.phaseDeg[g] = std::arg(p) * 180 / kPi; }
  double mean = 0; for (double m : out.magDb) mean += m; mean /= static_cast<double>(std::max<std::size_t>(1, out.magDb.size())); for (double& m : out.magDb) m -= mean; return out;
}

double peakingDbAt(const Filter& f, double freq, double fs) {   // RBJ peaking biquad, |H(e^jw)|
  const double A = std::pow(10.0, f.gainDb / 40), w0 = 2 * kPi * f.freq / fs, alpha = std::sin(w0) / (2 * f.q), cw = std::cos(w0);
  const double b0 = 1 + alpha * A, b1 = -2 * cw, b2 = 1 - alpha * A, a0 = 1 + alpha / A, a1 = -2 * cw, a2 = 1 - alpha / A; const double w = 2 * kPi * freq / fs; const cd z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2 * w);
  const cd num = cd(b0, 0) + cd(b1, 0) * z1 + cd(b2, 0) * z2, den = cd(a0, 0) + cd(a1, 0) * z1 + cd(a2, 0) * z2; return db(std::abs(num / den));
}
Response flatTarget(const Response& like, double levelDb) { Response t; t.freq = like.freq; t.magDb.assign(like.freq.size(), levelDb); t.phaseDeg.assign(like.freq.size(), 0); return t; }
Response houseTarget(const Response& like, double levelDb, double bassLiftDb, double kneeHz) { Response t = flatTarget(like, levelDb); for (std::size_t i = 0; i < t.freq.size(); ++i) t.magDb[i] += bassLiftDb / (1 + std::pow(t.freq[i] / kneeHz, 2)); return t; }
Response applyFilters(const Response& r, const std::vector<Filter>& fs) { Response o = r; for (std::size_t i = 0; i < o.freq.size(); ++i) for (const Filter& f : fs) o.magDb[i] += peakingDbAt(f, o.freq[i]); return o; }
double rmsErrorDb(const Response& a, const Response& b, double fLo, double fHi) { double s = 0; int n = 0; for (std::size_t i = 0; i < a.freq.size() && i < b.freq.size(); ++i) if (a.freq[i] >= fLo && a.freq[i] <= fHi) { const double d = a.magDb[i] - b.magDb[i]; s += d * d; ++n; } return n ? std::sqrt(s / n) : 0; }
std::vector<Filter> autoEq(const Response& measured, const Response& target, const AutoEqOptions& o) {
  std::vector<Filter> out; Response cur = measured;
  for (int it = 0; it < o.maxFilters; ++it) {
    std::size_t worst = 0; double worstDev = 0; for (std::size_t i = 0; i < cur.freq.size(); ++i) { if (cur.freq[i] < o.fLo || cur.freq[i] > o.fHi) continue; const double d = cur.magDb[i] - target.magDb[i]; if (std::fabs(d) > std::fabs(worstDev)) { worstDev = d; worst = i; } }
    if (std::fabs(worstDev) < o.minDb) break;
    // the bandwidth of the deviation: where it falls to half its depth on each side
    std::size_t lo = worst, hi = worst; while (lo > 0 && cur.freq[lo] > o.fLo && std::fabs(cur.magDb[lo] - target.magDb[lo]) > std::fabs(worstDev) * 0.5 && (cur.magDb[lo] - target.magDb[lo]) * worstDev > 0) --lo; while (hi + 1 < cur.freq.size() && cur.freq[hi] < o.fHi && std::fabs(cur.magDb[hi] - target.magDb[hi]) > std::fabs(worstDev) * 0.5 && (cur.magDb[hi] - target.magDb[hi]) * worstDev > 0) ++hi;
    const double bw = std::max(0.05, std::log2(cur.freq[hi] / cur.freq[lo])); double q = 1.0 / (2 * std::sinh(std::log(2.0) / 2 * bw)); q = std::max(o.qMin, std::min(o.qMax, q));
    double gain = -worstDev * 0.85; gain = std::max(-o.maxCutDb, std::min(o.maxBoostDb, gain)); if (std::fabs(gain) < 0.1) break;
    const Filter f{cur.freq[worst], q, gain}; out.push_back(f); cur = applyFilters(cur, {f});
  }
  return out;
}
std::vector<Filter> monitorAutoEq(const double* bandDb, int bands, int maxFilters, double maxBoostDb, double maxCutDb) {
  if (!bandDb || bands < 3 || maxFilters <= 0) return {};
  double mean = 0; int n = 0; for (int k = 0; k < bands; ++k) if (bandDb[k] > -90) { mean += bandDb[k]; ++n; }
  if (n < 3) return {};
  mean /= n;
  Response m; int first = -1, last = -1; for (int k = 0; k < bands; ++k) if (bandDb[k] > -90) { if (first < 0) first = k; last = k; }
  constexpr int ppo = 12;
  for (int i = first * ppo; i <= last * ppo; ++i) {   // between band centres: linear in dB over log frequency (a band that is silent is bridged)
    const double pos = static_cast<double>(i) / ppo; int k0 = static_cast<int>(pos); if (k0 >= last) k0 = last;
    int a = k0; while (a > first && bandDb[a] <= -90) --a; int b = std::min(last, k0 + 1); while (b < last && bandDb[b] <= -90) ++b;
    const double da = bandDb[a] > -90 ? bandDb[a] : mean, db = bandDb[b] > -90 ? bandDb[b] : mean;
    const double t = b > a ? std::max(0.0, std::min(1.0, (pos - a) / (b - a))) : 0;
    m.freq.push_back(31.25 * std::pow(2.0, pos)); m.magDb.push_back(da + (db - da) * t); m.phaseDeg.push_back(0);
  }
  AutoEqOptions o; o.maxFilters = maxFilters; o.fLo = m.freq.front(); o.fHi = m.freq.back(); o.maxBoostDb = maxBoostDb; o.maxCutDb = maxCutDb; o.minDb = 1.5; o.qMin = 0.5; o.qMax = 3;
  return autoEq(m, flatTarget(m, mean), o);
}
std::vector<Filter> roomCorrection(const Room& r, int maxFilters, double maxCutDb, double fHi) {
  const double hi = std::max(40.0, std::min(400.0, fHi)), rt = std::max(0.1, std::min(5.0, r.rt60)), cutMax = std::max(1.0, std::min(24.0, maxCutDb));
  const int nMax = std::max(1, std::min(10, maxFilters));
  Response cur = roomResponse(r, 48, 20, 400);
  double mean = 0; int n = 0; for (std::size_t i = 0; i < cur.freq.size(); ++i) if (cur.freq[i] <= hi) { mean += cur.magDb[i]; ++n; } mean = n ? mean / n : 0;
  const double modeBw = 2.2 / rt;   // a mode's half-power bandwidth in Hz: a cut is never narrower than the resonance it tames
  std::vector<Filter> out;
  for (int it = 0; it < nMax; ++it) {
    std::size_t worst = 0; double over = 0;   // the highest PEAK above the mean (nulls are left alone: EQ cannot fill a cancellation)
    for (std::size_t i = 0; i < cur.freq.size(); ++i) if (cur.freq[i] >= 20 && cur.freq[i] <= hi && cur.magDb[i] - mean > over) { over = cur.magDb[i] - mean; worst = i; }
    if (over < 1.5) break;
    std::size_t lo = worst, up = worst;   // where the peak falls to half its height on each side
    while (lo > 0 && cur.freq[lo] > 20 && cur.magDb[lo] - mean > over * 0.5) --lo;
    while (up + 1 < cur.freq.size() && cur.freq[up] < hi && cur.magDb[up] - mean > over * 0.5) ++up;
    const double bw = std::max(0.05, std::log2(cur.freq[up] / cur.freq[lo]));
    double q = 1.0 / (2 * std::sinh(std::log(2.0) / 2 * bw)); q = std::max(0.7, std::min({16.0, q, cur.freq[worst] / modeBw}));
    const Filter f{cur.freq[worst], q, -std::min(cutMax, over * 0.85)};
    if (f.gainDb > -0.1) break;
    out.push_back(f); cur = applyFilters(cur, {f});
  }
  return out;
}
std::string formatCalibration(const Calibration& c, const std::string& note) {
  std::string s = "# AmF X1 microphone calibration" + (note.empty() ? std::string() : " · " + note) + "\n# frequency (Hz)  deviation (dB)\n";
  for (std::size_t i = 0; i < c.freq.size() && i < c.db.size(); ++i) { char b[64]; std::snprintf(b, sizeof b, "%.3f\t%.3f\n", c.freq[i], c.db[i]); s += b; }
  return s;
}
std::string exportFiltersText(const std::vector<Filter>& f) { std::string s = "Filter Settings file\n\nRoom EQ V5.20\nDated: AmF X1 ACOUSTICS auto EQ\n\nNotes: parametric filters, peaking, for miniDSP / Equalizer APO / any PEQ\n\nEqualiser: Generic\n"; for (std::size_t i = 0; i < f.size(); ++i) { char b[128]; std::snprintf(b, sizeof b, "Filter %2zu: ON  PK       Fc %8.2f Hz  Gain %6.2f dB  Q %6.3f\n", i + 1, f[i].freq, f[i].gainDb, f[i].q); s += b; } return s; }
std::string exportFiltersJson(const std::vector<Filter>& f) { std::string s = "{\"type\":\"peaking\",\"filters\":["; for (std::size_t i = 0; i < f.size(); ++i) { char b[128]; std::snprintf(b, sizeof b, "%s{\"freq\":%.2f,\"q\":%.3f,\"gainDb\":%.2f}", i ? "," : "", f[i].freq, f[i].q, f[i].gainDb); s += b; } return s + "]}"; }

}  // namespace am::measure
