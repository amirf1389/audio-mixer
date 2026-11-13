// OPTO LEVELER, GLUE COMP, TAPE MACHINE, TRANSIENT SHAPER, DE-ESSER (see fx.hpp). No allocation in process(); coefficients are
// recomputed at the top of a block when a control moved.
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
// a soft-knee gain computer: how many dB to take off for a level x (dB)
double gainDb(double x, double thr, double ratio, double knee) {
  const double o = x - thr; if (2 * o < -knee) return 0; if (2 * std::fabs(o) <= knee) { const double t = o + knee / 2; return -(1 - 1 / ratio) * t * t / (2 * knee); }
  return -(1 - 1 / ratio) * o; }
}  // namespace

// ---------------------------------------------------------------- OPTO LEVELER
namespace { const Multi::Def kOpto[] = {{"reduction", "PEAK REDUCTION", 0.3}, {"gain", "GAIN", 0.25}, {"mode", "MODE", 0.0}, {"emphasis", "EMPHASIS", 0.3}}; }
Opto::Opto(double fs) : Multi(fs, kOpto, 4) { reset(); }
std::string Opto::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id);
  if (i == 0) return fmt("%.0f", v * 100); if (i == 1) return fmt("%+.1f dB", v * 24); if (i == 2) return v < 0.5 ? "COMPRESS" : "LIMIT"; return fmt("%.0f %%", v * 100); }
void Opto::reset() { env_ = fast_ = slow_ = lp_ = 0; gr_ = 0; }
void Opto::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double thr = -k_[0] * 40, ratio = k_[2] >= 0.5 ? 10 : 3, makeup = lin(k_[1] * 24), emph = k_[3], wet = mix_.load();
  const double aAtt = coef(10, fs_), aFast = coef(60, fs_), aSlow = coef(2500, fs_), aLp = coef(0.25, fs_); double minG = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double x = 0.5 * (L[i] + R[i]); lp_ += aLp * (x - lp_); const double sc = std::fabs(x + emph * 2 * (x - lp_));
    env_ += (sc > env_ ? aAtt : aFast * 0.2) * (sc - env_);
    const double want = gainDb(toDb(env_), thr, ratio, 8);   // the cell: a fast and a slow part; the slow one remembers long passages
    fast_ += (want < fast_ ? aAtt : aFast) * (want - fast_); slow_ += (want < slow_ ? aSlow * 4 : aSlow) * (want - slow_);
    const double g = std::min(fast_ * 0.6 + slow_ * 0.4, 0.0); minG = std::min(minG, g);
    const double gg = lin(g) * makeup;
    for (float* c : {L + i, R + i}) { const double d = *c; double y = d * gg; y = y + 0.04 * (std::tanh(y) - y); *c = static_cast<float>(d + (y - d) * wet); }
  }
  gr_ = minG;
}

// ---------------------------------------------------------------- GLUE COMP
namespace { const Multi::Def kGlue[] = {{"threshold", "THRESHOLD", 0.6}, {"ratio", "RATIO", 0.25}, {"attack", "ATTACK", 0.4}, {"release", "RELEASE", 0.4}, {"makeup", "MAKEUP", 0.1}, {"colour", "COLOUR", 0.3}};
double glueRatio(double v) { return 1.2 * std::pow(10.0 / 1.2, v); } double glueAtt(double v) { return 0.1 * std::pow(300.0, v); } double glueRel(double v) { return 50 * std::pow(24.0, v); } }
Glue::Glue(double fs) : Multi(fs, kGlue, 6) { reset(); }
std::string Glue::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id);
  if (i == 0) return fmt("%.1f dB", -v * 40); if (i == 1) return fmt("%.1f : 1", glueRatio(v)); if (i == 2) return fmt("%.1f ms", glueAtt(v)); if (i == 3) return fmt("%.0f ms", glueRel(v)); if (i == 4) return fmt("%+.1f dB", v * 18); return fmt("%.0f %%", v * 100); }
void Glue::reset() { env_ = 0; gr_ = 0; }
void Glue::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double thr = -k_[0] * 40, ratio = glueRatio(k_[1]), aA = coef(glueAtt(k_[2]), fs_), aR = coef(glueRel(k_[3]), fs_), mk = lin(k_[4] * 18), col = k_[5], wet = mix_.load(); double minG = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double lvl = toDb(std::max(std::fabs(L[i]), std::fabs(R[i]))), want = gainDb(lvl, thr, ratio, 6);
    env_ += (want < env_ ? aA : aR) * (want - env_); minG = std::min(minG, env_);
    const double g = lin(env_) * mk;
    for (float* c : {L + i, R + i}) { const double d = *c, y0 = d * g, y = y0 + col * 0.12 * (y0 * y0 - 0.5 * y0 * y0 * y0) - col * 0.02 * y0; *c = static_cast<float>(d + (y - d) * wet); }
  }
  gr_ = minG;
}

// ---------------------------------------------------------------- TAPE MACHINE
namespace { const Multi::Def kTape[] = {{"drive", "DRIVE", 0.4}, {"speed", "SPEED", 0.5}, {"bias", "BIAS", 0.5}, {"wow", "WOW & FLUTTER", 0.15}, {"output", "OUTPUT", 0.5}}; }
Tape::Tape(double fs) : Multi(fs, kTape, 5) { reset(); }
std::string Tape::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id);
  if (i == 0) return fmt("%+.1f dB", v * 18 - 6); if (i == 1) { static const char* t[] = {"7.5 ips", "15 ips", "30 ips"}; return t[speedOf(v)]; } if (i == 2) return v < 0.45 ? "DARK" : v > 0.55 ? "BRIGHT" : "NORMAL";
  if (i == 4) return fmt("%+.1f dB", (v - 0.5) * 24); return fmt("%.0f %%", v * 100); }
void Tape::reset() { for (auto& b : buf_) b.assign(static_cast<std::size_t>(fs_ * 0.012) + 8, 0.f); w_ = 0; ph1_ = ph2_ = 0; for (int c = 0; c < 2; ++c) { dcx_[c] = dcy_[c] = 0; bump_[c].clear(); roll_[c].clear(); bias_[c].clear(); } dirty_ = true; }
void Tape::design() {
  const int sp = speedOf(k_[1]); const double bumpF[] = {45, 65, 95}, rollF[] = {9000, 15000, 20000};
  for (int c = 0; c < 2; ++c) { bump_[c].design(Biquad::Peak, fs_, bumpF[sp], 1.2, 2.5); roll_[c].design(Biquad::LowPass, fs_, rollF[sp], 0.65, 0); bias_[c].design(Biquad::HighShelf, fs_, 6000, 0.7, (k_[2] - 0.5) * 6); }
}
void Tape::process(float* L, float* R, std::size_t n) {
  if (bypass_.load() || buf_[0].empty()) return;
  if (dirty_.exchange(false)) design();
  const double drive = lin(k_[0] * 18 - 6), out = lin((k_[4] - 0.5) * 24) / std::max(1.0, std::sqrt(drive)), wow = k_[3], wet = mix_.load();
  const double d1 = 2 * kPi * 0.55 / fs_, d2 = 2 * kPi * 7.3 / fs_; const std::size_t N = buf_[0].size(); float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) {
    const double ms = 3 + wow * (1.6 * std::sin(ph1_) + 0.25 * std::sin(ph2_)); const double dl = std::min(ms * fs_ / 1000.0, static_cast<double>(N - 3));
    for (int c = 0; c < 2; ++c) {
      const double x = ch[c][i]; double v = x * drive; v = std::tanh(v + 0.08 * v * v); { const double hp = v - dcx_[c] + 0.9995 * dcy_[c]; dcx_[c] = v; dcy_[c] = hp; v = hp; }   // the even harmonic adds an offset: a DC blocker takes it out (silence stays silent) v = bias_[c].run(roll_[c].run(bump_[c].run(static_cast<float>(v))));
      double y = v;
      if (wow > 0.001) { buf_[c][w_] = static_cast<float>(v); const double rp = static_cast<double>(w_) + N - dl; const std::size_t i0 = static_cast<std::size_t>(rp) % N, i1 = (i0 + 1) % N; const double fr = rp - std::floor(rp); y = buf_[c][i0] * (1 - fr) + buf_[c][i1] * fr; }
      y *= out; ch[c][i] = static_cast<float>(x + (y - x) * wet);
    }
    w_ = (w_ + 1) % N; ph1_ += d1; ph2_ += d2; if (ph1_ > 2 * kPi) ph1_ -= 2 * kPi; if (ph2_ > 2 * kPi) ph2_ -= 2 * kPi;
  }
}

// ---------------------------------------------------------------- TRANSIENT SHAPER
namespace { const Multi::Def kTr[] = {{"attack", "ATTACK", 0.65}, {"sustain", "SUSTAIN", 0.5}, {"output", "OUTPUT", 0.5}}; }
Transient::Transient(double fs) : Multi(fs, kTr, 3) { reset(); }
std::string Transient::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id); if (i == 2) return fmt("%+.1f dB", (v - 0.5) * 24); return fmt("%+.0f %%", (v - 0.5) * 200); }
void Transient::reset() { fast_ = slow_ = 0; g_ = 1; }
void Transient::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double att = (k_[0] - 0.5) * 2, sus = (k_[1] - 0.5) * 2, out = lin((k_[2] - 0.5) * 24), wet = mix_.load();
  const double aF = coef(1, fs_), rF = coef(40, fs_), aS = coef(30, fs_), rS = coef(300, fs_), aG = coef(1, fs_);
  for (std::size_t i = 0; i < n; ++i) {
    const double x = std::max(std::fabs(L[i]), std::fabs(R[i]));
    fast_ += (x > fast_ ? aF : rF) * (x - fast_); slow_ += (x > slow_ ? aS : rS) * (x - slow_);
    const double diff = toDb(fast_ + 1e-9) - toDb(slow_ + 1e-9);   // > 0 in an attack, < 0 in the tail
    const double gdb = std::clamp(diff > 0 ? att * diff * 1.2 : -sus * diff * 0.8, -18.0, 18.0);
    g_ += aG * (lin(gdb) - g_); const double g = g_ * out;
    for (float* c : {L + i, R + i}) { const double d = *c; *c = static_cast<float>(d + (d * g - d) * wet); }
  }
}

// ---------------------------------------------------------------- DE-ESSER
namespace { const Multi::Def kDs[] = {{"freq", "FREQUENCY", 0.4}, {"threshold", "THRESHOLD", 0.5}, {"range", "RANGE", 0.5}, {"listen", "LISTEN", 0.0}};
double dsF(double v) { return 3000 * std::pow(4.0, v); } }
DeEsser::DeEsser(double fs) : Multi(fs, kDs, 4) { reset(); }
std::string DeEsser::text(const std::string& id, double v) const { v = clamp01(v); const int i = index(id);
  if (i == 0) return hz(dsF(v)); if (i == 1) return fmt("%.1f dB", -v * 50); if (i == 2) return fmt("%.1f dB", v * 18); return v < 0.5 ? "OFF" : "ON"; }
void DeEsser::reset() { hp_[0].clear(); hp_[1].clear(); det_.clear(); env_ = 0; gr_ = 0; dirty_ = true; }
void DeEsser::design() { for (auto& h : hp_) h.design(Biquad::LowPass, fs_, dsF(k_[0]), 0.707, 0);   // hp_ holds the LOW part: the highs are what is left (an exact split)
  det_.design(Biquad::HighPass, fs_, dsF(k_[0]), 0.707, 0); }
void DeEsser::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  if (dirty_.exchange(false)) design();
  const double thr = -k_[1] * 50, range = k_[2] * 18; const bool listen = k_[3] >= 0.5; const double aA = coef(0.5, fs_), aR = coef(60, fs_), wet = mix_.load(); double minG = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double s = std::fabs(det_.run(static_cast<float>(0.5 * (L[i] + R[i])))); env_ += (s > env_ ? aA : aR) * (s - env_);
    const double gdb = std::max(-range, gainDb(toDb(env_), thr, 8, 4)); minG = std::min(minG, gdb); const double g = lin(gdb);
    float* ch[2] = {L + i, R + i};
    for (int c = 0; c < 2; ++c) { const double d = *ch[c], hi = d - hp_[c].run(static_cast<float>(d)), y = listen ? hi * (1 - g) : d - hi * (1 - g); *ch[c] = static_cast<float>(d + (y - d) * wet); }
  }
  gr_ = minG;
}

}  // namespace am::fx
