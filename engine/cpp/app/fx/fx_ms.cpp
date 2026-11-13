// MS COLOUR EQ, ISO EQ 10 and MOD MACHINE (see fx.hpp). Coefficients are worked out on the audio thread at the top of a block when a
// control moved (plain arithmetic, no allocation); filter state is kept, so turning a knob does not click.
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
double db(double v) { return (v - 0.5) * 24.0; }
double lin(double d) { return std::pow(10.0, d / 20.0); }
double nyq(double f, double fs) { return std::min(f, fs * 0.45); }
}  // namespace

void Biquad::design(Kind k, double fs, double f, double q, double gdb) {
  f = nyq(std::max(f, 5.0), fs); q = std::max(q, 0.1);
  const double A = std::pow(10.0, gdb / 40.0), w = 2 * kPi * f / fs, c = std::cos(w), sn = std::sin(w), al = sn / (2 * q);
  double B0, B1, B2, A0, A1, A2;
  switch (k) {
    case Peak: B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
    case LowShelf: { const double r = 2 * std::sqrt(A) * al; B0 = A * ((A + 1) - (A - 1) * c + r); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - r);
      A0 = (A + 1) + (A - 1) * c + r; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - r; break; }
    case HighShelf: { const double r = 2 * std::sqrt(A) * al; B0 = A * ((A + 1) + (A - 1) * c + r); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - r);
      A0 = (A + 1) - (A - 1) * c + r; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - r; break; }
    case HighPass: B0 = (1 + c) / 2; B1 = -(1 + c); B2 = (1 + c) / 2; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
    case LowPass: B0 = (1 - c) / 2; B1 = 1 - c; B2 = (1 - c) / 2; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
    default: B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;   // band-pass, 0 dB peak
  }
  b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
}

// ---------------------------------------------------------------- MS COLOUR EQ
namespace {
const Multi::Def kMs[] = {{"input", "INPUT", 0.5}, {"xformer", "XFORMER", 0.25},
  {"lowgain", "LOW", 0.5}, {"lowfreq", "FREQ", 0.5}, {"lowms", "M/S OFFSET", 0.5},
  {"congain", "CONTOUR", 0.5}, {"confreq", "FREQ", 0.5}, {"conbw", "BANDWIDTH", 0.5}, {"conms", "M/S OFFSET", 0.5},
  {"midgain", "MID", 0.5}, {"midfreq", "FREQ", 0.5}, {"midbw", "BANDWIDTH", 0.5}, {"midms", "M/S OFFSET", 0.5},
  {"highgain", "HIGH", 0.5}, {"highfreq", "FREQ", 0.5}, {"highms", "M/S OFFSET", 0.5},
  {"hpf", "HPF", 0.0}, {"lpf", "LPF", 1.0}, {"drive", "DRIVE", 0.0}, {"focus", "FOCUS", 0.5},
  {"solo", "SOLO", 0.0}, {"mono", "MONO", 0.0}, {"width", "WIDTH", 0.5}, {"rate", "RATE", 0.4}, {"depth", "DEPTH", 0.0}, {"output", "OUTPUT", 0.5}};
double lowF(double v) { return 20 * std::pow(15.0, v); } double conF(double v) { return 60 * std::pow(10.0, v); } double midF(double v) { return 300 * std::pow(20.0, v); }
double highF(double v) { return 2000 * std::pow(10.0, v); } double bwQ(double v) { return 4.0 - clamp01(v) * 3.6; } double focusF(double v) { return 100 * std::pow(80.0, v); }
double rateHz(double v) { return 0.02 * std::pow(250.0, v); } int soloOf(double v) { return static_cast<int>(clamp01(v) * 3.999); }
}  // namespace
MsEq::MsEq(double fs) : Multi(fs, kMs, static_cast<int>(sizeof kMs / sizeof kMs[0])) { reset(); }
std::string MsEq::text(const std::string& id, double v) const {
  v = clamp01(v); const int i = index(id);
  switch (i) {
    case 0: case 25: case 2: case 5: case 9: case 13: return fmt("%+.1f dB", db(v));
    case 3: return hz(lowF(v)); case 6: return hz(conF(v)); case 10: return hz(midF(v)); case 14: return hz(highF(v)); case 19: return hz(focusF(v));
    case 7: case 11: return fmt("Q %.1f", bwQ(v));
    case 4: case 8: case 12: case 15: { const double o = v * 2 - 1; if (std::fabs(o) < 0.02) return "M + S"; return o > 0 ? fmt("M %.0f %%", o * 100) : fmt("S %.0f %%", -o * 100); }
    case 16: return v < 0.02 ? "OFF" : hz(lowF(v)); case 17: return v > 0.98 ? "OFF" : hz(highF(v));
    case 21: return v < 0.02 ? "OFF" : hz(lowF(v));
    case 20: { static const char* t[] = {"OFF", "MID", "SIDE", "DELTA"}; return t[soloOf(v)]; }
    case 22: return fmt("%.0f %%", v * 200); case 23: return fmt("%.2f Hz", rateHz(v));
    default: return fmt("%.0f %%", v * 100);
  }
}
void MsEq::reset() { for (auto* a : {m_, s_}) for (int i = 0; i < 6; ++i) a[i].clear(); hp_[0].clear(); hp_[1].clear(); lp_[0].clear(); lp_[1].clear(); satM_.clear(); satS_.clear(); mono_.clear(); ph_ = 0; dcX_[0] = dcX_[1] = dcY_[0] = dcY_[1] = 0; dirty_ = true; }
void MsEq::design() {
  const double f = fs_; auto g = [&](int i) { return db(k_[i].load()); };
  auto band = [&](int slot, Biquad::Kind kd, double fr, double q, double gain, double ms) {
    m_[slot].design(kd, f, fr, q, gain * offsetMid(ms)); s_[slot].design(kd, f, fr, q, gain * offsetSide(ms)); };
  band(0, Biquad::LowShelf, lowF(k_[3]), 0.7, g(2), k_[4]);
  band(1, Biquad::Peak, conF(k_[6]), bwQ(k_[7]), g(5), k_[8]);
  band(2, Biquad::Peak, midF(k_[10]), bwQ(k_[11]), g(9), k_[12]);
  band(3, Biquad::HighShelf, highF(k_[14]), 0.7, g(13), k_[15]);
  for (auto& h : hp_) h.design(Biquad::HighPass, f, lowF(k_[16]), 0.707, 0);
  for (auto& l : lp_) l.design(Biquad::LowPass, f, highF(k_[17]), 0.707, 0);
  satM_.design(Biquad::BandPass, f, focusF(k_[19]), 0.8, 0); satS_ = Biquad{satM_.b0, satM_.b1, satM_.b2, satM_.a1, satM_.a2, satS_.z1, satS_.z2};
  mono_.design(Biquad::LowPass, f, lowF(k_[21]), 0.707, 0);
}
void MsEq::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  if (dirty_.exchange(false)) design();
  const double gin = lin(db(k_[0])), gout = lin(db(k_[25])), xf = k_[1], drive = k_[18], width = k_[22] * 2, depth = k_[24];
  const bool hp = k_[16] >= 0.02, lp = k_[17] <= 0.98, mono = k_[21] >= 0.02; const int solo = soloOf(k_[20]);
  const double dph = 2 * kPi * rateHz(k_[23]) / fs_, kd = 1 + drive * 9, wet = mix_.load();
  for (std::size_t i = 0; i < n; ++i) {
    const float dl = L[i], dr = R[i];
    const double l = dl * gin, r = dr * gin; double m = (l + r) * 0.5, s = (l - r) * 0.5;
    if (hp) { m = hp_[0].run(static_cast<float>(m)); s = hp_[1].run(static_cast<float>(s)); }
    if (lp) { m = lp_[0].run(static_cast<float>(m)); s = lp_[1].run(static_cast<float>(s)); }
    for (int b = 0; b < 4; ++b) { m = m_[b].run(static_cast<float>(m)); s = s_[b].run(static_cast<float>(s)); }
    if (mono) s -= mono_.run(static_cast<float>(s));
    s *= width * (1 + depth * 0.5 * std::sin(ph_)); ph_ += dph; if (ph_ > 2 * kPi) ph_ -= 2 * kPi;
    if (drive > 0) { m += drive * std::tanh(kd * satM_.run(static_cast<float>(m))) / std::sqrt(kd); s += drive * std::tanh(kd * satS_.run(static_cast<float>(s))) / std::sqrt(kd); }
    double o[2] = {m + s, m - s};
    for (int c = 0; c < 2; ++c) {   // the output transformer: a soft, slightly uneven curve; a DC blocker takes away what the unevenness adds
      const double x = o[c], y = (1 - xf) * x + xf * std::tanh(1.1 * x + 0.12 * x * x) / 1.1;
      const double hpY = y - dcX_[c] + 0.9995 * dcY_[c]; dcX_[c] = static_cast<float>(y); dcY_[c] = static_cast<float>(hpY); o[c] = hpY * gout; }
    double ol = dl + (o[0] - dl) * wet, or_ = dr + (o[1] - dr) * wet;
    if (solo == 1) { const double mm = (ol + or_) * 0.5; ol = or_ = mm; }
    else if (solo == 2) { const double ss = (ol - or_) * 0.5; ol = ss; or_ = -ss; }
    else if (solo == 3) { ol -= dl; or_ -= dr; }
    L[i] = static_cast<float>(ol); R[i] = static_cast<float>(or_);
  }
}

// ---------------------------------------------------------------- ISO EQ 10
namespace {
const Multi::Def kIso[] = {{"b31", "31", 0.5}, {"b63", "63", 0.5}, {"b125", "125", 0.5}, {"b250", "250", 0.5}, {"b500", "500", 0.5}, {"b1k", "1K", 0.5}, {"b2k", "2K", 0.5},
  {"b4k", "4K", 0.5}, {"b8k", "8K", 0.5}, {"b16k", "16K", 0.5}, {"q", "Q", 1.0}, {"colour", "COLOUR", 0.2}, {"output", "OUTPUT", 0.5}};
}  // namespace
constexpr double IsoEq::kFreq[10];
IsoEq::IsoEq(double fs) : Multi(fs, kIso, static_cast<int>(sizeof kIso / sizeof kIso[0])) { reset(); }
std::string IsoEq::text(const std::string& id, double v) const {
  v = clamp01(v); const int i = index(id);
  if (i >= 0 && i < 10) return fmt("%+.1f dB", gainDb(v)); if (i == 10) return v < 0.5 ? "CONSTANT" : "PROPORTIONAL"; if (i == 12) return fmt("%+.1f dB", db(v));
  return fmt("%.0f %%", v * 100);
}
void IsoEq::reset() { for (auto& c : f_) for (auto& b : c) b.clear(); dirty_ = true; }
void IsoEq::design() {
  const bool prop = k_[10] >= 0.5;
  for (int b = 0; b < 10; ++b) { const double g = gainDb(k_[b]); const double q = prop ? 0.6 + 2.2 * std::fabs(g) / 12.0 : 1.414;
    for (auto& c : f_) c[b].design(Biquad::Peak, fs_, kFreq[b], q, g); }
}
void IsoEq::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  if (dirty_.exchange(false)) design();
  bool flat = true; for (int b = 0; b < 10; ++b) if (std::fabs(k_[b] - 0.5) > 1e-4) flat = false;
  const double col = k_[11], gout = lin(db(k_[12])), wet = mix_.load(); float* ch[2] = {L, R};
  for (int c = 0; c < 2; ++c) for (std::size_t i = 0; i < n; ++i) {
    const float d = ch[c][i]; double x = d;
    if (!flat) for (int b = 0; b < 10; ++b) x = f_[c][b].run(static_cast<float>(x));
    x = x + col * 0.3 * (std::tanh(x) - x); x *= gout;
    ch[c][i] = static_cast<float>(d + (x - d) * wet);
  }
}

// ---------------------------------------------------------------- MOD MACHINE
namespace {
const Multi::Def kMod[] = {{"mode", "MODE", 0.0}, {"rate", "RATE", 0.35}, {"depth", "DEPTH", 0.5}, {"feedback", "FEEDBACK", 0.3}, {"tone", "TONE", 0.7}, {"spread", "SPREAD", 0.5}, {"colour", "COLOUR", 0.3}};
double modRate(double v) { return 0.05 * std::pow(200.0, v); }
}  // namespace
ModMachine::ModMachine(double fs) : Multi(fs, kMod, static_cast<int>(sizeof kMod / sizeof kMod[0])) { reset(); }
std::string ModMachine::text(const std::string& id, double v) const {
  v = clamp01(v); const int i = index(id);
  if (i == 0) { static const char* t[] = {"CHORUS", "FLANGER", "PHASER", "TREMOLO", "VIBRATO"}; return t[modeOf(v)]; }
  if (i == 1) return fmt("%.2f Hz", modRate(v)); if (i == 4) return hz(1000 * std::pow(18.0, v)); if (i == 5) return fmt("%.0f deg", v * 180);
  return fmt("%.0f %%", v * 100);
}
void ModMachine::reset() { for (auto& b : buf_) b.assign(static_cast<std::size_t>(fs_ * 0.05) + 8, 0.f); w_ = 0; ph_ = 0; fb_[0] = fb_[1] = lp_[0] = lp_[1] = 0; for (auto& a : ap_) for (auto& z : a) z = 0; }
void ModMachine::process(float* L, float* R, std::size_t n) {
  if (bypass_.load() || buf_[0].empty()) return;
  const int mode = modeOf(k_[0]); const double depth = k_[2], fb = k_[3] * 0.9, spread = k_[5] * kPi, col = k_[6], wet = mix_.load();
  const double dph = 2 * kPi * modRate(k_[1]) / fs_, lpA = 1 - std::exp(-2 * kPi * nyq(1000 * std::pow(18.0, k_[4].load()), fs_) / fs_);
  const std::size_t N = buf_[0].size(); float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) {
    for (int c = 0; c < 2; ++c) {
      const double lfo = 0.5 + 0.5 * std::sin(ph_ + (c ? spread : 0.0)); const double x = ch[c][i]; double y = x;
      if (mode == 3) { y = x * (1 - depth * lfo); }
      else if (mode == 2) {   // six first-order all-passes swept 200 Hz .. 3 kHz
        const double f = 200 * std::pow(15.0, lfo * depth), t = std::tan(kPi * nyq(f, fs_) / fs_), a = (t - 1) / (t + 1);
        const double fbp = fb * 0.7; double v = x + fbp * std::tanh(fb_[c]); for (int s = 0; s < 6; ++s) { const double o = a * v + ap_[c][s]; ap_[c][s] = static_cast<float>(v - a * o); v = o; }
        fb_[c] = static_cast<float>(v); y = 0.7 * (x + v) * (1 - 0.6 * fbp);
      } else {
        const double ms = mode == 0 ? 12 + depth * 8 * lfo : mode == 1 ? 0.5 + depth * 5 * lfo : 3 + depth * 4 * lfo;
        const double d = std::min(ms * fs_ / 1000.0, static_cast<double>(N - 3)); const double rp = static_cast<double>(w_) + N - d;
        const std::size_t i0 = static_cast<std::size_t>(rp) % N, i1 = (i0 + 1) % N; const double fr = rp - std::floor(rp);
        double v = buf_[c][i0] * (1 - fr) + buf_[c][i1] * fr;
        lp_[c] += static_cast<float>(lpA * (v - lp_[c])); v = lp_[c]; v = v + col * 0.5 * (std::tanh(1.5 * v) / 1.5 - v);
        buf_[c][w_] = static_cast<float>(x + (mode == 1 ? fb * v : 0.0));
        y = mode == 4 ? v : 0.7 * (x + v);
      }
      ch[c][i] = static_cast<float>(x + (y - x) * wet);
    }
    w_ = (w_ + 1) % N; ph_ += dph; if (ph_ > 2 * kPi) ph_ -= 2 * kPi;
  }
}

}  // namespace am::fx
