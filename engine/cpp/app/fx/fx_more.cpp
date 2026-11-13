// MAXIMIZER, MULTIBAND COMP, STEREO IMAGER, AUTO PAN, VOCAL DOUBLER (see fx.hpp). Buffers are sized in reset() / setSampleRate(),
// never in process().
#include "fx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace am::fx {
namespace {
constexpr double kPi = 3.14159265358979323846;
double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
std::string fmt(const char* f, double v) { char b[40]; std::snprintf(b, sizeof b, f, v); return b; }
std::string hz(double f) { return f >= 1000 ? fmt("%.1f kHz", f / 1000) : fmt("%.0f Hz", f); }
double toDb(double x) { return 20 * std::log10(std::max(x, 1e-9)); }
double lin(double d) { return std::pow(10.0, d / 20.0); }
double coef(double ms, double fs) { return 1 - std::exp(-1.0 / (std::max(ms, 0.01) * 0.001 * fs)); }
}  // namespace

// ---------------------------------------------------------------- MAXIMIZER
namespace { const Multi::Def kMax[] = {{"threshold", "THRESHOLD", 0.25}, {"ceiling", "CEILING", 0.9}, {"release", "RELEASE", 0.4}};
double maxRel(double v) { return 5 * std::pow(100.0, v); } }
Maximizer::Maximizer(double fs) : Multi(fs, kMax, 3) { reset(); }
void Maximizer::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; reset(); }
std::string Maximizer::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id);
  if (i == 0) return fmt("%.1f dB", -v * 12); if (i == 1) return fmt("%.1f dBFS", (v - 1) * 3); return fmt("%.0f ms", maxRel(v)); }
void Maximizer::reset() { const std::size_t la = static_cast<std::size_t>(fs_ * 0.0015) + 1; for (auto& b : buf_) b.assign(la, 0.f); need_.assign(la, 1.f); w_ = 0; g_ = 1; gr_ = 0; }
void Maximizer::process(float* L, float* R, std::size_t n) {
  if (bypass_.load() || need_.empty()) return;
  const double drive = lin(k_[0] * 12), ceil = lin((k_[1] - 1) * 3), aR = coef(maxRel(k_[2]), fs_), wet = mix_.load(); const std::size_t N = need_.size();
  const double aA = 1.0 / static_cast<double>(N); double minG = 1;
  for (std::size_t i = 0; i < n; ++i) {
    const double pk = std::max(std::fabs(L[i]), std::fabs(R[i])) * drive; need_[w_] = static_cast<float>(pk > ceil ? ceil / pk : 1.0);
    double want = 1; for (std::size_t k = 0; k < N; ++k) want = std::min(want, static_cast<double>(need_[k]));   // the lowest gain any sample in the look-ahead window needs
    if (want < g_) g_ += (want - g_) * std::min(1.0, 4.0 * aA); else g_ += aR * (want - g_);   // down within the look-ahead, up at RELEASE
    minG = std::min(minG, g_);
    const float dl = buf_[0][w_], dr = buf_[1][w_]; buf_[0][w_] = L[i]; buf_[1][w_] = R[i]; w_ = (w_ + 1) % N;   // the delayed sample leaves as the new one enters
    for (int c = 0; c < 2; ++c) { const double d = c ? dr : dl; double y = d * drive * g_; y = std::clamp(y, -ceil, ceil); float* o = c ? R + i : L + i; *o = static_cast<float>(d + (y - d) * wet); }
  }
  gr_ = toDb(minG);
}

// ---------------------------------------------------------------- MULTIBAND COMP
namespace { const Multi::Def kMb[] = {{"lowx", "LOW X", 0.4}, {"highx", "HIGH X", 0.5}, {"low", "LOW", 0.3}, {"mid", "MID", 0.3}, {"high", "HIGH", 0.3}, {"speed", "SPEED", 0.5}, {"output", "OUTPUT", 0.5}};
double lowX(double v) { return 60 * std::pow(500.0 / 60, v); } double highX(double v) { return 1000 * std::pow(8.0, v); } }
Multiband::Multiband(double fs) : Multi(fs, kMb, 7) { reset(); }
std::string Multiband::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id);
  if (i == 0) return hz(lowX(v)); if (i == 1) return hz(highX(v)); if (i == 5) return v < 0.33 ? "SLOW" : v < 0.66 ? "MEDIUM" : "FAST"; if (i == 6) return fmt("%+.1f dB", (v - 0.5) * 24); return fmt("%.0f %%", v * 100); }
void Multiband::reset() { for (auto& c : lo_) for (auto& b : c) b.clear(); for (auto& c : hi_) for (auto& b : c) b.clear(); env_[0] = env_[1] = env_[2] = 0; dirty_ = true; }
void Multiband::design() { for (int c = 0; c < 2; ++c) for (int k = 0; k < 2; ++k) { lo_[c][k].design(Biquad::LowPass, fs_, lowX(k_[0]), 0.707, 0); hi_[c][k].design(Biquad::LowPass, fs_, std::max(highX(k_[1]), lowX(k_[0]) * 2), 0.707, 0); } }
void Multiband::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  if (dirty_.exchange(false)) design();
  const double sp = k_[5], aA = coef(30 - sp * 25, fs_), aR = coef(400 - sp * 300, fs_), out = lin((k_[6] - 0.5) * 24), wet = mix_.load();
  const double thr[3] = {-k_[2] * 30, -k_[3] * 30, -k_[4] * 30};
  for (std::size_t i = 0; i < n; ++i) {
    double b[2][3];
    for (int c = 0; c < 2; ++c) { const double x = c ? R[i] : L[i]; const double lo = lo_[c][1].run(lo_[c][0].run(static_cast<float>(x))), lm = hi_[c][1].run(hi_[c][0].run(static_cast<float>(x)));
      b[c][0] = lo; b[c][1] = lm - lo; b[c][2] = x - lm; }   // low, mid, high add back to the input exactly
    double g[3];
    for (int k = 0; k < 3; ++k) { const double lvl = std::max(std::fabs(b[0][k]), std::fabs(b[1][k])); env_[k] += (lvl > env_[k] ? aA : aR) * (lvl - env_[k]);
      const double o = toDb(env_[k]) - thr[k]; g[k] = lin(o > 0 ? -o * 0.75 : 0); }   // 4:1 above each band's threshold
    for (int c = 0; c < 2; ++c) { float* p = c ? R + i : L + i; const double d = *p, y = (b[c][0] * g[0] + b[c][1] * g[1] + b[c][2] * g[2]) * out; *p = static_cast<float>(d + (y - d) * wet); }
  }
}

// ---------------------------------------------------------------- STEREO IMAGER
namespace { const Multi::Def kIm[] = {{"lowwidth", "LOW WIDTH", 0.4}, {"highwidth", "HIGH WIDTH", 0.6}, {"crossover", "CROSSOVER", 0.4}, {"output", "OUTPUT", 0.5}};
double imX(double v) { return 80 * std::pow(50.0, v); } }
Imager::Imager(double fs) : Multi(fs, kIm, 4) { reset(); }
std::string Imager::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id); if (i == 2) return hz(imX(v)); if (i == 3) return fmt("%+.1f dB", (v - 0.5) * 24); return fmt("%.0f %%", v * 200); }
void Imager::reset() { lp_.clear(); dirty_ = true; }
void Imager::design() { lp_.design(Biquad::LowPass, fs_, imX(k_[2]), 0.707, 0); }
void Imager::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  if (dirty_.exchange(false)) design();
  const double wl = k_[0] * 2, wh = k_[1] * 2, out = lin((k_[3] - 0.5) * 24), wet = mix_.load();
  for (std::size_t i = 0; i < n; ++i) {
    const double m = 0.5 * (L[i] + R[i]), s = 0.5 * (L[i] - R[i]), sl = lp_.run(static_cast<float>(s)), sh = s - sl, s2 = sl * wl + sh * wh;
    const double l = (m + s2) * out, r = (m - s2) * out; L[i] = static_cast<float>(L[i] + (l - L[i]) * wet); R[i] = static_cast<float>(R[i] + (r - R[i]) * wet);
  }
}

// ---------------------------------------------------------------- AUTO PAN
namespace { const Multi::Def kAp[] = {{"rate", "RATE", 0.4}, {"depth", "DEPTH", 0.7}, {"shape", "SHAPE", 0.0}}; double apRate(double v) { return 0.05 * std::pow(200.0, v); } }
AutoPan::AutoPan(double fs) : Multi(fs, kAp, 3) { reset(); }
std::string AutoPan::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id); if (i == 0) return fmt("%.2f Hz", apRate(v)); if (i == 2) return v < 0.33 ? "SINE" : v < 0.66 ? "TRIANGLE" : "SQUARE"; return fmt("%.0f %%", v * 100); }
void AutoPan::reset() { ph_ = 0; }
void AutoPan::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double dph = apRate(k_[0]) / fs_, depth = k_[1], sh = k_[2], wet = mix_.load();
  for (std::size_t i = 0; i < n; ++i) {
    const double sine = std::sin(2 * kPi * ph_), tri = 1 - 4 * std::fabs(ph_ - 0.5), sq = std::tanh(sine * 6);
    const double lfo = sh < 0.5 ? sine + (tri - sine) * (sh / 0.5) : tri + (sq - tri) * ((sh - 0.5) / 0.5);
    const double pan = 0.5 + 0.5 * lfo * depth, a = pan * kPi / 2, gl = std::cos(a) * std::sqrt(2.0), gr = std::sin(a) * std::sqrt(2.0);
    const double l = L[i] * gl, r = R[i] * gr; L[i] = static_cast<float>(L[i] + (l - L[i]) * wet); R[i] = static_cast<float>(R[i] + (r - R[i]) * wet);
    ph_ += dph; if (ph_ >= 1) ph_ -= 1;
  }
}

// ---------------------------------------------------------------- VOCAL DOUBLER
namespace { const Multi::Def kDb[] = {{"detune", "DETUNE", 0.4}, {"delay", "DELAY", 0.4}, {"width", "WIDTH", 0.8}, {"tone", "TONE", 0.6}}; double dbDelay(double v) { return 8 + v * 32; } }
Doubler::Doubler(double fs) : Multi(fs, kDb, 4) { reset(); }
std::string Doubler::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id); if (i == 0) return fmt("%.0f cents", v * 25); if (i == 1) return fmt("%.0f ms", dbDelay(v)); if (i == 3) return hz(1500 * std::pow(12.0, v)); return fmt("%.0f %%", v * 100); }
void Doubler::reset() { buf_.assign(static_cast<std::size_t>(fs_ * 0.06) + 8, 0.f); w_ = 0; ph_[0] = 0; ph_[1] = 0.37; lp_[0] = lp_[1] = 0; }
void Doubler::process(float* L, float* R, std::size_t n) {
  if (bypass_.load() || buf_.empty()) return;
  // a delay swept by a slow saw reads slightly faster or slower than it is written: a small, steady pitch offset (cents) for each copy
  const double cents = k_[0] * 25, base = dbDelay(k_[1]) * fs_ / 1000, width = k_[2], aLp = 1 - std::exp(-2 * kPi * std::min(1500 * std::pow(12.0, k_[3].load()), fs_ * 0.45) / fs_), wet = mix_.load();
  const double ratio = std::pow(2.0, cents / 1200) - 1, sweep = 0.006 * fs_; const double rate = std::fabs(ratio) * fs_ / std::max(sweep, 1.0); const std::size_t N = buf_.size();
  for (std::size_t i = 0; i < n; ++i) {
    const double x = 0.5 * (L[i] + R[i]); buf_[w_] = static_cast<float>(x); double v[2];
    for (int c = 0; c < 2; ++c) {
      const double p = ph_[c], win = std::sin(kPi * p);   // two copies, one sharp (c = 0), one flat; the window hides the saw's jump
      const double d = base + (c ? p : 1 - p) * sweep; const double rp = static_cast<double>(w_) + N - std::min(d, static_cast<double>(N - 3));
      const std::size_t i0 = static_cast<std::size_t>(rp) % N, i1 = (i0 + 1) % N; const double fr = rp - std::floor(rp);
      double y = (buf_[i0] * (1 - fr) + buf_[i1] * fr) * win; lp_[c] += static_cast<float>(aLp * (y - lp_[c])); v[c] = lp_[c];
      ph_[c] += rate / fs_; if (ph_[c] >= 1) ph_[c] -= 1;
    }
    const double l = L[i] + 0.7 * (v[0] * (0.5 + 0.5 * width) + v[1] * (0.5 - 0.5 * width)), r = R[i] + 0.7 * (v[1] * (0.5 + 0.5 * width) + v[0] * (0.5 - 0.5 * width));
    L[i] = static_cast<float>(L[i] + (l - L[i]) * wet); R[i] = static_cast<float>(R[i] + (r - R[i]) * wet); w_ = (w_ + 1) % N;
  }
}

}  // namespace am::fx

// ---------------------------------------------------------------- W-SHAPE TUNER
namespace am::fx {
namespace {
const Multi::Def kW[] = {{"amount", "AMOUNT", 0.6}, {"bass", "BASS", 0.55}, {"lowmid", "LOW-MID DIP", 0.4}, {"presence", "PRESENCE", 0.4}, {"upper", "UPPER DIP", 0.4}, {"air", "AIR", 0.5}, {"smooth", "SMOOTH", 0.6}, {"output", "OUTPUT", 0.5}};
double wGain(double b) { return std::pow(10.0, b / 20.0); }
// |H(f)| of one biquad in dB
double magDb(const Biquad& q, double f, double fs) { const double w = 2 * 3.14159265358979 * f / fs, cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
  const double nr = q.b0 + q.b1 * cr + q.b2 * c2r, ni = q.b1 * ci + q.b2 * c2i, dr = 1 + q.a1 * cr + q.a2 * c2r, di = q.a1 * ci + q.a2 * c2i; return 10 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-12); }
}  // namespace
void WShape::bands(const double* k, Band o[5]) {
  const double a = k[0], wide = k[6];   // SMOOTH: 0 -> Q 1.4 (tighter), 1 -> Q 0.5 (very wide)
  const double q = 1.4 - wide * 0.9;
  o[0] = {Biquad::LowShelf, 90, 0.6, a * k[1] * 9};           // BASS   0 .. +9 dB
  o[1] = {Biquad::Peak, 350, q, -a * k[2] * 6};                // LOW-MID DIP  0 .. -6 dB
  o[2] = {Biquad::Peak, 1800, q, a * k[3] * 6};                // PRESENCE  0 .. +6 dB
  o[3] = {Biquad::Peak, 5000, q * 1.2, -a * k[4] * 6};         // UPPER DIP  0 .. -6 dB
  o[4] = {Biquad::HighShelf, 10000, 0.6, a * k[5] * 9};        // AIR  0 .. +9 dB
}
WShape::WShape(double fs) : Multi(fs, kW, 8) { reset(); }
std::string WShape::text(const std::string& id, double v) const { v = v < 0 ? 0 : v > 1 ? 1 : v; char b[40]; const int i = index(id);
  const double a = k_[0]; switch (i) {
    case 1: std::snprintf(b, sizeof b, "%+.1f dB", a * v * 9); break; case 2: std::snprintf(b, sizeof b, "%+.1f dB", -a * v * 6); break; case 3: std::snprintf(b, sizeof b, "%+.1f dB", a * v * 6); break;
    case 4: std::snprintf(b, sizeof b, "%+.1f dB", -a * v * 6); break; case 5: std::snprintf(b, sizeof b, "%+.1f dB", a * v * 9); break; case 6: std::snprintf(b, sizeof b, "Q %.2f", 1.4 - v * 0.9); break;
    case 7: std::snprintf(b, sizeof b, "%+.1f dB", (v - 0.5) * 24); break; default: std::snprintf(b, sizeof b, "%.0f %%", v * 100); }
  return b; }
void WShape::reset() { for (auto& c : f_) for (auto& q : c) q.clear(); dirty_ = true; }
void WShape::design() {
  double k[8]; for (int i = 0; i < 8; ++i) k[i] = k_[i].load(); Band b[5]; bands(k, b);
  for (int c = 0; c < 2; ++c) for (int i = 0; i < 5; ++i) f_[c][i].design(b[i].kind, fs_, b[i].f, b[i].q, b[i].db);
  // loudness compensation: the curve's average power gain over 1/3-octave bands 40 Hz .. 16 kHz (a pink spectrum), taken back out
  double sum = 0; int n = 0; for (double f = 40; f <= 16000; f *= std::pow(2.0, 1.0 / 3)) { double d = 0; for (int i = 0; i < 5; ++i) d += magDb(f_[0][i], f, fs_); sum += std::pow(10.0, d / 10); ++n; }
  comp_ = -10 * std::log10(sum / n);
}
void WShape::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  if (dirty_.exchange(false)) design();
  const double g = wGain(comp_.load() + (k_[7] - 0.5) * 24), wet = mix_.load(); float* ch[2] = {L, R};
  for (int c = 0; c < 2; ++c) for (std::size_t i = 0; i < n; ++i) { const float d = ch[c][i]; float x = d; for (int b = 0; b < 5; ++b) x = f_[c][b].run(x); ch[c][i] = static_cast<float>(d + (x * g - d) * wet); }
}
}  // namespace am::fx
