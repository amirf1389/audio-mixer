// Core Audio backend (macOS): one AUHAL (kAudioUnitSubType_HALOutput) unit on one HAL device, float32 non-interleaved.
// - devices: every HAL device with streams (UID = DeviceConfig id), the system defaults, rates, transport -> kind (addCoreAudio)
// - open: the device's nominal rate and buffer size are negotiated by coreaudio_logic (pure, tested on every system); an AGGREGATE device
//   is never reclocked from here (its sub-devices may be shared with other programs): the unit converts instead
// - device change: listeners on the device list / defaults (DeviceChangeWatch) and on the open device's IsAlive / rate (deviceLost)
// Only built on Apple (CMake: elseif(APPLE)); the Linux build compiles and tests coreaudio_logic.cpp only. Expect to debug on a Mac.
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "audiomixer/coreaudio_logic.hpp"
#include "backends.hpp"

namespace am {
namespace {

constexpr UInt32 kMaxFrames = 4096;

AudioObjectPropertyAddress addr(AudioObjectPropertySelector s, AudioObjectPropertyScope sc = kAudioObjectPropertyScopeGlobal) {
  return AudioObjectPropertyAddress{s, sc, kAudioObjectPropertyElementMain};
}
template <typename T> bool getProp(AudioObjectID obj, AudioObjectPropertySelector s, AudioObjectPropertyScope sc, T& out) {
  const AudioObjectPropertyAddress a = addr(s, sc); UInt32 size = sizeof(T);
  return AudioObjectGetPropertyData(obj, &a, 0, nullptr, &size, &out) == noErr;
}
std::string cfToString(CFStringRef s) {
  if (!s) return {};
  char buf[512]; if (CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8)) return buf;
  return {};
}
std::string stringProp(AudioObjectID obj, AudioObjectPropertySelector s) {
  CFStringRef v = nullptr; if (!getProp(obj, s, kAudioObjectPropertyScopeGlobal, v) || !v) return {};
  std::string r = cfToString(v); CFRelease(v); return r;
}
std::vector<AudioObjectID> allDevices() {
  const AudioObjectPropertyAddress a = addr(kAudioHardwarePropertyDevices); UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr || size == 0) return {};
  std::vector<AudioObjectID> v(size / sizeof(AudioObjectID));
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, v.data()) != noErr) return {};
  v.resize(size / sizeof(AudioObjectID)); return v;
}
int channelCount(AudioObjectID dev, AudioObjectPropertyScope scope) {
  const AudioObjectPropertyAddress a = addr(kAudioDevicePropertyStreamConfiguration, scope); UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &size) != noErr || size == 0) return 0;
  std::vector<unsigned char> buf(size); auto* abl = reinterpret_cast<AudioBufferList*>(buf.data());
  if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, abl) != noErr) return 0;
  int n = 0; for (UInt32 i = 0; i < abl->mNumberBuffers; ++i) n += static_cast<int>(abl->mBuffers[i].mNumberChannels);
  return n;
}
std::vector<ca::RateRange> rateRanges(AudioObjectID dev) {
  const AudioObjectPropertyAddress a = addr(kAudioDevicePropertyAvailableNominalSampleRates); UInt32 size = 0;
  if (AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &size) != noErr || size == 0) return {};
  std::vector<AudioValueRange> r(size / sizeof(AudioValueRange));
  if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, r.data()) != noErr) return {};
  std::vector<ca::RateRange> out; for (const AudioValueRange& x : r) out.push_back({x.mMinimum, x.mMaximum}); return out;
}
AudioObjectID defaultDevice(bool input) {
  AudioObjectID d = kAudioObjectUnknown;
  getProp(kAudioObjectSystemObject, input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, d);
  return d;
}
AudioObjectID deviceByUid(const std::string& uid) {
  for (AudioObjectID d : allDevices()) if (stringProp(d, kAudioDevicePropertyDeviceUID) == uid) return d;
  return kAudioObjectUnknown;
}

class CoreAudioBackend : public Backend {
 public:
  ~CoreAudioBackend() override {
    stop(); unlisten();
    if (unit_) { AudioUnitUninitialize(unit_); AudioComponentInstanceDispose(unit_); unit_ = nullptr; }
  }
  const char* name() const override { return "coreaudio"; }
  bool open(const DeviceConfig& cfg, AudioCallback cb, std::string& err) override {
    cfg_ = cfg; cb_ = std::move(cb);
    const AudioObjectID dIn = defaultDevice(true), dOut = defaultDevice(false);
    const ca::DevicePlan plan = ca::planDevices(cfg.inDevice, cfg.outDevice, cfg.inChannels, cfg.outChannels,
                                                dIn ? stringProp(dIn, kAudioDevicePropertyDeviceUID) : std::string(),
                                                dOut ? stringProp(dOut, kAudioDevicePropertyDeviceUID) : std::string());
    if (!plan.ok) { err = plan.err; return false; }
    dev_ = deviceByUid(plan.wantOut ? plan.outId : plan.inId);
    if (dev_ == kAudioObjectUnknown) { err = "the audio device is not there (unplugged?)"; return false; }
    inCh_ = plan.wantIn ? ca::clampChannels(cfg.inChannels, channelCount(dev_, kAudioObjectPropertyScopeInput)) : 0;
    outCh_ = plan.wantOut ? ca::clampChannels(cfg.outChannels, channelCount(dev_, kAudioObjectPropertyScopeOutput)) : 0;
    if (inCh_ == 0 && outCh_ == 0) { err = "the device has no channels in the asked direction"; return false; }

    // RATE: the device's own nominal rate when it can run the asked one; an aggregate keeps its clock (the unit converts)
    Float64 cur = 0; getProp(dev_, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, cur);
    UInt32 transport = 0; getProp(dev_, kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal, transport);
    aggregate_ = ca::isAggregateTransport(transport);
    const double target = ca::pickSampleRate(cfg.sampleRate, rateRanges(dev_), cur);
    Boolean settable = false; { const AudioObjectPropertyAddress a = addr(kAudioDevicePropertyNominalSampleRate); AudioObjectIsPropertySettable(dev_, &a, &settable); }
    if (ca::shouldSetRate(cur, target, settable, aggregate_)) {
      const AudioObjectPropertyAddress a = addr(kAudioDevicePropertyNominalSampleRate); Float64 r = target;
      AudioObjectSetPropertyData(dev_, &a, 0, nullptr, sizeof r, &r);   // asynchronous: the rate listener below sees it settle
    }
    // BUFFER: inside the device's range
    AudioValueRange fr = {0, 0}; getProp(dev_, kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal, fr);
    UInt32 frames = std::min<UInt32>(kMaxFrames, ca::clampBufferFrames(cfg.frames, static_cast<unsigned>(fr.mMinimum), static_cast<unsigned>(fr.mMaximum)));
    { const AudioObjectPropertyAddress a = addr(kAudioDevicePropertyBufferFrameSize); AudioObjectSetPropertyData(dev_, &a, 0, nullptr, sizeof frames, &frames); }

    AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent comp = AudioComponentFindNext(nullptr, &d);
    if (!comp || AudioComponentInstanceNew(comp, &unit_) != noErr) { err = "no HAL audio unit"; return false; }
    UInt32 on = 1, off = 0;
    AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, inCh_ ? &on : &off, sizeof on);
    AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, outCh_ ? &on : &off, sizeof on);
    if (AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev_, sizeof dev_) != noErr) { err = "the audio unit refused the device"; return false; }
    AudioStreamBasicDescription f = {}; f.mSampleRate = cfg.sampleRate; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    f.mFramesPerPacket = 1; f.mBitsPerChannel = 32; f.mBytesPerFrame = f.mBytesPerPacket = 4;
    if (outCh_) { f.mChannelsPerFrame = static_cast<UInt32>(outCh_); if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) != noErr) { err = "the output format was refused"; return false; } }
    if (inCh_) { f.mChannelsPerFrame = static_cast<UInt32>(inCh_); if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &f, sizeof f) != noErr) { err = "the input format was refused"; return false; } }
    UInt32 maxF = kMaxFrames; AudioUnitSetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxF, sizeof maxF);

    // the engine always gets the channel counts it asked for: missing ones are silence / dropped
    inBuf_.assign(static_cast<std::size_t>(std::max(cfg.inChannels, 0)), std::vector<float>(kMaxFrames, 0.0f));
    inp_.clear(); for (auto& v : inBuf_) inp_.push_back(v.data());
    outBuf_.assign(static_cast<std::size_t>(std::max(cfg.outChannels, 0)), std::vector<float>(kMaxFrames, 0.0f));
    outp_.assign(outBuf_.size(), nullptr);
    ablIn_.resize(sizeof(AudioBufferList) + sizeof(AudioBuffer) * static_cast<std::size_t>(std::max(1, inCh_)));
    if (outCh_) { AURenderCallbackStruct cbs = {&CoreAudioBackend::render, this}; AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cbs, sizeof cbs); }
    else { AURenderCallbackStruct cbs = {&CoreAudioBackend::inputOnly, this}; AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cbs, sizeof cbs); }
    if (AudioUnitInitialize(unit_) != noErr) { err = "cannot initialize the audio unit"; return false; }
    listen();
    return true;
  }
  bool start(std::string& err) override { if (!unit_ || AudioOutputUnitStart(unit_) != noErr) { err = "cannot start the audio unit"; return false; } running_ = true; return true; }
  void stop() override { if (unit_ && running_) { AudioOutputUnitStop(unit_); running_ = false; } }   // returns after the render thread left the callback
  bool deviceLost() const override { return lost_.load(); }
  std::uint64_t xruns() const override { return xruns_.load(); }
  void latencyFrames(long& in, long& out) const override {
    auto side = [this](AudioObjectPropertyScope sc) { UInt32 l = 0, s = 0, b = 0; getProp(dev_, kAudioDevicePropertyLatency, sc, l); getProp(dev_, kAudioDevicePropertySafetyOffset, sc, s);
      getProp(dev_, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, b); return static_cast<long>(l + s + b); };
    in = inCh_ ? side(kAudioObjectPropertyScopeInput) : 0; out = outCh_ ? side(kAudioObjectPropertyScopeOutput) : 0;
  }

 private:
  void pullInput(AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32 frames) {
    if (inCh_ <= 0) return;
    auto* abl = reinterpret_cast<AudioBufferList*>(ablIn_.data()); abl->mNumberBuffers = static_cast<UInt32>(inCh_);
    for (int c = 0; c < inCh_; ++c) { abl->mBuffers[c].mNumberChannels = 1; abl->mBuffers[c].mDataByteSize = frames * 4; abl->mBuffers[c].mData = inBuf_[static_cast<std::size_t>(c)].data(); }
    if (AudioUnitRender(unit_, flags, ts, 1, frames, abl) != noErr) { ++xruns_; for (auto& v : inBuf_) std::fill(v.begin(), v.begin() + frames, 0.0f); }
  }
  static OSStatus render(void* self, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32, UInt32 frames, AudioBufferList* io) {
    auto* b = static_cast<CoreAudioBackend*>(self);
    if (frames > kMaxFrames) { for (UInt32 c = 0; c < io->mNumberBuffers; ++c) std::fill_n(static_cast<float*>(io->mBuffers[c].mData), frames, 0.0f); ++b->xruns_; return noErr; }
    b->pullInput(flags, ts, frames);
    for (std::size_t c = 0; c < b->outp_.size(); ++c) b->outp_[c] = c < io->mNumberBuffers ? static_cast<float*>(io->mBuffers[c].mData) : b->outBuf_[c].data();
    b->cb_(b->inp_.data(), b->outp_.data(), frames);
    return noErr;
  }
  static OSStatus inputOnly(void* self, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32, UInt32 frames, AudioBufferList*) {
    auto* b = static_cast<CoreAudioBackend*>(self); if (frames > kMaxFrames) { ++b->xruns_; return noErr; }
    b->pullInput(flags, ts, frames);
    for (std::size_t c = 0; c < b->outp_.size(); ++c) b->outp_[c] = b->outBuf_[c].data();
    b->cb_(b->inp_.data(), b->outp_.data(), frames);
    return noErr;
  }
  // the open device went away (unplugged, the aggregate was edited, coreaudiod restarted) or its rate moved under us: deviceLost()
  static OSStatus onDevice(AudioObjectID, UInt32 n, const AudioObjectPropertyAddress* a, void* self) {
    auto* b = static_cast<CoreAudioBackend*>(self);
    for (UInt32 i = 0; i < n; ++i) {
      if (a[i].mSelector == kAudioDevicePropertyDeviceIsAlive) { UInt32 alive = 1; getProp(b->dev_, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, alive); if (!alive) b->lost_ = true; }
      if (a[i].mSelector == kAudioDeviceProcessorOverload) ++b->xruns_;
    }
    return noErr;
  }
  void listen() {
    for (AudioObjectPropertySelector s : {kAudioDevicePropertyDeviceIsAlive, kAudioDeviceProcessorOverload}) { const AudioObjectPropertyAddress a = addr(s); AudioObjectAddPropertyListener(dev_, &a, &CoreAudioBackend::onDevice, this); }
    listening_ = true;
  }
  void unlisten() {
    if (!listening_) return;
    for (AudioObjectPropertySelector s : {kAudioDevicePropertyDeviceIsAlive, kAudioDeviceProcessorOverload}) { const AudioObjectPropertyAddress a = addr(s); AudioObjectRemovePropertyListener(dev_, &a, &CoreAudioBackend::onDevice, this); }
    listening_ = false;
  }

  DeviceConfig cfg_; AudioCallback cb_; AudioUnit unit_ = nullptr; AudioObjectID dev_ = kAudioObjectUnknown;
  int inCh_ = 0, outCh_ = 0; bool aggregate_ = false, running_ = false, listening_ = false;
  std::atomic<bool> lost_{false}; std::atomic<std::uint64_t> xruns_{0};
  std::vector<std::vector<float>> inBuf_, outBuf_; std::vector<const float*> inp_; std::vector<float*> outp_; std::vector<unsigned char> ablIn_;
};
}  // namespace

std::unique_ptr<Backend> makeCoreAudioBackend() { return std::make_unique<CoreAudioBackend>(); }

void addCoreAudio(std::vector<DeviceInfo>& out) {
  const AudioObjectID dIn = defaultDevice(true), dOut = defaultDevice(false);
  for (AudioObjectID d : allDevices()) {
    DeviceInfo x; x.backend = "coreaudio"; x.id = stringProp(d, kAudioDevicePropertyDeviceUID); x.name = stringProp(d, kAudioObjectPropertyName);
    x.inChannels = channelCount(d, kAudioObjectPropertyScopeInput); x.outChannels = channelCount(d, kAudioObjectPropertyScopeOutput);
    if (x.id.empty() || (x.inChannels == 0 && x.outChannels == 0)) continue;
    Float64 r = 0; if (getProp(d, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, r) && r > 0) x.defaultRate = r;
    for (const ca::RateRange& rr : rateRanges(d)) for (double c : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
      if (c >= rr.min && c <= rr.max && std::find(x.sampleRates.begin(), x.sampleRates.end(), c) == x.sampleRates.end()) x.sampleRates.push_back(c);
    std::sort(x.sampleRates.begin(), x.sampleRates.end());
    UInt32 t = 0; getProp(d, kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal, t);
    x.kind = ca::kindOfTransport(t, x.outChannels == 0);
    x.defaultIn = d == dIn; x.defaultOut = d == dOut;
    out.push_back(std::move(x));
  }
}

std::string coreAudioFingerprint() {
  std::vector<std::string> ids; for (AudioObjectID d : allDevices()) ids.push_back(stringProp(d, kAudioDevicePropertyDeviceUID));
  ids.push_back("in=" + std::to_string(defaultDevice(true))); ids.push_back("out=" + std::to_string(defaultDevice(false)));
  return ca::fingerprint(ids);
}

// DEVICE WATCH on macOS: the HAL calls these on its own notification thread; the callback only hands the news on
struct CoreAudioWatch {
  std::mutex m; std::function<void()> f; bool ok = false;
  static OSStatus fire(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* self) {
    auto* w = static_cast<CoreAudioWatch*>(self); std::lock_guard<std::mutex> l(w->m); if (w->f) w->f(); return noErr;
  }
};
static const AudioObjectPropertySelector kWatched[] = {kAudioHardwarePropertyDevices, kAudioHardwarePropertyDefaultInputDevice, kAudioHardwarePropertyDefaultOutputDevice};
void* coreAudioWatchStart(std::function<void()> f, bool& ok) {
  auto* w = new CoreAudioWatch; w->f = std::move(f); ok = true;
  for (AudioObjectPropertySelector s : kWatched) { const AudioObjectPropertyAddress a = addr(s); if (AudioObjectAddPropertyListener(kAudioObjectSystemObject, &a, &CoreAudioWatch::fire, w) != noErr) ok = false; }
  w->ok = ok; return w;
}
void coreAudioWatchStop(void* p) {
  auto* w = static_cast<CoreAudioWatch*>(p); if (!w) return;
  for (AudioObjectPropertySelector s : kWatched) { const AudioObjectPropertyAddress a = addr(s); AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &a, &CoreAudioWatch::fire, w); }
  std::lock_guard<std::mutex> l(w->m); w->f = nullptr;   // kept, not freed: a notification already queued on the HAL thread may still call fire()
}

}  // namespace am
#endif
