// LOUDNESS MANAGER · per-channel AUTO FADER and AUTO ON/OFF (gate), in the engine block by block.
//
//   meter      the strip's own input (pre-fader), K-weighted (BS.1770 shelf + high-pass), mean square in 50 ms pieces; SHORT-TERM =
//              the last 3 s (60 pieces), FAST = the current block. LUFS = -0.691 + 10 log10(mean square) (one channel).
//   ride       the fader offset that brings  input + user fader + ride  to TARGET: clamped to +-RANGE around the user's fader and so
//              that user + ride never goes above MAX (+10 dB), moved at most UP dB/s going up and DOWN dB/s going down. Because the
//              meter reads the input before the fader, the ride does not chase its own output (no loop). It only moves while the channel
//              has signal (short-term above the gate threshold), and holds still while the channel is muted or SUSPENDED (a hand on
//              the fader: touch() suspends it for 5 s and starts the ride from 0 around the new fader position).
//   gate       a channel whose FAST level stays under the gate threshold for HOLD seconds fades to FLOOR (-80 dB = off) over 1 s;
//              it opens again within one block (a 10 ms ramp in the mixer) as soon as the level is back over the threshold.
//
// Gain order on a strip (see Mixer::process):  user fader + DCA offset (the console, setGainDb, max +10)  x  ride  x  gate  x  automix
// share  x  ducker. The ride is also limited so that fader + ride <= +10 dB. The ride and gate gain is ramped across each block (no clicks).
// Fixed state, no allocation, no locks: real-time safe. Defaults: target -18 LUFS, range 10 dB, +3 / -6 dB/s, gate -50 LUFS, hold 4 s.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace am {

struct RideParams {
  bool on = false;            // AUTO FADER
  bool gate = false;          // AUTO ON/OFF
  double targetLufs = -18;    // -40 .. -6
  double rangeDb = 10;        // 0 .. 20: how far from the user's fader
  double upDbS = 3, downDbS = 6;   // speed limits (0.1 .. 40 dB/s)
  double gateLufs = -50;      // -80 .. -20
  double holdS = 4;           // 0.1 .. 30
  double floorDb = -80;       // where a closed gate goes (-80 = off)
  double maxDb = 10;          // user fader + ride never above this
  void clamp() {
    targetLufs = std::clamp(targetLufs, -40.0, -6.0); rangeDb = std::clamp(rangeDb, 0.0, 20.0); upDbS = std::clamp(upDbS, 0.1, 40.0); downDbS = std::clamp(downDbS, 0.1, 40.0);
    gateLufs = std::clamp(gateLufs, -80.0, -20.0); holdS = std::clamp(holdS, 0.1, 30.0); floorDb = std::clamp(floorDb, -80.0, -6.0); maxDb = std::clamp(maxDb, -20.0, 10.0);
  }
};

class LoudRide {
 public:
  static constexpr int kPieces = 60;   // 60 x 50 ms = 3 s
  void prepare(double fs, const double* pre5, const double* hp5) {
    fs_ = fs > 0 ? fs : 48000; for (int k = 0; k < 5; ++k) { c1_[k] = pre5[k]; c2_[k] = hp5[k]; }
    piece_ = static_cast<long>(fs_ * 0.05 + 0.5); reset();
  }
  void set(const RideParams& p) { const bool was = p_.on || p_.gate; p_ = p; p_.clamp(); if (!p_.on) rideDb_ = 0; if (!p_.gate) { gateDb_ = 0; silentS_ = 0; } if (!was && (p_.on || p_.gate)) resetMeter(); }
  const RideParams& params() const { return p_; }
  bool active() const { return p_.on || p_.gate; }
  void reset() { resetMeter(); rideDb_ = 0; gateDb_ = 0; silentS_ = 0; suspendS_ = 0; }
  void touch(double seconds = 5) { rideDb_ = 0; suspendS_ = std::max(0.0, seconds); }   // a hand on the fader: the new position is the new centre
  // the strip's input for this block (nullptr = silence)
  void measure(const float* x, std::size_t n) {
    double e = 0;
    if (x) { double a1 = z_[0], a2 = z_[1], b1 = z_[2], b2 = z_[3];
      for (std::size_t i = 0; i < n; ++i) { const double v = x[i], y1 = c1_[0] * v + a1; a1 = c1_[1] * v - c1_[3] * y1 + a2; a2 = c1_[2] * v - c1_[4] * y1;
        const double y2 = c2_[0] * y1 + b1; b1 = c2_[1] * y1 - c2_[3] * y2 + b2; b2 = c2_[2] * y1 - c2_[4] * y2; e += y2 * y2; }
      z_[0] = a1; z_[1] = a2; z_[2] = b1; z_[3] = b2;
      if (!std::isfinite(z_[0] + z_[1] + z_[2] + z_[3])) { z_[0] = z_[1] = z_[2] = z_[3] = 0; e = 0; } }
    fastLufs_ = n ? lufs(e / static_cast<double>(n)) : -120.0;
    acc_ += e; accN_ += static_cast<long>(n);
    if (accN_ >= piece_) { ring_[w_] = acc_ / static_cast<double>(accN_); w_ = (w_ + 1) % kPieces; if (filled_ < kPieces) ++filled_; acc_ = 0; accN_ = 0;
      double s = 0; for (int k = 0; k < filled_; ++k) s += ring_[k]; stLufs_ = lufs(s / filled_); }
  }
  // after measure(): moves the ride and the gate by one block of dt seconds. userDb = the strip's fader (with DCA); muted = held still.
  // Returns the ride + gate gain in dB for this block.
  double step(double dt, double userDb, bool muted) {
    if (suspendS_ > 0) suspendS_ = std::max(0.0, suspendS_ - dt);
    if (p_.on && !muted && suspendS_ <= 0 && filled_ >= 10 && stLufs_ > p_.gateLufs && stLufs_ > -70) {   // 0.5 s of signal at least
      const double hi = std::min(p_.rangeDb, p_.maxDb - userDb), lo = -p_.rangeDb;
      const double want = std::clamp(p_.targetLufs - (stLufs_ + userDb), lo, std::max(lo, hi));
      rideDb_ += want > rideDb_ ? std::min(want - rideDb_, p_.upDbS * dt) : std::max(want - rideDb_, -p_.downDbS * dt);
    }
    if (p_.on) { const double hi = std::min(p_.rangeDb, p_.maxDb - userDb); if (rideDb_ > hi) rideDb_ = std::max(-p_.rangeDb, hi); if (rideDb_ < -p_.rangeDb) rideDb_ = -p_.rangeDb; }
    else rideDb_ = 0;
    if (p_.gate && !muted) {
      if (fastLufs_ >= p_.gateLufs) { silentS_ = 0; gateDb_ = 0; }   // back at once (the mixer ramps it over the block)
      else { silentS_ += dt; if (silentS_ >= p_.holdS) gateDb_ = std::max(p_.floorDb, gateDb_ + p_.floorDb * dt); }   // to the floor over 1 s
    } else if (!p_.gate) { gateDb_ = 0; silentS_ = 0; }
    return rideDb_ + gateDb_;
  }
  double rideDb() const { return rideDb_; }
  double gateDb() const { return gateDb_; }
  bool gateClosed() const { return gateDb_ < -0.01; }
  double shortTerm() const { return filled_ ? stLufs_ : -120.0; }
  double fast() const { return fastLufs_; }
  bool suspended() const { return suspendS_ > 0; }
  double lin() const { const double d = rideDb_ + gateDb_; return d <= -79.9 ? 0.0 : std::pow(10.0, d / 20); }
  double prevLin = 1;   // the mixer's ramp start (last block's gain)
 private:
  static double lufs(double ms) { return ms > 1e-12 ? -0.691 + 10.0 * std::log10(ms) : -120.0; }
  void resetMeter() { for (double& r : ring_) r = 0; w_ = 0; filled_ = 0; acc_ = 0; accN_ = 0; stLufs_ = -120; fastLufs_ = -120; z_[0] = z_[1] = z_[2] = z_[3] = 0; }
  RideParams p_; double fs_ = 48000, c1_[5] = {1, 0, 0, 0, 0}, c2_[5] = {1, 0, 0, 0, 0}, z_[4] = {0, 0, 0, 0};
  double ring_[kPieces] = {}; int w_ = 0, filled_ = 0; double acc_ = 0; long accN_ = 0, piece_ = 2400;
  double stLufs_ = -120, fastLufs_ = -120, rideDb_ = 0, gateDb_ = 0, silentS_ = 0, suspendS_ = 0;
};

}  // namespace am
