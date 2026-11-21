// DUCKER · VOICE OVER MUSIC: while a KEY (voice) signal is above a threshold, the TARGET (music) channels are lowered by DEPTH dB,
// and the key channels can be lifted by up to VOICE LIFT dB in proportion to how far the music is down.
//
//   detector   the loudest key sample's magnitude, a peak follower (instant attack, 10 ms release) -> dBFS
//   gate       open while the detector is above THRESHOLD; stays open HOLD ms after it falls below
//   gain       a one-pole in dB toward DEPTH (open) or 0 dB (closed): ATTACK ms going down, RELEASE ms coming back. The gain moves a
//              little every sample (never a step), so ducking does not click.
//
// One process() call per sample, fixed state, no allocation, no locks: real-time safe. Defaults: -12 dB, 20 / 300 / 600 ms, -40 dBFS, lift 0.
#pragma once
#include <algorithm>
#include <cmath>

namespace am {

struct DuckParams {
  bool on = false;
  double thresholdDb = -40;   // key level that opens the ducker (dBFS, -80 .. 0)
  double depthDb = -12;       // how far the targets go down (-40 .. 0)
  double attackMs = 20, holdMs = 300, releaseMs = 600;
  double liftDb = 0;          // voice lift on the key channels while ducked (0 .. +6)
};

class Ducker {
 public:
  void set(const DuckParams& p, double fs) {
    p_ = p; fs_ = fs > 0 ? fs : 48000;
    p_.thresholdDb = std::clamp(p_.thresholdDb, -80.0, 0.0); p_.depthDb = std::clamp(p_.depthDb, -40.0, 0.0); p_.liftDb = std::clamp(p_.liftDb, 0.0, 6.0);
    p_.attackMs = std::clamp(p_.attackMs, 1.0, 500.0); p_.holdMs = std::clamp(p_.holdMs, 0.0, 5000.0); p_.releaseMs = std::clamp(p_.releaseMs, 10.0, 5000.0);
    thrLin_ = std::pow(10.0, p_.thresholdDb / 20);
    att_ = 1.0 - std::exp(-1.0 / (p_.attackMs * 0.001 * fs_)); rel_ = 1.0 - std::exp(-1.0 / (p_.releaseMs * 0.001 * fs_));
    detRel_ = std::exp(-1.0 / (0.010 * fs_)); holdN_ = static_cast<long>(p_.holdMs * 0.001 * fs_);
    if (!p_.on) reset();
  }
  const DuckParams& params() const { return p_; }
  void reset() { det_ = 0; gainDb_ = 0; holdLeft_ = 0; }
  // keyAbs: |loudest key sample| this sample. Returns the target gain (linear); lift() then holds the key gain (linear) of the same sample.
  double process(double keyAbs) {
    if (!p_.on) { gainDb_ = 0; return 1.0; }
    det_ = keyAbs > det_ ? keyAbs : det_ * detRel_;
    if (det_ >= thrLin_) holdLeft_ = holdN_ + 1;
    const bool open = holdLeft_ > 0; if (holdLeft_ > 0) --holdLeft_;
    const double target = open ? p_.depthDb : 0.0;
    gainDb_ += (target - gainDb_) * (target < gainDb_ ? att_ : rel_);
    return std::pow(10.0, gainDb_ / 20);
  }
  double lift() const { if (!p_.on || p_.liftDb <= 0 || p_.depthDb >= 0) return 1.0; const double f = std::clamp(gainDb_ / p_.depthDb, 0.0, 1.0); return std::pow(10.0, f * p_.liftDb / 20); }
  double gainDb() const { return gainDb_; }        // <= 0: the gain reduction on the targets now
  bool open() const { return holdLeft_ > 0; }
 private:
  DuckParams p_; double fs_ = 48000, thrLin_ = 0.01, att_ = 0, rel_ = 0, detRel_ = 0, det_ = 0, gainDb_ = 0; long holdN_ = 0, holdLeft_ = 0;
};

}  // namespace am
