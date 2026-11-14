// The I/O FRAME's processing objects: ECHO CANCELLER (AEC), TELEPHONE HYBRID and LOGIC / GPIO. See fx.hpp.
// Real-time rules as everywhere in the racks: nothing is allocated in process() (the filters are sized to their maximum in reset()),
// the knobs are atomics read once per block.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "audiomixer/frame.hpp"
#include "fx/fx.hpp"

namespace am::fx {
namespace {
constexpr int kMaxTaps = 4096;            // 85 ms at 48 kHz: the longest echo tail the cancellers model
double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
std::string fmt(const char* f, double v) { char b[48]; std::snprintf(b, sizeof b, f, v); return b; }
}

// NLMS with a mirrored ring: the newest `taps` reference samples are always one contiguous block (x[pos .. pos+taps)), so the dot product
// and the update are two straight loops the compiler vectorises. px is the window's energy, kept by adding the new sample and taking out
// the one that left.
float Nlms::run(float ref, float mic, double mu, bool adapt) {
  if (taps <= 0) return mic;
  pos = pos == 0 ? static_cast<std::size_t>(taps) - 1 : pos - 1;
  const float leaving = x[pos + static_cast<std::size_t>(taps)];
  x[pos] = ref; x[pos + static_cast<std::size_t>(taps)] = ref;
  px += static_cast<double>(ref) * ref - static_cast<double>(leaving) * leaving; if (px < 0) px = 0;
  const float* xw = &x[pos]; float* wp = w.data();
  // eight partial sums: the compiler can spread them over SIMD lanes (one running sum must be added in order and does not vectorise)
  float acc[8] = {0, 0, 0, 0, 0, 0, 0, 0}; int k = 0;
  for (; k + 8 <= taps; k += 8) for (int j = 0; j < 8; ++j) acc[j] += wp[k + j] * xw[k + j];
  float y = ((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]));
  for (; k < taps; ++k) y += wp[k] * xw[k];
  const float e = mic - y;
  if (adapt && px > 1e-7) { const float g = static_cast<float>(mu * e / (px + 1e-6)); for (int k = 0; k < taps; ++k) wp[k] += g * xw[k]; }
  return e;
}

// ── ECHO CANCELLER ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
EchoCanceller::EchoCanceller(double fs) : Simple(fs) { for (int i = 0; i < 5; ++i) k_[i] = knobDef(i); reset(); }
const char* EchoCanceller::knobId(int i) const { static const char* id[] = {"bulk", "tail", "adapt", "suppress", "mix"}; return id[i]; }
const char* EchoCanceller::knobTitle(int i) const { static const char* t[] = {"BULK DELAY", "TAIL", "ADAPT", "SUPPRESS", "MIX"}; return t[i]; }
double EchoCanceller::knobDef(int i) const { static const double d[] = {0.16, 0.4, 0.5, 0.4, 1.0}; return d[i]; }
std::string EchoCanceller::text(const std::string& id, double v) const {
  if (id == "bulk") return fmt("%.0f ms", v * 250); if (id == "tail") return fmt("%.0f ms", 16 + v * 69);
  if (id == "adapt") return fmt("%.2f", 0.05 + v * 0.95); if (id == "suppress") return fmt("%.0f dB", v * 30); return fmt("%.0f %%", v * 100);
}
void EchoCanceller::reset() { f_.resize(kMaxTaps); built_ = 0; eD_ = eE_ = 1e-9; farPk_ = 0; nlpG_ = 1; farHold_ = 0; erle_ = 0; dt_ = false; }
void EchoCanceller::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double bulkMs = k_[0] * 250, tailMs = 16 + k_[1] * 69, mu = 0.05 + k_[2] * 0.95, supDb = k_[3] * 30, mix = k_[4];
  const int taps = std::clamp(static_cast<int>(tailMs * fs_ / 1000.0), 64, kMaxTaps);
  if (taps != built_) { std::fill(f_.w.begin(), f_.w.end(), 0.f); f_.taps = taps; f_.px = 0; std::fill(f_.x.begin(), f_.x.end(), 0.f); f_.pos = 0; built_ = taps; }   // no allocation: the buffers hold kMaxTaps
  const std::size_t bulk = static_cast<std::size_t>(bulkMs * fs_ / 1000.0);
  const auto& tap = am::frame::refTap(); const double farDecay = std::exp(-1.0 / (0.25 * fs_)), eA = 1.0 - std::exp(-1.0 / (0.1 * fs_));
  const long holdN = static_cast<long>(0.06 * fs_); const double supG = dbToLin(-supDb); bool anyDt = false;
  for (std::size_t i = 0; i < n; ++i) {
    // the reference sample that left the speakers `bulk` samples before this mic sample (the newest tap sample is the end of the last block)
    const std::size_t age = bulk > i ? bulk - 1 - i : 0; const float ref = tap.at(age);
    const float mic = 0.5f * (L[i] + R[i]);
    farPk_ = std::max(farPk_ * farDecay, static_cast<double>(std::fabs(ref)));
    // Geigel: the near end talks when the mic is louder than the far end could make it
    if (farPk_ > 1e-4 && std::fabs(mic) > 0.6 * farPk_) farHold_ = holdN; else if (farHold_ > 0) --farHold_;
    const bool dt = farHold_ > 0, far = farPk_ > 1e-4; anyDt = anyDt || dt;
    const float e = f_.run(ref, mic, mu, far && !dt);
    if (far && !dt) { eD_ += eA * (static_cast<double>(mic) * mic - eD_); eE_ += eA * (static_cast<double>(e) * e - eE_); }
    // residual echo suppression: only while the far end alone is active
    const double want = far && !dt ? supG : 1.0; nlpG_ += (want - nlpG_) * (want < nlpG_ ? 0.002 : 0.0005);
    const float out = static_cast<float>(mic * (1.0 - mix) + e * nlpG_ * mix);
    L[i] = out; R[i] = out;
  }
  erle_ = 10.0 * std::log10(std::max(1e-12, eD_) / std::max(1e-12, eE_)); dt_ = anyDt;
}

// ── TELEPHONE HYBRID ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
PhoneHybrid::PhoneHybrid(double fs) : Simple(fs) { for (int i = 0; i < 5; ++i) k_[i] = knobDef(i); reset(); }
const char* PhoneHybrid::knobId(int i) const { static const char* id[] = {"tail", "band", "agc", "level", "bulk"}; return id[i]; }
const char* PhoneHybrid::knobTitle(int i) const { static const char* t[] = {"ECHO TAIL", "BAND", "AGC", "LEVEL", "LINE DELAY"}; return t[i]; }
double PhoneHybrid::knobDef(int i) const { static const double d[] = {0.5, 0.0, 0.6, 0.5, 0.2}; return d[i]; }
std::string PhoneHybrid::text(const std::string& id, double v) const {
  if (id == "tail") return fmt("%.0f ms", 8 + v * 34); if (id == "band") return v < 0.5 ? "NARROW 300-3400" : "WIDE 50-7000";
  if (id == "agc") return v < 0.02 ? std::string("OFF") : fmt("%.0f %%", v * 100); if (id == "level") return fmt("%+.1f dB", (v - 0.5) * 24);
  return fmt("%.0f ms", v * 100);
}
void PhoneHybrid::reset() { f_.resize(kMaxTaps); built_ = 0; hp_[0] = hp_[1] = 0; for (double& z : lp_) z = 0; env_ = 0; agc_ = 1; }
void PhoneHybrid::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double tailMs = 8 + k_[0] * 34, agcAmt = k_[2], lvl = dbToLin((k_[3] - 0.5) * 24); const bool wide = k_[1] >= 0.5;
  const int taps = std::clamp(static_cast<int>(tailMs * fs_ / 1000.0), 64, kMaxTaps);
  if (taps != built_) { std::fill(f_.w.begin(), f_.w.end(), 0.f); std::fill(f_.x.begin(), f_.x.end(), 0.f); f_.taps = taps; f_.px = 0; f_.pos = 0; built_ = taps; }
  const std::size_t bulk = static_cast<std::size_t>(k_[4] * 100 * fs_ / 1000.0);
  const double hpA = std::exp(-2 * 3.14159265358979 * (wide ? 50.0 : 300.0) / fs_), lpA = std::exp(-2 * 3.14159265358979 * (wide ? 7000.0 : 3400.0) / fs_);
  const double envA = 1.0 - std::exp(-1.0 / (0.3 * fs_)), target = dbToLin(-20.0), maxG = dbToLin(15.0 * agcAmt), minG = dbToLin(-15.0 * agcAmt);
  const auto& tap = am::frame::refTap(); double farPk = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t age = bulk > i ? bulk - 1 - i : 0; const float ref = tap.at(age); farPk = std::max(farPk * 0.9999, static_cast<double>(std::fabs(ref)));
    const float in = 0.5f * (L[i] + R[i]);
    float e = f_.run(ref, in, 0.4, farPk > 1e-4 && std::fabs(in) < 0.8 * farPk);   // the caller talking over the console: no adaptation
    // the phone band: a one-pole high-pass, then two one-pole low-passes
    double x = e; hp_[1] = hpA * (hp_[1] + x - hp_[0]); hp_[0] = x; x = hp_[1];
    lp_[0] = lpA * lp_[0] + (1 - lpA) * x; lp_[1] = lpA * lp_[1] + (1 - lpA) * lp_[0]; x = lp_[1];
    // AGC: the caller's level towards -20 dBFS RMS, by at most 15 dB either way (scaled by AGC), only while they talk
    env_ += envA * (x * x - env_); const double rms = std::sqrt(env_);
    if (agcAmt > 0.02 && rms > 1e-3) { const double want = std::clamp(target / rms, minG, maxG); agc_ += (want - agc_) * 0.0002; }
    const float out = static_cast<float>(x * agc_ * lvl); L[i] = out; R[i] = out;
  }
}

// ── LOGIC / GPIO ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
LogicObject::LogicObject(double fs) : Simple(fs) { for (int i = 0; i < 5; ++i) k_[i] = knobDef(i); reset(); }
const char* LogicObject::knobId(int i) const { static const char* id[] = {"mode", "threshold", "input", "output", "hold"}; return id[i]; }
const char* LogicObject::knobTitle(int i) const { static const char* t[] = {"MODE", "THRESHOLD", "CONTROL IN", "LOGIC OUT", "HOLD"}; return t[i]; }
double LogicObject::knobDef(int i) const { static const double d[] = {0.0, 0.6, 0.0, 0.0, 0.25}; return d[i]; }
static int pick(double v, int n) { return std::clamp(static_cast<int>(v * n), 0, n - 1); }
std::string LogicObject::text(const std::string& id, double v) const {
  if (id == "mode") { static const char* m[] = {"SIGNAL PRESENT", "MUTE ON INPUT", "OPEN ON INPUT", "DUCK ON INPUT"}; return m[pick(v, 4)]; }
  if (id == "threshold") return fmt("%.0f dBFS", -60 + v * 60); if (id == "input") return fmt("IN %.0f", 1.0 + pick(v, am::frame::kLogicIn));
  if (id == "output") return fmt("OUT %.0f", 1.0 + pick(v, am::frame::kLogicOut)); return fmt("%.0f ms", v * 2000);
}
void LogicObject::reset() { env_ = 0; g_ = 1; hold_ = 0; }
void LogicObject::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const int mode = pick(k_[0], 4), in = pick(k_[2], am::frame::kLogicIn), out = pick(k_[3], am::frame::kLogicOut);
  auto& lg = am::frame::logic();
  if (mode == 0) {   // SIGNAL PRESENT: the channel's level against the threshold, with the hold, onto the logic output
    const double thr = dbToLin(-60 + k_[1] * 60), a = 1.0 - std::exp(-1.0 / (0.01 * fs_)); bool above = false;
    for (std::size_t i = 0; i < n; ++i) { const double v = std::max(std::fabs(L[i]), std::fabs(R[i])); env_ += a * (v - env_); above = above || env_ > thr; }
    if (above) hold_ = static_cast<long>(k_[4] * 2.0 * fs_); else hold_ = std::max(0L, hold_ - static_cast<long>(n));
    lg.setOut(out, above || hold_ > 0); return;
  }
  const bool on = lg.getIn(in);
  const double want = mode == 1 ? (on ? 0.0 : 1.0) : mode == 2 ? (on ? 1.0 : 0.0) : (on ? dbToLin(-15) : 1.0);
  const double step = 1.0 / (0.01 * fs_);   // a 10 ms ramp: no click when a switch closes
  for (std::size_t i = 0; i < n; ++i) { g_ += std::clamp(want - g_, -step, step); L[i] = static_cast<float>(L[i] * g_); R[i] = static_cast<float>(R[i] * g_); }
}

}  // namespace am::fx
