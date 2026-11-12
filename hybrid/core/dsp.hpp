// 64-bit building blocks: transposed direct form II biquads (seven-band EQ), constant-power
// balance and Mid/Side width.
#pragma once
#include <algorithm>
#include <cmath>
#include "shared.hpp"

namespace amfb {

constexpr double kPi = 3.14159265358979323846;
inline double dbToLin(double db) { return db <= -90.0 ? 0.0 : std::pow(10.0, db / 20.0); }
inline double linToDb(double v) { return v <= 1e-9 ? -180.0 : 20.0 * std::log10(v); }

struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  enum Kind { LowShelf, Peak, HighShelf };

  // Audio EQ Cookbook (R. Bristow-Johnson) designs. A band at 0 dB is an exact pass-through.
  void design(Kind kind, double rate, double freq, double gainDb, double q) {
    if (std::abs(gainDb) < 1e-6) { b0 = 1; b1 = b2 = a1 = a2 = 0; return; }
    freq = std::clamp(freq, 10.0, rate * 0.49);
    q = std::clamp(q, 0.1, 18.0);
    const double A = std::pow(10.0, gainDb / 40.0), w = 2 * kPi * freq / rate, cs = std::cos(w), sn = std::sin(w);
    const double alpha = sn / (2 * q);
    double B0, B1, B2, A0, A1, A2;
    if (kind == Peak) {
      B0 = 1 + alpha * A; B1 = -2 * cs; B2 = 1 - alpha * A;
      A0 = 1 + alpha / A; A1 = -2 * cs; A2 = 1 - alpha / A;
    } else {
      const double r = 2 * std::sqrt(A) * alpha, s = kind == LowShelf ? 1.0 : -1.0;
      B0 = A * ((A + 1) - s * (A - 1) * cs + r);
      B1 = s * 2 * A * ((A - 1) - s * (A + 1) * cs);
      B2 = A * ((A + 1) - s * (A - 1) * cs - r);
      A0 = (A + 1) + s * (A - 1) * cs + r;
      A1 = -s * 2 * ((A - 1) + s * (A + 1) * cs);
      A2 = (A + 1) + s * (A - 1) * cs - r;
    }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
  }
};

struct BiquadState {
  double z1 = 0, z2 = 0;
  inline double run(const Biquad& c, double x) noexcept {   // transposed direct form II
    const double y = c.b0 * x + z1;
    z1 = c.b1 * x - c.a1 * y + z2;
    z2 = c.b2 * x - c.a2 * y;
    return y;
  }
};

struct Eq7 {
  Biquad c[kBands];
  BiquadState l[kBands], r[kBands];
  bool flat = true;
  void design(double rate, const float* freq, const float* gain, const float* q) {
    flat = true;
    for (int k = 0; k < kBands; ++k) {
      c[k].design(k == 0 ? Biquad::LowShelf : k == kBands - 1 ? Biquad::HighShelf : Biquad::Peak, rate, freq[k], gain[k], q[k]);
      if (std::abs(gain[k]) >= 1e-6f) flat = false;
    }
  }
  inline void run(double& x, double& y) noexcept {
    for (int k = 0; k < kBands; ++k) { x = l[k].run(c[k], x); y = r[k].run(c[k], y); }
  }
};

// Constant-power balance: the centre is unity on both sides, a side at full is +3 dB, the other silent.
inline void balanceGains(double pan, double& gl, double& gr) {
  const double t = (std::clamp(pan, -1.0, 1.0) + 1.0) * kPi / 4.0;
  gl = std::cos(t) * std::sqrt(2.0);
  gr = std::sin(t) * std::sqrt(2.0);
}

// Mid/Side width: 0 = mono, 1 = untouched, 2 = side doubled.
inline void midSide(double& l, double& r, double width) noexcept {
  const double m = 0.5 * (l + r), s = 0.5 * (l - r) * width;
  l = m + s; r = m - s;
}

}  // namespace amfb
