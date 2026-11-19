#include "fx.hpp"
#include "samples/samples.hpp"
#include "arrange/decode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace am::fx {
namespace {
constexpr double kPi = 3.14159265358979323846;
double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
std::string fmt(const char* f, double v) { char b[32]; std::snprintf(b, sizeof b, f, v); return b; }
}  // namespace

std::vector<std::pair<std::string, std::string>> catalog() { return {{"reverb", "PLATE REVERB"}, {"aec", "ECHO CANCELLER (AEC)"}, {"hybrid", "TELEPHONE HYBRID"}, {"logic", "LOGIC / GPIO"}, {"delay", "TAPE DELAY"}, {"syncdelay", "SYNC DELAY"}, {"antifeedback", "FEEDBACK KILLER"}, {"analog", "ANALOG CHANNEL"}, {"barrier", "SOUND BARRIER"}, {"dualeq", "DUAL LISTEN EQ"}, {"pitch", "PITCH SHIFTER"}, {"sampler", "SAMPLE PLAYER"}, {"desquash", "DE-SQUASH"}, {"chorus", "STEREO CHORUS"}, {"drive", "TUBE DRIVE"}, {"flanger", "FLANGER"}, {"phaser", "PHASER"}, {"tremolo", "TREMOLO"}, {"wah", "AUTO WAH"}, {"crusher", "BIT CRUSHER"}, {"exciter", "EXCITER"}, {"subsynth", "SUB SYNTH"}, {"tubeamp", "TUBE AMP"}, {"preamp", "PRE AMP"}, {"postamp", "POST AMP"}, {"regulator", "POWER REGULATOR"}, {"protect", "SHORT PROTECT"}, {"rider", "VOICE RIDER"}, {"clean", "VOICE CLEAN"}, {"hall", "HALL REVERB"}, {"room", "ROOM REVERB"}, {"mseq", "MS COLOUR EQ"}, {"isoeq", "ISO EQ 10"}, {"mod", "MOD MACHINE"}, {"opto", "OPTO LEVELER"}, {"glue", "GLUE COMP"}, {"tape", "TAPE MACHINE"}, {"transient", "TRANSIENT SHAPER"}, {"deesser", "DE-ESSER"}, {"maximizer", "MAXIMIZER"}, {"multiband", "MULTIBAND COMP"}, {"imager", "STEREO IMAGER"}, {"autopan", "AUTO PAN"}, {"doubler", "VOCAL DOUBLER"}, {"iamfelement", "IAMF ELEMENT"}, {"iamfrender", "IAMF RENDERER"}, {"wshape", "W-SHAPE TUNER"}, {"convclip", "CONVERTER CLIP"}}; }
std::unique_ptr<Effect> make(const std::string& k, double fs) {
  if (k == "aec") return std::make_unique<EchoCanceller>(fs);
  if (k == "hybrid") return std::make_unique<PhoneHybrid>(fs);
  if (k == "logic") return std::make_unique<LogicObject>(fs);
  if (k == "reverb") return std::make_unique<Reverb>(fs);
  if (k == "delay") return std::make_unique<TapeDelay>(fs);
  if (k == "syncdelay") return std::make_unique<SyncDelay>(fs);
  if (k == "antifeedback") return std::make_unique<FeedbackKiller>(fs);
  if (k == "chorus") return std::make_unique<Chorus>(fs);
  if (k == "analog") return std::make_unique<Analog>(fs);
  if (k == "barrier") return std::make_unique<Barrier>(fs);
  if (k == "dualeq") return std::make_unique<DualEq>(fs);
  if (k == "pitch") return std::make_unique<Pitch>(fs);
  if (k == "sampler") return std::make_unique<SamplePlayer>(fs);
  if (k == "desquash") return std::make_unique<DeSquash>(fs);
  if (k == "flanger") return std::make_unique<Flanger>(fs);
  if (k == "phaser") return std::make_unique<Phaser>(fs);
  if (k == "tremolo") return std::make_unique<Tremolo>(fs);
  if (k == "wah") return std::make_unique<AutoWah>(fs);
  if (k == "crusher") return std::make_unique<Crusher>(fs);
  if (k == "exciter") return std::make_unique<Exciter>(fs);
  if (k == "subsynth") return std::make_unique<SubSynth>(fs);
  if (k == "tubeamp") return std::make_unique<TubeAmp>(fs);
  if (k == "preamp") return std::make_unique<PreAmp>(fs);
  if (k == "postamp") return std::make_unique<PostAmp>(fs);
  if (k == "regulator") return std::make_unique<PowerRegulator>(fs);
  if (k == "protect") return std::make_unique<ShortProtect>(fs);
  if (k == "rider") return std::make_unique<VoiceRider>(fs);
  if (k == "clean") return std::make_unique<VoiceClean>(fs);
  if (k == "hall") return std::make_unique<Hall>(fs);
  if (k == "room") return std::make_unique<Room>(fs);
  if (k == "drive") return std::make_unique<Drive>(fs);
  if (k == "mseq") return std::make_unique<MsEq>(fs);
  if (k == "isoeq") return std::make_unique<IsoEq>(fs);
  if (k == "mod") return std::make_unique<ModMachine>(fs);
  if (k == "opto") return std::make_unique<Opto>(fs);
  if (k == "glue") return std::make_unique<Glue>(fs);
  if (k == "tape") return std::make_unique<Tape>(fs);
  if (k == "transient") return std::make_unique<Transient>(fs);
  if (k == "deesser") return std::make_unique<DeEsser>(fs);
  if (k == "maximizer") return std::make_unique<Maximizer>(fs);
  if (k == "multiband") return std::make_unique<Multiband>(fs);
  if (k == "imager") return std::make_unique<Imager>(fs);
  if (k == "autopan") return std::make_unique<AutoPan>(fs);
  if (k == "doubler") return std::make_unique<Doubler>(fs);
  if (k == "iamfelement") return std::make_unique<IamfElement>(fs);
  if (k == "iamfrender") return std::make_unique<IamfRenderer>(fs);
  if (k == "wshape") return std::make_unique<WShape>(fs);
  if (k == "convclip") return std::make_unique<ConvClip>(fs);
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


// ---------------------------------------------------------------- Sound barrier
Barrier::Barrier(double fs) : Simple(fs) { for (int i = 0; i < 5; ++i) k_[i] = knobDef(i); reset(); }
void Barrier::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; reset(); }
void Barrier::reset() { look_ = std::max(8, static_cast<int>(fs_ * 0.003)); for (auto& d : dl_) d.assign(static_cast<std::size_t>(look_) + 1, 0.f); w_ = 0; hold_ = 0; lp_[0] = lp_[1] = hp_[0] = hp_[1] = hp2_[0] = hp2_[1] = 0; fast_ = slow_ = eHigh_ = eBody_ = 0; g_ = 1; grDb_ = 0; auto_ = 1; peakIn_ = 0; autoDb_ = 0; mach_ = 0; }
std::string Barrier::text(const std::string& id, double v) const {
  v = clamp01(v); if (id == "push") return fmt("+%.1f dB", v * 18.0); if (id == "ceiling") return fmt("%.1f dBFS", -6.0 + v * 6.0); if (id == "auto") return v <= 0.001 ? std::string("OFF") : fmt("%.1f dB", v * 6.0); return fmt("%.0f %%", v * 100);
}
void Barrier::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double manual = std::pow(10.0, k_[0].load() * 18.0 / 20.0), impact = k_[1].load(), smooth = k_[2].load(), ceil = ceilingOf(k_[3].load()), autoAmt = k_[4].load();
  const double push = manual * auto_, aPk = std::exp(-1.0 / (0.5 * fs_)); double pkBlock = 0;
  const float aH = static_cast<float>(std::exp(-2.0 * kPi * 5000.0 / fs_));
  const double aFastUp = std::exp(-1.0 / (0.001 * fs_)), aFastDn = std::exp(-1.0 / (0.020 * fs_)), aSlow = std::exp(-1.0 / (0.060 * fs_)), aE = std::exp(-1.0 / (0.050 * fs_)), rel = 1.0 - std::exp(-1.0 / (0.120 * fs_));
  const std::size_t len = dl_[0].size(); const float wet = static_cast<float>(mix_.load()), dry = 1.f - wet; double worst = 1.0;
  for (std::size_t i = 0; i < n; ++i) {
    float body[2], high[2]; double pb = 0, sh = 0, sb = 0;
    for (int c = 0; c < 2; ++c) { const float x = (c ? R : L)[i]; hp_[c] = x + aH * (hp_[c] - x); hp2_[c] = hp_[c] + aH * (hp2_[c] - hp_[c]); body[c] = hp2_[c]; high[c] = x - body[c];   // body: under 5 kHz (two poles, so little treble leaks into it); the two add up to the signal
      pb = std::max(pb, static_cast<double>(std::fabs(body[c]))); sh += static_cast<double>(high[c]) * high[c]; sb += static_cast<double>(body[c]) * body[c]; }
    // IMPACT: a hit is where the fast follower runs ahead of the slow one
    fast_ = pb > fast_ ? pb + aFastUp * (fast_ - pb) : pb + aFastDn * (fast_ - pb); slow_ = pb + aSlow * (slow_ - pb);
    const double hit = slow_ > 1e-5 ? std::min(1.5, std::max(0.0, fast_ / slow_ - 1.0)) : 0.0, gi = 1.0 + impact * hit * 0.6;   // up to about +5.6 dB on the attack
    // SMOOTH: the treble is held back by how far it stands out against the body (a natural treble sits about 12 dB under it)
    eHigh_ = sh + aE * (eHigh_ - sh); eBody_ = sb + aE * (eBody_ - sb);
    const double over = eBody_ > 1e-9 ? 10.0 * std::log10((eHigh_ + 1e-12) / eBody_) + 12.0 : 0.0, gs = std::pow(10.0, -smooth * std::min(12.0, std::max(0.0, over) * 0.75 + (push - 1.0) * 0.35) / 20.0);
    float y[2]; double pk = 0;
    for (int c = 0; c < 2; ++c) { const double pre = static_cast<double>(body[c]) * gi + static_cast<double>(high[c]) * gs; pkBlock = std::max(pkBlock, std::fabs(pre)); double v = pre * push;
      const double knee = ceil * 1.5; if (std::fabs(v) > ceil * 0.5) { const double s = v < 0 ? -1.0 : 1.0, a = std::fabs(v) - ceil * 0.5; v = s * (ceil * 0.5 + (knee - ceil * 0.5) * std::tanh(a / (knee - ceil * 0.5))); }   // a soft knee from half the ceiling up
      y[c] = static_cast<float>(v); pk = std::max(pk, std::fabs(v)); }
    // CEILING: the gain needed for this sample is taken at once and held until the sample has left the delay; then it recovers
    const double need = pk > ceil ? ceil / pk : 1.0;
    if (need < g_) { g_ = need; hold_ = look_; } else if (hold_ > 0) --hold_; else g_ += rel * (1.0 - g_);
    worst = std::min(worst, g_);
    const std::size_t rd = (w_ + 1) % len;
    for (int c = 0; c < 2; ++c) { const float x = (c ? R : L)[i]; const float old = dl_[c][rd]; dl_[c][w_] = y[c];
      float o = static_cast<float>(static_cast<double>(old) * g_); const float cf = static_cast<float>(ceil); o = o > cf ? cf : o < -cf ? -cf : o;   // the last word: nothing above the ceiling
      (c ? R : L)[i] = dry * x + wet * o; }
    w_ = rd;
  }
  for (int c = 0; c < 2; ++c) { if (std::fabs(hp_[c]) < 1e-20f) hp_[c] = 0; if (std::fabs(hp2_[c]) < 1e-20f) hp2_[c] = 0; }
  grDb_ = -20.0 * std::log10(std::max(1e-6, worst));
  // AUTO, the run-up: the loudest of the last half second is brought to where the barrier works by the amount asked for
  const double decay = std::pow(aPk, static_cast<double>(n)); peakIn_ = std::max(pkBlock, peakIn_ * decay);
  mach_ = peakIn_ * push / ceil;
  if (autoAmt <= 0.001) auto_ = 1.0;
  else if (peakIn_ > 1e-4) {   // there is music (above -80 dBFS): climb in about 1.5 s, back off in 0.3 s; in silence, wait
    const double want = std::max(1.0, std::min(15.85, ceil * std::pow(10.0, autoAmt * 6.0 / 20.0) / (peakIn_ * manual)));
    const double tau = want > auto_ ? 1.5 : 0.3, a = std::exp(-static_cast<double>(n) / (tau * fs_)); auto_ = want + a * (auto_ - want);
  }
  autoDb_ = 20.0 * std::log10(auto_);
}
// ---------------------------------------------------------------- Dual listen EQ
DualEq::DualEq(double fs) : Simple(fs) { for (int i = 0; i < 4; ++i) k_[i] = knobDef(i); reset(); }
void DualEq::reset() { lp_[0] = lp_[1] = hp_[0] = hp_[1] = 0; eLow_ = eMid_ = eHigh_ = 0; gl_ = gh_ = 1; gLowDb_ = 0; gHighDb_ = 0; }
std::string DualEq::text(const std::string& id, double v) const {
  v = clamp01(v); if (id == "bass" || id == "treble") return fmt("%+.1f dB", (v - 0.5) * 12.0); if (id == "range") return fmt("%.0f dB", v * 12.0); return fmt("%.1f s", 4.0 * std::pow(0.075, v));
}
void DualEq::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  // a natural balance of music against its middle: the bass about level with it, the treble about 12 dB under; BASS / TREBLE move those marks
  const double wantLow = 0.0 + (k_[0].load() - 0.5) * 12.0, wantHigh = -12.0 + (k_[1].load() - 0.5) * 12.0, range = k_[2].load() * 12.0, tau = 4.0 * std::pow(0.075, k_[3].load());
  const float aL = static_cast<float>(std::exp(-2.0 * kPi * 200.0 / fs_)), aH = static_cast<float>(std::exp(-2.0 * kPi * 4000.0 / fs_));
  const double aE = std::exp(-1.0 / (0.3 * fs_)), aG = std::exp(-static_cast<double>(n) / (tau * fs_));   // loudness over 0.3 s; the gains move once a block
  const float wet = static_cast<float>(mix_.load()), dry = 1.f - wet; float* ch[2] = {L, R};
  const float gl = static_cast<float>(gl_), gh = static_cast<float>(gh_);
  for (std::size_t i = 0; i < n; ++i) {
    double sl = 0, sm = 0, sh = 0;
    for (int c = 0; c < 2; ++c) { const float x = ch[c][i]; lp_[c] = x + aL * (lp_[c] - x); hp_[c] = x + aH * (hp_[c] - x);   // lp_: under 200 Hz; hp_: under 4 kHz
      const float low = lp_[c], high = x - hp_[c], mid = x - low - high; sl += static_cast<double>(low) * low; sm += static_cast<double>(mid) * mid; sh += static_cast<double>(high) * high;
      ch[c][i] = dry * x + wet * (low * gl + mid + high * gh); }
    eLow_ = sl + aE * (eLow_ - sl); eMid_ = sm + aE * (eMid_ - sm); eHigh_ = sh + aE * (eHigh_ - sh);
  }
  for (int c = 0; c < 2; ++c) { if (std::fabs(lp_[c]) < 1e-20f) lp_[c] = 0; if (std::fabs(hp_[c]) < 1e-20f) hp_[c] = 0; }
  if (eMid_ > 2e-6) {   // the middle is there (above about -54 dBFS): listen and move; below that, hold
    const double lowDb = 10.0 * std::log10((eLow_ + 1e-12) / eMid_), highDb = 10.0 * std::log10((eHigh_ + 1e-12) / eMid_);
    const double tl = std::max(-range, std::min(range, wantLow - lowDb)), th = std::max(-range, std::min(range, wantHigh - highDb));
    const double nowL = 20.0 * std::log10(gl_), nowH = 20.0 * std::log10(gh_), nl = tl + aG * (nowL - tl), nh = th + aG * (nowH - th);
    gl_ = std::pow(10.0, nl / 20.0); gh_ = std::pow(10.0, nh / 20.0); gLowDb_ = nl; gHighDb_ = nh;
  }
}
// ---------------------------------------------------------------- Pitch shifter
Pitch::Pitch(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); reset(); }
void Pitch::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; reset(); }
void Pitch::reset() { for (auto& b : buf_) b.assign(static_cast<std::size_t>(fs_ * 0.09) + 32, 0.f); w_ = 0; ph_ = 0; }   // room for the longest window
std::string Pitch::text(const std::string& id, double v) const { v = clamp01(v); if (id == "pitch") return fmt("%+.0f st", std::round((v - 0.5) * 24.0)); if (id == "window") return fmt("%.0f ms", 20.0 + v * 70.0); return fmt("%+.0f cents", (v - 0.5) * 100.0); }
void Pitch::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const std::size_t len = buf_[0].size(); win_ = std::max(64, std::min(static_cast<int>(len) - 16, static_cast<int>(fs_ * (0.02 + k_[2].load() * 0.07))));
  const double ratio = std::pow(2.0, semitones(k_[0].load(), k_[1].load()) / 12.0), inc = (1.0 - ratio) / static_cast<double>(win_);
  const float wet = static_cast<float>(mix_.load()), dry = 1.f - wet; float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) {
    for (int c = 0; c < 2; ++c) buf_[c][w_] = ch[c][i];
    float out[2] = {0, 0};
    for (int tap = 0; tap < 2; ++tap) {
      double p = ph_ + 0.5 * tap; p -= std::floor(p);
      const double d = p * static_cast<double>(win_) + 2.0; double rp = static_cast<double>(w_) - d; if (rp < 0) rp += static_cast<double>(len);
      const std::size_t i0 = static_cast<std::size_t>(rp) % len, i1 = (i0 + 1) % len; const float fr = static_cast<float>(rp - std::floor(rp)), g = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * p));
      for (int c = 0; c < 2; ++c) out[c] += g * (buf_[c][i0] + fr * (buf_[c][i1] - buf_[c][i0]));
    }
    for (int c = 0; c < 2; ++c) ch[c][i] = dry * ch[c][i] + wet * out[c];
    ph_ += inc; ph_ -= std::floor(ph_); w_ = (w_ + 1) % len;
  }
}
// ---------------------------------------------------------------- Analog channel
Analog::Analog(double fs) : Simple(fs) { for (int i = 0; i < 4; ++i) k_[i] = knobDef(i); reset(); }
void Analog::reset() { lp_[0] = lp_[1] = 0; }
std::string Analog::text(const std::string& id, double v) const {
  v = clamp01(v); if (id == "drive") return fmt("+%.0f dB", v * 18.0); if (id == "tone") return fmt("%.1f kHz", 3.0 * std::pow(7.0, v)); if (id == "output") return fmt("%+.1f dB", (v - 0.5) * 24.0);
  return fmt("%.0f %%", v * 100);
}
void Analog::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double g = std::pow(10.0, k_[0].load() * 18.0 / 20.0), b = k_[1].load() * 0.4, tb = std::tanh(b), norm = 1.0 / (g * (1.0 - tb * tb));   // unity gain for a quiet signal
  const float a = static_cast<float>(std::exp(-2.0 * kPi * std::min(0.45 * fs_, 3000.0 * std::pow(7.0, k_[2].load())) / fs_)), out = static_cast<float>(std::pow(10.0, (k_[3].load() - 0.5) * 24.0 / 20.0));
  const float wet = static_cast<float>(mix_.load()), dry = 1.f - wet; float* ch[2] = {L, R};
  for (int c = 0; c < 2; ++c) { float lp = lp_[c];
    for (std::size_t i = 0; i < n; ++i) { const float x = ch[c][i]; const float y = static_cast<float>((std::tanh(g * static_cast<double>(x) + b) - tb) * norm);
      lp = y + a * (lp - y); ch[c][i] = dry * x + wet * lp * out; }
    lp_[c] = std::fabs(lp) < 1e-20f ? 0.f : lp; }
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
FeedbackKiller::FeedbackKiller(double fs) : Simple(fs) { for (int i = 0; i < count(); ++i) k_[i] = knobDef(i); setSampleRate(fs); }
void FeedbackKiller::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; frame_.assign(kN, 0.f); reset(); }
int FeedbackKiller::fixedCount() const { return std::clamp(static_cast<int>(std::lround(clamp01(k_[6].load()) * 24.0)), 0, kNotches); }
void FeedbackKiller::reset() { for (Notch& n : n_) n = Notch{}; fill_ = 0; lastBin_ = -1; streak_ = 0; active_ = 0; }
std::string FeedbackKiller::text(const std::string& id, double v) const {
  v = clamp01(v); if (id == "sens") return fmt("%.0f dB", 24.0 - v * 15.0); if (id == "depth") return fmt("-%.0f dB", 6.0 + v * 24.0);
  if (id == "width") return fmt("Q %.0f", 60.0 - v * 50.0); if (id == "mode") return v >= 0.5 ? "LIVE" : "HOLD";
  if (id == "lift") return fmt("%.0f s", 2.0 + v * 58.0);
  if (id == "program") { static const char* p[] = {"SPEECH", "MUSIC LO", "MUSIC MID", "MUSIC HI"}; return p[std::min(3, static_cast<int>(v * 4.0))]; }
  if (id == "fixed") return fmt("%.0f / 24", std::round(v * 24.0));
  return fmt("%.0f %%", v * 100);
}
void FeedbackKiller::design(Notch& n) const {   // a peaking cut (RBJ): gain depthDb at hz, the band as narrow as WIDTH says
  static const double progQ[] = {1.0, 1.6, 2.2, 3.0};   // MUSIC keeps the filters narrower than SPEECH: less of the programme is touched
  const double q = (60.0 - clamp01(k_[2].load()) * 50.0) * progQ[std::min(3, static_cast<int>(clamp01(k_[5].load()) * 4.0))], A = std::pow(10.0, n.depthDb / 40.0), w = 2.0 * kPi * std::min(n.hz, fs_ * 0.45) / fs_, al = std::sin(w) / (2.0 * q), c = std::cos(w), a0 = 1.0 + al / A;
  n.b0 = (1.0 + al * A) / a0; n.b1 = -2.0 * c / a0; n.b2 = (1.0 - al * A) / a0; n.a1 = -2.0 * c / a0; n.a2 = (1.0 - al / A) / a0;
}
void FeedbackKiller::analyse() {
  // the spectrum of the frame, Hann window (a plain radix-2 FFT on the stack: 1024 points, once every 21 ms)
  static thread_local double re[kN], im[kN];
  for (int i = 0; i < kN; ++i) { re[i] = frame_[static_cast<std::size_t>(i)] * (0.5 - 0.5 * std::cos(2.0 * kPi * i / (kN - 1))); im[i] = 0; }
  for (int i = 1, j = 0; i < kN; ++i) { int bit = kN >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
  for (int len = 2; len <= kN; len <<= 1) { const double ang = -2.0 * kPi / len, wr = std::cos(ang), wi = std::sin(ang);
    for (int i = 0; i < kN; i += len) { double cr = 1, ci = 0; for (int k = 0; k < len / 2; ++k) { const int a = i + k, b = a + len / 2; const double xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr; re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi; const double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } } }
  // the loudest bin between 80 Hz and 12 kHz, and the level of everything else
  const int lo = std::max(2, static_cast<int>(80.0 * kN / fs_)), hi = std::min(kN / 2 - 2, static_cast<int>(12000.0 * kN / fs_)); int pk = -1; double pv = 0, total = 0;
  static thread_local double mag[kN / 2];
  for (int k = lo; k <= hi; ++k) { mag[k] = re[k] * re[k] + im[k] * im[k]; total += mag[k]; if (mag[k] > pv) { pv = mag[k]; pk = k; } }
  for (Notch& n : n_) if (n.on) ++n.idle;
  if (pk < 0 || pv <= 0) { streak_ = 0; lastBin_ = -1; return; }
  const double peak = mag[pk - 1 < lo ? pk : pk - 1] + mag[pk] + mag[pk + 1 > hi ? pk : pk + 1], rest = std::max(1e-18, (total - peak) / std::max(1, hi - lo - 2));   // the peak's own three bins against the average of the others
  const double standDb = 10.0 * std::log10(pv / rest), levelDb = 10.0 * std::log10(pv / (0.25 * kN * kN * 0.25) + 1e-18);   // 0 dB = a full-scale sine
  const double need = 24.0 - clamp01(k_[0].load()) * 15.0;
  if (standDb < need || levelDb < -50.0) { streak_ = 0; lastBin_ = -1; return; }
  // a howl is a PURE tone (no overtone an octave up worth mentioning) that does not move: measured against the bin where the run
  // started, so that a slow glide (a voice sliding, a sweep) breaks the run even though each frame is close to the one before
  const bool pure = 2 * pk > hi || mag[2 * pk] + mag[2 * pk - 1] + mag[2 * pk + 1] < pv * 0.01;
  if (pure && lastBin_ >= 0 && std::abs(pk - lastBin_) <= 1) ++streak_; else { streak_ = pure ? 1 : 0; lastBin_ = pure ? pk : -1; }
  if (streak_ < 16) return;                                           // a third of a second on one frequency
  streak_ = 13;                                                       // keeps ringing: act again in three frames
  const double l = std::sqrt(mag[pk - 1]), c = std::sqrt(mag[pk]), r = std::sqrt(mag[pk + 1]), den = l - 2 * c + r, off = std::fabs(den) > 1e-12 ? 0.5 * (l - r) / den : 0.0;
  const double hz = (pk + std::max(-0.5, std::min(0.5, off))) * fs_ / kN, maxDepth = -(6.0 + clamp01(k_[1].load()) * 24.0);
  Notch* use = nullptr;
  for (Notch& n : n_) if (n.on && std::fabs(n.hz - hz) < std::max(12.0, hz * 0.03)) use = &n;      // the same howl: deepen its notch
  if (!use) { for (Notch& n : n_) if (!n.on) { use = &n; break; }   // FIXED slots come first: the first howls found (the room's own) stay
    if (!use) { const int f = fixedCount(); use = f < kNotches ? &n_[f] : &n_[0]; for (int i = f; i < kNotches; ++i) if (n_[i].idle > use->idle) use = &n_[i]; *use = Notch{}; }   // all in use: the LIVE one needed longest ago
    use->on = true; use->hz = hz; use->depthDb = 0; }
  use->hz = 0.7 * use->hz + 0.3 * hz; use->depthDb = std::max(maxDepth, use->depthDb - 6.0); use->idle = 0; design(*use);
}
void FeedbackKiller::process(float* L, float* R, std::size_t n) {
  if (bypass_.load() || frame_.empty()) return;
  const bool live = k_[3].load() >= 0.5; const long giveBack = static_cast<long>((2.0 + clamp01(k_[4].load()) * 58.0) * fs_ / kN); const int fixedN = fixedCount();
  for (std::size_t i = 0; i < n; ++i) {
    frame_[fill_++] = 0.5f * (L[i] + R[i]);                           // what is analysed is the input: a notch does not hide the howl from the detector's own memory of where it was
    if (fill_ >= static_cast<std::size_t>(kN)) { fill_ = 0; analyse();
      if (live) for (int k = fixedN; k < kNotches; ++k) if (n_[k].on && n_[k].idle > giveBack) n_[k] = Notch{};   // LIVE filters lift after LIFT TIME without that howl; FIXED ones stay
      int on = 0; for (const Notch& nt : n_) on += nt.on ? 1 : 0; active_.store(on); }
    double x[2] = {L[i], R[i]};
    for (Notch& nt : n_) { if (!nt.on) continue;
      for (int ch = 0; ch < 2; ++ch) { const double y = nt.b0 * x[ch] + nt.z[ch][0]; nt.z[ch][0] = nt.b1 * x[ch] - nt.a1 * y + nt.z[ch][1]; nt.z[ch][1] = nt.b2 * x[ch] - nt.a2 * y; x[ch] = y; } }
    L[i] = static_cast<float>(x[0]); R[i] = static_cast<float>(x[1]);
  }
}
SyncDelay::SyncDelay(double fs) : Simple(fs) { for (int i = 0; i < 5; ++i) k_[i] = knobDef(i); setSampleRate(fs); }
double SyncDelay::beatsOf(int note) { static const double b[] = {2.0, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25}; return b[note < 0 ? 0 : note > 5 ? 5 : note]; }   // in quarter notes
void SyncDelay::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; for (auto& b : buf_) b.assign(static_cast<std::size_t>(fs_ * 2.1), 0.f); reset(); }   // the longest: a half note at 60 BPM = 2 s
void SyncDelay::reset() { for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.f); w_ = 0; lp_[0] = lp_[1] = 0; }
std::string SyncDelay::text(const std::string& id, double v) const {
  v = clamp01(v); if (id == "tempo") return fmt("%.0f BPM", bpmOf(v));
  if (id == "note") { static const char* n[] = {"1/2", "1/4", "1/8 dotted", "1/8", "1/8 triplet", "1/16"}; return n[noteOf(v)]; }
  return fmt("%.0f %%", v * 100);
}
void SyncDelay::process(float* L, float* R, std::size_t n) {
  if (bypass_.load() || buf_[0].empty()) return;
  const std::size_t len = buf_[0].size(); const double sec = 60.0 / bpmOf(k_[0].load()) * beatsOf(noteOf(k_[1].load()));
  const std::size_t d = std::max<std::size_t>(1, std::min(len - 1, static_cast<std::size_t>(sec * fs_ + 0.5)));
  const float fb = static_cast<float>(k_[2].load() * 0.92), wet = static_cast<float>(mix_.load()), dry = 1.f - wet, width = static_cast<float>(k_[4].load());
  const float a = static_cast<float>(std::exp(-2.0 * kPi * (600.0 + k_[3].load() * k_[3].load() * 11400.0) / fs_));
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t r = (w_ + len - d) % len; const float dl = buf_[0][r], dr = buf_[1][r];
    lp_[0] = dl * (1 - a) + lp_[0] * a; lp_[1] = dr * (1 - a) + lp_[1] * a;
    const float in = 0.5f * (L[i] + R[i]);
    // WIDTH 0: each side feeds itself; WIDTH 1: the input goes to the left line only and the lines feed each other, so the echoes alternate
    buf_[0][w_] = (L[i] * (1 - width) + in * width) + fb * (lp_[0] * (1 - width) + lp_[1] * width);
    buf_[1][w_] = (R[i] * (1 - width)) + fb * (lp_[1] * (1 - width) + lp_[0] * width);
    w_ = (w_ + 1) % len;
    L[i] = L[i] * dry + dl * wet; R[i] = R[i] * dry + dr * wet;
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
SubSynth::SubSynth(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); }
void SubSynth::reset() { lp1_ = lp2_ = env_ = o1_ = o2_ = last_ = 0; flip_ = false; }
std::string SubSynth::text(const std::string& id, double v) const { v = clamp01(v); if (id == "range") return fmt("%.0f Hz", 60.0 + v * 90.0); if (id == "tight") return fmt("%.0f ms", 120.0 - v * 100.0); return fmt("%.0f %%", v * 100); }
void SubSynth::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double fc = 60.0 + clamp01(k_[1].load()) * 90.0, a = 1.0 - std::exp(-2 * kPi * fc / fs_), ao = 1.0 - std::exp(-2 * kPi * (fc * 0.6) / fs_);
  const double rel = 1.0 - std::exp(-1.0 / ((0.120 - clamp01(k_[2].load()) * 0.100) * fs_)), att = 1.0 - std::exp(-1.0 / (0.005 * fs_));
  const double amt = clamp01(k_[0].load()) * mix_.load() * 2.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double x = 0.5 * (static_cast<double>(L[i]) + R[i]);
    lp1_ += (x - lp1_) * a; lp2_ += (lp1_ - lp2_) * a;                    // the bass, 12 dB/oct below RANGE
    const double e = std::fabs(lp2_); env_ += (e - env_) * (e > env_ ? att : rel);
    if (last_ <= 0 && lp2_ > 0) flip_ = !flip_; last_ = lp2_;             // half the frequency: one flip per cycle
    const double sq = (flip_ ? 1.0 : -1.0) * env_;
    o1_ += (sq - o1_) * ao; o2_ += (o1_ - o2_) * ao;                       // the square smoothed into a round sub
    const float add = static_cast<float>(std::isfinite(o2_) ? o2_ * amt : 0.0); L[i] += add; R[i] += add;
  }
}
void Exciter::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const float a = static_cast<float>(1 - std::exp(-2 * kPi * (1000.0 + k_[1].load() * 7000.0) / fs_)), drive = static_cast<float>(2.0 + k_[2].load() * 10.0), amt = static_cast<float>(k_[0].load() * mix_.load()); float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) for (int s = 0; s < 2; ++s) { lp_[s] += (ch[s][i] - lp_[s]) * a; const float hi = ch[s][i] - lp_[s]; ch[s][i] += amt * (std::tanh(drive * hi) / drive * 2.f - hi * 0.5f); }
}


// ---------------------------------------------------------------- Tube amp, Pre amp, Post amp, Hall, Room
TubeAmp::TubeAmp(double fs) : Simple(fs) { for (int i = 0; i < 6; ++i) k_[i] = knobDef(i); }
void TubeAmp::reset() { for (int s = 0; s < 2; ++s) hp_[s] = lo_[s] = hi_[s] = pr_[s] = c1_[s] = c2_[s] = dc_[s] = 0; sag_ = 0; }
std::string TubeAmp::text(const std::string& id, double v) const { v = clamp01(v); if (id == "gain") return fmt("%.1f", v * 10); if (id == "master") return fmt("%.1f dB", -30 + v * 36); if (id == "presence") return fmt("%.1f", v * 10); return fmt("%+.1f dB", (v - 0.5) * 24); }
void TubeAmp::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const float pre = static_cast<float>(std::pow(10.0, (k_[0].load() * 46.0) / 20.0)), gB = static_cast<float>(std::pow(10.0, (k_[1].load() - 0.5) * 24 / 20)), gM = static_cast<float>(std::pow(10.0, (k_[2].load() - 0.5) * 24 / 20)), gT = static_cast<float>(std::pow(10.0, (k_[3].load() - 0.5) * 24 / 20));
  const float pres = static_cast<float>(k_[4].load() * 1.5), master = static_cast<float>(std::pow(10.0, (-30 + k_[5].load() * 36) / 20)), wet = static_cast<float>(mix_.load()), dry = 1.f - wet;
  auto pole = [this](double f) { return static_cast<float>(1 - std::exp(-2 * kPi * f / fs_)); }; const float aHp = pole(90), aLo = pole(220), aHi = pole(2200), aPr = pole(3500), aCab = pole(4800), aDc = pole(12); const double sagA = 1 - std::exp(-1.0 / (0.004 * fs_)), sagR = 1 - std::exp(-1.0 / (0.12 * fs_));
  const float bias = 0.18f, b0 = std::tanh(bias); float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) {
    double pk = 0;
    for (int s = 0; s < 2; ++s) { const float x = ch[s][i]; hp_[s] += (x - hp_[s]) * aHp; float v = (x - hp_[s]) * pre;
      v = std::tanh(v + bias) - b0;                                                          // the pre-amp valve: asymmetric, so even harmonics
      lo_[s] += (v - lo_[s]) * aLo; hi_[s] += (v - hi_[s]) * aHi; const float low = lo_[s], high = v - hi_[s], mid = hi_[s] - lo_[s]; v = low * gB + mid * gM + high * gT;   // tone stack
      v = std::tanh(v * static_cast<float>(1.6 - 0.9 * sag_)) * 0.9f;                         // the power stage, softer while it sags
      pr_[s] += (v - pr_[s]) * aPr; v += (v - pr_[s]) * pres;                                 // presence
      c1_[s] += (v - c1_[s]) * aCab; c2_[s] += (c1_[s] - c2_[s]) * aCab; v = c2_[s];           // the cabinet: 12 dB / octave above 4.8 kHz
      dc_[s] += (v - dc_[s]) * aDc; v -= dc_[s]; pk = std::max(pk, static_cast<double>(std::fabs(v)));
      ch[s][i] = dry * x + wet * v * master; }
    sag_ += (std::min(1.0, pk) - sag_) * (pk > sag_ ? sagA : sagR);
  }
}
PreAmp::PreAmp(double fs) : Simple(fs) { for (int i = 0; i < 4; ++i) k_[i] = knobDef(i); }
void PreAmp::reset() { hp_[0] = hp_[1] = dc_[0] = dc_[1] = 0; }
std::string PreAmp::text(const std::string& id, double v) const { v = clamp01(v); if (id == "gain") return fmt("%+.1f dB", -12 + v * 48); if (id == "lowcut") return v < 0.02 ? std::string("off") : fmt("%.0f Hz", 20 + v * 280); if (id == "out") return fmt("%+.1f dB", -24 + v * 36); return fmt("%.0f %%", v * 100); }
void PreAmp::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const float g = static_cast<float>(std::pow(10.0, (-12 + k_[0].load() * 48) / 20)), out = static_cast<float>(std::pow(10.0, (-24 + k_[3].load() * 36) / 20)), warm = static_cast<float>(k_[2].load()); const double lc = k_[1].load();
  const float aHp = lc < 0.02 ? 0.f : static_cast<float>(1 - std::exp(-2 * kPi * (20 + lc * 280) / fs_)), aDc = static_cast<float>(1 - std::exp(-2 * kPi * 8 / fs_)); float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) for (int s = 0; s < 2; ++s) { float v = ch[s][i]; if (aHp > 0) { hp_[s] += (v - hp_[s]) * aHp; v -= hp_[s]; } v *= g;
    if (warm > 0) { const float sat = std::tanh(v * (1 + 2 * warm)) / (1 + 2 * warm) + 0.25f * warm * v * v / (1 + v * v); v = v * (1 - warm) + sat * warm; dc_[s] += (v - dc_[s]) * aDc; v -= dc_[s]; }   // second harmonic from the squared term, its offset removed
    const float y = v * out; ch[s][i] = 8.f * std::tanh(y / 8.f); }   // the supply rails: like a real preamp it runs out of voltage near +18 dBFS instead of passing any level
}
PostAmp::PostAmp(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); }
void PostAmp::reset() { lp_[0] = lp_[1] = 0; }
std::string PostAmp::text(const std::string& id, double v) const { v = clamp01(v); if (id == "level") return fmt("%+.1f dB", -24 + v * 36); if (id == "tilt") return fmt("%+.1f dB", (v - 0.5) * 12); return v < 0.01 ? std::string("off") : fmt("%.1f dBFS", -v * 12); }
void PostAmp::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const float g = static_cast<float>(std::pow(10.0, (-24 + k_[0].load() * 36) / 20)), tilt = static_cast<float>((k_[1].load() - 0.5) * 12), gLo = static_cast<float>(std::pow(10.0, -tilt / 40)), gHi = static_cast<float>(std::pow(10.0, tilt / 40)), a = static_cast<float>(1 - std::exp(-2 * kPi * 1000 / fs_));
  const double c = k_[2].load(); const float ceil = static_cast<float>(std::pow(10.0, -c * 12 / 20)); float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) for (int s = 0; s < 2; ++s) { float v = ch[s][i]; lp_[s] += (v - lp_[s]) * a; v = (lp_[s] * gLo + (v - lp_[s]) * gHi) * g; if (c >= 0.01) v = ceil * std::tanh(v / ceil); ch[s][i] = v; }
}

PowerRegulator::PowerRegulator(double fs) : Simple(fs) { for (int i = 0; i < 4; ++i) k_[i] = knobDef(i); }
void PowerRegulator::reset() { ms_ = 0; g_ = 1; gainDb_ = 0; }
std::string PowerRegulator::text(const std::string& id, double v) const { v = clamp01(v); if (id == "target") return fmt("%.1f dBFS", -30 + v * 24); if (id == "range") return fmt("%.1f dB", v * 12); if (id == "speed") return fmt("%.1f s", 0.2 + v * 4.8); return fmt("%.1f dBFS", -12 + v * 12); }
void PowerRegulator::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double target = -30 + k_[0].load() * 24, range = k_[1].load() * 12, secs = 0.2 + k_[2].load() * 4.8; const float ceil = static_cast<float>(std::pow(10.0, (-12 + k_[3].load() * 12) / 20));
  const double aMs = 1 - std::exp(-1.0 / (0.3 * fs_)), aG = 1 - std::exp(-1.0 / (secs * fs_));   // the level over 300 ms; the gain moves at SPEED
  for (std::size_t i = 0; i < n; ++i) {
    const double p = 0.5 * (static_cast<double>(L[i]) * L[i] + static_cast<double>(R[i]) * R[i]); ms_ += (p - ms_) * aMs;
    const double lvl = 10 * std::log10(ms_ + 1e-12);
    if (lvl > -60) { const double want = std::pow(10.0, std::clamp(target - lvl, -range, range) / 20); g_ += (want - g_) * aG; }   // silence: hold the gain, do not turn the noise up
    float l = static_cast<float>(L[i] * g_), r = static_cast<float>(R[i] * g_);
    L[i] = l > ceil ? ceil : l < -ceil ? -ceil : l; R[i] = r > ceil ? ceil : r < -ceil ? -ceil : r; }
  gainDb_ = 20 * std::log10(g_ + 1e-12);
}
ShortProtect::ShortProtect(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); }
void ShortProtect::reset() { over_ = safe_ = 0; dc_[0] = dc_[1] = 0; g_ = 1; cut_ = false; tripped_ = false; }
std::string ShortProtect::text(const std::string& id, double v) const { v = clamp01(v); if (id == "threshold") return fmt("%.1f dBFS", -12 + v * 12); if (id == "time") return fmt("%.0f ms", 5 + v * 495); return fmt("%.1f s", 0.5 + v * 9.5); }
void ShortProtect::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double thr = std::pow(10.0, (-12 + k_[0].load() * 12) / 20), tripAfter = (5 + k_[1].load() * 495) * 1e-3 * fs_, holdFor = (0.5 + k_[2].load() * 9.5) * fs_;
  const double aDc = 1 - std::exp(-1.0 / (0.5 * fs_)), aCut = 1 - std::exp(-1.0 / (0.003 * fs_)), aBack = 1 - std::exp(-1.0 / (0.25 * fs_));   // DC over half a second; cut in 3 ms, back in 250 ms
  for (std::size_t i = 0; i < n; ++i) {
    const double l = L[i], r = R[i]; dc_[0] += (l - dc_[0]) * aDc; dc_[1] += (r - dc_[1]) * aDc;
    const bool fault = std::fabs(l) >= thr || std::fabs(r) >= thr || std::fabs(dc_[0]) > 0.25 || std::fabs(dc_[1]) > 0.25;   // at or over the threshold, or a quarter of full scale of DC
    if (fault) { over_ += 1; safe_ = 0; } else { over_ = std::max(0.0, over_ - 0.25); safe_ += 1; }   // a short gap inside a fault does not clear it at once
    if (!cut_ && over_ >= tripAfter) { cut_ = true; trips_.fetch_add(1); }
    if (cut_ && safe_ >= holdFor) { cut_ = false; over_ = 0; }
    g_ += ((cut_ ? 0.0 : 1.0) - g_) * (cut_ ? aCut : aBack);
    if (!cut_ && g_ > 0.999) g_ = 1;   // back: exactly unity again, so the sound is untouched bit for bit
    if (g_ < 0.9999) { L[i] = static_cast<float>(l * g_); R[i] = static_cast<float>(r * g_); } }
  tripped_ = cut_;
}

VoiceRider::VoiceRider(double fs) : Simple(fs) { for (int i = 0; i < 4; ++i) k_[i] = knobDef(i); }
void VoiceRider::reset() { hp_ = lp_ = ms_ = 0; g_ = 1; gainDb_ = 0; speaking_ = false; }
std::string VoiceRider::text(const std::string& id, double v) const { v = clamp01(v); if (id == "target") return fmt("%.1f dBFS", -30 + v * 20); if (id == "range") return fmt("%.1f dB", v * 12); if (id == "speed") return fmt("%.0f ms", 50 + v * 950); return fmt("%.0f dBFS", -70 + v * 40); }
void VoiceRider::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double target = -30 + k_[0].load() * 20, range = k_[1].load() * 12, secs = (50 + k_[2].load() * 950) * 1e-3, sense = -70 + k_[3].load() * 40;
  const double aHp = 1 - std::exp(-2 * 3.14159265358979 * 150 / fs_), aLp = 1 - std::exp(-2 * 3.14159265358979 * 4000 / fs_), aMs = 1 - std::exp(-1.0 / (0.05 * fs_)), aG = 1 - std::exp(-1.0 / (secs * fs_));
  bool spoke = false;
  for (std::size_t i = 0; i < n; ++i) {
    const double m = 0.5 * (static_cast<double>(L[i]) + R[i]); hp_ += (m - hp_) * aHp; lp_ += ((m - hp_) - lp_) * aLp;   // the voice band: above 150 Hz, below 4 kHz
    ms_ += (lp_ * lp_ - ms_) * aMs; const double lvl = 10 * std::log10(ms_ + 1e-12);
    if (lvl > sense) { spoke = true; const double want = std::pow(10.0, std::clamp(target - lvl, -range, range) / 20); g_ += (want - g_) * aG; }   // speaking: ride; silent: hold
    L[i] = static_cast<float>(L[i] * g_); R[i] = static_cast<float>(R[i] * g_); }
  gainDb_ = 20 * std::log10(g_ + 1e-12); speaking_ = spoke;
}
VoiceClean::VoiceClean(double fs) : Simple(fs) { for (int i = 0; i < 3; ++i) k_[i] = knobDef(i); reset(); }
void VoiceClean::reset() { for (auto& c : lp_) for (double& v : c) v = 0; for (int b = 0; b < kBands; ++b) { env_[b] = fast_[b] = slow_[b] = 0; nf_[b] = 1e-5; g_[b] = 1; } redDb_ = 0; }
std::string VoiceClean::text(const std::string& id, double v) const { v = clamp01(v); if (id == "denoise") return v < 0.01 ? std::string("off") : fmt("-%.0f dB", v * 24); if (id == "dereverb") return v < 0.01 ? std::string("off") : fmt("-%.0f dB", v * 12); return fmt("%+.1f dB", (v - 0.5) * 24); }
void VoiceClean::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  const double dn = k_[0].load(), dr = k_[1].load(), out = std::pow(10.0, (k_[2].load() - 0.5) * 24 / 20);
  const double floorDn = std::pow(10.0, -dn * 24 / 20), floorDr = std::pow(10.0, -dr * 12 / 20);
  static const double kXover[3] = {250, 1500, 5000}; double a[3]; for (int k = 0; k < 3; ++k) a[k] = 1 - std::exp(-2 * 3.14159265358979 * kXover[k] / fs_);
  const double aAtt = 1 - std::exp(-1.0 / (0.005 * fs_)), aRel = 1 - std::exp(-1.0 / (0.08 * fs_)), nfUp = std::pow(10.0, 6.0 / 20 / fs_);   // the floor creeps up 6 dB a second and drops at once
  const double aFastA = 1 - std::exp(-1.0 / (0.001 * fs_)), aFastR = 1 - std::exp(-1.0 / (0.03 * fs_)), aSlowA = 1 - std::exp(-1.0 / (0.08 * fs_)), aSlowR = 1 - std::exp(-1.0 / (0.4 * fs_)), aG = 1 - std::exp(-1.0 / (0.004 * fs_));
  double worst = 1; float* ch[2] = {L, R};
  for (std::size_t i = 0; i < n; ++i) {
    double band[2][kBands];
    for (int c = 0; c < 2; ++c) { const double x = ch[c][i]; for (int k = 0; k < 3; ++k) lp_[c][k] += (x - lp_[c][k]) * a[k];
      band[c][0] = lp_[c][0]; band[c][1] = lp_[c][1] - lp_[c][0]; band[c][2] = lp_[c][2] - lp_[c][1]; band[c][3] = x - lp_[c][2]; }   // the four add back to x exactly
    double y[2] = {0, 0};
    for (int b = 0; b < kBands; ++b) {
      const double mag = std::max(std::fabs(band[0][b]), std::fabs(band[1][b]));
      env_[b] += (mag - env_[b]) * (mag > env_[b] ? aAtt : aRel);
      nf_[b] = std::min(std::min(nf_[b] * nfUp, 0.03), std::max(env_[b], 1e-7));   // the level the band falls back to; never above -30 dBFS (that is no longer noise)
      double g = 1;
      if (dn >= 0.01) { const double over = env_[b] / (nf_[b] * 2.5); if (over < 1) g = std::max(floorDn, over * over); }   // at the floor: down; 8 dB above it: untouched
      fast_[b] += (mag - fast_[b]) * (mag > fast_[b] ? aFastA : aFastR); slow_[b] += (mag - slow_[b]) * (mag > slow_[b] ? aSlowA : aSlowR);
      if (dr >= 0.01 && slow_[b] > 1e-7 && fast_[b] < slow_[b]) g *= std::max(floorDr, std::pow(fast_[b] / slow_[b], dr));   // only dying away: the room's tail
      g_[b] += (g - g_[b]) * aG; worst = std::min(worst, g_[b]);
      y[0] += band[0][b] * g_[b]; y[1] += band[1][b] * g_[b]; }
    L[i] = static_cast<float>(y[0] * out); R[i] = static_cast<float>(y[1] * out); }
  redDb_ = 20 * std::log10(worst + 1e-12);
}
Fdn::Fdn(double fs, int lines, bool hall) : Simple(fs), lines_(lines), hall_(hall) { reset(); }
void Fdn::reset() {
  static const double hallMs[8] = {37.1, 43.7, 53.3, 61.9, 71.3, 79.7, 89.1, 97.3}, roomMs[4] = {11.3, 14.9, 19.1, 23.3};
  for (int i = 0; i < lines_; ++i) { d_[i].assign(static_cast<std::size_t>(fs_ * (hall_ ? hallMs[i] : roomMs[i]) * 0.001 * 1.6) + 64, 0.f); w_[i] = 0; lp_[i] = 0; }
  pre_.assign(static_cast<std::size_t>(fs_ * 0.26) + 8, 0.f); er_.assign(static_cast<std::size_t>(fs_ * 0.09) + 8, 0.f); pw_ = ew_ = 0; ph_ = 0;
}
std::string Fdn::text(const std::string& id, double v) const { v = clamp01(v); if (id == "decay") return hall_ ? fmt("%.1f s", 0.5 + v * v * 19.5) : fmt("%.2f s", 0.1 + v * 2.9); if (id == "predelay") return fmt("%.0f ms", v * 250); return fmt("%.0f %%", v * 100); }
void Fdn::process(float* L, float* R, std::size_t n) {
  if (bypass_.load()) return;
  static const double hallMs[8] = {37.1, 43.7, 53.3, 61.9, 71.3, 79.7, 89.1, 97.3}, roomMs[4] = {11.3, 14.9, 19.1, 23.3};
  const double size = 0.4 + k_[0].load() * 1.2, v1 = k_[1].load(), rt = hall_ ? 0.5 + v1 * v1 * 19.5 : 0.1 + v1 * 2.9; const float damp = static_cast<float>(1 - std::exp(-2 * kPi * (16000.0 * std::pow(0.08, k_[2].load())) / fs_));
  const double mod = hall_ ? k_[4].load() * 0.0012 * fs_ : 0, early = hall_ ? 0.25 : k_[3].load(); const std::size_t preN = hall_ ? static_cast<std::size_t>(k_[3].load() * 0.25 * fs_) : 0;
  double len[8]; float g[8]; for (int i = 0; i < lines_; ++i) { len[i] = std::min(static_cast<double>(d_[i].size() - 8), fs_ * (hall_ ? hallMs[i] : roomMs[i]) * 0.001 * size); g[i] = static_cast<float>(std::pow(10.0, -3.0 * (len[i] / fs_) / rt)); }
  const float wet = static_cast<float>(mix_.load()), dry = 1.f - wet, norm = 2.f / static_cast<float>(lines_), inG = hall_ ? 0.35f : 0.5f; const double inc = 2 * kPi * 0.6 / fs_;
  for (std::size_t i = 0; i < n; ++i) {
    float in = 0.5f * (L[i] + R[i]); const std::size_t pl = pre_.size(); pre_[pw_] = in; if (preN) in = pre_[(pw_ + pl - std::min(preN, pl - 1)) % pl]; pw_ = (pw_ + 1) % pl;
    const std::size_t el = er_.size(); er_[ew_] = in; float e = 0; if (early > 0) { static const double tap[4] = {0.0113, 0.0197, 0.0311, 0.0437}; for (int t = 0; t < 4; ++t) e += er_[(ew_ + el - static_cast<std::size_t>(tap[t] * fs_ * size) % el) % el] * (0.7f - 0.12f * static_cast<float>(t)); } ew_ = (ew_ + 1) % el;
    float o[8], sum = 0;
    for (int k = 0; k < lines_; ++k) { const std::size_t dl = d_[k].size(); double rd = len[k]; if (mod > 0 && (k & 1)) rd += mod * (1 + std::sin(ph_ + k)); const double rp = static_cast<double>(w_[k]) + static_cast<double>(dl) - rd; const std::size_t r0 = static_cast<std::size_t>(rp) % dl, r1 = (r0 + 1) % dl; const float fr = static_cast<float>(rp - std::floor(rp));
      const float y = d_[k][r0] * (1 - fr) + d_[k][r1] * fr; lp_[k] += (y - lp_[k]) * damp; o[k] = lp_[k] * g[k]; sum += o[k]; }
    for (int k = 0; k < lines_; ++k) { d_[k][w_[k]] = in * inG + o[k] - norm * sum; w_[k] = (w_[k] + 1) % d_[k].size(); }   // Householder feedback: lossless mixing, the decay comes from g
    ph_ += inc; if (ph_ > 2 * kPi) ph_ -= 2 * kPi;
    float l = 0, r = 0; for (int k = 0; k < lines_; ++k) { if (k & 1) r += o[k]; else l += o[k]; } const float sc = hall_ ? 0.5f : 0.7f, ee = static_cast<float>(early) * e * 0.5f;
    L[i] = dry * L[i] + wet * (l * sc + ee); R[i] = dry * R[i] + wet * (r * sc + ee);
  }
}

}  // namespace am::fx

namespace am::fx {
// ── SAMPLE PLAYER ──
SamplePlayer::SamplePlayer(double fs) : Simple(fs) { for (int i = 0; i < count(); ++i) k_[i] = knobDef(i); load(); }
void SamplePlayer::load() {
  const int id = sampleOf(k_[0].load()); auto b = std::make_shared<Buf>(); b->id = id;
  if (!am::samples::render(id, fs_, b->L, b->R)) { b->L.clear(); b->R.clear(); }
  std::atomic_store(&buf_, std::shared_ptr<const Buf>(std::move(b))); loaded_ = id;
}
// PLAY ON is a trigger: every write of ON starts the sample from the top, even when OFF and ON arrive within one audio block (a pad hit
// twice quickly, a script): the audio thread sees the count change, not only an OFF -> ON edge it may never observe.
void SamplePlayer::set(const std::string& id, double v) { Simple::set(id, v); if (id == "play" && v >= 0.5) ++trig_; if (id == "sample" && sampleOf(k_[0].load()) != loaded_ && fileName().empty()) load(); }
bool SamplePlayer::loadFile(const std::string& path, std::string& err) {
  am::arr::Audio a; if (!am::arr::readAudio(path, a, err)) return false;
  if (a.L.size() > static_cast<std::size_t>(a.rate * 600)) { err = "the file is longer than 10 minutes"; return false; }
  auto b = std::make_shared<Buf>(); b->id = sampleOf(k_[0].load()); b->file = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
  const double ratio = a.rate / fs_; const std::size_t n = static_cast<std::size_t>(static_cast<double>(a.L.size()) / ratio);
  b->L.resize(n); b->R.resize(n);
  for (std::size_t i = 0; i < n; ++i) { const double x = static_cast<double>(i) * ratio; const std::size_t j = static_cast<std::size_t>(x); const double f = x - static_cast<double>(j);   // linear: enough for a cue or a jingle
    const std::size_t j1 = std::min(j + 1, a.L.size() - 1); b->L[i] = static_cast<float>(a.L[j] + (a.L[j1] - a.L[j]) * f); b->R[i] = static_cast<float>(a.R[j] + (a.R[j1] - a.R[j]) * f); }
  std::atomic_store(&buf_, std::shared_ptr<const Buf>(std::move(b)));
  return true;
}
void SamplePlayer::clearFile() { loaded_ = 0; load(); }
void SamplePlayer::setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; load(); reset(); }
void SamplePlayer::reset() { pos_ = 0; wasOn_ = false; playing_ = false; seenTrig_ = trig_.load(); }
std::string SamplePlayer::text(const std::string& id, double v) const {
  v = clamp01(v);
  if (id == "sample") { const std::string f = fileName(); if (!f.empty()) return "FILE " + f; const int n = sampleOf(v); for (const auto& i : am::samples::list()) if (i.id == n) return std::to_string(n) + " " + i.name; return std::to_string(n); }
  if (id == "play" || id == "loop") return v >= 0.5 ? "ON" : "OFF";
  return v <= 0 ? std::string("-inf dB") : fmt("%+.1f dB", -40.0 + v * 46.0);
}
void SamplePlayer::process(float* L, float* R, std::size_t n) {
  if (bypass()) return;
  const bool on = k_[1].load() >= 0.5, loop = k_[2].load() >= 0.5; const double lv = k_[3].load();
  { const unsigned tr = trig_.load(); if ((on && !wasOn_) || tr != seenTrig_) pos_ = 0; seenTrig_ = tr; }   // switched on, or triggered again: from the top
  wasOn_ = on;
  const std::shared_ptr<const Buf> b = std::atomic_load(&buf_);
  if (!on || !b || b->L.empty() || lv <= 0) { playing_ = false; return; }
  if (b->file.empty() && b->id != sampleOf(k_[0].load())) pos_ = 0;   // a new sample arrived: start it
  const float g = static_cast<float>(std::pow(10.0, (-40.0 + lv * 46.0) / 20.0)); const std::size_t len = b->L.size();
  bool play = pos_ < len;
  for (std::size_t i = 0; i < n; ++i) {
    if (pos_ >= len) { if (loop) pos_ = 0; else { play = false; break; } }
    L[i] += g * b->L[pos_]; if (R && R != L) R[i] += g * (b->R.empty() ? b->L[pos_] : b->R[pos_]); ++pos_;
  }
  playing_ = play || (loop && len > 0);
}
// ── DE-SQUASH ──
DeSquash::DeSquash(double fs) : Simple(fs) { for (int i = 0; i < count(); ++i) k_[i] = knobDef(i); }
void DeSquash::reset() { fast_ = slow_ = 0; g_ = 1; lift_ = 0; }
std::string DeSquash::text(const std::string& id, double v) const {
  v = clamp01(v);
  if (id == "amount") return fmt("%.0f %%", v * 100.0);
  if (id == "range") return fmt("%.1f dB", v * 12.0);
  return fmt("%.1f ms", 1.0 + (1.0 - v) * 19.0);
}
void DeSquash::process(float* L, float* R, std::size_t n) {
  if (bypass()) return;
  const double amount = k_[0].load(), rangeDb = k_[1].load() * 12.0, atkMs = 1.0 + (1.0 - k_[2].load()) * 19.0;
  const double aF = 1.0 - std::exp(-1.0 / (atkMs * 1e-3 * fs_)), rF = 1.0 - std::exp(-1.0 / (0.050 * fs_)), aS = 1.0 - std::exp(-1.0 / (0.300 * fs_)), rS = 1.0 - std::exp(-1.0 / (0.300 * fs_));
  const double gMax = std::pow(10.0, rangeDb / 20.0), gSmooth = 1.0 - std::exp(-1.0 / (0.002 * fs_));
  double peak = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double x = std::max(std::fabs(static_cast<double>(L[i])), R && R != L ? std::fabs(static_cast<double>(R[i])) : 0.0);
    fast_ += (x > fast_ ? aF : rF) * (x - fast_); slow_ += (fast_ > slow_ ? aS : rS) * (fast_ - slow_);   // the slow one follows the fast one: on a steady sound they meet, on an attack the fast one leads
    double target = 1.0;
    if (slow_ > 1e-4 && fast_ > slow_) target = std::min(gMax, std::pow(fast_ / slow_, amount));   // only lifts: an attack above the body
    g_ += gSmooth * (target - g_);
    L[i] = static_cast<float>(L[i] * g_); if (R && R != L) R[i] = static_cast<float>(R[i] * g_);
    peak = std::max(peak, g_);
  }
  lift_ = 20.0 * std::log10(peak);
}
}  // namespace am::fx
