// THE WATERFALL's logic, apart from any drawing: the log-frequency axis, the folding of an FFT's power spectrum into log-spaced bands,
// the ring of past spectra (fixed storage: nothing is allocated once it is configured), the level colour map and the 3D view (yaw, tilt,
// a little perspective) fitted into a rectangle. The console's Waterfall3D item draws with it; the unit test checks it on its own.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace am::measure::wf {

// LOG FREQUENCY: u = 0 at fLo, 1 at fHi, equal steps per octave
struct LogAxis {
  double fLo = 20, fHi = 20000;
  double u(double f) const { f = std::clamp(f, fLo, fHi); return std::log(f / fLo) / std::log(fHi / fLo); }
  double freq(double u) const { return fLo * std::pow(fHi / fLo, std::clamp(u, 0.0, 1.0)); }
};

// BANDS: n log-spaced bands from fLo to fHi over a power spectrum of fftSize points (bins 0 .. fftSize / 2). A band wider than a bin
// takes the strongest bin in it (a sine reads its level whatever the band width); a band narrower than a bin (the low end) reads the
// spectrum between the two bins around its centre.
class Bands {
 public:
  void prepare(int n, std::size_t fftSize, double fs, double fLo = 20, double fHi = 20000) {
    n_ = std::max(2, n); axis_ = LogAxis{fLo, std::min(fHi, fs * 0.5)}; k0_.assign(static_cast<std::size_t>(n_), 0); k1_.assign(static_cast<std::size_t>(n_), 0); frac_.assign(static_cast<std::size_t>(n_), 0.f);
    const double binHz = fs / static_cast<double>(fftSize); const int last = static_cast<int>(fftSize / 2);
    for (int b = 0; b < n_; ++b) {
      const double lo = axis_.freq(static_cast<double>(b) / n_), hi = axis_.freq(static_cast<double>(b + 1) / n_), c = std::sqrt(lo * hi);
      int k0 = static_cast<int>(std::ceil(lo / binHz)), k1 = static_cast<int>(std::floor(hi / binHz)); k0 = std::clamp(k0, 1, last); k1 = std::clamp(k1, 1, last);
      if (k1 < k0) { const double x = std::clamp(c / binHz, 1.0, static_cast<double>(last - 1)); k0 = static_cast<int>(std::floor(x)); k1 = -1; frac_[static_cast<std::size_t>(b)] = static_cast<float>(x - k0); }   // between two bins
      k0_[static_cast<std::size_t>(b)] = k0; k1_[static_cast<std::size_t>(b)] = k1;
    }
  }
  int size() const { return n_; }
  const LogAxis& axis() const { return axis_; }
  double centre(int b) const { return axis_.freq((b + 0.5) / n_); }
  // power: |X[k]|^2 already scaled so that a full-scale sine reads 1; out: dB per band (floor -160)
  void fold(const float* power, float* outDb) const {
    for (int b = 0; b < n_; ++b) { const std::size_t i = static_cast<std::size_t>(b); double p;
      if (k1_[i] < 0) { const int k = k0_[i]; p = power[k] + (power[k + 1] - power[k]) * frac_[i]; }
      else { p = 0; for (int k = k0_[i]; k <= k1_[i]; ++k) p = std::max<double>(p, power[k]); }
      outDb[i] = static_cast<float>(p > 1e-16 ? 10 * std::log10(p) : -160.0); }
  }
 private:
  int n_ = 0; LogAxis axis_; std::vector<int> k0_, k1_; std::vector<float> frac_;
};

// THE HISTORY: rows x bins floats in one block, the newest row at age 0. configure() sizes it (and clears); push() hands out the next row
// to fill and makes it the newest; nothing is allocated after configure().
class RowRing {
 public:
  void configure(int rows, int bins, float fill = -160.f) { rows_ = std::max(1, rows); bins_ = std::max(1, bins); data_.assign(static_cast<std::size_t>(rows_) * static_cast<std::size_t>(bins_), fill); head_ = 0; count_ = 0; fill_ = fill; }
  float* push() { head_ = (head_ + 1) % rows_; count_ = std::min(count_ + 1, rows_); return data_.data() + static_cast<std::size_t>(head_) * static_cast<std::size_t>(bins_); }
  const float* row(int age) const { age = std::clamp(age, 0, rows_ - 1); const int r = ((head_ - age) % rows_ + rows_) % rows_; return data_.data() + static_cast<std::size_t>(r) * static_cast<std::size_t>(bins_); }
  void clear() { std::fill(data_.begin(), data_.end(), fill_); count_ = 0; }
  int rows() const { return rows_; }
  int bins() const { return bins_; }
  int count() const { return count_; }
  const float* storage() const { return data_.data(); }   // the tests check that it never moves
 private:
  std::vector<float> data_; int rows_ = 1, bins_ = 1, head_ = 0, count_ = 0; float fill_ = -160.f;
};

// LEVEL COLOURS: quiet is the dark of the console's panels, then indigo, the cyan of its meters, orange, the AmF amber, and a warm white at
// the top of the range, each step brighter than the one before. t = 0 .. 1; a 256-step table.
struct Rgba8 { std::uint8_t r = 0, g = 0, b = 0, a = 255; };
class ColorMap {
 public:
  ColorMap() {
    struct S { double t; double r, g, b; };
    static constexpr S stops[] = {{0.00, 10, 14, 26}, {0.22, 38, 44, 120}, {0.45, 20, 170, 190}, {0.62, 249, 115, 22}, {0.78, 245, 181, 36}, {0.90, 255, 214, 128}, {1.00, 255, 246, 232}};
    for (int i = 0; i < 256; ++i) { const double t = i / 255.0; int s = 0; while (s + 1 < 6 && t > stops[s + 1].t) ++s; const S& a = stops[s]; const S& b = stops[s + 1];
      const double k = std::clamp((t - a.t) / (b.t - a.t), 0.0, 1.0);
      lut_[i] = Rgba8{static_cast<std::uint8_t>(std::lround(a.r + (b.r - a.r) * k)), static_cast<std::uint8_t>(std::lround(a.g + (b.g - a.g) * k)), static_cast<std::uint8_t>(std::lround(a.b + (b.b - a.b) * k)), 255}; }
  }
  Rgba8 at(double t) const { if (!(t > 0)) return lut_[0]; return lut_[std::min(255, static_cast<int>(t * 255.0 + 0.5))]; }
  static double luminance(Rgba8 c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
 private:
  Rgba8 lut_[256];
};

// THE VIEW: the spectrum history as a block in space: x = log frequency (-width / 2 .. width / 2), y = level (0 .. height), z = age (newest at the
// front). Turned by yaw about the vertical, tilted by pitch (0 = seen from the front, 90 = from above: a spectrogram), a little perspective,
// then scaled and centred so the whole block fits the rectangle inside the margin, whatever the angle.
struct View { double yawDeg = -28, pitchDeg = 32, height = 0.42, depth = 1.0, perspective = 0.30, width = 1.5; };
struct P2 { double x = 0, y = 0; };
class Projector {
 public:
  void set(const View& v, double w, double h, double margin) {
    v_ = v; const double ya = v.yawDeg * kPi / 180, pa = std::clamp(v.pitchDeg, -10.0, 90.0) * kPi / 180; cy_ = std::cos(ya); sy_ = std::sin(ya); cp_ = std::cos(pa); sp_ = std::sin(pa);
    persp_ = v.perspective * cp_; scale_ = 1; ox_ = oy_ = 0;
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (int c = 0; c < 8; ++c) { const P2 p = map(c & 1, (c >> 1) & 1, (c >> 2) & 1); x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    const double bw = std::max(1e-9, x1 - x0), bh = std::max(1e-9, y1 - y0), aw = std::max(1.0, w - 2 * margin), ah = std::max(1.0, h - 2 * margin);
    scale_ = std::min(aw / bw, ah / bh); ox_ = w / 2 - (x0 + x1) / 2 * scale_; oy_ = h / 2 - (y0 + y1) / 2 * scale_;
  }
  // u: 0 .. 1 along log frequency; level: 0 .. 1; age: 0 (newest, front) .. 1 (oldest, back). Screen pixels, y down.
  P2 map(double u, double level, double age) const {
    const double X = (u - 0.5) * v_.width, Y = level * v_.height, Z = (age - 0.5) * v_.depth;
    const double x1 = X * cy_ + Z * sy_, z1 = -X * sy_ + Z * cy_;           // yaw
    const double y2 = Y * cp_ + z1 * sp_, z2 = -Y * sp_ + z1 * cp_;         // pitch: from above, depth turns into height on the screen
    const double s = 1.0 / std::max(0.35, 1.0 + persp_ * z2);
    return P2{ox_ + x1 * s * scale_, oy_ - y2 * s * scale_};
  }
  bool olderIsFarther() const { return cy_ * cp_ >= 0; }   // draw order: the far rows first
  double scale() const { return scale_; }
 private:
  static constexpr double kPi = 3.14159265358979323846;
  View v_; double cy_ = 1, sy_ = 0, cp_ = 1, sp_ = 0, persp_ = 0, scale_ = 1, ox_ = 0, oy_ = 0;
};

// dB to 0 .. 1 in the shown range
inline double norm(double db, double lo, double hi) { return std::clamp((db - lo) / std::max(1e-9, hi - lo), 0.0, 1.0); }

}  // namespace am::measure::wf
