// WATERFALL: the log-frequency axis round-trips and puts octaves at equal steps; a full-scale sine folded into bands reads ~0 dB in its
// own band and far less elsewhere, also at the low end where bands are narrower than a bin; the history ring keeps the newest row at age 0,
// wraps, and never reallocates; the colour map is ordered from dark to bright and clamps; the view fits the whole block centred inside the
// rectangle at every preset angle and draws far rows first.
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#include "measure/measure.hpp"
#include "measure/waterfall.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am::measure::wf;

static std::vector<float> powerOfSine(double f, double amp, std::size_t n, double fs) {   // Hann window, the scaling the console uses
  std::vector<std::complex<double>> x(n);
  for (std::size_t i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos(2 * M_PI * static_cast<double>(i) / static_cast<double>(n)); x[i] = amp * std::sin(2 * M_PI * f * static_cast<double>(i) / fs) * w; }
  am::measure::fftInPlace(x.data(), n);
  std::vector<float> p(n / 2 + 1); for (std::size_t k = 0; k <= n / 2; ++k) { const double a = std::abs(x[k]) * 4.0 / static_cast<double>(n); p[k] = static_cast<float>(a * a); }
  return p;
}

int main() {
  // 1. the axis
  { LogAxis ax; CHECK(std::fabs(ax.u(20)) < 1e-12 && std::fabs(ax.u(20000) - 1) < 1e-12); CHECK(std::fabs(ax.freq(ax.u(1234.5)) - 1234.5) < 1e-6);
    CHECK(std::fabs((ax.u(200) - ax.u(100)) - (ax.u(2000) - ax.u(1000))) < 1e-12);   // an octave is the same width everywhere
    CHECK(ax.u(5) == 0 && ax.u(30000) == 1); }                                         // clamped
  // 2. bands from the shared FFT: 1 kHz and 40 Hz full-scale sines
  { const double fs = 48000; const std::size_t n = 4096; Bands b; b.prepare(128, n, fs); CHECK(b.size() == 128);
    std::vector<float> db(128);
    for (double f : {1000.0, 40.0, 9000.0}) {
      const std::vector<float> p = powerOfSine(f, 1.0, n, fs); b.fold(p.data(), db.data());
      int at = 0; for (int i = 0; i < 128; ++i) if (std::fabs(std::log(b.centre(i) / f)) < std::fabs(std::log(b.centre(at) / f))) at = i;
      CHECK(db[static_cast<std::size_t>(at)] > -3.0 && db[static_cast<std::size_t>(at)] < 1.0);
      float far = -1000; for (int i = 0; i < 128; ++i) if (std::fabs(std::log2(b.centre(i) / f)) > 2.5) far = std::max(far, db[static_cast<std::size_t>(i)]);
      CHECK(far < -50); }
    const std::vector<float> q = powerOfSine(1000, 0.1, n, fs); b.fold(q.data(), db.data()); float top = -1000; for (float v : db) top = std::max(top, v); CHECK(std::fabs(top + 20) < 3); }   // -20 dBFS reads -20
  // 3. the history ring
  { RowRing r; r.configure(4, 3); const float* mem = r.storage(); CHECK(r.count() == 0 && r.row(0)[0] == -160.f);
    for (int k = 1; k <= 6; ++k) { float* w = r.push(); for (int i = 0; i < 3; ++i) w[i] = static_cast<float>(k); }
    CHECK(r.count() == 4 && r.row(0)[0] == 6 && r.row(1)[2] == 5 && r.row(3)[1] == 3 && r.row(9)[0] == 3);   // newest at age 0, ages clamp to the oldest
    CHECK(r.storage() == mem);   // no reallocation while running
    r.clear(); CHECK(r.count() == 0 && r.row(0)[0] == -160.f); }
  // 4. the colour map: dark to bright, clamped
  { ColorMap c; double prev = -1; for (int i = 0; i <= 20; ++i) { const double l = ColorMap::luminance(c.at(i / 20.0)); CHECK(l >= prev - 12); prev = l; }   // brighter as it goes (a hue turn may dip slightly)
    CHECK(ColorMap::luminance(c.at(1.0)) > 200 && ColorMap::luminance(c.at(0.0)) < 20);
    CHECK(c.at(-3).r == c.at(0).r && c.at(7).g == c.at(1).g && c.at(std::nan("")).b == c.at(0).b);
    CHECK(norm(-60, -90, -30) == 0.5 && norm(-200, -90, 0) == 0 && norm(10, -90, 0) == 1); }
  // 5. the view: every corner of the block inside the rectangle, the block centred, at each preset and when turned round
  { const double w = 800, h = 450, m = 24;
    for (const View& v : {View{-28, 32}, View{0, 90}, View{0, 0}, View{60, 10}, View{170, 45}, View{-120, 70}}) {
      Projector p; p.set(v, w, h, m); double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
      for (int c = 0; c < 8; ++c) { const P2 q = p.map(c & 1, (c >> 1) & 1, (c >> 2) & 1); x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); y0 = std::min(y0, q.y); y1 = std::max(y1, q.y); }
      CHECK(x0 >= m - 0.5 && x1 <= w - m + 0.5 && y0 >= m - 0.5 && y1 <= h - m + 0.5);
      CHECK(std::fabs((x0 + x1) / 2 - w / 2) < 0.5 && std::fabs((y0 + y1) / 2 - h / 2) < 0.5);
      CHECK(std::fabs(x1 - x0 - (w - 2 * m)) < 0.5 || std::fabs(y1 - y0 - (h - 2 * m)) < 0.5); }   // and as large as fits
    Projector top; top.set(View{0, 90}, w, h, m); CHECK(top.map(0.5, 0, 0).y > top.map(0.5, 0, 1).y);       // from above: the newest row at the bottom
    CHECK(std::fabs(top.map(0.5, 0, 0.5).y - top.map(0.5, 1, 0.5).y) < 1e-6);                                 // and the level does not move it
    Projector front; front.set(View{0, 0}, w, h, m); CHECK(front.map(0.5, 1, 0).y < front.map(0.5, 0, 0).y); // from the front: louder is higher
    CHECK(front.map(0, 0, 0).x < front.map(1, 0, 0).x);                                                       // low frequencies on the left
    Projector p3; p3.set(View{-28, 32}, w, h, m); CHECK(p3.olderIsFarther()); Projector back; back.set(View{170, 30}, w, h, m); CHECK(!back.olderIsFarther()); }
  std::printf("waterfall: axis, bands, ring, colours and view OK\n");
  return 0;
}
