// Audio Mixer runtime: see include/audiomixer/runtime.hpp.
#include "audiomixer/runtime.hpp"
#include <cmath>

#include <algorithm>
#include <cstring>

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
#endif
#ifdef __APPLE__
  if (name == "coreaudio") return makeCoreAudioBackend();
#endif
  return nullptr;
}

enum Op { kGain, kPan, kMute, kSolo, kMaster, kLimiter, kClipDb, kClear, kEq, kDelay, kPolarity, kXover, kChLimiter, kComp, kGate, kSend, kBusLevel, kBusMute, kBusReturn, kMonitor, kInRoute, kOutRoute, kTrim, kPeakHold };

Runtime::Runtime(int strips) : mixer_(strips, 48000) { std::memset(&pub_, 0, sizeof pub_); }
Runtime::~Runtime() { stop(); }

bool Runtime::open(const DeviceConfig& cfg, std::string& err) {
  stop();
  std::string name = cfg.backend;
  if (name == "auto") name = availableBackends().front();
  backend_ = createBackend(name);
  if (!backend_) { err = "the backend \"" + name + "\" is not built into this binary"; return false; }
  backendName_ = name; nIn_ = std::max(0, cfg.inChannels); nOut_ = std::max(0, cfg.outChannels);
  mixer_ = Mixer(mixer_.strips(), cfg.sampleRate);                                  // the mixer runs at the device rate
  const std::size_t n = std::max<std::size_t>(cfg.frames, 16) * 8;                  // scratch for blocks up to 8 periods (devices may hand out more than the period)
  silence_.assign(n, 0.0f); left_.assign(n, 0.0f); right_.assign(n, 0.0f); monL_.assign(n, 0.0f); monR_.assign(n, 0.0f); extL_.assign(n, 0.0f); extR_.assign(n, 0.0f);
  for (int s = 0; s < kMaxStrips; ++s) { inRoute_[s] = s; trim_[s] = 1.0; }
  for (int o = 0; o < kMaxOutputs; ++o) outRoute_[o] = defaultOutputSource(o);
  ch_.assign(static_cast<std::size_t>(mixer_.strips()), dsp::Channel(cfg.sampleRate)); chBuf_.assign(ch_.size(), std::vector<float>(n, 0.0f));
  busBuf_.assign(2 * kMaxBuses, std::vector<float>(n, 0.0f));
  head_ = 0; tail_ = 0; blocks_ = 0; frames_ = 0; std::memset(&pub_, 0, sizeof pub_);
  return backend_->open(cfg, [this](const float* const* in, float* const* out, std::size_t frames) { onBlock(in, out, frames); }, err);
}
bool Runtime::start(std::string& err) { if (!backend_) { err = "open() first"; return false; } if (running_) return true; running_ = backend_->start(err); return running_; }
void Runtime::stop() { if (backend_ && running_) backend_->stop(); running_ = false; }

void Runtime::post(const Cmd& c) {
  std::lock_guard<std::mutex> g(producers_);
  const unsigned h = head_.load(std::memory_order_relaxed);
  if (h - tail_.load(std::memory_order_acquire) >= kRing) return;                   // full: the newest change is dropped (the audio thread is not running)
  ring_[h % kRing] = c; head_.store(h + 1, std::memory_order_release);
}
void Runtime::setGainDb(int s, double db) { post({kGain, s, db, 0, 0}); }
void Runtime::setPan(int s, double p) { post({kPan, s, p, 0, 0}); }
void Runtime::setMute(int s, bool on) { post({kMute, s, on ? 1.0 : 0.0, 0, 0}); }
void Runtime::setSolo(int s, bool on) { post({kSolo, s, on ? 1.0 : 0.0, 0, 0}); }
void Runtime::setMasterDb(double db) { post({kMaster, 0, db, 0, 0}); }
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
void Runtime::setInputRoute(int strip, int input) { if (strip < 0 || strip >= kMaxStrips) return; post({kInRoute, strip, 0, 0, 0, std::max(-1, std::min(kMaxStrips - 1, input))}); }
void Runtime::setOutputRoute(int out, int source) { if (out < 0 || out >= kMaxOutputs) return; post({kOutRoute, out, 0, 0, 0, std::max(0, std::min(kSrcBus0L + 2 * kMaxBuses - 1, source))}); }
void Runtime::setInserts(int strip, std::shared_ptr<const InsertChain> chain) { if (strip >= 0 && strip < kMaxStrips) inserts_[strip].store(std::move(chain)); }
void Runtime::setTrimDb(int strip, double db) { if (strip < 0 || strip >= kMaxStrips) return; post({kTrim, strip, std::pow(10.0, std::clamp(db, -24.0, 24.0) / 20), 0, 0}); }
void Runtime::setMonitor(int source, double db, bool dim, bool mono, bool mute) {
  double lin = std::pow(10.0, std::clamp(db, -120.0, 10.0) / 20); if (dim) lin *= 0.1; if (mute || db <= -119) lin = 0;
  post({kMonitor, std::clamp(source, 0, kMaxBuses), lin, 0, 0, mono ? 1 : 0});
}

void Runtime::onBlock(const float* const* in, float* const* out, std::size_t frames) {
  for (unsigned t = tail_.load(std::memory_order_relaxed), h = head_.load(std::memory_order_acquire); t != h; ++t) {   // control changes of this block
    const Cmd& c = ring_[t % kRing];
    switch (c.op) {
      case kGain: mixer_.setGainDb(c.a, c.x); break;
      case kPan: mixer_.setPan(c.a, c.x); break;
      case kMute: mixer_.setMute(c.a, c.x != 0); break;
      case kSolo: mixer_.setSolo(c.a, c.x != 0); break;
      case kMaster: mixer_.setMasterDb(c.x); break;
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
      case kTrim: trim_[c.a] = c.x; break;
      default: break;
    }
    tail_.store(t + 1, std::memory_order_release);
  }
  frames = std::min(frames, left_.size());
  const float* ins[kMaxStrips];
  for (int s = 0; s < mixer_.strips(); ++s) {
    const int src = inRoute_[s];
    ins[s] = (src >= 0 && src < nIn_ && in && in[src]) ? in[src] : silence_.data();
    dsp::Channel& ch = ch_[static_cast<std::size_t>(s)];
    const double tr = trim_[s];
    const std::shared_ptr<const InsertChain> chain = inserts_[s].load();
    const bool anyInsert = chain && !chain->empty();
    if (ch.active() || tr != 1.0 || anyInsert) {   // the strip's input trim, channel DSP and inserts, in a scratch block
      float* b = chBuf_[static_cast<std::size_t>(s)].data();
      if (tr != 1.0) { const float g = static_cast<float>(tr); for (std::size_t i = 0; i < frames; ++i) b[i] = ins[s][i] * g; } else std::memcpy(b, ins[s], frames * sizeof(float));
      if (ch.active()) ch.process(b, frames);
      if (anyInsert) for (const auto& ins_ : *chain) if (ins_) ins_->process(b, frames);
      ins[s] = b;
    }
  }
  float* busOut[2 * kMaxBuses]; for (int k = 0; k < 2 * kMaxBuses; ++k) busOut[k] = busBuf_[static_cast<std::size_t>(k)].data();
  ExtraSource* ext = extra_.load();
  if (ext) { ext->render(extL_.data(), extR_.data(), frames, mixer_.sampleRate()); }
  mixer_.process(ins, frames, left_.data(), right_.data(), busOut, ext ? extL_.data() : nullptr, ext ? extR_.data() : nullptr);
  // the monitor section: source, level (smoothed over the block, no clicks), mono, mute -> outputs 0 / 1, with its own peak
  { const float* ml = monSrc_ > 0 ? busOut[2 * (monSrc_ - 1)] : left_.data(); const float* mr = monSrc_ > 0 ? busOut[2 * (monSrc_ - 1) + 1] : right_.data();
    const double g0 = monGain_, g1 = monGainTarget_; monGain_ = g1; const double step = frames ? (g1 - g0) / static_cast<double>(frames) : 0; double pl = 0, pr = 0;
    float* ol = monL_.data(); float* or_ = monR_.data();
    for (std::size_t i = 0; i < frames; ++i) {
      const float g = static_cast<float>(g0 + step * static_cast<double>(i));
      float l = ml[i] * g, r = mr[i] * g; if (monMono_) { l = r = 0.5f * (l + r); }
      ol[i] = l; or_[i] = r;
      pl = std::max(pl, static_cast<double>(std::fabs(l))); pr = std::max(pr, static_cast<double>(std::fabs(r)));
    }
    monPk_[0] = std::max(pl, monPk_[0] * 0.8); monPk_[1] = std::max(pr, monPk_[1] * 0.8); }
  // the output matrix: every device output carries one source
  for (int c = 0; c < nOut_; ++c) {
    if (!out || !out[c]) continue;
    const int src = c < kMaxOutputs ? outRoute_[c] : kSrcOff; const float* from = nullptr;
    if (src == kSrcMasterL) from = left_.data(); else if (src == kSrcMasterR) from = right_.data();
    else if (src == kSrcMonL) from = monL_.data(); else if (src == kSrcMonR) from = monR_.data();
    else if (src >= kSrcBus0L && src - kSrcBus0L < 2 * kMaxBuses) from = busOut[src - kSrcBus0L];
    if (from) std::memcpy(out[c], from, frames * sizeof(float)); else std::memset(out[c], 0, frames * sizeof(float));
  }
  ++blocks_; frames_ += frames;
  publish();
}

void Runtime::publish() {
  Snapshot s; std::memset(&s, 0, sizeof s);
  for (int i = 0; i < mixer_.strips(); ++i) { const MeterState& m = mixer_.strip(i).meter.state(); const std::size_t k = static_cast<std::size_t>(i); s.strips[i] = {m.pk, m.rms, m.hold, m.clipN, k < ch_.size() ? ch_[k].compressor().gainReductionDb() : 0.0, k < ch_.size() ? ch_[k].gate().gainDb() : 0.0, mixer_.strip(i).inPk}; }
  for (int c = 0; c < 2; ++c) { const MeterState& m = mixer_.masterMeter(c); s.master[c] = {m.pk, m.rms, m.hold, m.clipN, 0.0, 0.0, 0.0}; }
  for (int k = 0; k < kMaxBuses; ++k) { const MeterState& m = mixer_.bus(k).meter.state(); s.buses[k] = {m.pk, m.rms, m.hold, m.clipN, 0.0, 0.0, 0.0}; }
  s.momentary = mixer_.loudness().momentary(); s.shortTerm = mixer_.loudness().shortTerm(); s.integrated = mixer_.loudness().integrated(); s.lra = mixer_.loudness().range(); s.truePeak = mixer_.loudness().truePeak();
  s.monitorPk[0] = monPk_[0]; s.monitorPk[1] = monPk_[1];
  s.correlation = mixer_.phase().corr; s.blocks = blocks_; s.frames = frames_; s.xruns = backend_ ? backend_->xruns() : 0;
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
