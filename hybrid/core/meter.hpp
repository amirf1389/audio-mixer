// Stereo meter: sample peak, RMS (300 ms), true peak (4x oversampled, as ITU-R BS.1770 annex 2
// describes), peak hold and phase correlation.
#pragma once
#include <cmath>
#include "dsp.hpp"

namespace amfb {

class TruePeak4x {
 public:
  TruePeak4x() {
    // 48-tap windowed sinc, split in four phases of 12 taps; every phase is normalised to unity gain
    for (int p = 0; p < 4; ++p) {
      double sum = 0;
      for (int k = 0; k < kTaps; ++k) {
        const int n = 4 * k + p;
        const double x = (n - (4 * kTaps - 1) / 2.0) / 4.0;
        const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
        const double win = 0.54 - 0.46 * std::cos(2 * kPi * (n + 0.5) / (4 * kTaps));   // Hamming
        h_[p][k] = sinc * win; sum += h_[p][k];
      }
      for (int k = 0; k < kTaps; ++k) h_[p][k] /= sum;
    }
  }
  inline double run(double x) noexcept {      // returns the largest of the four oversampled values
    z_[pos_] = x;
    double peak = 0;
    for (int p = 0; p < 4; ++p) {
      double acc = 0;
      int j = pos_;
      for (int k = kTaps - 1; k >= 0; --k) { acc += h_[p][k] * z_[j]; j = j == 0 ? kTaps - 1 : j - 1; }
      peak = std::max(peak, std::abs(acc));
    }
    pos_ = pos_ + 1 == kTaps ? 0 : pos_ + 1;
    return peak;
  }

 private:
  static constexpr int kTaps = 12;
  double h_[4][kTaps]{}, z_[kTaps]{};
  int pos_ = 0;
};

class StereoMeter {
 public:
  void setRate(double rate) { rmsK_ = 1.0 - std::exp(-1.0 / (0.3 * rate)); rate_ = rate; }
  inline void feed(double l, double r) noexcept {
    pl_ = std::max(pl_, std::abs(l)); pr_ = std::max(pr_, std::abs(r));
    tp_ = std::max(tp_, std::max(tl_.run(l), tr_.run(r)));
    ml_ += rmsK_ * (l * l - ml_); mr_ += rmsK_ * (r * r - mr_);
    xy_ += rmsK_ * (l * r - xy_);
  }
  // once per block: publish and start the next block's peaks
  void publish(StripMeters& m, std::size_t frames) noexcept {
    const double block = std::max(pl_, pr_);
    if (block >= hold_) { hold_ = block; holdLeft_ = 1.5 * rate_; }
    else if ((holdLeft_ -= double(frames)) <= 0) hold_ *= std::pow(0.5, double(frames) / (0.15 * rate_));   // then falls 6 dB per 150 ms
    const double den = std::sqrt(ml_ * mr_);
    m.peakL = float(pl_); m.peakR = float(pr_); m.truePeak = float(tp_); m.hold = float(hold_);
    m.rmsL = float(std::sqrt(ml_)); m.rmsR = float(std::sqrt(mr_));
    m.phase = float(den > 1e-12 ? std::clamp(xy_ / den, -1.0, 1.0) : 0.0);
    pl_ = pr_ = tp_ = 0;
  }

 private:
  TruePeak4x tl_, tr_;
  double pl_ = 0, pr_ = 0, tp_ = 0, ml_ = 0, mr_ = 0, xy_ = 0, hold_ = 0, holdLeft_ = 0, rmsK_ = 0, rate_ = 48000;
};

}  // namespace amfb
