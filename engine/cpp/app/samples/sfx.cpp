#include "sfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace am::sfx {
namespace {
constexpr double kPi = 3.14159265358979323846;
struct Rng { std::uint32_t s; explicit Rng(std::uint32_t seed) : s(seed ? seed : 1) {} float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return static_cast<float>(s) / 2147483648.f - 1.f; } };
// a fixed 0..1 value per (variation, axis): the variations spread over each axis without lining up
double axis(int v, int k) { std::uint32_t h = static_cast<std::uint32_t>(v * 2654435761u) ^ static_cast<std::uint32_t>(k * 40503u + 0x9e3779b9u); h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15; return (h & 0xffffff) / 16777215.0; }
double lerp(double a, double b, double t) { return a + (b - a) * t; }
double fade(double t, double len, double in, double out) { if (t < in) return t / in; if (t > len - out) return std::max(0.0, (len - t) / out); return 1.0; }

struct Family { const char* name; double minSec, maxSec; };
const Family kFam[kFamilies] = {
  {"Whoosh", 0.4, 1.6}, {"Riser", 1.5, 6.0}, {"Downlifter", 1.0, 4.0}, {"Impact", 0.8, 3.0}, {"Boom", 1.0, 4.0},
  {"Laser", 0.12, 0.8}, {"Zap", 0.1, 0.6}, {"Click", 0.03, 0.15}, {"Notify", 0.25, 1.2}, {"Alarm", 1.0, 3.0},
  {"Siren", 1.5, 4.0}, {"Glitch", 0.2, 1.2}, {"Bubble", 0.08, 0.5}, {"Coin", 0.15, 0.6}, {"Power Up", 0.4, 1.5},
  {"Power Down", 0.4, 1.5}, {"Wind", 2.0, 6.0}, {"Rain", 2.0, 6.0}, {"Thunder", 2.0, 6.0}, {"Bell", 1.0, 5.0},
};
double seconds(int fam, int v) { const Family& f = kFam[fam]; return std::round(lerp(f.minSec, f.maxSec, axis(v, 1)) * 100) / 100; }
}  // namespace

const std::vector<std::string>& families() { static const std::vector<std::string> l = [] { std::vector<std::string> v; for (const Family& f : kFam) v.push_back(f.name); return v; }(); return l; }

Info info(int id) {
  if (id < 1 || id > kCount) return {0, {}, {}, 0};
  const int fam = (id - 1) / kPerFamily, v = (id - 1) % kPerFamily; char b[64]; std::snprintf(b, sizeof b, "%s %02d", kFam[fam].name, v + 1);
  return {id, b, kFam[fam].name, seconds(fam, v)};
}

bool render(int id, double fs, std::vector<float>& L, std::vector<float>& R) {
  if (id < 1 || id > kCount || fs <= 0) return false;
  const int fam = (id - 1) / kPerFamily, var = (id - 1) % kPerFamily;
  const double len = seconds(fam, var), pitch = axis(var, 2), bright = axis(var, 3), motion = axis(var, 4), width = 0.15 + 0.7 * axis(var, 5);
  const std::size_t n = static_cast<std::size_t>(len * fs); L.assign(n, 0.f); R.assign(n, 0.f);
  Rng rng(static_cast<std::uint32_t>(id * 2246822519u + 7)), rngR(static_cast<std::uint32_t>(id * 3266489917u + 11));
  double ph = 0, ph2 = 0, lp = 0, lp2 = 0, lpR = 0, lp2R = 0, hold = 0; float sh = 0;
  const double nyq = 0.45 * fs;
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / fs, u = t / len;   // u: 0..1 through the sound
    double v = 0, vr = 0; bool stereo = false;
    const auto noise2 = [&](double fc) {   // two decorrelated 2-pole low-passed noises (left / right)
      const double a = std::min(0.99, 2 * kPi * std::min(fc, nyq) / fs); const float w = rng.next(), wr = rngR.next();
      lp += (w - lp) * a; lp2 += (lp - lp2) * a; lpR += (wr - lpR) * a; lp2R += (lpR - lp2R) * a; stereo = true; };
    switch (fam) {
      case 0: { const double fc = lerp(300, 2500 + 5000 * bright, std::sin(kPi * u)); noise2(fc); const double e = std::pow(std::sin(kPi * std::pow(u, 0.6 + motion)), 2);
        const double pan = 0.5 + 0.5 * std::sin(kPi * (u - 0.5)) * width; v = (lp - lp2 * 0.6) * 2.4 * e * (1 - pan) * 2; vr = (lpR - lp2R * 0.6) * 2.4 * e * pan * 2; break; }   // whoosh: band noise passing by
      case 1: { const double f0 = lerp(80, 300, pitch), f1 = f0 * lerp(6, 24, bright); const double f = f0 * std::pow(f1 / f0, std::pow(u, 1.5)); ph += 2 * kPi * f / fs; ph2 += 2 * kPi * f * 1.007 / fs;
        noise2(lerp(500, 9000, u)); const double e = std::pow(u, 1.3) * fade(t, len, 0.02, 0.03); v = (0.45 * std::sin(ph) + 0.35 * (lp - lp2) * motion * 2) * e; vr = (0.45 * std::sin(ph2) + 0.35 * (lpR - lp2R) * motion * 2) * e; break; }   // riser
      case 2: { const double f0 = lerp(800, 3000, pitch), f1 = lerp(30, 90, bright); const double f = f0 * std::pow(f1 / f0, std::pow(u, 0.7)); ph += 2 * kPi * f / fs; noise2(lerp(8000, 200, u));
        const double e = std::pow(1 - u, 1.2) * fade(t, len, 0.01, 0.05); v = (0.5 * std::sin(ph) + 0.3 * (lp - lp2) * (0.5 + motion)) * e; vr = (0.5 * std::sin(ph + 0.4) + 0.3 * (lpR - lp2R) * (0.5 + motion)) * e; break; }   // downlifter
      case 3: { const double f = lerp(35, 90, pitch) * (1 + 3 * std::exp(-t / 0.05)); ph += 2 * kPi * f / fs; noise2(lerp(1500, 9000, bright) * std::exp(-t / 0.3) + 200);
        const double body = std::sin(ph) * std::exp(-t / (len * 0.3)), crack = (lp - lp2 * 0.3) * 2 * std::exp(-t / lerp(0.03, 0.2, motion)), tail = lp2 * std::exp(-t / (len * 0.45)); v = body + crack + tail * 0.8; vr = body + (lpR - lp2R * 0.3) * 2 * std::exp(-t / lerp(0.03, 0.2, motion)) + lp2R * std::exp(-t / (len * 0.45)) * 0.8; break; }   // impact
      case 4: { const double f = lerp(28, 60, pitch) * (1 + lerp(1, 4, bright) * std::exp(-t / 0.08)); ph += 2 * kPi * f / fs; const double s = std::tanh(1.5 * std::sin(ph)) * std::exp(-t / (len * lerp(0.2, 0.5, motion)));
        v = vr = s * fade(t, len, 0.002, 0.05); break; }   // boom: a pitched-down sub with a little drive
      case 5: { const double f = lerp(600, 3500, pitch) * std::pow(lerp(0.05, 0.4, bright), u); ph += 2 * kPi * f / fs; const double sq = std::sin(ph) + 0.33 * std::sin(3 * ph) + motion * 0.2 * std::sin(5 * ph);
        v = vr = 0.5 * sq * std::exp(-t / (len * 0.6)) * fade(t, len, 0.002, 0.02); break; }   // laser: fast falling square-ish
      case 6: { const double fc = lerp(200, 1200, pitch), fm = fc * lerp(0.5, 3.5, bright); ph2 += 2 * kPi * fm / fs; ph += 2 * kPi * fc * (1 + lerp(1, 8, motion) * std::exp(-t / (len * 0.3)) * std::sin(ph2)) / fs;
        v = vr = 0.55 * std::sin(ph) * std::exp(-t / (len * 0.35)) * fade(t, len, 0.001, 0.01); break; }   // zap: FM burst
      case 7: { const double f = lerp(1500, 6000, pitch); const double e = std::exp(-t / lerp(0.003, 0.02, bright)); noise2(lerp(3000, 12000, motion)); v = vr = (0.6 * std::sin(2 * kPi * f * t) + 0.5 * (lp - lp2)) * e * fade(t, len, 0.0005, 0.005); break; }   // UI click
      case 8: { static const int iv[6] = {4, 5, 7, 12, 3, 9}; const double f1 = lerp(500, 1400, pitch), f2 = f1 * std::pow(2.0, iv[var % 6] / 12.0); const double split = lerp(0.25, 0.5, motion); const double f = u < split ? f1 : f2; const double tt = u < split ? t : t - split * len;
        v = vr = 0.5 * (std::sin(2 * kPi * f * tt) + bright * 0.3 * std::sin(2 * kPi * 2 * f * tt)) * std::exp(-tt / (len * 0.3)) * fade(t, len, 0.003, 0.02); break; }   // notify: two-tone chime
      case 9: { const double rate = lerp(2, 9, motion); const bool on = std::fmod(t * rate, 1.0) < 0.5; const double f = lerp(600, 2000, pitch); const double sq = std::fmod(f * t, 1.0) < 0.5 ? 1.0 : -1.0; lp += (sq - lp) * lerp(0.05, 0.6, bright);
        v = vr = (on ? 0.35 * lp : 0.0) * fade(t, len, 0.005, 0.02); break; }   // alarm: pulsed square
      case 10: { const double lo = lerp(400, 800, pitch), hi = lo * lerp(1.3, 2.0, bright); const double f = lo + (hi - lo) * (0.5 + 0.5 * std::sin(2 * kPi * lerp(0.3, 2.5, motion) * t)); ph += 2 * kPi * f / fs;
        v = 0.45 * std::sin(ph) * fade(t, len, 0.1, 0.3); vr = v * (1 - 0.3 * width * std::sin(2 * kPi * 0.2 * t)); stereo = true; break; }   // siren
      case 11: { const double chunk = lerp(0.01, 0.06, motion); if (std::fmod(t, chunk) < 1.0 / fs || i == 0) { hold = lerp(100, 3000, (rng.next() + 1) * 0.5 * (0.4 + pitch)); sh = rng.next(); }
        ph += 2 * kPi * hold / fs; const double crush = std::round(std::sin(ph) * lerp(2, 16, bright)) / lerp(2, 16, bright); v = (sh > -0.3 ? crush * 0.45 : 0.0) * fade(t, len, 0.002, 0.02); vr = sh > 0 ? v : -v * 0.8; stereo = true; break; }   // glitch: stutter + bit crush
      case 12: { const double f = lerp(300, 900, pitch) * (1 + lerp(1, 4, bright) * u); ph += 2 * kPi * f / fs; v = vr = 0.5 * std::sin(ph) * std::sin(kPi * u) * (1 - 0.3 * motion * u); break; }   // bubble: a rising sine blip
      case 13: { const double f1 = lerp(700, 1600, pitch), f2 = f1 * (bright > 0.5 ? 4.0 / 3.0 : 3.0 / 2.0); const double f = u < lerp(0.15, 0.35, motion) ? f1 : f2; const double sq = std::fmod(f * t, 1.0) < 0.5 ? 1.0 : -1.0;
        v = vr = 0.28 * sq * std::exp(-t / (len * 0.5)) * fade(t, len, 0.001, 0.01); break; }   // coin: two-note square
      case 14: case 15: { const int steps = 4 + static_cast<int>(motion * 8); const int k = std::min(steps - 1, static_cast<int>(u * steps)); const int semi = fam == 14 ? k * (bright > 0.5 ? 4 : 3) : (steps - 1 - k) * (bright > 0.5 ? 4 : 3);
        const double f = lerp(200, 600, pitch) * std::pow(2.0, semi / 12.0); ph += 2 * kPi * f / fs; const double tri = 2 / kPi * std::asin(std::sin(ph)); const double local = std::fmod(u * steps, 1.0);
        v = vr = 0.4 * tri * (1 - 0.6 * local) * fade(t, len, 0.003, 0.03); break; }   // power up / down: an arpeggio
      case 16: { noise2(lerp(250, 900, pitch) * (1 + 0.6 * std::sin(2 * kPi * lerp(0.05, 0.3, motion) * t))); const double g = 0.5 + 0.3 * std::sin(2 * kPi * 0.13 * t + var) + 0.2 * std::sin(2 * kPi * 0.37 * t);
        v = lp2 * g * 3 * fade(t, len, 0.6, 0.6); vr = lp2R * g * 3 * fade(t, len, 0.6, 0.6); break; }   // wind
      case 17: { noise2(lerp(2000, 7000, bright)); const double drops = (rng.next() > 1.0 - lerp(0.0005, 0.004, motion)) ? 1.0 : 0.0; hold = hold * 0.995 + drops; ph += 2 * kPi * lerp(1500, 4000, pitch) / fs;
        v = ((lp - lp2) * 1.6 + 0.3 * hold * std::sin(ph)) * fade(t, len, 0.4, 0.5); vr = ((lpR - lp2R) * 1.6 + 0.3 * hold * std::sin(ph + 1)) * fade(t, len, 0.4, 0.5); break; }   // rain: hiss + droplets
      case 18: { noise2(lerp(80, 250, pitch) + 2000 * std::exp(-t / 0.08) * bright); const double rumble = std::exp(-t / (len * lerp(0.25, 0.5, motion))) * (1 + 0.5 * std::sin(2 * kPi * 0.8 * t));
        v = lp2 * 6 * rumble + (lp - lp2) * 2 * std::exp(-t / 0.1); vr = lp2R * 6 * rumble + (lpR - lp2R) * 2 * std::exp(-t / 0.1); v *= fade(t, len, 0.002, 0.3); vr *= fade(t, len, 0.002, 0.3); break; }   // thunder: crack + rumble
      case 19: { static const double part[5] = {1.0, 2.76, 5.40, 8.93, 13.34}; const double f0 = lerp(180, 900, pitch); double s = 0, sr = 0;
        for (int k = 0; k < 5; ++k) { const double a = std::exp(-t / (len * (0.5 - 0.08 * k) * (0.6 + 0.4 * motion))) * std::pow(lerp(0.3, 0.8, bright), k); s += a * std::sin(2 * kPi * f0 * part[k] * t); sr += a * std::sin(2 * kPi * f0 * part[k] * 1.002 * t); }
        v = 0.35 * s * fade(t, len, 0.001, 0.1); vr = 0.35 * sr * fade(t, len, 0.001, 0.1); stereo = true; break; }   // bell: inharmonic partials
      default: break;
    }
    if (!stereo) vr = v;
    L[i] = static_cast<float>(std::max(-0.98, std::min(0.98, v))); R[i] = static_cast<float>(std::max(-0.98, std::min(0.98, vr)));
  }
  // every effect peaks near -3 dBFS so the pads sit at the same level whatever the recipe
  float pk = 0; for (std::size_t i = 0; i < n; ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
  if (pk > 1e-5f) { const float g = 0.708f / pk; for (std::size_t i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; } }
  return true;
}

}  // namespace am::sfx
