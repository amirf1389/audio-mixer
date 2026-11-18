// Audio Mixer runtime: see include/audiomixer/runtime.hpp.
#include "audiomixer/runtime.hpp"

#include <thread>
#include <chrono>
#include <cmath>

#include <algorithm>
#include <cstring>

#include "audiomixer/fpu.hpp"
#include "backends.hpp"

namespace am {

std::vector<std::string> availableBackends() {
  std::vector<std::string> v;
#ifdef AM_HAVE_ALSA
  v.push_back("alsa");
#endif
#ifdef _WIN32
  v.push_back("wasapi");
  v.push_back("asio");
  v.push_back("dsound");
#endif
#ifdef __APPLE__
  v.push_back("coreaudio");
#endif
  v.push_back("null");
  return v;
}
std::unique_ptr<Backend> createBackend(const std::string& name) {
  if (name == "null") return makeNullBackend();
#ifdef AM_HAVE_ALSA
  if (name == "alsa") return makeAlsaBackend();
#endif
#ifdef _WIN32
  if (name == "wasapi") return makeWasapiBackend();
  if (name == "asio") return makeAsioBackend();
  if (name == "dsound") return makeDsoundBackend();
#endif
#ifdef __APPLE__
  if (name == "coreaudio") return makeCoreAudioBackend();
#endif
  return nullptr;
}

enum Op { kGain, kPan, kMute, kSolo, kMaster, kLimiter, kClipDb, kClear, kEq, kDelay, kPolarity, kXover, kChLimiter, kComp, kGate, kSend, kBusLevel, kBusMute, kBusReturn, kMonitor, kInRoute, kOutRoute, kTrim, kPeakHold, kBusFx, kWidth, kAutomix, kAutoLevel, kOutBits, kProfReset, kDcBlock, kAnalogCh, kSpatial, kMasterFx, kAutoPol, kAmp, kAmpEq, kOutTrim };

Runtime::Runtime(int strips) : mixer_(strips, 48000) { std::memset(&pub_, 0, sizeof pub_); }
Runtime::~Runtime() { stop(); }

bool Runtime::open(const DeviceConfig& cfg, std::string& err) {
  stop();
  std::string name = cfg.backend;
  if (name == "auto") name = availableBackends().front();
  backend_ = createBackend(name);
  if (!backend_) { err = "the backend \"" + name + "\" is not built into this binary"; return false; }
  backendName_ = name; exclusive_ = cfg.exclusive; outDevice_ = cfg.outDevice; nIn_ = std::max(0, cfg.inChannels); nOut_ = std::max(0, cfg.outChannels);
  mixer_ = Mixer(mixer_.strips(), cfg.sampleRate);                                  // the mixer runs at the device rate
  const std::size_t n = std::max<std::size_t>(cfg.frames, 16) * 8;                  // scratch for blocks up to 8 periods (devices may hand out more than the period)
  silence_.assign(n, 0.0f); left_.assign(n, 0.0f); right_.assign(n, 0.0f); monL_.assign(n, 0.0f); monR_.assign(n, 0.0f); extL_.assign(n, 0.0f); extR_.assign(n, 0.0f); netBuf_.assign(kNetInputs, std::vector<float>(n, 0.0f));
  for (int s = 0; s < kMaxStrips; ++s) { inRoute_[s] = s; trim_[s] = 1.0; }
  for (int o = 0; o < kMaxOutputs; ++o) outRoute_[o] = defaultOutputSource(o);
  ch_.assign(static_cast<std::size_t>(mixer_.strips()), dsp::Channel(cfg.sampleRate)); chBuf_.assign(ch_.size(), std::vector<float>(n, 0.0f)); sumBuf_.assign(n, 0.0f);
  busBuf_.assign(2 * kMaxBuses, std::vector<float>(n, 0.0f)); mono_.assign(n, 0.0f);
  for (int o = 0; o < kMaxOutputs; ++o) { amp_[o].prepare(cfg.sampleRate); amp_[o].set(ampUi_[o]); }   // the delay lines sized at the device rate, the settings kept
  head_ = 0; tail_ = 0; blocks_ = 0; frames_ = 0; std::memset(&pub_, 0, sizeof pub_);
  return backend_->open(cfg, [this](const float* const* in, float* const* out, std::size_t frames) { onBlock(in, out, frames); }, err);
}
bool Runtime::start(std::string& err) { if (!backend_) { err = "open() first"; return false; } if (running_) return true; running_ = backend_->start(err); return running_; }
void Runtime::stop() { if (backend_ && running_) backend_->stop(); running_ = false; }

void Runtime::post(const Cmd& c) {
  std::lock_guard<std::mutex> g(producers_);
  // A full ring while the audio thread runs means a burst (a whole console loaded at once): wait for the next block to empty it. Dropping a
  // change here once lost "detach this effect", and the audio thread went on using an effect that was then deleted. Only when nothing is
  // emptying the ring (not started, or stalled for 200 ms) is the change given up.
  unsigned h = head_.load(std::memory_order_relaxed);
  for (int tries = 0; h - tail_.load(std::memory_order_acquire) >= kRing; ++tries) {
    if (!running_ || tries >= 400) return;
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  ring_[h % kRing] = c; head_.store(h + 1, std::memory_order_release);
}
void Runtime::setGainDb(int s, double db) { post({kGain, s, db, 0, 0}); }
void Runtime::setPan(int s, double p) { post({kPan, s, p, 0, 0}); }
void Runtime::setSpatial(int s, bool on, double az, double el, double dist, double spread) { Cmd c{kSpatial, s, az, el, dist}; c.b = on ? 1 : 0; c.w = spread; post(c); }
void Runtime::setMute(int s, bool on) { post({kMute, s, on ? 1.0 : 0.0, 0, 0}); }
void Runtime::setSolo(int s, bool on) { post({kSolo, s, on ? 1.0 : 0.0, 0, 0}); }
void Runtime::setMasterDb(double db) { post({kMaster, 0, db, 0, 0}); }
void Runtime::setOutputTrimDb(double db) { post({kOutTrim, 0, db, 0, 0}); }
void Runtime::setLimiter(bool on, double c, double r) { post({kLimiter, on ? 1 : 0, c, r, 0}); }
void Runtime::setClipDb(double db) { post({kClipDb, 0, db, 0, 0}); }
void Runtime::setPeakHoldMs(double ms) { post({kPeakHold, 0, ms, 0, 0}); }
void Runtime::clearClips() { post({kClear, 0, 0, 0, 0}); }
void Runtime::setEqBand(int s, int band, dsp::FilterType t, double f, double q, double g) { Cmd c{kEq, s, f, q, g}; c.b = band; c.w = static_cast<double>(t); post(c); }
void Runtime::setDelayMs(int s, double ms) { post({kDelay, s, ms, 0, 0}); }
void Runtime::setPolarity(int s, bool inv) { post({kPolarity, s, inv ? 1.0 : 0.0, 0, 0}); }
void Runtime::setCrossover(int s, dsp::XoverType t, int hpO, double hpF, int lpO, double lpF) { Cmd c{kXover, s, hpF, lpF, static_cast<double>(hpO)}; c.b = static_cast<int>(t); c.w = static_cast<double>(lpO); post(c); }
void Runtime::setChannelLimiter(int s, bool on, double ceil, double rel) { Cmd c{kChLimiter, s, ceil, rel, 0}; c.b = on ? 1 : 0; post(c); }
void Runtime::setCompressor(int s, bool on, double thr, double ratio, double knee, double att, double rel, double makeup) { Cmd c{kComp, s, thr, ratio, knee}; c.b = on ? 1 : 0; c.w = att; c.v = rel; c.u = makeup; post(c); }
void Runtime::setGate(int s, bool on, double thr, double range, double att, double hold, double rel) { Cmd c{kGate, s, thr, range, att}; c.b = on ? 1 : 0; c.w = hold; c.v = rel; post(c); }
void Runtime::setSend(int s, int b, double db, bool pre) { Cmd c{kSend, s, db, pre ? 1.0 : 0.0, 0}; c.b = b; post(c); }
void Runtime::setBusLevel(int b, double db) { post({kBusLevel, b, db, 0, 0}); }
void Runtime::setBusMute(int b, bool on) { post({kBusMute, b, on ? 1.0 : 0.0, 0, 0}); }
void Runtime::setBusReturn(int b, double db) { post({kBusReturn, b, db, 0, 0}); }
void Runtime::setInputRoute(int strip, int input) { if (strip < 0 || strip >= kMaxStrips) return; post({kInRoute, strip, 0, 0, 0, (input >= kNetInBase && input < kNetInBase + kNetInputs) ? input : std::max(-1, std::min(kMaxStrips - 1, input))}); }
void Runtime::setOutputRoute(int out, int source) { if (out < 0 || out >= kMaxOutputs) return; post({kOutRoute, out, 0, 0, 0, source == kSrcMasterMono ? source : std::max(0, std::min(kSrcBus0L + 2 * kMaxBuses - 1, source))}); }
void Runtime::setAmp(int out, const AmpParams& p) {
  if (out < 0 || out >= kMaxOutputs) return;
  ampUi_[out] = p;
  const int flags = (p.on ? 1 : 0) | (p.mute ? 2 : 0) | (p.invert ? 4 : 0) | (p.limitOn ? 8 : 0) | (std::clamp(p.hpOrder, 0, 8) << 4) | (std::clamp(p.lpOrder, 0, 8) << 8) | (static_cast<int>(p.type) << 12);
  if (!running_) { amp_[out].set(p); return; }   // nothing runs: set it directly
  Cmd c{kAmp, out, p.gainDb, p.delayMs, p.hpHz, flags}; c.w = p.lpHz; c.v = p.limitDb; c.u = p.releaseMs; post(c);
  for (int b = 0; b < AmpParams::kBands; ++b) { Cmd e{kAmpEq, out, p.eq[b].freq, p.eq[b].q, p.eq[b].gainDb, b}; e.w = static_cast<int>(p.eq[b].type); post(e); }
}
AmpParams Runtime::amp(int out) const { return out >= 0 && out < kMaxOutputs ? ampUi_[out] : AmpParams{}; }
double Runtime::ampResponseDb(int out, double hz) const { if (out < 0 || out >= kMaxOutputs) return 0; AmpChannel a; a.prepare(mixer_.sampleRate() > 0 ? mixer_.sampleRate() : 48000.0); a.set(ampUi_[out]); return a.responseDbAt(hz); }
void Runtime::setOutputBits(int bits) { post({kOutBits, bits, 0, 0, 0}); }
void Runtime::setDcBlock(bool on) { post({kDcBlock, on ? 1 : 0, 0, 0, 0}); }
void Runtime::setAnalogChannels(bool on) { post({kAnalogCh, on ? 1 : 0, 0, 0, 0}); }
void Runtime::resetProfiler() { post({kProfReset, 0, 0, 0, 0}); }
void Runtime::setAutomix(int s, bool on, double w) { post({kAutomix, s, on ? 1.0 : 0.0, w, 0}); }
std::string Runtime::sampleFormat() const { return backend_ ? backend_->sampleFormat() : std::string(); }
std::vector<std::string> Runtime::outputNames() const { return backend_ ? backend_->outputNames() : std::vector<std::string>(); }
double Runtime::inputSilentSeconds() const { return backend_ ? backend_->inputSilentSeconds() : 0.0; }
void Runtime::latencyFrames(long& in, long& out) const { in = out = 0; if (backend_) backend_->latencyFrames(in, out); }
int Runtime::deviceOutputBits() const {
  std::string f = sampleFormat(); const std::size_t o = f.find("out "); if (o != std::string::npos) f = f.substr(o);
  if (f.find("shared") != std::string::npos) return 0;   // WASAPI shared: Windows mixes in float, the console's output is float
  if (f.rfind("16-bit integer", 0) == 0 || f.find("out 16-bit integer") == 0) return 16;
  if (f.rfind("24-bit integer", 0) == 0 || f.find("out 24-bit integer") == 0) return 24;
  return 0;
}
void Runtime::setAutoPolarity(int s, bool on) { post({kAutoPol, s, on ? 1.0 : 0.0, 0, 0}); }
void Runtime::setAutoLevel(bool on, double t, double m) { post({kAutoLevel, on ? 1 : 0, t, m, 0}); }
void Runtime::setBusProcessor(int b, BusProcessor* p) { Cmd c{kBusFx, b, 0, 0, 0}; c.p = p; post(c); }
void Runtime::setMasterProcessor(BusProcessor* p) { Cmd c{kMasterFx, 0, 0, 0, 0}; c.p = p; post(c); }
void Runtime::setWidth(double w) { post({kWidth, 0, w, 0, 0}); }
void Runtime::setInserts(int strip, std::shared_ptr<const InsertChain> chain) { if (strip >= 0 && strip < kMaxStrips) std::atomic_store(&inserts_[strip], std::move(chain)); }
void Runtime::setTrimDb(int strip, double db) { if (strip < 0 || strip >= kMaxStrips) return; post({kTrim, strip, std::pow(10.0, std::clamp(db, -24.0, 24.0) / 20), 0, 0}); }
void Runtime::setMonitor(int source, double db, bool dim, bool mono, bool mute) {
  double lin = std::pow(10.0, std::clamp(db, -120.0, 10.0) / 20); if (dim) lin *= 0.1; if (mute || db <= -119) lin = 0;
  post({kMonitor, std::clamp(source, 0, kMaxBuses), lin, 0, 0, mono ? 1 : 0});
}

void Runtime::onBlock(const float* const* in, float* const* out, std::size_t frames) {
  am::denormalsOff();   // per thread; cheap, so every callback (a backend may move the callback to another thread)
  const std::int64_t t0 = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if (lastStartNs_) { const double period = static_cast<double>(frames) * 1e6 / mixer_.sampleRate(); const double gap = static_cast<double>(t0 - lastStartNs_) / 1000.0; jitterUs_ += (std::fabs(gap - period) - jitterUs_) * 0.1; }
  lastStartNs_ = t0;
  for (unsigned t = tail_.load(std::memory_order_relaxed), h = head_.load(std::memory_order_acquire); t != h; ++t) {   // control changes of this block
    const Cmd& c = ring_[t % kRing];
    switch (c.op) {
      case kGain: mixer_.setGainDb(c.a, c.x); break;
      case kPan: mixer_.setPan(c.a, c.x); break;
      case kSpatial: mixer_.setSpatial(c.a, c.b != 0, c.x, c.y, c.z, c.w); break;
      case kMute: mixer_.setMute(c.a, c.x != 0); break;
      case kSolo: mixer_.setSolo(c.a, c.x != 0); break;
      case kMaster: mixer_.setMasterDb(c.x); break;
      case kOutTrim: mixer_.setOutputTrimDb(c.x); break;
      case kLimiter: mixer_.setLimiter(c.a != 0, c.x, c.y); break;
      case kClipDb: mixer_.setClipDb(c.x); break;
      case kPeakHold: mixer_.setPeakHoldMs(c.x); break;
      case kClear: mixer_.clearClips(); break;
      case kEq: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setEqBand(c.b, {static_cast<dsp::FilterType>(static_cast<int>(c.w)), c.x, c.y, c.z}); break;
      case kDelay: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setDelayMs(c.x); break;
      case kPolarity: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setPolarity(c.x != 0); break;
      case kXover: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setCrossover(static_cast<dsp::XoverType>(c.b), static_cast<int>(c.z), c.x, static_cast<int>(c.w), c.y); break;
      case kChLimiter: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setLimiter(c.b != 0, c.x, c.y); break;
      case kComp: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setCompressor(c.b != 0, c.x, c.y, c.z, c.w, c.v, c.u); break;
      case kGate: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setGate(c.b != 0, c.x, c.y, c.z, c.w, c.v); break;
      case kSend: mixer_.setSend(c.a, c.b, c.x, c.y != 0); break;
      case kBusLevel: mixer_.setBusLevel(c.a, c.x); break;
      case kBusMute: mixer_.setBusMute(c.a, c.x != 0); break;
      case kBusReturn: mixer_.setBusReturn(c.a, c.x); break;
      case kMonitor: monSrc_ = c.a; monGainTarget_ = c.x; monMono_ = c.b != 0; break;
      case kInRoute: inRoute_[c.a] = c.b; break;
      case kOutRoute: outRoute_[c.a] = c.b; break;
      case kAmp: if (c.a >= 0 && c.a < kMaxOutputs) { AmpParams p; p.on = c.b & 1; p.mute = c.b & 2; p.invert = c.b & 4; p.limitOn = c.b & 8; p.hpOrder = (c.b >> 4) & 15; p.lpOrder = (c.b >> 8) & 15;
        p.type = static_cast<dsp::XoverType>((c.b >> 12) & 3); p.gainDb = c.x; p.delayMs = c.y; p.hpHz = c.z; p.lpHz = c.w; p.limitDb = c.v; p.releaseMs = c.u;
        for (int b = 0; b < AmpParams::kBands; ++b) { p.eq[b] = amp_[c.a].params().eq[b]; }
        amp_[c.a].set(p); } break;   // the bands follow in their own commands
      case kAmpEq: if (c.a >= 0 && c.a < kMaxOutputs && c.b >= 0 && c.b < AmpParams::kBands) { AmpParams p = amp_[c.a].params(); p.eq[c.b] = {static_cast<dsp::FilterType>(static_cast<int>(c.w)), c.x, c.y, c.z}; amp_[c.a].set(p); } break;
      case kTrim: trim_[c.a] = c.x; break;
      case kMasterFx: mixer_.setMasterProcessor(static_cast<BusProcessor*>(c.p)); break;
      case kBusFx: mixer_.setBusProcessor(c.a, static_cast<BusProcessor*>(c.p)); break;
      case kWidth: mixer_.setWidth(c.x); break;
      case kAutomix: mixer_.setAutomix(c.a, c.x != 0, c.y); break;
      case kAutoPol: if (c.a >= 0 && c.a < kMaxStrips) { autoPol_[c.a] = c.x != 0; apXY_[c.a] = apXX_[c.a] = apYY_[c.a] = apBad_[c.a] = apHold_[c.a] = apCorr_[c.a] = 0; } break;
      case kAutoLevel: mixer_.setAutoLevel(c.a != 0, c.x, c.y); break;
      case kOutBits: mixer_.setOutputBits(c.a); break;
      case kDcBlock: mixer_.setDcBlock(c.a != 0); break;
      case kAnalogCh: mixer_.setAnalogChannels(c.a != 0); break;
      case kProfReset: maxBlockUs_ = 0; jitterUs_ = 0; break;
      default: break;
    }
    tail_.store(t + 1, std::memory_order_release);
  }
  frames = std::min(frames, left_.size());
  const float* ins[kMaxStrips];
  NetInput* netIn = netIn_.load();
  if (netIn) { float* nb[kNetInputs]; for (int k = 0; k < kNetInputs; ++k) nb[k] = netBuf_[static_cast<std::size_t>(k)].data(); netIn->pull(nb, kNetInputs, frames, mixer_.sampleRate()); }
  const int tapRaw = tapRaw_.load(std::memory_order_relaxed); const float* rawIn = nullptr;
  for (int s = 0; s < mixer_.strips(); ++s) {
    const int src = inRoute_[s];
    ins[s] = (src >= kNetInBase && src < kNetInBase + kNetInputs) ? (netIn ? netBuf_[static_cast<std::size_t>(src - kNetInBase)].data() : silence_.data())
           : (src >= 0 && src < nIn_ && in && in[src]) ? in[src] : silence_.data();
    if (s == tapRaw) rawIn = ins[s];
    dsp::Channel& ch = ch_[static_cast<std::size_t>(s)];
    const double tr = trim_[s];
    const std::shared_ptr<const InsertChain> chain = std::atomic_load(&inserts_[s]);
    const bool anyInsert = chain && !chain->empty();
    if (ch.active() || tr != 1.0 || anyInsert) {   // the strip's input trim, channel DSP and inserts, in a scratch block
      float* b = chBuf_[static_cast<std::size_t>(s)].data();
      if (tr != 1.0) { const float g = static_cast<float>(tr); for (std::size_t i = 0; i < frames; ++i) b[i] = ins[s][i] * g; } else std::memcpy(b, ins[s], frames * sizeof(float));
      if (ch.active()) ch.process(b, frames);
      if (anyInsert) for (const auto& ins_ : *chain) if (ins_) ins_->process(b, frames);
      ins[s] = b;
    }
  }
  {   // AUTO PHASE: each strip that asks for it against the rest of the mix
    bool any = false; for (int s = 0; s < mixer_.strips(); ++s) any = any || autoPol_[s];
    if (any && frames <= sumBuf_.size()) {
      float* sum = sumBuf_.data(); std::fill(sum, sum + frames, 0.0f);
      for (int s = 0; s < mixer_.strips(); ++s) if (ins[s] != silence_.data()) for (std::size_t i = 0; i < frames; ++i) sum[i] += ins[s][i];
      const double dt = static_cast<double>(frames) / mixer_.sampleRate(), a = std::min(1.0, dt / 1.0);   // about one second of memory
      for (int s = 0; s < mixer_.strips(); ++s) {
        if (!autoPol_[s]) continue;
        double xy = 0, xx = 0, yy = 0; const float* x = ins[s];
        for (std::size_t i = 0; i < frames; ++i) { const double xs = x[i], ys = static_cast<double>(sum[i]) - xs; xy += xs * ys; xx += xs * xs; yy += ys * ys; }
        const double fn = static_cast<double>(frames);
        apXY_[s] += a * (xy / fn - apXY_[s]); apXX_[s] += a * (xx / fn - apXX_[s]); apYY_[s] += a * (yy / fn - apYY_[s]);
        const bool signal = apXX_[s] > 1e-7 && apYY_[s] > 1e-7;   // both above about -70 dBFS
        apCorr_[s] = signal ? std::clamp(apXY_[s] / std::sqrt(apXX_[s] * apYY_[s]), -1.0, 1.0) : 0.0;
        if (apHold_[s] > 0) { apHold_[s] -= dt; continue; }
        apBad_[s] = signal && apCorr_[s] < -0.35 ? apBad_[s] + dt : std::max(0.0, apBad_[s] - dt);
        if (apBad_[s] >= 1.0) {   // out of phase for a second: turn it round; it takes effect from the next block
          dsp::Channel& ch = ch_[static_cast<std::size_t>(s)]; ch.setPolarity(!ch.params().polarityInvert);
          apBad_[s] = 0; apHold_[s] = 3.0; apXY_[s] = -apXY_[s]; apCorr_[s] = -apCorr_[s];
        }
      }
    }
  }
  float* busOut[2 * kMaxBuses]; for (int k = 0; k < 2 * kMaxBuses; ++k) busOut[k] = busBuf_[static_cast<std::size_t>(k)].data();
  ExtraSource* ext = extra_.load();
  if (ext) { ext->render(extL_.data(), extR_.data(), frames, mixer_.sampleRate()); }
  mixer_.process(ins, frames, left_.data(), right_.data(), busOut, ext ? extL_.data() : nullptr, ext ? extR_.data() : nullptr);
  if (rawIn) ins[tapRaw] = rawIn;   // from here on only the taps read ins: the measured strip as the device delivered it
  if (Tap* tap = tap_.load()) tap->onBlock(ins, mixer_.strips(), left_.data(), right_.data(), frames, mixer_.sampleRate());
  if (Tap* tap = netTap_.load()) tap->onBlock(ins, mixer_.strips(), left_.data(), right_.data(), frames, mixer_.sampleRate());
  { float* ch[2] = {left_.data(), right_.data()}; mixer_.applyOutputTrim(ch, 2, frames); }   // GAIN MODE's listening level: after the meters and the taps
  // the monitor section: source, level (smoothed over the block, no clicks), mono, mute -> outputs 0 / 1, with its own peak
  { const float* ml = monSrc_ > 0 ? busOut[2 * (monSrc_ - 1)] : left_.data(); const float* mr = monSrc_ > 0 ? busOut[2 * (monSrc_ - 1) + 1] : right_.data();
    const double g0 = monGain_, g1 = monGainTarget_; monGain_ = g1; const double step = frames ? (g1 - g0) / static_cast<double>(frames) : 0; double pl = 0, pr = 0;
    float* ol = monL_.data(); float* or_ = monR_.data();
    long tone = toneLeft_.load(); const long tone0 = tone, toneAll = toneTotal_.load(); const double toneStep = 6.283185307179586 * 1000.0 / mixer_.sampleRate();
    for (std::size_t i = 0; i < frames; ++i) {
      const float g = static_cast<float>(g0 + step * static_cast<double>(i));
      float l = ml[i] * g, r = mr[i] * g; if (monMono_) { l = r = 0.5f * (l + r); }
      if (tone > 0) { const long done = toneAll - tone; const long edge = std::min<long>(480, toneAll / 4 + 1); const float env = done < edge ? static_cast<float>(done) / static_cast<float>(edge) : tone < edge ? static_cast<float>(tone) / static_cast<float>(edge) : 1.f;
        const float t = 0.1f * env * static_cast<float>(std::sin(tonePhase_)); tonePhase_ += toneStep; if (tonePhase_ > 6.283185307179586) tonePhase_ -= 6.283185307179586; l += t; r += t; --tone; }
      ol[i] = l; or_[i] = r;
      pl = std::max(pl, static_cast<double>(std::fabs(l))); pr = std::max(pr, static_cast<double>(std::fabs(r)));
    }
    if (tone0 > 0) { long expect = tone0; toneLeft_.compare_exchange_strong(expect, tone); }   // a new testTone() during the block wins
    monPk_[0] = std::max(pl, monPk_[0] * 0.8); monPk_[1] = std::max(pr, monPk_[1] * 0.8); }
  // the output matrix: every device output carries one source
  for (int c = 0; c < nOut_; ++c) {
    if (!out || !out[c]) continue;
    const int src = c < kMaxOutputs ? outRoute_[c] : kSrcOff; const float* from = nullptr;
    if (src == kSrcMasterL) from = left_.data(); else if (src == kSrcMasterR) from = right_.data();
    else if (src == kSrcMonL) from = monL_.data(); else if (src == kSrcMonR) from = monR_.data();
    else if (src >= kSrcBus0L && src - kSrcBus0L < 2 * kMaxBuses) from = busOut[src - kSrcBus0L];
    else if (src == kSrcMasterMono && mono_.size() >= frames) { for (std::size_t i = 0; i < frames; ++i) mono_[i] = 0.5f * (left_[i] + right_[i]); from = mono_.data(); }
    if (from) std::memcpy(out[c], from, frames * sizeof(float)); else std::memset(out[c], 0, frames * sizeof(float));
    if (c < kMaxOutputs) amp_[c].process(out[c], static_cast<int>(frames));   // the amplifier stage (off = untouched)
  }
  ++blocks_; frames_ += frames;
  { const std::int64_t t1 = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); blockUs_ = static_cast<double>(t1 - t0) / 1000.0; if (blockUs_ > maxBlockUs_) maxBlockUs_ = blockUs_; loadPct_ = 100.0 * blockUs_ / (static_cast<double>(frames) * 1e6 / mixer_.sampleRate()); }
  publish();
}

void Runtime::publish() {
  Snapshot s; std::memset(&s, 0, sizeof s);
  for (int i = 0; i < mixer_.strips(); ++i) { const MeterState& m = mixer_.strip(i).meter.state(); const std::size_t k = static_cast<std::size_t>(i); s.strips[i] = {m.pk, m.rms, m.hold, m.clipN, k < ch_.size() ? ch_[k].compressor().gainReductionDb() : 0.0, k < ch_.size() ? ch_[k].gate().gainDb() : 0.0, mixer_.strip(i).inPk, mixer_.automixGainDb(i), apCorr_[i], k < ch_.size() && ch_[k].params().polarityInvert}; }
  for (int c = 0; c < 2; ++c) { const MeterState& m = mixer_.masterMeter(c); s.master[c] = {m.pk, m.rms, m.hold, m.clipN, 0.0, 0.0, 0.0, 0.0, 0.0, false}; }
  for (int k = 0; k < kMaxBuses; ++k) { const MeterState& m = mixer_.bus(k).meter.state(); s.buses[k] = {m.pk, m.rms, m.hold, m.clipN, 0.0, 0.0, 0.0, 0.0, 0.0, false}; }
  s.momentary = mixer_.loudness().momentary(); s.shortTerm = mixer_.loudness().shortTerm(); s.integrated = mixer_.loudness().integrated(); s.lra = mixer_.loudness().range(); s.truePeak = mixer_.loudness().truePeak(); s.autoLevelDb = mixer_.autoLevelGainDb();
  s.monitorPk[0] = monPk_[0]; s.monitorPk[1] = monPk_[1];
  for (int o = 0; o < kMaxOutputs; ++o) { s.ampPk[o] = amp_[o].peak(); s.ampGrDb[o] = amp_[o].gainReductionDb(); }
  s.correlation = mixer_.phase().corr; s.blocks = blocks_; s.frames = frames_; s.loadPct = loadPct_; s.blockUs = blockUs_; s.maxBlockUs = maxBlockUs_; s.jitterUs = jitterUs_; s.xruns = backend_ ? backend_->xruns() : 0;
  const unsigned q = seq_.load(std::memory_order_relaxed);
  seq_.store(q + 1, std::memory_order_release);                                    // odd: being written
  std::memcpy(&pub_, &s, sizeof s);
  seq_.store(q + 2, std::memory_order_release);
}
Snapshot Runtime::snapshot() const {
  Snapshot s;
  for (;;) {
    const unsigned a = seq_.load(std::memory_order_acquire);
    if (a & 1u) continue;
    std::memcpy(&s, &pub_, sizeof s);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (seq_.load(std::memory_order_acquire) == a) return s;
  }
}

}  // namespace am
