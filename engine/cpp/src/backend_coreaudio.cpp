// Core Audio backend (macOS): the HAL audio unit with input and output enabled, float32 non-interleaved at the requested rate. NOT compiled or run in this repository
// (there is no Xcode here): expect to debug it. Build with: c++ -std=c++17 ... -framework AudioToolbox -framework CoreAudio -framework AudioUnit
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <vector>

#include "backends.hpp"

namespace am {
namespace {
class CoreAudioBackend : public Backend {
 public:
  ~CoreAudioBackend() override { stop(); if (unit_) { AudioComponentInstanceDispose(unit_); unit_ = nullptr; } }
  const char* name() const override { return "coreaudio"; }
  bool open(const DeviceConfig& cfg, AudioCallback cb, std::string& err) override {
    cfg_ = cfg; cb_ = std::move(cb);
    AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent comp = AudioComponentFindNext(nullptr, &d); if (!comp || AudioComponentInstanceNew(comp, &unit_) != noErr) { err = "no HAL audio unit"; return false; }
    const bool wantIn = cfg.inChannels > 0 && cfg.inDevice != "-", wantOut = cfg.outChannels > 0 && cfg.outDevice != "-"; UInt32 on = 1, off = 0;
    AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, wantIn ? &on : &off, sizeof on);
    AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, wantOut ? &on : &off, sizeof on);
    AudioStreamBasicDescription f = {}; f.mSampleRate = cfg.sampleRate; f.mFormatID = kAudioFormatLinearPCM; f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    f.mFramesPerPacket = 1; f.mBitsPerChannel = 32; f.mBytesPerFrame = f.mBytesPerPacket = 4;
    if (wantOut) { f.mChannelsPerFrame = static_cast<UInt32>(cfg.outChannels); AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f); }
    if (wantIn) { f.mChannelsPerFrame = static_cast<UInt32>(cfg.inChannels); AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &f, sizeof f); }
    UInt32 frames = cfg.frames; AudioUnitSetProperty(unit_, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0, &frames, sizeof frames);
    inBuf_.assign(static_cast<std::size_t>(cfg.inChannels), std::vector<float>(4096, 0.0f)); for (auto& v : inBuf_) inp_.push_back(v.data());
    ablIn_.resize(sizeof(AudioBufferList) + sizeof(AudioBuffer) * static_cast<std::size_t>(std::max(1, cfg.inChannels)));
    AURenderCallbackStruct cbs = {&CoreAudioBackend::render, this}; AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cbs, sizeof cbs);
    if (AudioUnitInitialize(unit_) != noErr) { err = "cannot initialize the audio unit"; return false; }
    return true;
  }
  bool start(std::string& err) override { if (AudioOutputUnitStart(unit_) != noErr) { err = "cannot start the audio unit"; return false; } return true; }
  void stop() override { if (unit_) AudioOutputUnitStop(unit_); }
 private:
  static OSStatus render(void* self, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32, UInt32 frames, AudioBufferList* io) {
    auto* b = static_cast<CoreAudioBackend*>(self); const int ci = b->cfg_.inChannels;
    if (ci > 0 && frames <= 4096) {
      auto* abl = reinterpret_cast<AudioBufferList*>(b->ablIn_.data()); abl->mNumberBuffers = static_cast<UInt32>(ci);
      for (int c = 0; c < ci; ++c) { abl->mBuffers[c].mNumberChannels = 1; abl->mBuffers[c].mDataByteSize = frames * 4; abl->mBuffers[c].mData = b->inBuf_[static_cast<std::size_t>(c)].data(); }
      if (AudioUnitRender(b->unit_, flags, ts, 1, frames, abl) != noErr) for (auto& v : b->inBuf_) std::fill(v.begin(), v.begin() + frames, 0.0f);
    }
    std::vector<float*> out(io->mNumberBuffers); for (UInt32 c = 0; c < io->mNumberBuffers; ++c) out[c] = static_cast<float*>(io->mBuffers[c].mData);
    b->cb_(b->inp_.data(), out.data(), frames); return noErr;
  }
  DeviceConfig cfg_; AudioCallback cb_; AudioUnit unit_ = nullptr; std::vector<std::vector<float>> inBuf_; std::vector<const float*> inp_; std::vector<unsigned char> ablIn_;
};
}  // namespace
std::unique_ptr<Backend> makeCoreAudioBackend() { return std::make_unique<CoreAudioBackend>(); }
}  // namespace am
#endif
