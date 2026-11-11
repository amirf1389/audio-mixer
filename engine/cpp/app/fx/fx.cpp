#include "fx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace am::fx {
namespace {
constexpr double kPi = 3.14159265358979323846;
double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
std::string fmt(const char* f, double v) { char b[32]; std::snprintf(b, sizeof b, f, v); return b; }
}  // namespace

std::vector<std::pair<std::string, std::string>> catalog() { return {{"reverb", "PLATE REVERB"}, {"delay", "TAPE DELAY"}, {"chorus", "STEREO CHORUS"}, {"drive", "TUBE DRIVE"}, {"flanger", "FLANGER"}, {"phaser", "PHASER"}, {"tremolo", "TREMOLO"}, {"wah", "AUTO WAH"}, {"crusher", "BIT CRUSHER"}, {"exciter", "EXCITER"}}; }
std::unique_ptr<Effect> make(const std::string& k, double fs) {
  if (k == "reverb") return std::make_unique<Reverb>(fs);
  if (k == "delay") return std::make_unique<TapeDelay>(fs);
  if (k == "chorus") return std::make_unique<Chorus>(fs);
  if (k == "flanger") return std::make_unique<Flanger>(fs);
  if (k == "phaser") return std::make_unique<Phaser>(fs);
  if (k == "tremolo") return std::make_unique<Tremolo>(fs);
  if (k == "wah") return std::make_unique<AutoWah>(fs);
  if (k == "crusher") return std::make_unique<Crusher>(fs);
  if (k == "exciter") return std::make_unique<Exciter>(fs);
  if (k == "drive") return std::make_unique<Drive>(fs);
  return nullptr;
}

// ---------------------------------------------------------------- Reverb
Reverb::Reverb(double fs) : fs_(fs) { setSampleRate(fs); }
void Reverb::setSampleRate(double fs) {
  fs_ = fs > 0 ? fs : 48000; const double k = fs_ / 44100.0;
  const int combMs[4] = {1557, 1617, 1491, 1422}, apMs[2] = {225, 556};   // Schroeder lengths (samples at 44.1 k), the right side spread by 23 samples
  for (int s = 0; s < 2; ++s) { for (int c = 0; c < 4; ++c) { comb_[s][c].buf.assign(static_cast<std::size_t>(combMs[c] * k) + (s ? 23 : 0), 0.f); comb_[s][c].i = 0; comb_[s][c].store = 0; } for (int a = 0; a < 2; ++a) { ap_[s][a].assign(static_cast<std::size_t>(apMs[a] * k) + (s ? 23 : 0), 0.f); api_[s][a] = 0; } pre_buf_[s].assign(static_cast<std::size_t>(fs_ * 0.25) + 1, 0.f); }
  prei_ = 0; design();
}
void Reverb::design() {
  const double d = decay_.load(); const double decaySec = 0.3 + d * d * 11.7;                           // 0.3 .. 12 s, square law for fine control at the short end
  const double len = 1557.0 / 44100.0 * (fs_ / 44100.0) * 44100.0 / fs_;          // the reference comb length in seconds (~35 ms)
  fbGain_ = static_cast<float>(std::pow(10.0, -3.0 * len / decaySec));             // -60 dB in decaySec
  dampA_ = static_cast<float>(0.05 + damp_.load() * 0.9); preSamples_ = static_cast<std::size_t>(pre_.load() * 0.2 * fs_);
}
void Reverb::reset() { for (int s = 0; s < 2; ++s) { for (auto& c : comb_[s]) { std::fill(c.buf.begin(), c.buf.end(), 0.f); c.store = 0; } for (auto& a : ap_[s]) std::fill(a.begin(), a.end(), 0.f); std::fill(pre_buf_[s].begin(), pre_buf_[s].end(), 0.f); } }
std::vector<Param> Reverb::params() const { return {{"decay", "DECAY", "s", decay_.load(), 0.35}, {"damp", "DAMP", "", damp_.load(), 0.5}, {"pre", "PRE-DELAY", "ms", pre_.load(), 0.1}, {"width", "WIDTH", "", width_.load(), 1.0}}; }
void Reverb::set(const std::string& id, double v) { v = clamp01(v); if (id == "decay") decay_ = v; else if (id == "damp") damp_ = v; else if (id == "pre") pre_ = v; else if (id == "width") width_ = v; design(); }
std::string Reverb::text(const std::string& id, double v) const { v = clamp01(v); if (id == "decay") return fmt("%.1f s", 0.3 + v * v * 11.7); if (id == "pre") return fmt("%.0f ms", v * 200); return fmt("%.0f %%", v * 100); }
void Reverb::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const float fb = fbGain_.load(), damp = dampA_.load(), wet = static_cast<float>(mix_.load()), dry = 1.f - wet, width = static_cast<float>(width_.load());
  const std::size_t pre = preSamples_.load(), preLen = pre_buf_[0].size();
  for (std::size_t i = 0; i < n; ++i) {
    const float inL = L[i], inR = R[i]; float x[2] = {inL, inR};
    for (int s = 0; s < 2; ++s) { pre_buf_[s][prei_] = x[s]; x[s] = pre_buf_[s][(prei_ + preLen - pre) % preLen]; }
    prei_ = (prei_ + 1) % preLen;
    const float mono = 0.5f * (x[0] + x[1]); float outS[2] = {0, 0};
    for (int s = 0; s < 2; ++s) {
      float acc = 0;
      for (auto& c : comb_[s]) { const float y = c.buf[c.i]; c.store = y * (1 - damp) + c.store * damp; c.buf[c.i] = mono + c.store * fb; if (++c.i >= c.buf.size()) c.i = 0; acc += y; }
      acc *= 0.25f;
      for (int a = 0; a < 2; ++a) { std::vector<float>& ap = ap_[s][a]; std::size_t& k = api_[s][a]; const float bufOut = ap[k]; const float y = -acc + bufOut; ap[k] = acc + bufOut * 0.5f; if (++k >= ap.size()) k = 0; acc = y; }
      outS[s] = acc;
    }
    const float mid = 0.5f * (outS[0] + outS[1]), side = 0.5f * (outS[0] - outS[1]) * width;
    L[i] = dry * inL + wet * (mid + side); R[i] = dry * inR + wet * (mid - side);
  }
}

// ---------------------------------------------------------------- Tape delay
TapeDelay::TapeDelay(double fs) : fs_(fs) { setSampleRate(fs); }
void TapeDelay::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; for (auto& b : buf_) b.assign(static_cast<std::size_t>(fs_ * 2.1), 0.f); w_ = 0; lp_[0] = lp_[1] = 0; }
void TapeDelay::reset() { for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.f); lp_[0] = lp_[1] = 0; }
std::vector<Param> TapeDelay::params() const { return {{"time", "TIME", "ms", time_.load(), 0.3}, {"fb", "FEEDBACK", "", fb_.load(), 0.4}, {"tone", "TONE", "", tone_.load(), 0.5}, {"pingpong", "PING-PONG", "", pingpong_.load(), 0}}; }
void TapeDelay::set(const std::string& id, double v) { v = clamp01(v); if (id == "time") time_ = v; else if (id == "fb") fb_ = v; else if (id == "tone") tone_ = v; else if (id == "pingpong") pingpong_ = v; }
std::string TapeDelay::text(const std::string& id, double v) const { v = clamp01(v); if (id == "time") return fmt("%.0f ms", 20 + v * 1980); if (id == "pingpong") return v >= 0.5 ? "ON" : "OFF"; return fmt("%.0f %%", v * 100); }
void TapeDelay::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const std::size_t len = buf_[0].size(), d = std::min(len - 1, static_cast<std::size_t>((0.02 + time_.load() * 1.98) * fs_)); const float fb = static_cast<float>(fb_.load() * 0.95), wet = static_cast<float>(mix_.load()), dry = 1.f - wet;
  const float a = static_cast<float>(std::exp(-2.0 * kPi * (500.0 + tone_.load() * tone_.load() * 11500.0) / fs_)); const bool pp = pingpong_.load() >= 0.5;
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t r = (w_ + len - d) % len; const float dl = buf_[0][r], dr = buf_[1][r];
    lp_[0] = dl * (1 - a) + lp_[0] * a; lp_[1] = dr * (1 - a) + lp_[1] * a;
    const float inL = L[i], inR = R[i];
    buf_[0][w_] = inL + (pp ? lp_[1] : lp_[0]) * fb; buf_[1][w_] = (pp ? 0.f : inR) + (pp ? lp_[0] : lp_[1]) * fb;
    if (++w_ >= len) w_ = 0;
    L[i] = dry * inL + wet * lp_[0]; R[i] = dry * inR + wet * lp_[1];
  }
}

// ---------------------------------------------------------------- Chorus
Chorus::Chorus(double fs) : fs_(fs) { setSampleRate(fs); }
void Chorus::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; for (auto& b : buf_) b.assign(static_cast<std::size_t>(fs_ * 0.05), 0.f); w_ = 0; phase_ = 0; }
void Chorus::reset() { for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.f); }
std::vector<Param> Chorus::params() const { return {{"rate", "RATE", "Hz", rate_.load(), 0.25}, {"depth", "DEPTH", "", depth_.load(), 0.5}, {"spread", "SPREAD", "", spread_.load(), 0.5}}; }
void Chorus::set(const std::string& id, double v) { v = clamp01(v); if (id == "rate") rate_ = v; else if (id == "depth") depth_ = v; else if (id == "spread") spread_ = v; }
std::string Chorus::text(const std::string& id, double v) const { v = clamp01(v); if (id == "rate") return fmt("%.2f Hz", 0.1 + v * 4.9); return fmt("%.0f %%", v * 100); }
void Chorus::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const std::size_t len = buf_[0].size(); const double hz = 0.1 + rate_.load() * 4.9, inc = 2 * kPi * hz / fs_, base = 0.012 * fs_, depth = depth_.load() * 0.008 * fs_, off = spread_.load() * kPi; const float wet = static_cast<float>(mix_.load()), dry = 1.f - wet;
  for (std::size_t i = 0; i < n; ++i) {
    buf_[0][w_] = L[i]; buf_[1][w_] = R[i];
    float out[2];
    for (int s = 0; s < 2; ++s) { const double dSamp = base + depth * (1 + std::sin(phase_ + (s ? off : 0))); const double rp = static_cast<double>(w_) + len - dSamp; const std::size_t r0 = static_cast<std::size_t>(rp) % len, r1 = (r0 + 1) % len; const float f = static_cast<float>(rp - std::floor(rp)); out[s] = buf_[s][r0] * (1 - f) + buf_[s][r1] * f; }
    phase_ += inc; if (phase_ > 2 * kPi) phase_ -= 2 * kPi;
    if (++w_ >= len) w_ = 0;
    L[i] = dry * L[i] + wet * out[0]; R[i] = dry * R[i] + wet * out[1];
  }
}

// ---------------------------------------------------------------- Drive
Drive::Drive(double fs) : fs_(fs) { setSampleRate(fs); }
void Drive::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; lp_[0] = lp_[1] = last_[0] = last_[1] = 0; }
void Drive::reset() { lp_[0] = lp_[1] = last_[0] = last_[1] = 0; }
std::vector<Param> Drive::params() const { return {{"drive", "DRIVE", "", drive_.load(), 0.4}, {"tone", "TONE", "", tone_.load(), 0.6}, {"out", "OUTPUT", "dB", out_.load(), 0.7}}; }
void Drive::set(const std::string& id, double v) { v = clamp01(v); if (id == "drive") drive_ = v; else if (id == "tone") tone_ = v; else if (id == "out") out_ = v; }
std::string Drive::text(const std::string& id, double v) const { v = clamp01(v); if (id == "drive") return fmt("%.1f dB", v * 36); if (id == "out") return fmt("%.1f dB", -24 + v * 30); return fmt("%.0f %%", v * 100); }
void Drive::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const float g = static_cast<float>(std::pow(10.0, drive_.load() * 36 / 20)), outG = static_cast<float>(std::pow(10.0, (-24 + out_.load() * 30) / 20)); const float tn = tone_.load(); const float a = static_cast<float>(std::exp(-2.0 * kPi * (800.0 + tn * tn * 15000.0) / fs_)); const float wet = static_cast<float>(mix_.load()), dry = 1.f - wet;
  float* ch[2] = {L, R};
  for (int s = 0; s < 2; ++s) for (std::size_t i = 0; i < n; ++i) {
    const float x = ch[s][i]; const float mid = 0.5f * (last_[s] + x); last_[s] = x;   // 2x: the shaper sees the in-between sample too, then averages
    const float y = 0.5f * (std::tanh(mid * g) + std::tanh(x * g)) / std::tanh(std::max(1.f, g * 0.5f));
    lp_[s] = y * (1 - a) + lp_[s] * a; ch[s][i] = dry * x + wet * lp_[s] * outG;
  }
}


// ---------------------------------------------------------------- Flanger, Phaser, Tremolo, Auto wah, Bit crusher, Exciter
Flanger::Flanger(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); reset(); }
void Flanger::reset() { for (auto& b : buf_) b.assign(static_cast<std::size_t>(fs_ * 0.02) + 8, 0.f); w_ = 0; fb_[0] = fb_[1] = 0; }
std::string Flanger::text(const std::string& id, double v) const { v = clamp01(v); if (id == "rate") return fmt("%.2f Hz", 0.05 + v * 4.95); return fmt("%.0f %%", v * 100); }
void Flanger::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const std::size_t len = buf_[0].size(); const double inc = 2 * kPi * (0.05 + k_[0].load() * 4.95) / fs_, base = 0.001 * fs_, depth = k_[1].load() * 0.0035 * fs_; const float fb = static_cast<float>(k_[2].load() * 0.85), wet = static_cast<float>(mix_.load()), dry = 1.f - wet * 0.5f; float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) {
    for (int s = 0; s < 2; ++s) { const double d = base + depth * (1 + std::sin(ph_ + (s ? kPi * 0.5 : 0))); const double rp = static_cast<double>(w_) + static_cast<double>(len) - d; const std::size_t r0 = static_cast<std::size_t>(rp) % len, r1 = (r0 + 1) % len; const float fr = static_cast<float>(rp - std::floor(rp));
      const float del = buf_[s][r0] * (1 - fr) + buf_[s][r1] * fr; buf_[s][w_] = ch[s][i] + del * fb; ch[s][i] = dry * ch[s][i] + wet * del; }
    ph_ += inc; if (ph_ > 2 * kPi) ph_ -= 2 * kPi; if (++w_ >= len) w_ = 0;
  }
}
Phaser::Phaser(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); reset(); }
void Phaser::reset() { for (auto& c : z_) for (float& v : c) v = 0; fb_[0] = fb_[1] = 0; }
std::string Phaser::text(const std::string& id, double v) const { v = clamp01(v); if (id == "rate") return fmt("%.2f Hz", 0.05 + v * 3.95); return fmt("%.0f %%", v * 100); }
void Phaser::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double inc = 2 * kPi * (0.05 + k_[0].load() * 3.95) / fs_, depth = k_[1].load(); const float fb = static_cast<float>(k_[2].load() * 0.8), wet = static_cast<float>(mix_.load()) * 0.5f, dry = 1.f - wet; float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) {
    for (int s = 0; s < 2; ++s) { const double f = 300.0 * std::pow(8.0, 0.5 + 0.5 * depth * std::sin(ph_ + (s ? kPi * 0.25 : 0))), t = std::tan(kPi * std::min(f, fs_ * 0.45) / fs_); const float a = static_cast<float>((t - 1) / (t + 1));
      float x = ch[s][i] + fb_[s] * fb; for (int k = 0; k < 6; ++k) { const float y = a * x + z_[s][k]; z_[s][k] = x - a * y; x = y; } fb_[s] = x; ch[s][i] = dry * ch[s][i] + wet * x; }
    ph_ += inc; if (ph_ > 2 * kPi) ph_ -= 2 * kPi;
  }
}
Tremolo::Tremolo(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); }
void Tremolo::reset() { ph_ = 0; }
std::string Tremolo::text(const std::string& id, double v) const { v = clamp01(v); if (id == "rate") return fmt("%.1f Hz", 0.5 + v * 14.5); return fmt("%.0f %%", v * 100); }
void Tremolo::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double inc = 2 * kPi * (0.5 + k_[0].load() * 14.5) / fs_, depth = k_[1].load() * mix_.load(), shape = 1.0 + k_[2].load() * 12.0;
  for (std::size_t i = 0; i < n; ++i) { const double lfo = std::tanh(shape * std::sin(ph_)) / std::tanh(shape); const float g = static_cast<float>(1.0 - depth * (0.5 - 0.5 * lfo)); L[i] *= g; R[i] *= g; ph_ += inc; if (ph_ > 2 * kPi) ph_ -= 2 * kPi; }
}
AutoWah::AutoWah(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); }
void AutoWah::reset() { lp_[0] = lp_[1] = bp_[0] = bp_[1] = 0; env_ = 0; }
std::string AutoWah::text(const std::string&, double v) const { return fmt("%.0f %%", clamp01(v) * 100); }
void AutoWah::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double sens = 1.0 + k_[0].load() * 30.0, range = 0.5 + k_[1].load() * 3.5, att = 1 - std::exp(-1.0 / (0.004 * fs_)), rel = 1 - std::exp(-1.0 / (0.12 * fs_)); const float q = static_cast<float>(1.0 - k_[2].load() * 0.92), wet = static_cast<float>(mix_.load()), dry = 1.f - wet; float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) { const double pk = std::max(std::fabs(L[i]), std::fabs(R[i])); env_ += (pk - env_) * (pk > env_ ? att : rel);
    const double fc = std::min(fs_ * 0.2, 250.0 * std::pow(2.0, range * std::min(1.0, env_ * sens))); const float f = static_cast<float>(2 * std::sin(kPi * fc / fs_));
    for (int s = 0; s < 2; ++s) { lp_[s] += f * bp_[s]; const float hp = ch[s][i] - lp_[s] - q * bp_[s]; bp_[s] += f * hp; ch[s][i] = dry * ch[s][i] + wet * bp_[s] * 1.5f; } }
}
Crusher::Crusher(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); }
void Crusher::reset() { hold_[0] = hold_[1] = lp_[0] = lp_[1] = 0; cnt_ = 0; }
std::string Crusher::text(const std::string& id, double v) const { v = clamp01(v); if (id == "bits") return fmt("%.0f bit", 4 + v * 12); if (id == "down") return fmt("1 / %.0f", 1 + v * 31); return fmt("%.0f %%", v * 100); }
void Crusher::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double steps = std::pow(2.0, std::floor(4 + k_[0].load() * 12) - 1), every = 1 + std::floor(k_[1].load() * 31); const float a = static_cast<float>(1 - std::exp(-2 * kPi * (500.0 + k_[2].load() * k_[2].load() * 19000.0) / fs_)), wet = static_cast<float>(mix_.load()), dry = 1.f - wet; float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) { if (cnt_ <= 0) { for (int s = 0; s < 2; ++s) hold_[s] = static_cast<float>(std::round(ch[s][i] * steps) / steps); cnt_ = every; } cnt_ -= 1;
    for (int s = 0; s < 2; ++s) { lp_[s] += (hold_[s] - lp_[s]) * a; ch[s][i] = dry * ch[s][i] + wet * lp_[s]; } }
}
Exciter::Exciter(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); }
void Exciter::reset() { lp_[0] = lp_[1] = 0; }
std::string Exciter::text(const std::string& id, double v) const { v = clamp01(v); if (id == "freq") return fmt("%.0f Hz", 1000 + v * 7000); return fmt("%.0f %%", v * 100); }
void Exciter::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const float a = static_cast<float>(1 - std::exp(-2 * kPi * (1000.0 + k_[1].load() * 7000.0) / fs_)), drive = static_cast<float>(2.0 + k_[2].load() * 10.0), amt = static_cast<float>(k_[0].load() * mix_.load()); float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) for (int s = 0; s < 2; ++s) { lp_[s] += (ch[s][i] - lp_[s]) * a; const float hi = ch[s][i] - lp_[s]; ch[s][i] += amt * (std::tanh(drive * hi) / drive * 2.f - hi * 0.5f); }
}

}  // namespace am::fx
