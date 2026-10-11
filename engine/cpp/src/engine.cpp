// Audio Mixer engine (C++17): see include/audiomixer/engine.hpp.
#include "audiomixer/engine.hpp"

#include <algorithm>
#include <cstring>

namespace am {
namespace {
constexpr double kPi = 3.14159265358979323846;
double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
double linOf(double db) { return std::pow(10.0, db / 20.0); }
double roundHalfUp(double v) { return std::floor(v + 0.5); }
}  // namespace

// ───────── meter ─────────
ClipConfig ClipConfig::fromDb(double db) { ClipConfig c; c.lin = linOf(clampd(db, -12.0, 0.0)); return c; }

void Meter::add(const float* x, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    const double v = x[i], a = std::fabs(v);
    if (a > wPk_) wPk_ = a;
    wSs_ += v * v;
    if (a >= cfg_.lin) { if (++run_ > maxRun_) maxRun_ = run_; } else run_ = 0;
  }
  wN_ += n;
}
void Meter::end(double nowMs) {
  const double rms = wN_ ? std::sqrt(wSs_ / static_cast<double>(wN_)) : 0.0;
  s_.pk = wPk_ >= s_.pk ? wPk_ : s_.pk * 0.9 + wPk_ * 0.1;            // fast attack, slow release
  s_.rms = rms >= s_.rms ? rms : s_.rms * 0.9 + rms * 0.1;
  if (wPk_ >= s_.hold || nowMs - s_.holdAt > cfg_.holdMs) { s_.hold = wPk_; s_.holdAt = nowMs; }
  if (maxRun_ >= cfg_.run || wPk_ >= 1.0) {                           // clip: a flat top, or full scale; events closer than mergeMs are one
    if (nowMs - s_.clipAt > cfg_.mergeMs) ++s_.clipN;
    s_.clipAt = nowMs; s_.latched = true;
    if (wPk_ > s_.clipPk) s_.clipPk = wPk_;
  }
  wPk_ = 0; wSs_ = 0; wN_ = 0; run_ = 0; maxRun_ = 0;
}

// ───────── loudness ─────────
namespace {
double lk(double ms) { return ms > 0 ? -0.691 + 10.0 * std::log10(ms) : -kInf; }
double meanLoud(const double* a, std::size_t n) { double s = 0; for (std::size_t i = 0; i < n; ++i) s += a[i]; return n ? lk(s / static_cast<double>(n)) : -kInf; }
void kPre(double* c, double fs, double f0, double g, double q) {
  const double k = std::tan(kPi * f0 / fs), vh = std::pow(10.0, g / 20.0), vb = std::pow(vh, 0.4996667741545416), a0 = 1.0 + k / q + k * k;
  c[0] = (vh + vb * k / q + k * k) / a0; c[1] = 2.0 * (k * k - vh) / a0; c[2] = (vh - vb * k / q + k * k) / a0; c[3] = 2.0 * (k * k - 1.0) / a0; c[4] = (1.0 - k / q + k * k) / a0;
}
void kHp(double* c, double fs, double f0, double q) {
  const double k = std::tan(kPi * f0 / fs), a0 = 1.0 + k / q + k * k;
  c[0] = 1.0; c[1] = -2.0; c[2] = 1.0; c[3] = 2.0 * (k * k - 1.0) / a0; c[4] = (1.0 - k / q + k * k) / a0;
}
}  // namespace

Loudness::Loudness(double sampleRate, std::size_t maxBlocks) : fs_(sampleRate), cap_(maxBlocks), subSize_(static_cast<int>(roundHalfUp(sampleRate * 0.1))) {
  kPre(c1_, fs_, 1681.974450955533, 3.999843853973347, 0.7071752369554196);
  kHp(c2_, fs_, 38.13547087602444, 0.5003270373238773);
  for (int p = 0; p < 3; ++p) {                                       // 4x oversampling: 12-tap windowed sinc, phases 1/4, 2/4, 3/4
    const double f = (p + 1) / 4.0; double sum = 0;
    for (int k = -5; k <= 6; ++k) { const double t = f - k, s = t == 0 ? 1.0 : std::sin(kPi * t) / (kPi * t), w = 0.5 + 0.5 * std::cos(kPi * t / 6.0); tp_[p][k + 5] = s * w; sum += s * w; }
    for (int k = 0; k < 12; ++k) tp_[p][k] /= sum;
  }
}
void Loudness::reset() { blocks_.clear(); shorts_.clear(); integrated_ = -kInf; lra_ = 0; truePeak_ = -kInf; }

void Loudness::gate() {
  std::vector<double> abs;
  for (double b : blocks_) if (lk(b) > -70.0) abs.push_back(b);
  if (abs.empty()) integrated_ = -kInf;
  else {
    const double rel = meanLoud(abs.data(), abs.size()) - 10.0; double s = 0; std::size_t np = 0;
    for (double b : abs) if (lk(b) > rel) { s += b; ++np; }
    integrated_ = np ? lk(s / static_cast<double>(np)) : -kInf;
  }
  std::vector<double> sa; double sum = 0;
  for (double v : shorts_) if (v > -70.0) { sa.push_back(v); sum += std::pow(10.0, (v + 0.691) / 10.0); }
  lra_ = 0;
  if (sa.size() > 1) {                                                // loudness range: gates -70 / -20 LU, 10th..95th percentile
    const double rel = -0.691 + 10.0 * std::log10(sum / static_cast<double>(sa.size())) - 20.0;
    std::vector<double> v; for (double x : sa) if (x > rel) v.push_back(x);
    std::sort(v.begin(), v.end());
    if (v.size() > 1) {
      const double m = static_cast<double>(v.size() - 1);
      lra_ = std::max(0.0, v[static_cast<std::size_t>(std::min(m, std::floor(0.95 * m + 0.5)))] - v[static_cast<std::size_t>(std::floor(0.10 * m + 0.5))]);
    }
  }
}

void Loudness::feed(const float* left, const float* right, std::size_t n) {
  const float* ch[2] = {left, right};
  double e = 0, pk = 0;
  std::vector<float> buf(11 + n);
  for (int c = 0; c < 2; ++c) {
    const float* x = ch[c]; double* z = z_ + c * 4;
    std::memcpy(buf.data(), h_[c], 11 * sizeof(float)); std::memcpy(buf.data() + 11, x, n * sizeof(float));
    for (std::size_t m = 5; m < n + 5; ++m) {                         // true peak: the samples and 3 interpolated phases between them
      const double a = std::fabs(static_cast<double>(buf[m]));
      if (a > pk) pk = a;
      for (int p = 0; p < 3; ++p) { double s = 0; for (int j = 0; j < 12; ++j) s += static_cast<double>(buf[m - 5 + j]) * tp_[p][j]; s = std::fabs(s); if (s > pk) pk = s; }
    }
    std::memcpy(h_[c], buf.data() + n, 11 * sizeof(float));
    double a1 = z[0], a2 = z[1], b1 = z[2], b2 = z[3];
    for (std::size_t i = 0; i < n; ++i) {                              // K-weighting, direct form II transposed
      const double v = x[i], y1 = c1_[0] * v + a1;
      a1 = c1_[1] * v - c1_[3] * y1 + a2; a2 = c1_[2] * v - c1_[4] * y1;
      const double y2 = c2_[0] * y1 + b1; b1 = c2_[1] * y1 - c2_[3] * y2 + b2; b2 = c2_[2] * y1 - c2_[4] * y2;
      e += y2 * y2;
    }
    z[0] = a1; z[1] = a2; z[2] = b1; z[3] = b2;
  }
  subAcc_ += e; subN_ += static_cast<int>(n);
  const double tpdb = pk > 0 ? 20.0 * std::log10(pk) : -kInf;
  if (tpdb > truePeak_) truePeak_ = tpdb;
  if (subN_ >= subSize_) {
    const double ms = subAcc_ / subN_; subAcc_ = 0; subN_ = 0;
    if (sub_.size() >= 30) sub_.erase(sub_.begin());
    sub_.push_back(ms);
    if (sub_.size() >= 4) {
      double m4 = 0; for (std::size_t i = sub_.size() - 4; i < sub_.size(); ++i) m4 += sub_[i];
      m4 /= 4.0; momentary_ = lk(m4);
      if (blocks_.size() >= cap_) blocks_.erase(blocks_.begin());
      blocks_.push_back(m4);
    } else momentary_ = lk(ms);
    if (sub_.size() >= 30) { double s = 0; for (double v : sub_) s += v; shortTerm_ = lk(s / 30.0); if (shorts_.size() >= cap_) shorts_.erase(shorts_.begin()); shorts_.push_back(shortTerm_); }
    else shortTerm_ = meanLoud(sub_.data(), sub_.size());
    gate();
  }
}

// ───────── phase / scope / VU ─────────
PhaseResult phaseCorrelation(const float* L, const float* R, std::size_t n, double widthPercent, bool invL, bool invR) {
  const double width = widthPercent / 100.0; double sLR = 0, sLL = 0, sRR = 0, peak = 0;
  for (std::size_t i = 0; i < n; ++i) {
    double l = invL ? -static_cast<double>(L[i]) : static_cast<double>(L[i]), r = invR ? -static_cast<double>(R[i]) : static_cast<double>(R[i]);
    const double mid = (l + r) * 0.5, side = (l - r) * 0.5 * width;
    l = mid + side; r = mid - side;
    sLR += l * r; sLL += l * l; sRR += r * r;
    const double a = std::max(std::fabs(l), std::fabs(r)); if (a > peak) peak = a;
  }
  const double den = std::sqrt(sLL * sRR); const bool silent = peak < 0.0005;
  return {(silent || den == 0) ? 0.0 : clampd(sLR / den, -1.0, 1.0), peak, n ? std::sqrt(sLL / static_cast<double>(n)) : 0.0, n ? std::sqrt(sRR / static_cast<double>(n)) : 0.0, silent};
}

ScopeResult scopeSweep(const float* A, std::size_t size, double fs, const ScopeConfig& cfg) {
  ScopeResult out;
  const double tb = cfg.timebaseMs > 0 ? cfg.timebaseMs : 1.0, lvl = linOf(cfg.triggerDb);
  const std::size_t N = static_cast<std::size_t>(std::max(64.0, std::min(static_cast<double>(size >> 1), roundHalfUp(fs * tb * 10.0 / 1000.0))));
  std::size_t start = size - N;
  for (std::size_t i = size >= 2 * N ? size - 2 * N + 1 : 1; i < size - N; ++i) {   // the newest sweep that begins with a crossing and still has N samples after it
    const bool hit = cfg.rising ? (A[i - 1] < lvl && A[i] >= lvl) : (A[i - 1] > -lvl && A[i] <= -lvl);
    if (i >= 1 && hit) { start = i; out.triggered = true; }
  }
  double mn = kInf, mx = -kInf, ss = 0, pk = 0;
  for (std::size_t i = start; i < start + N; ++i) { const double v = A[i], a = std::fabs(v); mn = std::min(mn, v); mx = std::max(mx, v); ss += v * v; pk = std::max(pk, a); }
  const double rms = std::sqrt(ss / static_cast<double>(N));
  const std::size_t from = size > 8192 ? size - 8192 : 1; const double hy = std::max(0.002, pk * 0.05);   // frequency: rising zero crossings with hysteresis
  double first = -1, last = -1; int cnt = 0; bool armed = false;
  for (std::size_t i = from; i < size; ++i) {
    if (A[i] < -hy) armed = true;
    else if (armed && A[i - 1] < 0 && static_cast<double>(A[i]) >= 0) {
      const double d = static_cast<double>(A[i]) - static_cast<double>(A[i - 1]);
      const double t = static_cast<double>(i - 1) + (-static_cast<double>(A[i - 1])) / (d == 0 ? 1.0 : d);
      if (first < 0) first = t;
      last = t; ++cnt; armed = false;
    }
  }
  out.n = N; out.start = start; out.p2p = std::isfinite(mx - mn) ? mx - mn : 0.0; out.rms = rms; out.crestDb = rms > 1e-6 ? 20.0 * std::log10(pk / rms) : 0.0;
  out.freq = cnt >= 2 ? (cnt - 1) * fs / (last - first) : 0.0;
  return out;
}

void VuMeter::step(double rms, double ref) {
  const double db = rms > 0.00003 ? std::max(-60.0, 20.0 * std::log10(rms)) : -60.0, target = clampd(db - ref, -20.0, 3.0);
  vel_ = (vel_ + (target - needle_) * 0.08) * 0.82;                    // spring and damper: 300 ms integration
  needle_ += vel_; led_ = target >= 0.5;
}

// ───────── Int16 meter ─────────
S16Meter::S16Meter(int channels, double clipDb) : ch_(std::max(1, channels)), peak_(ch_, 0), run_(ch_, 0), sumSq_(ch_, 0.0), clipCh_(ch_, 0) { setClipDb(clipDb); }
void S16Meter::setClipDb(double clipDb) { limit_ = static_cast<std::int32_t>(clampd(std::ceil(32768.0 * linOf(clampd(clipDb, -12.0, 0.0))), 1.0, 32767.0)); }
void S16Meter::push(const std::int16_t* x, std::size_t samples) {
  const std::size_t n = samples / static_cast<std::size_t>(ch_) * static_cast<std::size_t>(ch_);
  int c = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const std::int32_t v = x[i], a = v < 0 ? -v : v;
    if (a > peak_[c]) peak_[c] = a;
    sumSq_[c] += static_cast<double>(v) * v;
    if (a >= limit_) { ++clips_; if (++run_[c] == 3) { ++runs_; clipCh_[c] = 1; } } else run_[c] = 0;
    if (a >= 32767) clipCh_[c] = 1;
    if (++c == ch_) c = 0;
  }
  frames_ += static_cast<std::uint32_t>(n / static_cast<std::size_t>(ch_));
}
S16Snapshot S16Meter::snapshot() {
  S16Snapshot s; s.peakDb.resize(ch_); s.rmsDb.resize(ch_); s.clipCh.resize(ch_);
  for (int c = 0; c < ch_; ++c) {
    s.peakDb[c] = peak_[c] > 0 ? std::max(-90.0, 20.0 * std::log10(peak_[c] / 32768.0)) : -90.0;
    s.rmsDb[c] = (frames_ && sumSq_[c] > 0) ? std::max(-90.0, 20.0 * std::log10(std::sqrt(sumSq_[c] / frames_) / 32768.0)) : -90.0;
    s.clipCh[c] = clipCh_[c] != 0; s.clipping = s.clipping || s.clipCh[c];
  }
  s.clip = clips_; s.clipRuns = runs_; s.frames = frames_;
  std::fill(peak_.begin(), peak_.end(), 0); std::fill(sumSq_.begin(), sumSq_.end(), 0.0); std::fill(clipCh_.begin(), clipCh_.end(), 0);   // the run counters carry over
  frames_ = clips_ = runs_ = 0;
  return s;
}

// ───────── mixer ─────────
Mixer::Mixer(int strips, double sampleRate)
    : n_(std::max(1, std::min(strips, kMaxStrips))), fs_(sampleRate), strips_(static_cast<std::size_t>(n_)), limCeil_(linOf(-0.5)), master_{Meter(clip_), Meter(clip_)}, loudness_(sampleRate) {}

void Mixer::setGainDb(int s, double db) { if (s < 0 || s >= n_) return; db = std::min(db, 10.0); strips_[s].gainDb = db; strips_[s].gainLin = db <= -60.0 ? 0.0 : linOf(db); }
void Mixer::setPan(int s, double p) { if (s >= 0 && s < n_) strips_[s].pan = clampd(p, -1.0, 1.0); }
void Mixer::setMute(int s, bool on) { if (s >= 0 && s < n_) strips_[s].mute = on; }
void Mixer::setSolo(int s, bool on) { if (s >= 0 && s < n_) strips_[s].solo = on; }
void Mixer::setMasterDb(double db) { db = std::min(db, 10.0); masterDb_ = db; masterLin_ = db <= -60.0 ? 0.0 : linOf(db); }
void Mixer::setLimiter(bool on, double ceilingDb, double releaseMs) { limOn_ = on; limCeil_ = linOf(clampd(ceilingDb, -24.0, 0.0)); limReleaseMs_ = std::max(1.0, releaseMs); }
void Mixer::setClipDb(double db) { clip_ = ClipConfig::fromDb(db); for (auto& s : strips_) s.meter.setClip(clip_); master_[0].setClip(clip_); master_[1].setClip(clip_); }
void Mixer::clearClips() { for (auto& s : strips_) s.meter.state().clearClip(); master_[0].state().clearClip(); master_[1].state().clearClip(); }

void Mixer::process(const float* const* in, std::size_t frames, float* outL, float* outR) {
  constexpr std::size_t kChunk = 512;
  bool anySolo = false; for (const auto& s : strips_) anySolo = anySolo || s.solo;
  const double rel = 1.0 - std::exp(-1.0 / (limReleaseMs_ * 0.001 * fs_));   // one-pole release: the time constant is the release time
  float tmp[kChunk];
  for (std::size_t done = 0; done < frames; done += kChunk) {
    const std::size_t n = std::min(kChunk, frames - done);
    for (std::size_t i = 0; i < n; ++i) { outL[done + i] = 0.0f; outR[done + i] = 0.0f; }
    for (int s = 0; s < n_; ++s) {
      Strip& st = strips_[static_cast<std::size_t>(s)];
      const double ang = (st.pan + 1.0) * kPi * 0.25, gl = std::cos(ang), gr = std::sin(ang); const bool audible = !st.mute && (!anySolo || st.solo);
      for (std::size_t i = 0; i < n; ++i) tmp[i] = static_cast<float>(static_cast<double>(in[s][done + i]) * st.gainLin);   // post-fader signal: what the strip meter shows
      st.meter.add(tmp, n);
      if (audible) for (std::size_t i = 0; i < n; ++i) { outL[done + i] += static_cast<float>(tmp[i] * gl); outR[done + i] += static_cast<float>(tmp[i] * gr); }
    }
    for (std::size_t i = 0; i < n; ++i) {
      double l = static_cast<double>(outL[done + i]) * masterLin_, r = static_cast<double>(outR[done + i]) * masterLin_;
      if (limOn_) {
        const double pk = std::max(std::fabs(l), std::fabs(r)), need = pk > limCeil_ ? limCeil_ / pk : 1.0;
        if (need < limGain_) limGain_ = need; else limGain_ += (1.0 - limGain_) * rel;
        if (limGain_ > need) limGain_ = need;                           // instant attack: never above what this sample needs
        l *= limGain_; r *= limGain_;
      }
      outL[done + i] = static_cast<float>(l); outR[done + i] = static_cast<float>(r);
    }
    master_[0].add(outL + done, n); master_[1].add(outR + done, n);
    loudness_.feed(outL + done, outR + done, n);
    phase_ = phaseCorrelation(outL + done, outR + done, n);
  }
  timeMs_ += static_cast<double>(frames) * 1000.0 / fs_;
  for (auto& s : strips_) s.meter.end(timeMs_);
  master_[0].end(timeMs_); master_[1].end(timeMs_);
}

}  // namespace am
