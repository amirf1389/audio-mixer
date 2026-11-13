// IAMF ELEMENT and IAMF RENDERER (see fx.hpp): sound placed around the listener, after the audio elements and the renderer of IAMF
// (the immersive audio format of the Eclipsa plug-ins). The ear model is a classic spherical-head model, not a measured HRTF set.
#include "fx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace am::fx {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kHead = 0.0875, kSound = 343.0;   // head radius (m), speed of sound (m/s)
double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
std::string fmt(const char* f, double v) { char b[40]; std::snprintf(b, sizeof b, f, v); return b; }
double lin(double d) { return std::pow(10.0, d / 20.0); }
double coef(double ms, double fs) { return 1 - std::exp(-1.0 / (std::max(ms, 0.01) * 0.001 * fs)); }
}  // namespace

void Ears::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; reset(); }
void Ears::reset() { buf_.assign(static_cast<std::size_t>(fs_ * 0.002) + 8, 0.f); w_ = 0; for (auto& e : e_) { e.z = 0; e.notch.clear(); e.back.clear(); } }
void Ears::place(double az, double el) {
  const double a = az * kPi / 180, e = el * kPi / 180, x = std::sin(a) * std::cos(e);   // x: how far to the right (+1) or left (-1)
  const double lat = std::asin(std::clamp(x, -1.0, 1.0)), itd = kHead / kSound * (lat + std::sin(lat));   // Woodworth: > 0, the right ear hears first
  const double K = 2 * fs_, tau = 1 / (2 * kSound / kHead);
  for (int s = 0; s < 2; ++s) {   // 0 = left ear (axis -x), 1 = right ear (+x)
    Side& d = e_[s]; const double toEar = std::acos(std::clamp(s ? x : -x, -1.0, 1.0)) * 180 / kPi;   // degrees between the source and this ear's axis
    const double alpha = (1 + 0.1 / 2) + (1 - 0.1 / 2) * std::cos(toEar / 150 * kPi);   // Brown and Duda: 2 facing the ear, 0.1 in its shadow
    const double b0 = 1 + alpha * tau * K, b1 = 1 - alpha * tau * K, a0 = 1 + tau * K, a1 = 1 - tau * K; d.b0 = b0 / a0; d.b1 = b1 / a0; d.a1 = a1 / a0;
    d.delay = ((s == 1) == (itd >= 0) ? 0.0 : std::fabs(itd)) * fs_;   // the far ear hears it later
    d.notch.design(Biquad::Peak, fs_, 8000 + 2500 * el / 90, 4, -8 * (1 - 0.5 * std::fabs(el) / 90));
    d.back.design(Biquad::HighShelf, fs_, 4000, 0.7, -5 * std::max(0.0, -std::cos(a)));
  }
}
void Ears::run(float x, float& l, float& r) {
  if (buf_.empty()) { l = r = x; return; }
  const std::size_t N = buf_.size(); buf_[w_] = x; float out[2];
  for (int s = 0; s < 2; ++s) { Side& d = e_[s]; const double rp = static_cast<double>(w_) + N - std::min(d.delay, static_cast<double>(N - 3));
    const std::size_t i0 = static_cast<std::size_t>(rp) % N, i1 = (i0 + 1) % N; const double fr = rp - std::floor(rp), v = buf_[i0] * (1 - fr) + buf_[i1] * fr;
    const double y = d.b0 * v + d.z; d.z = d.b1 * v - d.a1 * y;   // the head's shadow (first order, transposed)
    out[s] = d.back.run(d.notch.run(static_cast<float>(y))); }
  l = out[0]; r = out[1]; w_ = (w_ + 1) % N;
}

// ---------------------------------------------------------------- IAMF ELEMENT
namespace { const Multi::Def kEl[] = {{"azimuth", "AZIMUTH", 0.5}, {"elevation", "ELEVATION", 0.5}, {"distance", "DISTANCE", 0.19}, {"spread", "SPREAD", 0.2}, {"render", "RENDER", 0.0}}; }
IamfElement::IamfElement(double fs) : Multi(fs, kEl, 5) { ears_.setSampleRate(fs_); reset(); }
void IamfElement::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; ears_.setSampleRate(fs_); reset(); }
std::string IamfElement::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id);
  if (i == 0) { const double a = azOf(v); return std::fabs(a) < 0.5 ? "FRONT" : fmt(a > 0 ? "%.0f° R" : "%.0f° L", std::fabs(a)); }
  if (i == 1) { const double e = elOf(v); return std::fabs(e) < 0.5 ? "EAR LEVEL" : fmt("%+.0f°", e); }
  if (i == 2) return fmt("%.1f m", distOf(v)); if (i == 4) return v < 0.5 ? "BINAURAL" : "SPEAKERS"; return fmt("%.0f %%", v * 100); }
void IamfElement::reset() { ears_.reset(); air_[0] = air_[1] = 0; placedAz_ = placedEl_ = 1e9; }
void IamfElement::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double az = azOf(k_[0]), el = elOf(k_[1]), d = distOf(k_[2]), spread = k_[3], wet = mix_.load(); const bool bin = k_[4] < 0.5;
  if (bin && (az != placedAz_ || el != placedEl_)) { ears_.place(az, el); placedAz_ = az; placedEl_ = el; }
  const double g = std::min(1.0, 1.0 / d), aAir = 1 - std::exp(-2 * kPi * std::min(20000.0 / (1 + d * 0.15), fs_ * 0.45) / fs_);
  double fa = az; if (fa > 90) fa = 180 - fa; if (fa < -90) fa = -180 - fa;   // speakers: a sound behind is folded to the front
  const double pa = (fa + 90) / 180 * kPi / 2, pl = std::cos(pa) * std::sqrt(2.0), pr = std::sin(pa) * std::sqrt(2.0);
  for (std::size_t i = 0; i < n; ++i) {
    const double m = 0.5 * (L[i] + R[i]) * g, s = 0.5 * (L[i] - R[i]) * g * spread; float l, r;
    if (bin) ears_.run(static_cast<float>(m), l, r); else { l = static_cast<float>(m * pl); r = static_cast<float>(m * pr); }
    air_[0] += static_cast<float>(aAir * (l + s - air_[0])); air_[1] += static_cast<float>(aAir * (r - s - air_[1]));
    L[i] = static_cast<float>(L[i] + (air_[0] - L[i]) * wet); R[i] = static_cast<float>(R[i] + (air_[1] - R[i]) * wet);
  }
}

// ---------------------------------------------------------------- IAMF RENDERER
namespace { const Multi::Def kRn[] = {{"layout", "LAYOUT", 0.0}, {"target", "TARGET", 0.23}, {"normalize", "NORMALIZE", 0.0}, {"output", "OUTPUT", 0.5}}; }
IamfRenderer::IamfRenderer(double fs) : Multi(fs, kRn, 4) { left_.setSampleRate(fs_); right_.setSampleRate(fs_); reset(); }
void IamfRenderer::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; left_.setSampleRate(fs_); right_.setSampleRate(fs_); dirty_ = true; reset(); }
std::string IamfRenderer::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id);
  if (i == 0) return v < 0.5 ? "STEREO" : "BINAURAL"; if (i == 1) return fmt("%.0f LKFS", targetOf(v)); if (i == 2) return v < 0.5 ? "OFF" : "ON"; return fmt("%+.1f dB", (v - 0.5) * 24); }
void IamfRenderer::reset() { left_.reset(); right_.reset(); left_.place(-30, 0); right_.place(30, 0); for (int c = 0; c < 2; ++c) { kShelf_[c].clear(); kHp_[c].clear(); } ms_ = 0; g_ = 1; lufs_ = -70; dirty_ = true; }
void IamfRenderer::design() { for (int c = 0; c < 2; ++c) { kShelf_[c].design(Biquad::HighShelf, fs_, 1500, 0.71, 4.0); kHp_[c].design(Biquad::HighPass, fs_, 38, 0.5, 0); } }   // K-weighting (BS.1770)
void IamfRenderer::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  if (dirty_.exchange(false)) design();
  const bool bin = k_[0] >= 0.5, norm = k_[2] >= 0.5; const double target = targetOf(k_[1]), out = lin((k_[3] - 0.5) * 24), wet = mix_.load();
  const double aMs = coef(3000, fs_), aG = coef(2000, fs_);
  for (std::size_t i = 0; i < n; ++i) {
    const double kl = kHp_[0].run(kShelf_[0].run(L[i])), kr = kHp_[1].run(kShelf_[1].run(R[i])); ms_ += aMs * (kl * kl + kr * kr - ms_);
    float l = L[i], r = R[i];
    if (bin) { float al, ar, bl, br; left_.run(L[i], al, ar); right_.run(R[i], bl, br); l = static_cast<float>(0.7071 * (al + bl)); r = static_cast<float>(0.7071 * (ar + br)); }
    double lu = -0.691 + 10 * std::log10(ms_ + 1e-12);
    if (norm && lu > -60) { const double want = lin(std::clamp(target - lu, -12.0, 12.0)); g_ += aG * (want - g_); } else if (!norm) g_ += aG * (1 - g_);
    const double gg = g_ * out; L[i] = static_cast<float>(L[i] + (l * gg - L[i]) * wet); R[i] = static_cast<float>(R[i] + (r * gg - R[i]) * wet);
  }
  lufs_ = -0.691 + 10 * std::log10(ms_ + 1e-12);
}

}  // namespace am::fx

// ---------------------------------------------------------------- CONVERTER CLIP
namespace am::fx {
namespace {
const Multi::Def kCc[] = {{"adc", "ADC", 1.0}, {"dac", "DAC", 1.0}, {"input", "INPUT", 0.5}, {"drive", "DRIVE", 0.25}, {"decorrelate", "DE-CORRELATE", 0.0}, {"output", "OUTPUT", 0.5},
  {"drift", "DRIFT", 0.0}, {"ceiling", "CEILING", 0.9}, {"dither", "DITHER", 0.0}, {"bits", "BITS", 1.0}, {"ovs", "OVS", 0.5}, {"monitor", "MONITOR", 0.0}};
double ccLin(double d) { return std::pow(10.0, d / 20.0); }
// the clipper: straight up to 60 % of the ceiling, then a smooth (cubic) bend that lands flat exactly on the ceiling
double softClip(double x, double c) { const double k = 0.6 * c, a = std::fabs(x); if (a <= k) return x; const double span = c - k, t = std::min(1.0, (a - k) / (2 * span));
  const double y = k + span * (2 * t - t * t); return x < 0 ? -std::min(y, c) : std::min(y, c); }
}  // namespace
ConvClip::ConvClip(double fs) : Multi(fs, kCc, 12) { reset(); }
void ConvClip::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; reset(); }
double ConvClip::rnd() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return static_cast<double>(rng_) / 4294967296.0; }
std::string ConvClip::text(const std::string& id, double v) const { v = v < 0 ? 0 : v > 1 ? 1 : v; const int i = index(id); char b[40];
  switch (i) { case 0: case 1: case 6: return v < 0.5 ? "OFF" : "ON"; case 2: case 5: std::snprintf(b, sizeof b, "%+.1f dB", (v - 0.5) * 24); return b; case 3: std::snprintf(b, sizeof b, "+%.1f dB", v * 18); return b;
    case 7: std::snprintf(b, sizeof b, "%.1f dBFS", (v - 1) * 3); return b; case 8: { static const char* t[] = {"OFF", "TPDF", "SHAPED"}; return t[pickOf(v, 3)]; } case 9: std::snprintf(b, sizeof b, "%d bit", bitsOf(v)); return b;
    case 10: std::snprintf(b, sizeof b, "%dx", ovsOf(v)); return b; case 11: { static const char* t[] = {"LR", "M", "S", "Δ"}; return t[pickOf(v, 4)]; } default: std::snprintf(b, sizeof b, "%.0f %%", v * 100); return b; } }
void ConvClip::reset() { for (int c = 0; c < 2; ++c) { for (auto& q : up_[c]) q.clear(); for (auto& q : down_[c]) q.clear(); adc_[c].clear(); dac_[c].clear(); ap_[c] = apIn_[c] = 0; drift_[c] = driftT_[c] = 1; err_[c] = 0; } ovsDesigned_ = 0; dirty_ = true; gr_ = 0; }
void ConvClip::design() {
  const int N = ovsOf(k_[10]); const double fo = fs_ * N;
  for (int c = 0; c < 2; ++c) { for (int s = 0; s < 2; ++s) { up_[c][s].design(Biquad::LowPass, fo, fs_ * 0.45, s ? 1.31 : 0.54, 0); down_[c][s].design(Biquad::LowPass, fo, fs_ * 0.45, s ? 1.31 : 0.54, 0); }   // 4th-order Butterworth around the clipper
    adc_[c].design(Biquad::LowPass, fs_, std::min(20500.0, fs_ * 0.45), 0.6, 0); dac_[c].design(Biquad::LowPass, fs_, std::min(21500.0, fs_ * 0.46), 0.6, 0); }
  ovsDesigned_ = N;
}
void ConvClip::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  if (dirty_.exchange(false) || ovsDesigned_ != ovsOf(k_[10])) design();
  const bool adc = k_[0] >= 0.5, dac = k_[1] >= 0.5, drift = k_[6] >= 0.5; const int N = ovsOf(k_[10]), dith = pickOf(k_[8], 3), mon = pickOf(k_[11], 4), bits = bitsOf(k_[9]);
  const double gin = ccLin((k_[2] - 0.5) * 24) * ccLin(k_[3] * 18), gout = ccLin((k_[5] - 0.5) * 24), ceil = ccLin((k_[7] - 1) * 3), dec = k_[4] * 0.35, wet = mix_.load();
  const double lsb = std::ldexp(1.0, -(bits - 1));
  if (drift) for (int c = 0; c < 2; ++c) if (rnd() < 0.02) driftT_[c] = ccLin((rnd() - 0.5) * 0.3);   // now and then a new resting point, within +-0.15 dB
  double minGr = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double in[2] = {L[i], R[i]}; double o[2];
    for (int c = 0; c < 2; ++c) {
      drift_[c] += 0.00002 * ((drift ? driftT_[c] : 1.0) - drift_[c]);
      double x = in[c] * gin * drift_[c];
      if (adc) { x = adc_[c].run(static_cast<float>(x)); x = x + 0.003 * x * x - 0.006 * x * x * x; }   // the input converter's analog stage: a hair of 2nd and 3rd harmonic
      if (dec > 0) { const double a = c ? dec : -dec, y = a * x + apIn_[c] - a * ap_[c]; apIn_[c] = x; ap_[c] = y; x = y; }   // a first-order all-pass, opposite on the two sides
      double y = 0, pre = std::fabs(x);
      for (int j = 0; j < N; ++j) { double u = j == 0 ? x * N : 0.0; if (N > 1) { u = up_[c][1].run(up_[c][0].run(static_cast<float>(u))); } else u = x;
        double cl = softClip(u, ceil); if (N > 1) cl = down_[c][1].run(down_[c][0].run(static_cast<float>(cl))); if (j == N - 1) y = cl; }
      if (pre > 1e-6) minGr = std::min(minGr, 20 * std::log10(std::max(std::fabs(y), 1e-9) / pre));
      if (dac) { y = dac_[c].run(static_cast<float>(y)); y = y + 0.002 * y * y - 0.004 * y * y * y; }
      y *= gout;
      if (dith > 0) { const double tp = (rnd() - rnd()) * lsb; double v = y + tp - (dith == 2 ? err_[c] : 0.0); const double q = std::round(v / lsb) * lsb; err_[c] = q - v + tp; y = q; }   // TPDF, or with first-order noise shaping
      y = std::clamp(y, -ceil * gout, ceil * gout);   // never over the ceiling (times OUTPUT)
      o[c] = y;
    }
    double l = o[0], r = o[1];
    if (mon == 1) { const double m = 0.5 * (l + r); l = r = m; } else if (mon == 2) { const double s = 0.5 * (l - r); l = s; r = -s; } else if (mon == 3) { l -= in[0]; r -= in[1]; }
    L[i] = static_cast<float>(in[0] + (l - in[0]) * wet); R[i] = static_cast<float>(in[1] + (r - in[1]) * wet);
  }
  gr_ = minGr;
}
}  // namespace am::fx
