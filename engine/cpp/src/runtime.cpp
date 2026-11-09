// Audio Mixer runtime: see include/audiomixer/runtime.hpp.
#include "audiomixer/runtime.hpp"

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

enum Op { kGain, kPan, kMute, kSolo, kMaster, kLimiter, kClipDb, kClear, kSpatialOn, kSpatialPos, kSpatialSpread, kEq, kDelay, kPolarity, kXover, kChLimiter, kComp, kGate };

Runtime::Runtime(int strips) : mixer_(strips, 48000) { std::memset(&pub_, 0, sizeof pub_); }
Runtime::~Runtime() { stop(); }

bool Runtime::open(const DeviceConfig& cfg, std::string& err) {
  stop();
  std::string name = cfg.backend;
  if (name == "auto") name = availableBackends().front();
  backend_ = createBackend(name);
  if (!backend_) { err = "the backend \"" + name + "\" is not built into this binary"; return false; }
  spatial_.reset(); spatialOn_ = false;
  if (Spatial::available()) spatial_.reset(new Spatial(cfg.sampleRate, 256));
  backendName_ = name; nIn_ = std::max(0, cfg.inChannels); nOut_ = std::max(0, cfg.outChannels);
  mixer_ = Mixer(mixer_.strips(), cfg.sampleRate);                                  // the mixer runs at the device rate
  const std::size_t n = std::max<std::size_t>(cfg.frames, 16) * 8;                  // scratch for blocks up to 8 periods (devices may hand out more than the period)
  silence_.assign(n, 0.0f); left_.assign(n, 0.0f); right_.assign(n, 0.0f);
  ch_.assign(static_cast<std::size_t>(mixer_.strips()), dsp::Channel(cfg.sampleRate)); chBuf_.assign(ch_.size(), std::vector<float>(n, 0.0f));
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
void Runtime::clearClips() { post({kClear, 0, 0, 0, 0}); }
void Runtime::setEqBand(int s, int band, dsp::FilterType t, double f, double q, double g) { Cmd c{kEq, s, f, q, g}; c.b = band; c.w = static_cast<double>(t); post(c); }
void Runtime::setDelayMs(int s, double ms) { post({kDelay, s, ms, 0, 0}); }
void Runtime::setPolarity(int s, bool inv) { post({kPolarity, s, inv ? 1.0 : 0.0, 0, 0}); }
void Runtime::setCrossover(int s, dsp::XoverType t, int hpO, double hpF, int lpO, double lpF) { Cmd c{kXover, s, hpF, lpF, static_cast<double>(hpO)}; c.b = static_cast<int>(t); c.w = static_cast<double>(lpO); post(c); }
void Runtime::setChannelLimiter(int s, bool on, double ceil, double rel) { Cmd c{kChLimiter, s, ceil, rel, 0}; c.b = on ? 1 : 0; post(c); }
void Runtime::setCompressor(int s, bool on, double thr, double ratio, double knee, double att, double rel, double makeup) { Cmd c{kComp, s, thr, ratio, knee}; c.b = on ? 1 : 0; c.w = att; c.v = rel; c.u = makeup; post(c); }
void Runtime::setGate(int s, bool on, double thr, double range, double att, double hold, double rel) { Cmd c{kGate, s, thr, range, att}; c.b = on ? 1 : 0; c.w = hold; c.v = rel; post(c); }
bool Runtime::setSpatial(bool on) { if (!spatial_ || !spatial_->ok()) return false; post({kSpatialOn, on ? 1 : 0, 0, 0, 0}); return true; }
void Runtime::setSpatialPosition(double az, double el, double d, double spread) { post({kSpatialPos, 0, az, el, d}); post({kSpatialSpread, 0, spread, 0, 0}); }

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
      case kClear: mixer_.clearClips(); break;
      case kSpatialOn: spatialOn_ = c.a != 0; break;
      case kSpatialPos: if (spatial_) { spAz_ = c.x; spEl_ = c.y; spDist_ = c.z; spatial_->setPosition(spAz_, spEl_, spDist_, spSpread_); } break;
      case kSpatialSpread: if (spatial_) { spSpread_ = c.x; spatial_->setPosition(spAz_, spEl_, spDist_, spSpread_); } break;
      case kEq: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setEqBand(c.b, {static_cast<dsp::FilterType>(static_cast<int>(c.w)), c.x, c.y, c.z}); break;
      case kDelay: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setDelayMs(c.x); break;
      case kPolarity: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setPolarity(c.x != 0); break;
      case kXover: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setCrossover(static_cast<dsp::XoverType>(c.b), static_cast<int>(c.z), c.x, static_cast<int>(c.w), c.y); break;
      case kChLimiter: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setLimiter(c.b != 0, c.x, c.y); break;
      case kComp: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setCompressor(c.b != 0, c.x, c.y, c.z, c.w, c.v, c.u); break;
      case kGate: if (c.a >= 0 && c.a < static_cast<int>(ch_.size())) ch_[static_cast<std::size_t>(c.a)].setGate(c.b != 0, c.x, c.y, c.z, c.w, c.v); break;
      default: break;
    }
    tail_.store(t + 1, std::memory_order_release);
  }
  frames = std::min(frames, left_.size());
  const float* ins[kMaxStrips];
  for (int s = 0; s < mixer_.strips(); ++s) {
    ins[s] = (s < nIn_ && in && in[s]) ? in[s] : silence_.data();
    dsp::Channel& ch = ch_[static_cast<std::size_t>(s)];
    if (ch.active()) { float* b = chBuf_[static_cast<std::size_t>(s)].data(); std::memcpy(b, ins[s], frames * sizeof(float)); ch.process(b, frames); ins[s] = b; }   // the strip's channel DSP, in a scratch block
  }
  mixer_.process(ins, frames, left_.data(), right_.data());
  if (spatialOn_ && spatial_) spatial_->process(left_.data(), right_.data(), left_.data(), right_.data(), frames);     // after the meters: they show the stereo master
  for (int c = 0; c < nOut_; ++c) {
    if (c == 0) std::memcpy(out[c], left_.data(), frames * sizeof(float));
    else if (c == 1) std::memcpy(out[c], right_.data(), frames * sizeof(float));
    else std::memset(out[c], 0, frames * sizeof(float));
  }
  ++blocks_; frames_ += frames;
  publish();
}

void Runtime::publish() {
  Snapshot s; std::memset(&s, 0, sizeof s);
  for (int i = 0; i < mixer_.strips(); ++i) { const MeterState& m = mixer_.strip(i).meter.state(); const std::size_t k = static_cast<std::size_t>(i); s.strips[i] = {m.pk, m.rms, m.hold, m.clipN, k < ch_.size() ? ch_[k].compressor().gainReductionDb() : 0.0, k < ch_.size() ? ch_[k].gate().gainDb() : 0.0}; }
  for (int c = 0; c < 2; ++c) { const MeterState& m = mixer_.masterMeter(c); s.master[c] = {m.pk, m.rms, m.hold, m.clipN, 0.0, 0.0}; }
  s.momentary = mixer_.loudness().momentary(); s.shortTerm = mixer_.loudness().shortTerm(); s.integrated = mixer_.loudness().integrated(); s.lra = mixer_.loudness().range(); s.truePeak = mixer_.loudness().truePeak();
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
