#include "analyze/file_analyze.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

#include "arrange/decode.hpp"

namespace am::fa {
namespace {
constexpr double kPi = 3.14159265358979323846;
double db(double lin) { return lin > 1e-6 ? 20.0 * std::log10(lin) : -120.0; }
double dbPow(double p) { return p > 1e-12 ? 10.0 * std::log10(p) : -120.0; }
std::string fmt(const char* f, double a, double b = 0, double c = 0) { char s[200]; std::snprintf(s, sizeof s, f, a, b, c); return s; }

struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
// BS.1770 K-weighting for any rate (the published 48 kHz coefficients, re-derived from their analog prototypes)
void kWeight(double fs, Biquad& shelf, Biquad& hp) {
  { const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196, K = std::tan(kPi * f0 / fs), Vh = std::pow(10.0, G / 20), Vb = std::pow(Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
    shelf.b0 = (Vh + Vb * K / Q + K * K) / a0; shelf.b1 = 2 * (K * K - Vh) / a0; shelf.b2 = (Vh - Vb * K / Q + K * K) / a0; shelf.a1 = 2 * (K * K - 1) / a0; shelf.a2 = (1 - K / Q + K * K) / a0; }
  { const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan(kPi * f0 / fs), a0 = 1 + K / Q + K * K;
    hp.b0 = 1; hp.b1 = -2; hp.b2 = 1; hp.a1 = 2 * (K * K - 1) / a0; hp.a2 = (1 - K / Q + K * K) / a0; }
}
// the K-weighted square of both channels summed, per 100 ms step (blocks and windows are sums of these)
std::vector<double> kSteps(const std::vector<float>& L, const std::vector<float>& R, double fs, bool mono, std::size_t& step) {
  step = static_cast<std::size_t>(std::max(1.0, std::round(fs / 10))); const std::size_t n = std::min(L.size(), R.size());
  Biquad s[2], h[2]; for (int c = 0; c < 2; ++c) kWeight(fs, s[c], h[c]);
  std::vector<double> out(n / step, 0.0);
  for (std::size_t i = 0; i < out.size() * step; ++i) {
    const double l = h[0].run(s[0].run(L[i])); double e = l * l;
    if (!mono) { const double r = h[1].run(s[1].run(R[i])); e += r * r; }
    out[i / step] += e;
  }
  return out;
}
double lufsOf(double ms) { return ms > 1e-12 ? -0.691 + 10.0 * std::log10(ms) : -120.0; }
// blocks of `len` steps, hop of `hop` steps: mean square of each
std::vector<double> blocks(const std::vector<double>& st, std::size_t step, int len, int hop) {
  std::vector<double> b; if (st.size() < static_cast<std::size_t>(len)) { if (!st.empty()) { double s = 0; for (double v : st) s += v; b.push_back(s / static_cast<double>(st.size() * step)); } return b; }
  for (std::size_t i = 0; i + len <= st.size(); i += hop) { double s = 0; for (int k = 0; k < len; ++k) s += st[i + k]; b.push_back(s / static_cast<double>(step * len)); }
  return b;
}
double gatedIntegrated(const std::vector<double>& b) {
  double s = 0; int n = 0; for (double v : b) if (lufsOf(v) > -70) { s += v; ++n; } if (!n) return -120;
  const double rel = lufsOf(s / n) - 10; s = 0; n = 0; for (double v : b) if (lufsOf(v) > -70 && lufsOf(v) > rel) { s += v; ++n; }
  return n ? lufsOf(s / n) : -120;
}
}  // namespace

double kWeightedLufs(const std::vector<float>& L, const std::vector<float>& R, double rate, bool mono) {
  std::size_t step = 1; const auto st = kSteps(L, R, rate, mono, step); return gatedIntegrated(blocks(st, step, 4, 1));
}

double truePeak(const std::vector<float>& x) {
  // 4x polyphase interpolation (12 taps per phase, Hann-windowed sinc), evaluated only near the loud samples: an inter-sample peak more than
  // 6 dB over both neighbours does not occur in real programme, so the quiet parts are skipped (the cost of a long file stays low)
  constexpr int T = 12, P = 4; static double h[P][T]; static bool init = false;
  if (!init) { for (int p = 0; p < P; ++p) for (int t = 0; t < T; ++t) { const double k = (T / 2 - 1) - t + static_cast<double>(p) / P; const double s = std::fabs(k) < 1e-12 ? 1 : std::sin(kPi * k) / (kPi * k);
      const double w = 0.5 + 0.5 * std::cos(kPi * k / (T / 2 + 1)); h[p][t] = s * w; } init = true; }
  double sp = 0; for (float v : x) sp = std::max(sp, static_cast<double>(std::fabs(v)));
  double tp = sp; const double near = sp * 0.5; const long n = static_cast<long>(x.size());
  for (long i = 0; i + 1 < n; ++i) {
    if (std::fabs(x[i]) < near && std::fabs(x[i + 1]) < near) continue;
    for (int p = 1; p < P; ++p) { double y = 0; for (int t = 0; t < T; ++t) { const long j = i - (T / 2 - 1) + t; if (j >= 0 && j < n) y += h[p][t] * x[j]; } tp = std::max(tp, std::fabs(y)); }
  }
  return tp;
}

Analysis analyse(const std::vector<float>& L, const std::vector<float>& R, double rate, bool mono) {
  Analysis a; const std::size_t n = std::min(L.size(), R.size());
  if (n == 0 || rate <= 0) { a.error = "no audio"; return a; }
  a.rate = rate; a.seconds = static_cast<double>(n) / rate;
  // levels, peaks, clipping (3 samples or more in a row at full scale)
  double ss[2] = {0, 0}; for (int c = 0; c < 2; ++c) { const auto& x = c ? R : L; double pk = 0; int run = 0;
    for (std::size_t i = 0; i < n; ++i) { const double v = std::fabs(x[i]); pk = std::max(pk, v); ss[c] += v * v; if (v >= 0.9999) { if (++run == 3) ++a.clippedRuns; } else run = 0; }
    a.ch[c].peakDb = db(pk); a.ch[c].rmsDb = dbPow(ss[c] / n); a.ch[c].truePeakDb = db(truePeak(x)); if (mono) break; }
  if (mono) { a.ch[1] = a.ch[0]; ss[1] = ss[0]; }
  a.peakDb = std::max(a.ch[0].peakDb, a.ch[1].peakDb); a.truePeakDb = std::max(a.ch[0].truePeakDb, a.ch[1].truePeakDb);
  a.rmsDb = dbPow((ss[0] + ss[1]) / (2.0 * n)); a.crestDb = a.peakDb - a.rmsDb;
  // loudness and loudness range
  std::size_t step = 1; const auto st = kSteps(L, R, rate, mono, step);
  a.lufs = gatedIntegrated(blocks(st, step, 4, 1));
  { auto w = blocks(st, step, 30, 10); std::vector<double> g; for (double v : w) if (lufsOf(v) > -70) g.push_back(v);
    double s = 0; for (double v : g) s += v; const double rel = g.empty() ? -120 : lufsOf(s / g.size()) - 20; std::vector<double> l;
    for (double v : g) if (lufsOf(v) > rel) l.push_back(lufsOf(v)); std::sort(l.begin(), l.end());
    if (l.size() >= 2) { const auto q = [&](double p) { return l[static_cast<std::size_t>(std::round(p * (l.size() - 1)))]; }; a.lra = q(0.95) - q(0.10); } }
  // octave bands: Welch average (8192-point Hann) of the mid signal; at most 400 frames, spread over the whole file
  { constexpr std::size_t N = 8192; std::vector<double> pw(N / 2, 0.0), win(N); for (std::size_t i = 0; i < N; ++i) win[i] = 0.5 - 0.5 * std::cos(2 * kPi * i / N);
    const std::size_t frames = n >= N ? std::min<std::size_t>(400, (n - N) / (N / 2) + 1) : 1; const std::size_t hop = frames > 1 ? (n - N) / (frames - 1) : 0;
    std::vector<std::complex<double>> b(N);
    for (std::size_t f = 0; f < frames; ++f) { const std::size_t o = f * hop; for (std::size_t i = 0; i < N; ++i) { const std::size_t j = o + i; const double m = j < n ? 0.5 * (L[j] + R[j]) : 0.0; b[i] = {m * win[i], 0}; }
      am::measure::fftInPlace(b.data(), N); for (std::size_t k = 1; k < N / 2; ++k) pw[k] += std::norm(b[k]); }
    double tot = 0; for (double v : pw) tot += v; const double ms = (ss[0] + ss[1]) / (2.0 * n);
    double sum = 0; int used = 0;
    for (int k = 0; k < kBands; ++k) { const double fc = 31.25 * std::pow(2.0, k), lo = fc / std::sqrt(2.0), hi = std::min(fc * std::sqrt(2.0), rate / 2);
      double e = 0; for (std::size_t i = 1; i < N / 2; ++i) { const double f = i * rate / N; if (f >= lo && f < hi) e += pw[i]; }
      a.bandDb[k] = tot > 0 ? dbPow(ms * e / tot) : -120; if (a.bandDb[k] > -90) { sum += a.bandDb[k]; ++used; } }
    const double mean = used ? sum / used : 0; for (int k = 0; k < kBands; ++k) a.balanceDb[k] = a.bandDb[k] > -90 ? a.bandDb[k] - mean : 0; }
  a.ok = true; return a;
}

Analysis analyseFile(const std::string& path) {
  am::arr::Audio au; std::string err; Analysis a;
  if (!am::arr::readAudio(path, au, err)) { a.error = err.empty() ? "cannot read the file" : err; a.path = path; return a; }
  bool mono = au.L.size() == au.R.size(); if (mono) for (std::size_t i = 0; i < au.L.size(); ++i) if (au.L[i] != au.R[i]) { mono = false; break; }
  a = analyse(au.L, au.R, au.rate, mono); a.path = path; return a;
}

Setup suggest(const Analysis& a, const std::string& target, double customLufs) {
  Setup s; s.targetLufs = target == "broadcast" ? -23 : target == "custom" ? std::clamp(customLufs, -36.0, -6.0) : -14;
  s.ceilingDb = -1; s.limCeiling = s.ceilingDb;
  if (!a.ok || a.lufs <= -70) { s.limOn = false; s.reasons.push_back("Nothing loud enough to measure (below -70 LUFS): no change suggested."); s.afterLufs = a.lufs; s.afterTruePeakDb = a.truePeakDb; return s; }
  // 1. gain to the target
  s.gainDb = std::clamp(s.targetLufs - a.lufs, -24.0, 24.0);
  s.reasons.push_back(fmt("GAIN %+.1f dB: the file measures %.1f LUFS, the target is %.0f LUFS.", s.gainDb, a.lufs, s.targetLufs));
  if (std::fabs(s.gainDb) >= 24) s.reasons.push_back("The gain is held at the trim's 24 dB range.");
  // 2. EQ: the monitor's Auto EQ on the octave bands, against the pink reference; not for a single tone or a narrow sound
  int live = 0; double maxShare = 0, tot = 0; for (int k = 0; k < kBands; ++k) if (a.bandDb[k] > -90) { ++live; tot += std::pow(10.0, a.bandDb[k] / 10); }
  for (int k = 0; k < kBands; ++k) if (a.bandDb[k] > -90 && tot > 0) maxShare = std::max(maxShare, std::pow(10.0, a.bandDb[k] / 10) / tot);
  if (live < 6 || maxShare > 0.8) s.reasons.push_back("EQ off: the sound sits in one band (a tone or a narrow source); a correction to the reference would only add noise.");
  else {
    s.filters = am::measure::monitorAutoEq(a.bandDb, kBands, 6, 4, 8);
    double worst = 0; for (int k = 0; k < kBands; ++k) { double g = 0; for (const auto& f : s.filters) g += am::measure::peakingDbAt(f, 31.25 * std::pow(2.0, k), 48000);
      s.eqDb[k] = std::round(std::clamp(g, -8.0, 4.0) * 2) / 2; worst = std::max(worst, std::fabs(s.eqDb[k])); }
    s.eqOn = worst >= 1.0;
    if (s.eqOn) { int kMax = 0; for (int k = 1; k < kBands; ++k) if (std::fabs(a.balanceDb[k]) > std::fabs(a.balanceDb[kMax])) kMax = k;
      const double fc = 31.25 * std::pow(2.0, kMax);
      s.reasons.push_back(fmt("EQ on: the %.0f Hz octave is %+.1f dB from the pink reference; the Auto EQ moves the bands at most 4 dB up, 8 dB down.", fc, a.balanceDb[kMax])); }
    else { for (double& g : s.eqDb) g = 0; s.reasons.push_back("EQ off: every octave is within 1 dB of what the correction would give."); }
  }
  // 3. compressor: by the loudness range and the crest factor
  const double lraMax = s.targetLufs <= -20 ? 15 : 9;
  if (a.crestDb < 8) s.reasons.push_back(fmt("Compressor off: crest factor %.1f dB, the file is already dense.", a.crestDb));
  else if (a.lra > lraMax || a.crestDb > 18) {
    s.compOn = true; s.compRatio = a.lra > lraMax + 6 || a.crestDb > 22 ? 3 : 2; s.compThr = std::round(s.targetLufs + 3);
    s.reasons.push_back(fmt("Compressor %.0f:1 at %.0f dB: loudness range %.1f LU (%.1f dB crest) is wide for this target.", s.compRatio, s.compThr, a.lra > 0 ? a.lra : a.crestDb));
  } else s.reasons.push_back(fmt("Compressor off: loudness range %.1f LU suits this target.", a.lra));
  // 4. limiter at -1 dBTP: always on as the safety; the reason says how hard it works
  const double tpAfter = a.truePeakDb + s.gainDb; const double over = tpAfter - s.ceilingDb;
  s.limOn = true;
  if (over > 0) s.reasons.push_back(fmt("LIMITER at %.0f dBTP: after the gain the true peak would be %+.1f dBTP, %.1f dB over.", s.ceilingDb, tpAfter, over));
  else s.reasons.push_back(fmt("LIMITER at %.0f dBTP as a safety: the true peak after the gain is %+.1f dBTP.", s.ceilingDb, tpAfter));
  if (over > 6) s.reasons.push_back("More than 6 dB of limiting: the compressor takes the first part, or choose a lower target.");
  if (a.clippedRuns > 0) s.reasons.push_back(fmt("The file is clipped in %.0f places: lowering the level does not repair them.", a.clippedRuns));
  // predicted numbers after: the limiter takes about half of what it removes from the loudness, the compressor narrows the range
  s.afterTruePeakDb = std::min(tpAfter, s.ceilingDb);
  s.afterLufs = a.lufs + s.gainDb - (over > 0 ? over * 0.5 : 0) * (s.compOn ? 0.6 : 1.0);
  s.afterLra = s.compOn ? a.lra / (1 + (s.compRatio - 1) * 0.5) : a.lra;
  return s;
}

}  // namespace am::fa
