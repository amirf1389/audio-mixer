// AmF DE-HISS (see fx.hpp). Our own implementation: an iterative radix-2 complex FFT on fixed tables, the two channels packed into one
// transform (L real, R imaginary; the gain is real and symmetric, so they separate again in the inverse), Hann^2 overlap-add at 75 %
// (sum 1.5, scaled out). All buffers are members of fixed size; process() only copies samples and runs a frame every 256 of them.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "audiomixer/fpu.hpp"
#include "fx.hpp"

namespace am::fx {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kTiny = 1e-20f;
double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
std::string fmt(const char* f, double v) { char b[40]; std::snprintf(b, sizeof b, f, v); return b; }
double reduceDb(double v) { return v * 40; }                    // 0 .. 40 dB
double threshDb(double v) { return v * 24 - 6; }                // -6 .. +18 dB over the profile
double freqHz(double v) { return 20 * std::pow(1000.0, v); }    // 20 Hz .. 20 kHz
double attackMs(double v) { return 1 * std::pow(100.0, v); }    // 1 .. 100 ms
double releaseMs(double v) { return 10 * std::pow(100.0, v); }  // 10 .. 1000 ms
struct Def { const char* id; const char* title; const char* units; double def; };
const Def kDefs[] = {{"reduce", "REDUCTION", "dB", 0.3}, {"thresh", "THRESHOLD", "dB", 0.5}, {"freq", "FREQ FROM", "Hz", 2.0 / 3}, {"hfonly", "HF ONLY", "", 0},
                     {"attack", "ATTACK", "ms", 0.349}, {"release", "RELEASE", "ms", 0.54}, {"learn", "LEARN", "", 0}, {"auto", "AUTO LEARN", "", 1}, {"listen", "LISTEN NOISE", "", 0}};
}  // namespace

DeHiss::DeHiss(double fs) : fs_(fs > 0 ? fs : 48000) {
  for (int i = 0; i < kN; ++i) win_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * i / kN));   // periodic Hann: Hann^2 at hop N/4 sums to 1.5
  for (int i = 0; i < kN / 2; ++i) { cos_[i] = static_cast<float>(std::cos(2 * kPi * i / kN)); sin_[i] = static_cast<float>(-std::sin(2 * kPi * i / kN)); }
  int bits = 0; while ((1 << bits) < kN) ++bits;
  for (int i = 0; i < kN; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev_[i] = static_cast<std::uint16_t>(r); }
  reset();
}
std::vector<Param> DeHiss::params() const {
  const double v[] = {amount_, thresh_, freq_, hfonly_, attack_, release_, learn_, auto_, listen_}; std::vector<Param> out;
  for (int i = 0; i < 9; ++i) out.push_back({kDefs[i].id, kDefs[i].title, kDefs[i].units, v[i], kDefs[i].def}); return out;
}
void DeHiss::set(const std::string& id, double v) {
  v = clamp01(v); const bool sw = v >= 0.5;
  if (id == "reduce") amount_ = v; else if (id == "thresh") thresh_ = v; else if (id == "freq") freq_ = v; else if (id == "hfonly") hfonly_ = sw ? 1 : 0;
  else if (id == "attack") attack_ = v; else if (id == "release") release_ = v; else if (id == "learn") learn_ = sw ? 1 : 0; else if (id == "auto") auto_ = sw ? 1 : 0; else if (id == "listen") listen_ = sw ? 1 : 0;
}
std::string DeHiss::text(const std::string& id, double v) const {
  v = clamp01(v);
  if (id == "reduce") return fmt("%.1f dB", reduceDb(v)); if (id == "thresh") return fmt("%+.1f dB", threshDb(v));
  if (id == "freq") { const double f = freqHz(v); return f >= 1000 ? fmt("%.2f kHz", f / 1000) : fmt("%.0f Hz", f); }
  if (id == "attack") return fmt("%.1f ms", attackMs(v)); if (id == "release") return fmt("%.0f ms", releaseMs(v));
  if (id == "learn") return v >= 0.5 ? "LEARNING" : "OFF"; if (id == "listen") return v >= 0.5 ? "NOISE ONLY" : "OFF";
  return v >= 0.5 ? "ON" : "OFF";
}
void DeHiss::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; reset(); }
void DeHiss::reset() {
  std::fill(std::begin(inL_), std::end(inL_), 0.f); std::fill(std::begin(inR_), std::end(inR_), 0.f); std::fill(std::begin(outL_), std::end(outL_), 0.f); std::fill(std::begin(outR_), std::end(outR_), 0.f);
  std::fill(std::begin(fifoL_), std::end(fifoL_), 0.f); std::fill(std::begin(fifoR_), std::end(fifoR_), 0.f);
  std::fill(std::begin(pow_), std::end(pow_), kTiny); std::fill(std::begin(noise_), std::end(noise_), kTiny); std::fill(std::begin(gs_), std::end(gs_), 1.f); std::fill(std::begin(acc_), std::end(acc_), 0.f);
  pos_ = 0; learnFrames_ = 0; learning_ = false; primed_ = false; red_ = 0;
}
void DeHiss::fft(float* re, float* im, const float* c, const float* s, const std::uint16_t* rev, bool inverse) {
  for (int i = 0; i < kN; ++i) { const int j = rev[i]; if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
  const float sg = inverse ? -1.f : 1.f;
  for (int len = 2; len <= kN; len <<= 1) { const int half = len >> 1, step = kN / len;
    for (int b = 0; b < kN; b += len) for (int k = 0; k < half; ++k) {
      const float wr = c[k * step], wi = sg * s[k * step]; float& ar = re[b + k]; float& ai = im[b + k]; float& br = re[b + k + half]; float& bi = im[b + k + half];
      const float tr = br * wr - bi * wi, ti = br * wi + bi * wr; br = ar - tr; bi = ai - ti; ar += tr; ai += ti; } }
}
void DeHiss::process(float* L, float* R, std::size_t n) {
  ScopedNoDenormals noDenormals;
  std::size_t i = 0;
  while (i < n) {
    const std::size_t take = std::min(n - i, static_cast<std::size_t>(kHop - pos_)); float* il = inL_ + (kN - kHop + pos_); float* ir = inR_ + (kN - kHop + pos_);
    for (std::size_t j = 0; j < take; ++j) { il[j] = L[i + j]; ir[j] = R[i + j]; L[i + j] = fifoL_[pos_ + static_cast<int>(j)]; R[i + j] = fifoR_[pos_ + static_cast<int>(j)]; }
    pos_ += static_cast<int>(take); i += take;
    if (pos_ == kHop) { frame(); pos_ = 0; }
  }
}
void DeHiss::frame() {
  for (int k = 0; k < kN; ++k) { re_[k] = inL_[k] * win_[k]; im_[k] = inR_[k] * win_[k]; }
  fft(re_, im_, cos_, sin_, rev_, false);
  const double frameRate = fs_ / kHop;
  // |XL|^2 + |XR|^2 per bin, lightly smoothed in time
  for (int k = 0; k < kBins; ++k) { const int m = (kN - k) & (kN - 1); const float p = 0.5f * (re_[k] * re_[k] + im_[k] * im_[k] + re_[m] * re_[m] + im_[m] * im_[m]) + kTiny;
    tmp_[k] = p; pow_[k] = 0.6f * pow_[k] + 0.4f * p; }
  // the noise profile: LEARN (mean of the raw power while on), else AUTO (the mean of the bins near the floor, a slow rise above it)
  const bool learn = learn_.load() >= 0.5;
  if (learn) { if (!learning_) { learnFrames_ = 0; std::fill(std::begin(acc_), std::end(acc_), 0.f); } ++learnFrames_; const float inv = 1.f / static_cast<float>(learnFrames_);
    for (int k = 0; k < kBins; ++k) { acc_[k] += tmp_[k]; noise_[k] = acc_[k] * inv + kTiny; } primed_ = true; }
  else if (auto_.load() >= 0.5) { const float rise = static_cast<float>(std::pow(10.0, 1.0 / frameRate));   // near the floor: its mean; above it: +10 dB/s at most
    for (int k = 0; k < kBins; ++k) { const float p = pow_[k]; float& z = noise_[k]; if (!primed_) z = p; else if (p < 2 * z) z += 0.05f * (p - z); else z = std::min(z * rise, p); z = std::max(z, kTiny); }
    primed_ = true; }
  learning_ = learn;
  // the gain: Wiener-style over the power smoothed across 3 bins, floored at REDUCTION (0.2 dB under it: pure hiss drops by at least the amount)
  const float thr = static_cast<float>(std::pow(10.0, threshDb(thresh_.load()) / 10)), floorG = static_cast<float>(std::pow(10.0, -(reduceDb(amount_.load()) + 0.2) / 20));
  const float ca = static_cast<float>(1 - std::exp(-1.0 / (attackMs(attack_.load()) * 0.001 * frameRate))), cr = static_cast<float>(1 - std::exp(-1.0 / (releaseMs(release_.load()) * 0.001 * frameRate)));
  for (int k = 0; k < kBins; ++k) { const float ps = 0.25f * pow_[k > 0 ? k - 1 : 1] + 0.5f * pow_[k] + 0.25f * pow_[k < kBins - 1 ? k + 1 : kBins - 2];
    const float w = std::max(0.f, 1.f - thr * noise_[k] / ps), g = floorG + (1 - floorG) * w; float& s = gs_[k]; s += (g > s ? ca : cr) * (g - s); }
  const double f0 = freqHz(freq_.load()), low = hfonly_.load() >= 0.5 ? 0.0 : 0.5, wet = mix_.load(); const bool listen = listen_.load() >= 0.5, byp = bypass_.load();
  double sumDb = 0;
  for (int k = 0; k < kBins; ++k) {
    float g = 0.25f * gs_[k > 0 ? k - 1 : 1] + 0.5f * gs_[k] + 0.25f * gs_[k < kBins - 1 ? k + 1 : kBins - 2];
    const double hz = k * fs_ / kN, t = hz <= 0 ? 0.0 : std::clamp((std::log2(hz / f0) + 0.5) / 0.5, 0.0, 1.0), depth = t + (1 - t) * low;
    if (depth < 1) g = depth <= 0 ? 1.f : std::pow(g, static_cast<float>(depth));
    sumDb += 20 * std::log10(std::max(g, 1e-6f));
    g = static_cast<float>(1 - wet + wet * g); if (listen) g = 1 - g; if (byp) g = 1;
    g_[k] = g; }
  red_ = static_cast<float>(sumDb / kBins);
  re_[0] *= g_[0]; im_[0] *= g_[0]; re_[kN / 2] *= g_[kN / 2]; im_[kN / 2] *= g_[kN / 2];
  for (int k = 1; k < kN / 2; ++k) { re_[k] *= g_[k]; im_[k] *= g_[k]; re_[kN - k] *= g_[k]; im_[kN - k] *= g_[k]; }
  fft(re_, im_, cos_, sin_, rev_, true);
  const float sc = 1.f / (kN * 1.5f);
  for (int k = 0; k < kN; ++k) { const float w = win_[k] * sc; outL_[k] += re_[k] * w; outR_[k] += im_[k] * w; }
  std::memcpy(fifoL_, outL_, sizeof fifoL_); std::memcpy(fifoR_, outR_, sizeof fifoR_);
  std::memmove(outL_, outL_ + kHop, sizeof(float) * (kN - kHop)); std::memmove(outR_, outR_ + kHop, sizeof(float) * (kN - kHop));
  std::fill(outL_ + kN - kHop, outL_ + kN, 0.f); std::fill(outR_ + kN - kHop, outR_ + kN, 0.f);
  std::memmove(inL_, inL_ + kHop, sizeof(float) * (kN - kHop)); std::memmove(inR_, inR_ + kHop, sizeof(float) * (kN - kHop));
}
}  // namespace am::fx
