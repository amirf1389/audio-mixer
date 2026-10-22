// 360 degree output: a stereo pair rendered as two virtual speakers in a binaural scene by the Resonance Audio engine (google/resonance-audio, Apache-2.0).
// Optional: compiled in only when the build is given the Resonance Audio headers and library (AM_RESONANCE_INCLUDE / AM_RESONANCE_LIB in CMake, or
// scripts/build-resonance.js, which fetches and builds the pinned sources); Spatial::available() says whether this binary has it.
// The azimuth is in degrees, 0 = ahead, positive = to the right; elevation up is positive; the distance is in metres; spread is the angle between each speaker and the azimuth.
// The renderer works in fixed blocks: the output is delayed by exactly block() frames, and any number of frames may be passed to process().
#pragma once
#include <cstddef>
#include <memory>

namespace am {

class Spatial {
 public:
  Spatial(double sampleRate, unsigned block = 256);
  ~Spatial();
  Spatial(const Spatial&) = delete;
  Spatial& operator=(const Spatial&) = delete;
  static bool available();
  bool ok() const;                                        // false when not available or the engine could not be created
  unsigned block() const;
  void setPosition(double azimuthDeg, double elevationDeg, double distanceM, double spreadDeg);
  // planar in -> planar out (the buffers may be the same); frames is not limited
  void process(const float* inL, const float* inR, float* outL, float* outR, std::size_t frames);
 private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};

}  // namespace am
