// 360 degree output over the Resonance Audio engine: see audiomixer/spatial.hpp. Without AM_HAVE_RESONANCE this is an empty stand-in (available() is false, process() passes the stereo through).
#include "audiomixer/spatial.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef AM_HAVE_RESONANCE
#include "resonance_audio_api.h"
#endif

namespace am {

struct Spatial::Impl {
  unsigned block = 256; double rate = 48000;
  double az = 0, el = 0, dist = 2, spread = 30; bool dirty = true;
#ifdef AM_HAVE_RESONANCE
  std::unique_ptr<vraudio::ResonanceAudioApi> api; vraudio::ResonanceAudioApi::SourceId srcL = -1, srcR = -1;
  std::vector<float> inL, inR, outL, outR, bufL, bufR, tmpL, tmpR;     // input FIFO (planar), output FIFO, one block of scratch
  std::size_t inCount = 0, outCount = 0;
#endif
};

bool Spatial::available() {
#ifdef AM_HAVE_RESONANCE
  return true;
#else
  return false;
#endif
}

#ifdef AM_HAVE_RESONANCE
Spatial::Spatial(double sampleRate, unsigned block) : p_(new Impl) {
  p_->rate = sampleRate; p_->block = std::max(16u, block);
  p_->api.reset(vraudio::CreateResonanceAudioApi(2, p_->block, static_cast<int>(sampleRate)));
  if (!p_->api) return;
  p_->srcL = p_->api->CreateSoundObjectSource(vraudio::kBinauralHighQuality);
  p_->srcR = p_->api->CreateSoundObjectSource(vraudio::kBinauralHighQuality);
  const std::size_t n = p_->block;
  p_->inL.assign(n, 0.0f); p_->inR.assign(n, 0.0f); p_->bufL.assign(n, 0.0f); p_->bufR.assign(n, 0.0f); p_->tmpL.assign(n, 0.0f); p_->tmpR.assign(n, 0.0f);
  p_->outL.assign(n, 0.0f); p_->outR.assign(n, 0.0f); p_->outCount = n;     // one block of silence in the output FIFO: the fixed latency
}
Spatial::~Spatial() = default;
bool Spatial::ok() const { return p_ && p_->api; }
unsigned Spatial::block() const { return p_ ? p_->block : 0; }
void Spatial::setPosition(double a, double e, double d, double s) { p_->az = a; p_->el = e; p_->dist = std::max(0.1, d); p_->spread = s; p_->dirty = true; }

void Spatial::process(const float* inL, const float* inR, float* outL, float* outR, std::size_t frames) {
  if (!ok()) { if (outL != inL) std::memcpy(outL, inL, frames * sizeof(float)); if (outR != inR) std::memcpy(outR, inR, frames * sizeof(float)); return; }
  Impl& s = *p_;
  if (s.dirty) {                                                           // x right, y up, -z ahead (Resonance's convention)
    const double rad = 3.14159265358979323846 / 180.0, el = s.el * rad;
    const double a[2] = {(s.az - s.spread) * rad, (s.az + s.spread) * rad};
    const vraudio::ResonanceAudioApi::SourceId id[2] = {s.srcL, s.srcR};
    for (int i = 0; i < 2; ++i) s.api->SetSourcePosition(id[i], static_cast<float>(s.dist * std::sin(a[i]) * std::cos(el)), static_cast<float>(s.dist * std::sin(el)), static_cast<float>(-s.dist * std::cos(a[i]) * std::cos(el)));
    s.dirty = false;
  }
  const std::size_t B = s.block;
  // the input FIFO is one block long: a block is rendered whenever it is full, and the rendered block is appended to the output FIFO
  std::vector<float>& oL = s.outL; std::vector<float>& oR = s.outR;
  std::size_t done = 0;
  while (done < frames) {
    const std::size_t take = std::min(frames - done, B - s.inCount);
    std::memcpy(&s.inL[s.inCount], inL + done, take * sizeof(float)); std::memcpy(&s.inR[s.inCount], inR + done, take * sizeof(float));
    s.inCount += take;
    // hand out what is ready from the output FIFO for these input frames
    const std::size_t give = std::min(take, s.outCount);
    // (the FIFO always holds at least `take` frames: it starts with one block and gains one block each time a block is rendered)
    std::memcpy(outL + done, oL.data(), give * sizeof(float)); std::memcpy(outR + done, oR.data(), give * sizeof(float));
    std::memmove(oL.data(), oL.data() + give, (s.outCount - give) * sizeof(float)); std::memmove(oR.data(), oR.data() + give, (s.outCount - give) * sizeof(float));
    s.outCount -= give;
    if (give < take) { std::memset(outL + done + give, 0, (take - give) * sizeof(float)); std::memset(outR + done + give, 0, (take - give) * sizeof(float)); }
    done += take;
    if (s.inCount == B) {
      const float* l = s.inL.data(); const float* r = s.inR.data();
      s.api->SetPlanarBuffer(s.srcL, &l, 1, B); s.api->SetPlanarBuffer(s.srcR, &r, 1, B);
      float* dst[2] = {s.tmpL.data(), s.tmpR.data()};
      s.api->FillPlanarOutputBuffer(2, B, dst);
      if (oL.size() < s.outCount + B) { oL.resize(s.outCount + B); oR.resize(s.outCount + B); }
      std::memcpy(oL.data() + s.outCount, s.tmpL.data(), B * sizeof(float)); std::memcpy(oR.data() + s.outCount, s.tmpR.data(), B * sizeof(float));
      s.outCount += B; s.inCount = 0;
    }
  }
}
#else
Spatial::Spatial(double sampleRate, unsigned block) : p_(new Impl) { p_->rate = sampleRate; p_->block = block; }
Spatial::~Spatial() = default;
bool Spatial::ok() const { return false; }
unsigned Spatial::block() const { return p_ ? p_->block : 0; }
void Spatial::setPosition(double, double, double, double) {}
void Spatial::process(const float* inL, const float* inR, float* outL, float* outR, std::size_t frames) {
  if (outL != inL) std::memcpy(outL, inL, frames * sizeof(float));
  if (outR != inR) std::memcpy(outR, inR, frames * sizeof(float));
}
#endif

}  // namespace am
