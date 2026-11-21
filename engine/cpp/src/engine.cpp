// Audio Mixer engine (C++17): see include/audiomixer/engine.hpp.
#include "audiomixer/frame.hpp"
#include "audiomixer/engine.hpp"
#include "audiomixer/simd.hpp"

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

void Meter::add(const float* x, std::size_t n) {   // peak, sum of squares and the clip runs (the SIMD kernel: see simd.hpp)
  simd::kernels().meter(x, n, cfg_.lin, &wPk_, &wSs_, &run_, &maxRun_);
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

Loudness::Loudness(double sampleRate, std::size_t maxBlocks) : bCount_(kBins, 0u), sCount_(kBins, 0u), bEnergy_(kBins, 0.0), scratch_(11 + 8192), fs_(sampleRate), cap_(maxBlocks), subSize_(static_cast<int>(roundHalfUp(sampleRate * 0.1))) {
  sub_.reserve(32);
  kPre(c1_, fs_, 1681.974450955533, 3.999843853973347, 0.7071752369554196);
  kHp(c2_, fs_, 38.13547087602444, 0.5003270373238773);
  for (int p = 0; p < 3; ++p) {                                       // 4x oversampling: 12-tap windowed sinc, phases 1/4, 2/4, 3/4
    const double f = (p + 1) / 4.0; double sum = 0;
    for (int k = -5; k <= 6; ++k) { const double t = f - k, s = t == 0 ? 1.0 : std::sin(kPi * t) / (kPi * t), w = 0.5 + 0.5 * std::cos(kPi * t / 6.0); tp_[p][k + 5] = s * w; sum += s * w; }
    for (int k = 0; k < 12; ++k) tp_[p][k] /= sum;
  }
}
void Loudness::reset() { std::fill(bCount_.begin(), bCount_.end(), 0u); std::fill(sCount_.begin(), sCount_.end(), 0u); std::fill(bEnergy_.begin(), bEnergy_.end(), 0.0); bTotal_ = sTotal_ = 0; integrated_ = -kInf; lra_ = 0; truePeak_ = -kInf; }

void Loudness::gate() {   // BS.1770 gating on the histograms: absolute -70 LUFS (only blocks above it are binned), then relative -10 LU
  if (!bTotal_) integrated_ = -kInf;
  else {
    double e = 0; std::uint64_t n = 0; for (int k = 0; k < kBins; ++k) { e += bEnergy_[static_cast<std::size_t>(k)]; n += bCount_[static_cast<std::size_t>(k)]; }
    const double rel = lk(e / static_cast<double>(n)) - 10.0; const int from = std::max(0, binOf(rel) + 1);   // the bins wholly above the relative gate
    double s = 0; std::uint64_t np = 0; for (int k = from; k < kBins; ++k) { s += bEnergy_[static_cast<std::size_t>(k)]; np += bCount_[static_cast<std::size_t>(k)]; }
    integrated_ = np ? lk(s / static_cast<double>(np)) : -kInf;
  }
  lra_ = 0;   // loudness range (EBU 3342): short-term values above -70, relative gate -20 LU, then the 10th..95th percentile
  if (sTotal_ > 1) {
    static const std::vector<double> binEnergy = [] { std::vector<double> t(kBins); for (int k = 0; k < kBins; ++k) t[static_cast<std::size_t>(k)] = std::pow(10.0, (kLo + (k + 0.5) * kStep + 0.691) / 10.0); return t; }();   // once per program
    double sum = 0; for (int k = 0; k < kBins; ++k) if (sCount_[static_cast<std::size_t>(k)]) sum += sCount_[static_cast<std::size_t>(k)] * binEnergy[static_cast<std::size_t>(k)];
    const double rel = -0.691 + 10.0 * std::log10(sum / static_cast<double>(sTotal_)) - 20.0; const int from = std::max(0, binOf(rel) + 1);
    std::uint64_t m = 0; for (int k = from; k < kBins; ++k) m += sCount_[static_cast<std::size_t>(k)];
    if (m > 1) {
      const double last = static_cast<double>(m - 1); const std::uint64_t i10 = static_cast<std::uint64_t>(std::floor(0.10 * last + 0.5)), i95 = static_cast<std::uint64_t>(std::min(last, std::floor(0.95 * last + 0.5)));
      double v10 = 0, v95 = 0; std::uint64_t c = 0; bool got10 = false;
      for (int k = from; k < kBins; ++k) { const std::uint64_t n = sCount_[static_cast<std::size_t>(k)]; if (!n) continue; const double v = kLo + (k + 0.5) * kStep;
        if (!got10 && c + n > i10) { v10 = v; got10 = true; } if (c + n > i95) { v95 = v; break; } c += n; }
      lra_ = std::max(0.0, v95 - v10);
    }
  }
}

void Loudness::feed(const float* left, const float* right, std::size_t n) {
  const float* ch[2] = {left, right};
  double e = 0, pk = 0;
  if (scratch_.size() < 11 + n) scratch_.resize(11 + n);   // only when a bigger block than ever arrives (a device change), never per block
  std::vector<float>& buf = scratch_;
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
      const double lm = lk(m4); if (lm > -70.0) { const int b = binOf(lm); ++bCount_[static_cast<std::size_t>(b)]; bEnergy_[static_cast<std::size_t>(b)] += m4; ++bTotal_; }   // absolute gate
    } else momentary_ = lk(ms);
    if (sub_.size() >= 30) { double s = 0; for (double v : sub_) s += v; shortTerm_ = lk(s / 30.0); if (shortTerm_ > -70.0) { ++sCount_[static_cast<std::size_t>(binOf(shortTerm_))]; ++sTotal_; } }
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
void Mixer::setSpatial(int strip, bool on, double azDeg, double elDeg, double distM, double spread) {
  if (strip < 0 || strip >= n_) return;
  Spatial& sp = strips_[static_cast<std::size_t>(strip)].sp; const bool was = sp.on;
  sp.on = on; sp.az = std::fmod(azDeg, 360.0); sp.el = std::max(-90.0, std::min(90.0, elDeg)); sp.dist = std::max(0.3, std::min(20.0, distM)); sp.spread = std::max(0.0, std::min(1.0, spread));
  const double az = sp.az * kPi / 180.0, el = sp.el * kPi / 180.0, s = std::sin(az) * std::cos(el) * (1.0 - sp.spread), back = std::max(0.0, -std::cos(az) * std::cos(el)) * (1.0 - sp.spread);
  const double itd = (0.0875 / 343.0) * (std::asin(std::max(-1.0, std::min(1.0, s))) + s) * fs_;   // samples; + = the source is on the right, the left ear hears it later
  sp.tgtL = itd > 0 ? itd : 0; sp.tgtR = itd < 0 ? -itd : 0;
  const double a = std::fabs(s), gd = 1.0 / std::max(1.0, sp.dist), near = 0.70710678 + 0.29289322 * a, far = 0.70710678 - 0.20710678 * a;
  sp.gL = gd * (s > 0 ? far : near); sp.gR = gd * (s > 0 ? near : far);
  auto pole = [this](double fc) { return fc >= 0.45 * fs_ ? 1.0 : 1.0 - std::exp(-2.0 * kPi * fc / fs_); };
  const double shadow = pole(20000.0 * std::pow(1500.0 / 20000.0, a)); sp.aL = s > 0 ? shadow : 1.0; sp.aR = s < 0 ? shadow : 1.0; sp.aRear = pole(20000.0 * std::pow(7000.0 / 20000.0, back));
  if (!was && on) { sp.dL = sp.tgtL; sp.dR = sp.tgtR; sp.lpL = sp.lpR = sp.rrL = sp.rrR = 0; sp.w = 0; for (float& v : sp.ring) v = 0.f; }
}

void Mixer::setDepth(int s, double d) { if (s >= 0 && s < n_) strips_[s].depth = clampd(d, -1.0, 1.0); }
void Mixer::process5(const float* const* in, std::size_t frames, float* const* outs) {
  constexpr std::size_t kChunk = 512; bool anySolo = false; for (const auto& s : strips_) anySolo = anySolo || s.solo;
  const double rel = 1.0 - std::exp(-1.0 / (limReleaseMs_ * 0.001 * fs_)); float tmp[kChunk];
  for (std::size_t done = 0; done < frames; done += kChunk) {
    const std::size_t n = std::min(kChunk, frames - done);
    for (int c = 0; c < 5; ++c) for (std::size_t i = 0; i < n; ++i) outs[c][done + i] = 0.0f;
    for (int s = 0; s < n_; ++s) {
      Strip& st = strips_[static_cast<std::size_t>(s)]; const bool audible = !st.mute && (!anySolo || st.solo);
      const double ang = (st.pan + 1.0) * kPi * 0.25, gl = std::cos(ang), gr = std::sin(ang), front = std::sqrt((st.depth + 1.0) * 0.5), rear = std::sqrt((1.0 - st.depth) * 0.5), centre = front * std::max(0.0, 1.0 - std::fabs(st.pan)) * 0.7071;
      const double gL = gl * front * std::sqrt(1.0 - centre * centre * 0.5), gR = gr * front * std::sqrt(1.0 - centre * centre * 0.5), gC = centre, gLs = gl * rear, gRs = gr * rear;   // the centre takes part of a centred front strip, constant power
      double ipk = done == 0 ? 0.0 : st.inPk; const double g = st.gainLin * (st.automix ? st.amGain : 1.0);
      for (std::size_t i = 0; i < n; ++i) { const double x = static_cast<double>(in[s][done + i]); if (std::fabs(x) > ipk) ipk = std::fabs(x); tmp[i] = static_cast<float>(x * g); }
      st.inPk = ipk; st.meter.add(tmp, n);
      if (audible) for (std::size_t i = 0; i < n; ++i) { outs[0][done + i] += static_cast<float>(tmp[i] * gL); outs[1][done + i] += static_cast<float>(tmp[i] * gR); outs[2][done + i] += static_cast<float>(tmp[i] * gC); outs[3][done + i] += static_cast<float>(tmp[i] * gLs); outs[4][done + i] += static_cast<float>(tmp[i] * gRs); }
    }
    for (std::size_t i = 0; i < n; ++i) {
      double v[5]; double pk = 0; for (int c = 0; c < 5; ++c) { v[c] = static_cast<double>(outs[c][done + i]) * masterLin_; if (!std::isfinite(v[c])) { v[c] = 0; ++nonFinite_; } pk = std::max(pk, std::fabs(v[c])); }
      if (limOn_) { const double need = pk > limCeil_ ? limCeil_ / pk : 1.0; if (need < limGain_) limGain_ = need; else limGain_ += (1.0 - limGain_) * rel; if (limGain_ > need) limGain_ = need; for (double& x : v) x *= limGain_; }
      for (int c = 0; c < 5; ++c) outs[c][done + i] = static_cast<float>(v[c]);
    }
    master_[0].add(outs[0] + done, n); master_[1].add(outs[1] + done, n); loudness_.feed(outs[0] + done, outs[1] + done, n);
  }
  timeMs_ += static_cast<double>(frames) * 1000.0 / fs_; for (auto& s : strips_) s.meter.end(timeMs_);
}
void Mixer::applyOutputTrim(float* const* ch, int nch, std::size_t n) {
  if (trimLin_ == 1.0 && trimTarget_ == 1.0) return;
  const double a = trimLin_, b = trimTarget_, step = n ? (b - a) / static_cast<double>(n) : 0.0;
  for (int c = 0; c < nch; ++c) { double g = a; for (std::size_t i = 0; i < n; ++i) { g += step; ch[c][i] = static_cast<float>(ch[c][i] * g); } }
  trimLin_ = b;
}
void Mixer::setMute(int s, bool on) { if (s >= 0 && s < n_) strips_[s].mute = on; }
void Mixer::setSolo(int s, bool on) { if (s >= 0 && s < n_) strips_[s].solo = on; }
void Mixer::setMasterDb(double db) { db = std::min(db, 10.0); masterDb_ = db; masterLin_ = db <= -60.0 ? 0.0 : linOf(db); }
const double (&TruePeak4x::coeffs())[3][TruePeak4x::kTaps] {
  struct C { double c[3][kTaps]; C() { for (int p = 0; p < 3; ++p) { const double f = (p + 1) / 4.0; double sum = 0;   // phases 1/4, 2/4, 3/4
      for (int k = -5; k <= 6; ++k) { const double t = f - k, sn = t == 0 ? 1.0 : std::sin(kPi * t) / (kPi * t), w = 0.5 + 0.5 * std::cos(kPi * t / 6.0); c[p][k + 5] = sn * w; sum += sn * w; }
      for (int k = 0; k < kTaps; ++k) c[p][k] /= sum; } } };
  static const C c; return c.c;
}
void TruePeak4x::pushBlock(const double* x, std::size_t n, double* pk, double* delayed) { simd::kernels().truePeak(h_, &w_, &coeffs()[0][0], x, n, pk, delayed); }
void Mixer::setTruePeakLimiter(bool on) { if (on == tpOn_) return; tpOn_ = on; tpL_.reset(); tpR_.reset(); tpPrevNeed_ = 1; }
void Mixer::setLimiter(bool on, double ceilingDb, double releaseMs) { limOn_ = on; limCeil_ = linOf(clampd(ceilingDb, -24.0, 0.0)); limReleaseMs_ = std::max(1.0, releaseMs); }
void Mixer::setClipDb(double db) { const double hold = clip_.holdMs; clip_ = ClipConfig::fromDb(db); clip_.holdMs = hold; for (auto& s : strips_) s.meter.setClip(clip_); master_[0].setClip(clip_); master_[1].setClip(clip_); for (auto& b : buses_) b.meter.setClip(clip_); }
void Mixer::setPeakHoldMs(double ms) { clip_.holdMs = ms < 0 ? 0 : ms; for (auto& s : strips_) s.meter.setClip(clip_); master_[0].setClip(clip_); master_[1].setClip(clip_); for (auto& b : buses_) b.meter.setClip(clip_); }
void Mixer::setSend(int s, int b, double db, bool pre) {
  if (s < 0 || s >= n_ || b < 0 || b >= kMaxBuses) return;
  Send& x = sends_[s][b]; x.lin = db <= -60.0 ? 0.0 : linOf(std::min(db, 10.0)); x.pre = pre;
  anySend_ = false; for (int i = 0; i < n_; ++i) for (int k = 0; k < kMaxBuses; ++k) anySend_ = anySend_ || sends_[i][k].lin > 0;
}
void Mixer::setBusLevel(int b, double db) { if (b >= 0 && b < kMaxBuses) buses_[b].levelLin = db <= -60.0 ? 0.0 : linOf(std::min(db, 10.0)); }
void Mixer::setBusMute(int b, bool on) { if (b >= 0 && b < kMaxBuses) buses_[b].mute = on; }
void Mixer::setAutomix(int s, bool on, double w) { if (s < 0 || s >= n_) return; Strip& st = strips_[static_cast<std::size_t>(s)]; st.automix = on; st.amWeight = clampd(w, 0.25, 4.0); if (!on) { st.amGain = 1; st.amLevel = 0; } automixN_ = 0; for (const auto& x : strips_) automixN_ += x.automix ? 1 : 0; }
double Mixer::automixGainDb(int s) const { if (s < 0 || s >= n_) return 0; const Strip& st = strips_[static_cast<std::size_t>(s)]; return st.automix ? 20 * std::log10(std::max(1e-4, st.amGain)) : 0; }
void Mixer::setAutoLevel(bool on, double target, double maxDb) { if (on && !autoOn_) autoSec_ = 0; autoOn_ = on; autoTarget_ = clampd(target, -30, -6); autoMax_ = clampd(maxDb, 0, 24); if (!on) autoDb_ = 0; }
void Mixer::setBusProcessor(int k, BusProcessor* p) { if (k >= 0 && k < kMaxBuses) buses_[k].fx = p; }
void Mixer::setWidth(double w) { width_ = w < 0 ? 0 : w > 2 ? 2 : w; }
void Mixer::setBusReturn(int b, double db) { if (b >= 0 && b < kMaxBuses) buses_[b].returnLin = db <= -60.0 ? 0.0 : linOf(std::min(db, 10.0)); }
void Mixer::clearClips() { for (auto& s : strips_) s.meter.state().clearClip(); master_[0].state().clearClip(); master_[1].state().clearClip(); }

void Mixer::process(const float* const* in, std::size_t frames, float* outL, float* outR, float* const* busOut, const float* extL, const float* extR, const unsigned char* idle) {
  double limMin = 1.0;   // the lowest limiter gain of this call, for the reduction meter
  constexpr std::size_t kChunk = 512;
  bool anySolo = false; for (const auto& s : strips_) anySolo = anySolo || s.solo;
  const double rel = 1.0 - std::exp(-1.0 / (limReleaseMs_ * 0.001 * fs_));   // one-pole release: the time constant is the release time
  float tmp[kChunk];
  static thread_local float busL[kMaxBuses][kChunk], busR[kMaxBuses][kChunk], busM[kChunk];   // aux bus sums of this chunk (fixed, never allocated)
  const bool buses = anySend_ || busOut;
  for (std::size_t done = 0; done < frames; done += kChunk) {
    const std::size_t n = std::min(kChunk, frames - done);
    for (std::size_t i = 0; i < n; ++i) { outL[done + i] = 0.0f; outR[done + i] = 0.0f; }
    if (buses) for (int k = 0; k < kMaxBuses; ++k) for (std::size_t i = 0; i < n; ++i) { busL[k][i] = 0.0f; busR[k][i] = 0.0f; }
    if (automixN_ > 0) {   // gain sharing: each member's share of one unity gain, from its smoothed weighted input level
      const double attack = 1.0 - std::exp(-static_cast<double>(n) / (0.010 * fs_)), release = 1.0 - std::exp(-static_cast<double>(n) / (0.300 * fs_)); double sum = 0;
      for (int s = 0; s < n_; ++s) { Strip& st = strips_[static_cast<std::size_t>(s)]; if (!st.automix) continue; double e = 0; for (std::size_t i = 0; i < n; ++i) { const double x = in[s][done + i]; e += x * x; } const double lvl = std::sqrt(e / static_cast<double>(n)) * st.amWeight; st.amLevel += (lvl - st.amLevel) * (lvl > st.amLevel ? attack : release); sum += st.amLevel; }
      for (int s = 0; s < n_; ++s) { Strip& st = strips_[static_cast<std::size_t>(s)]; if (!st.automix) continue; const double share = sum > 1e-9 ? st.amLevel / sum : 1.0 / automixN_; const double target = std::max(0.01, std::min(1.0, share * automixN_ > 1.0 ? 1.0 : share * automixN_)); st.amGain += (target - st.amGain) * (target > st.amGain ? attack : release); }
    }
    for (int s = 0; s < n_; ++s) {
      Strip& st = strips_[static_cast<std::size_t>(s)];
      if (idle && idle[s] && !st.sp.on && std::fabs(st.anLp) < 1e-9) { st.anLp = 0; st.inPk = 0; continue; }   // silence in, nothing ringing: nothing to add (the meter decays in end())
      const double ang = (st.pan + 1.0) * kPi * 0.25, gl = std::cos(ang), gr = std::sin(ang); const bool audible = !st.mute && (!anySolo || st.solo);
      double ipk = done == 0 ? 0.0 : st.inPk; const double g = st.gainLin * (st.automix ? st.amGain : 1.0);
      if (analog_) {   // ANALOG: the channel's output stage (see setAnalogChannels), before the fader
        const double a = 1.0 - std::exp(-2.0 * kPi * 20000.0 / fs_), k = 0.55, b = 0.04, tb = std::tanh(k * b), norm = 1.0 / (k * (1.0 - tb * tb));
        for (std::size_t i = 0; i < n; ++i) { const double x = static_cast<double>(in[s][done + i]); if (std::fabs(x) > ipk) ipk = std::fabs(x);
          const double sat = (std::tanh(k * (x + b)) - tb) * norm; st.anLp += a * (sat - st.anLp); tmp[i] = static_cast<float>(st.anLp * g); }
      } else { const double p = simd::kernels().gainPeak(in[s] + done, n, g, tmp); if (p > ipk) ipk = p; }   // post-fader signal: what the strip meter shows
      st.inPk = ipk; st.meter.add(tmp, n);
      if (st.sp.on) {   // 360 audio: the ring always runs (so un-muting does not click), the ears are summed when audible
        Spatial& sp = st.sp;
        for (std::size_t i = 0; i < n; ++i) {
          sp.ring[sp.w & 511u] = tmp[i]; sp.dL += (sp.tgtL - sp.dL) * 0.002; sp.dR += (sp.tgtR - sp.dR) * 0.002;
          auto tap = [&sp](double d) { const double p = static_cast<double>(sp.w) - d; const double fl = std::floor(p); const unsigned k = static_cast<unsigned>(static_cast<long long>(fl)); const double fr = p - fl; return sp.ring[k & 511u] * (1.0 - fr) + sp.ring[(k + 1u) & 511u] * fr; };
          sp.lpL += (tap(sp.dL) - sp.lpL) * sp.aL; sp.lpR += (tap(sp.dR) - sp.lpR) * sp.aR; sp.rrL += (sp.lpL - sp.rrL) * sp.aRear; sp.rrR += (sp.lpR - sp.rrR) * sp.aRear; ++sp.w;
          if (audible) { outL[done + i] += static_cast<float>(sp.rrL * sp.gL); outR[done + i] += static_cast<float>(sp.rrR * sp.gR); }
        }
      } else if (audible) simd::kernels().panAdd(tmp, n, gl, gr, outL + done, outR + done);
      if (anySend_) for (int k = 0; k < kMaxBuses; ++k) {
        const Send& sd = sends_[s][k]; if (sd.lin <= 0) continue;
        if (sd.pre) { for (std::size_t i = 0; i < n; ++i) { const double v = static_cast<double>(in[s][done + i]) * sd.lin; busL[k][i] += static_cast<float>(v * gl); busR[k][i] += static_cast<float>(v * gr); } }
        else if (audible) { for (std::size_t i = 0; i < n; ++i) { const double v = static_cast<double>(tmp[i]) * sd.lin; busL[k][i] += static_cast<float>(v * gl); busR[k][i] += static_cast<float>(v * gr); } }
      }
    }
    if (buses) for (int k = 0; k < kMaxBuses; ++k) {   // bus level and mute, meter (the louder side), return into the master, the bus's own output
      Bus& b = buses_[k]; const double g = b.mute ? 0.0 : b.levelLin;
      for (std::size_t i = 0; i < n; ++i) { busL[k][i] = static_cast<float>(busL[k][i] * g); busR[k][i] = static_cast<float>(busR[k][i] * g); busM[i] = std::fabs(busL[k][i]) > std::fabs(busR[k][i]) ? busL[k][i] : busR[k][i]; }
      if (b.fx) { b.fx->process(busL[k], busR[k], n); for (std::size_t i = 0; i < n; ++i) busM[i] = std::fabs(busL[k][i]) > std::fabs(busR[k][i]) ? busL[k][i] : busR[k][i]; }
      b.meter.add(busM, n);
      if (b.returnLin > 0) for (std::size_t i = 0; i < n; ++i) { outL[done + i] += static_cast<float>(busL[k][i] * b.returnLin); outR[done + i] += static_cast<float>(busR[k][i] * b.returnLin); }
      if (busOut) { if (busOut[2 * k]) std::copy(busL[k], busL[k] + n, busOut[2 * k] + done); if (busOut[2 * k + 1]) std::copy(busR[k], busR[k] + n, busOut[2 * k + 1] + done); }
    }
    if (extL && extR) for (std::size_t i = 0; i < n; ++i) { outL[done + i] += extL[done + i]; outR[done + i] += extR[done + i]; }   // the arrangement player, pre master
    if (masterFx_) masterFx_->process(outL + done, outR + done, n);   // the master chain (program EQ, mastering)
    if (width_ != 1.0) for (std::size_t i = 0; i < n; ++i) { const float mid = 0.5f * (outL[done + i] + outR[done + i]), side = 0.5f * (outL[done + i] - outR[done + i]) * static_cast<float>(width_); outL[done + i] = mid + side; outR[done + i] = mid - side; }
    if (analog_) for (std::size_t i = 0; i < n; ++i) { const float l = outL[done + i], r = outR[done + i]; outL[done + i] = l + 0.005f * r; outR[done + i] = r + 0.005f * l; }   // ANALOG: bus crosstalk, -46 dB
    // NaN / inf guard: one broken plug-in or a bad sample must not poison the DC blocker, the limiter's envelope and the device for
    // good (a NaN never decays): a non-finite sample becomes silence here, before the master processing; counted for the diagnostics.
    for (std::size_t i = 0; i < n; ++i) {
      if (!std::isfinite(outL[done + i])) { outL[done + i] = 0.0f; ++nonFinite_; }
      if (!std::isfinite(outR[done + i])) { outR[done + i] = 0.0f; ++nonFinite_; }
    }
    if (dcOn_) {   // DC blocker before the auto level and the limiter: y[n] = x[n] - x[n-1] + R y[n-1], R for 5 Hz
      const double R = 1.0 - 2 * kPi * 5.0 / fs_; float* ch[2] = {outL + done, outR + done};
      for (int c = 0; c < 2; ++c) for (std::size_t i = 0; i < n; ++i) { const double x = ch[c][i]; const double y = x - dcX_[c] + R * dcY_[c]; dcX_[c] = x; dcY_[c] = y; ch[c][i] = static_cast<float>(y); }
    }
    if (autoOn_) {   // steer the short-term loudness toward the target, slowly, within [-12, +max] dB, once the 3 s window has filled
      const double step = static_cast<double>(n) / fs_; autoSec_ += step; const double st = loudness_.shortTerm();
      if (autoSec_ >= 3.0 && st > -70 && st < -1) { const double want = std::max(-12.0, std::min(autoMax_, autoDb_ + (autoTarget_ - st))); autoDb_ += want > autoDb_ ? std::min(want - autoDb_, 1.0 * step) : std::max(want - autoDb_, -6.0 * step); }
    } else { autoDb_ = 0; autoSec_ = 0; }
    const double autoLin = autoOn_ ? linOf(autoDb_) : 1.0;
    static thread_local double tpIn[2][kChunk], tpPk[2][kChunk], tpDl[2][kChunk];   // TRUE PEAK: the detector runs over the chunk first (SIMD kernel)
    if (limOn_ && tpOn_) {
      for (std::size_t i = 0; i < n; ++i) { tpIn[0][i] = static_cast<double>(outL[done + i]) * masterLin_ * autoLin; tpIn[1][i] = static_cast<double>(outR[done + i]) * masterLin_ * autoLin; }
      tpL_.pushBlock(tpIn[0], n, tpPk[0], tpDl[0]); tpR_.pushBlock(tpIn[1], n, tpPk[1], tpDl[1]);
    }
    for (std::size_t i = 0; i < n; ++i) {
      double l = static_cast<double>(outL[done + i]) * masterLin_ * autoLin, r = static_cast<double>(outR[done + i]) * masterLin_ * autoLin;
      if (limOn_ && tpOn_) {   // TRUE PEAK: the gain for the sample 6 behind, from its own peak and the inter-sample peaks on both sides of it
        const double pk = std::max(tpPk[0][i], tpPk[1][i]), now = pk > limCeil_ ? limCeil_ / pk : 1.0, need = std::min(now, tpPrevNeed_); tpPrevNeed_ = now;
        l = tpDl[0][i]; r = tpDl[1][i];
        if (need < limGain_) limGain_ = need; else limGain_ += (1.0 - limGain_) * rel;
        if (limGain_ > need) limGain_ = need;
        l *= limGain_; r *= limGain_; if (limGain_ < limMin) limMin = limGain_;
      } else if (limOn_) {
        const double pk = std::max(std::fabs(l), std::fabs(r)), need = pk > limCeil_ ? limCeil_ / pk : 1.0;
        if (need < limGain_) limGain_ = need; else limGain_ += (1.0 - limGain_) * rel;
        if (limGain_ > need) limGain_ = need;                           // instant attack: never above what this sample needs
        l *= limGain_; r *= limGain_; if (limGain_ < limMin) limMin = limGain_;
      }
      outL[done + i] = static_cast<float>(l); outR[done + i] = static_cast<float>(r);
    }
    master_[0].add(outL + done, n); master_[1].add(outR + done, n);
    loudness_.feed(outL + done, outR + done, n);
    phase_ = phaseCorrelation(outL + done, outR + done, n);
    if (outBits_) {   // TPDF dither at the device depth: the last thing before the converter (after the limiter; adds at most one LSB)
      float* ch[2] = {outL + done, outR + done}; const double q = 1.0 / static_cast<double>(1u << (outBits_ - 1));
      for (int c = 0; c < 2; ++c) for (std::size_t i = 0; i < n; ++i) { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; const double r1 = static_cast<double>(rng_) / 4294967296.0; rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; const double r2 = static_cast<double>(rng_) / 4294967296.0; ch[c][i] = static_cast<float>(std::floor((ch[c][i] + (r1 - r2) * q) / q + 0.5) * q); }
    }
    am::frame::refTap().write(outL + done, outR + done, n);   // the far-end reference for the echo cancellers (what the speakers / the phone line get)
  }
  timeMs_ += static_cast<double>(frames) * 1000.0 / fs_;
  for (auto& s : strips_) s.meter.end(timeMs_);
  for (auto& b : buses_) b.meter.end(timeMs_);
  master_[0].end(timeMs_); master_[1].end(timeMs_);
  limGrDb_ = limOn_ && limMin < 1.0 ? -20.0 * std::log10(std::max(limMin, 1e-6)) : 0.0;
}

}  // namespace am
