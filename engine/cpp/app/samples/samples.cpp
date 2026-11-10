#include "samples.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>

#include "arrange/arrange.hpp"

namespace am::samples {
namespace {
constexpr double kPi = 3.14159265358979323846;
struct Rng { std::uint32_t s; explicit Rng(std::uint32_t seed) : s(seed ? seed : 1) {} float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return static_cast<float>(s) / 2147483648.f - 1.f; } };
double env(double t, double a, double d, double s, double r, double hold) { if (t < a) return t / a; if (t < a + d) return 1 - (1 - s) * (t - a) / d; if (t < hold) return s; return s * std::exp(-(t - hold) / r); }
double noteHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

struct Def { const char* name; const char* cat; double sec; int kind; double a, b, c; };   // kind decides the recipe; a / b / c tune it
// 100 definitions
const Def kDefs[] = {
  // drums (1..20)
  {"Kick Deep", "Drums", 0.7, 1, 50, 0.35, 0.6}, {"Kick Punch", "Drums", 0.5, 1, 60, 0.2, 0.8}, {"Kick 808 Long", "Drums", 1.6, 1, 45, 1.2, 0.5}, {"Kick Tight", "Drums", 0.3, 1, 70, 0.12, 0.9}, {"Kick Click", "Drums", 0.4, 1, 80, 0.15, 1.0},
  {"Snare Tight", "Drums", 0.35, 2, 200, 0.12, 0.6}, {"Snare Fat", "Drums", 0.5, 2, 170, 0.25, 0.5}, {"Snare Rim", "Drums", 0.2, 2, 420, 0.06, 0.9}, {"Snare Brush", "Drums", 0.6, 2, 150, 0.4, 0.2}, {"Snare Trap", "Drums", 0.4, 2, 240, 0.18, 0.8},
  {"Hat Closed", "Drums", 0.12, 3, 8000, 0.05, 0}, {"Hat Open", "Drums", 0.6, 3, 7000, 0.4, 0}, {"Hat Pedal", "Drums", 0.2, 3, 6000, 0.1, 0}, {"Ride", "Drums", 1.5, 3, 5000, 1.0, 1}, {"Crash", "Drums", 2.0, 3, 4000, 1.6, 1},
  {"Tom Low", "Drums", 0.8, 1, 90, 0.5, 0.3}, {"Tom Mid", "Drums", 0.6, 1, 130, 0.4, 0.3}, {"Tom High", "Drums", 0.5, 1, 180, 0.3, 0.3}, {"Clap", "Drums", 0.4, 4, 1500, 0.25, 0}, {"Snap", "Drums", 0.15, 4, 2500, 0.08, 0},
  // percussion (21..35)
  {"Conga Open", "Percussion", 0.5, 1, 200, 0.3, 0.1}, {"Conga Slap", "Percussion", 0.25, 1, 320, 0.1, 0.4}, {"Bongo High", "Percussion", 0.3, 1, 400, 0.15, 0.1}, {"Cowbell", "Percussion", 0.4, 5, 540, 0.3, 800}, {"Woodblock", "Percussion", 0.15, 5, 1200, 0.08, 1800},
  {"Shaker", "Percussion", 0.2, 3, 9000, 0.12, 0}, {"Tambourine", "Percussion", 0.4, 3, 6500, 0.3, 1}, {"Clave", "Percussion", 0.15, 5, 2500, 0.07, 3000}, {"Triangle", "Percussion", 1.2, 5, 4200, 1.0, 6300}, {"Cabasa", "Percussion", 0.3, 3, 8500, 0.2, 0},
  {"Djembe Bass", "Percussion", 0.6, 1, 110, 0.4, 0.2}, {"Djembe Tone", "Percussion", 0.35, 1, 260, 0.2, 0.2}, {"Tabla Na", "Percussion", 0.4, 5, 700, 0.3, 1400}, {"Rimshot Metal", "Percussion", 0.2, 5, 900, 0.1, 2200}, {"Finger Click", "Percussion", 0.1, 4, 3000, 0.05, 0},
  // bass notes (36..47): C1..B1
  {"Bass Sub C1", "Bass", 1.0, 6, 24, 0, 0}, {"Bass Sub D1", "Bass", 1.0, 6, 26, 0, 0}, {"Bass Sub E1", "Bass", 1.0, 6, 28, 0, 0}, {"Bass Sub F1", "Bass", 1.0, 6, 29, 0, 0}, {"Bass Sub G1", "Bass", 1.0, 6, 31, 0, 0}, {"Bass Sub A1", "Bass", 1.0, 6, 33, 0, 0},
  {"Bass Saw C2", "Bass", 1.0, 7, 36, 0, 0}, {"Bass Saw D2", "Bass", 1.0, 7, 38, 0, 0}, {"Bass Saw E2", "Bass", 1.0, 7, 40, 0, 0}, {"Bass Saw F2", "Bass", 1.0, 7, 41, 0, 0}, {"Bass Saw G2", "Bass", 1.0, 7, 43, 0, 0}, {"Bass Saw A2", "Bass", 1.0, 7, 45, 0, 0},
  // leads (48..59): C4..B4 square / pluck
  {"Lead Square C4", "Lead", 0.8, 8, 60, 0, 0}, {"Lead Square D4", "Lead", 0.8, 8, 62, 0, 0}, {"Lead Square E4", "Lead", 0.8, 8, 64, 0, 0}, {"Lead Square F4", "Lead", 0.8, 8, 65, 0, 0}, {"Lead Square G4", "Lead", 0.8, 8, 67, 0, 0}, {"Lead Square A4", "Lead", 0.8, 8, 69, 0, 0},
  {"Pluck C4", "Lead", 0.6, 9, 60, 0, 0}, {"Pluck E4", "Lead", 0.6, 9, 64, 0, 0}, {"Pluck G4", "Lead", 0.6, 9, 67, 0, 0}, {"Pluck C5", "Lead", 0.6, 9, 72, 0, 0}, {"Pluck E5", "Lead", 0.6, 9, 76, 0, 0}, {"Pluck G5", "Lead", 0.6, 9, 79, 0, 0},
  // pads and keys (60..71)
  {"Pad C Major", "Pad", 3.0, 10, 48, 4, 7}, {"Pad A Minor", "Pad", 3.0, 10, 45, 3, 7}, {"Pad F Major", "Pad", 3.0, 10, 41, 4, 7}, {"Pad G Major", "Pad", 3.0, 10, 43, 4, 7}, {"Pad D Minor", "Pad", 3.0, 10, 50, 3, 7}, {"Pad E Minor", "Pad", 3.0, 10, 52, 3, 7},
  {"Keys C3", "Keys", 1.5, 11, 48, 0, 0}, {"Keys E3", "Keys", 1.5, 11, 52, 0, 0}, {"Keys G3", "Keys", 1.5, 11, 55, 0, 0}, {"Keys C4", "Keys", 1.5, 11, 60, 0, 0}, {"Keys E4", "Keys", 1.5, 11, 64, 0, 0}, {"Keys G4", "Keys", 1.5, 11, 67, 0, 0},
  // fx and transitions (72..86)
  {"Riser 2 s", "FX", 2.0, 12, 200, 4000, 0}, {"Riser 4 s", "FX", 4.0, 12, 100, 6000, 0}, {"Downer", "FX", 2.0, 12, 4000, 100, 0}, {"Impact Low", "FX", 1.5, 13, 40, 0, 0}, {"Impact Big", "FX", 2.5, 13, 30, 0, 0},
  {"Noise Burst", "FX", 0.5, 14, 0.5, 0, 0}, {"Noise Sweep Up", "FX", 2.0, 15, 300, 8000, 0}, {"Noise Sweep Down", "FX", 2.0, 15, 8000, 300, 0}, {"Laser Zap", "FX", 0.3, 12, 3000, 200, 0}, {"Bleep", "FX", 0.15, 5, 1000, 0.1, 0},
  {"Siren", "FX", 2.0, 16, 600, 900, 0}, {"Wind", "FX", 3.0, 17, 400, 0, 0}, {"Rain", "FX", 3.0, 17, 3000, 0, 0}, {"Heartbeat", "FX", 1.2, 18, 55, 0, 0}, {"Reverse Cymbal", "FX", 1.5, 19, 4000, 0, 0},
  // calibration and test (87..100)
  {"Tone 100 Hz -18 dBFS", "Test", 2.0, 20, 100, -18, 0}, {"Tone 440 Hz -18 dBFS", "Test", 2.0, 20, 440, -18, 0}, {"Tone 1 kHz -18 dBFS", "Test", 2.0, 20, 1000, -18, 0}, {"Tone 1 kHz 0 dBFS", "Test", 2.0, 20, 1000, 0, 0}, {"Tone 10 kHz -18 dBFS", "Test", 2.0, 20, 10000, -18, 0},
  {"Pink Noise -20 dBFS", "Test", 3.0, 21, -20, 0, 0}, {"White Noise -20 dBFS", "Test", 3.0, 22, -20, 0, 0}, {"Sweep 20-20k", "Test", 5.0, 23, 20, 20000, 0}, {"Polarity Pulse", "Test", 1.0, 24, 0, 0, 0}, {"Click Track 120", "Test", 2.0, 25, 120, 0, 0},
  {"Left Only 1 kHz", "Test", 1.5, 26, 1000, -18, 0}, {"Right Only 1 kHz", "Test", 1.5, 27, 1000, -18, 0}, {"Count In 1-2-3-4", "Test", 2.0, 28, 120, 0, 0}, {"Silence 1 s", "Test", 1.0, 29, 0, 0, 0},
};
constexpr int kCount = sizeof(kDefs) / sizeof(kDefs[0]);
float lin(double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); }
}  // namespace

const std::vector<Info>& list() { static const std::vector<Info> l = [] { std::vector<Info> v; for (int i = 0; i < kCount; ++i) v.push_back({i + 1, kDefs[i].name, kDefs[i].cat, kDefs[i].sec}); return v; }(); return l; }
std::string fileName(int id) { if (id < 1 || id > kCount) return {}; char b[96]; std::snprintf(b, sizeof b, "%03d-%s.wav", id, kDefs[id - 1].name); return b; }

bool render(int id, double fs, std::vector<float>& L, std::vector<float>& R) {
  if (id < 1 || id > kCount || fs <= 0) return false;
  const Def& d = kDefs[id - 1]; const std::size_t n = static_cast<std::size_t>(d.sec * fs); L.assign(n, 0.f); R.assign(n, 0.f); Rng rng(static_cast<std::uint32_t>(id * 2654435761u));
  double ph = 0, lp = 0, lp2 = 0; float b0 = 0, b1 = 0, b2 = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / fs; double v = 0, vr = 0; bool stereo = false;
    switch (d.kind) {
      case 1: { const double f = d.a * (1 + 3 * std::exp(-t / 0.03)); ph += 2 * kPi * f / fs; v = std::sin(ph) * std::exp(-t / d.b) + d.c * 0.4 * rng.next() * std::exp(-t / 0.01); break; }                    // drum: pitched body + click
      case 2: { const double tone = std::sin(2 * kPi * d.a * t) * std::exp(-t / (d.b * 0.6)); const float w = rng.next(); lp += (w - lp) * 0.6; v = 0.5 * tone + (1 - d.c * 0.5) * lp * std::exp(-t / d.b); break; }   // snare
      case 3: { const float w = rng.next(); lp += (w - lp) * std::min(1.0, d.a / fs * 2); const double hp = w - lp; v = hp * std::exp(-t / d.b) * (d.c > 0 ? 0.8 + 0.2 * std::sin(2 * kPi * 70 * t) : 1.0); break; }   // cymbal / hat
      case 4: { const float w = rng.next(); lp += (w - lp) * std::min(1.0, d.a / fs * 4); const double bursts = (t < 0.03 || (t > 0.012 && t < 0.02) || (t > 0.024 && t < 0.03)) ? 1.0 : std::exp(-(t - 0.03) / d.b); v = lp * bursts; break; }   // clap
      case 5: { v = (std::sin(2 * kPi * d.a * t) + 0.6 * std::sin(2 * kPi * d.c * t)) * 0.6 * std::exp(-t / d.b); break; }                                  // metallic / wood (two partials)
      case 6: { const double f = noteHz(static_cast<int>(d.a)); v = (std::sin(2 * kPi * f * t) + 0.3 * std::sin(2 * kPi * 2 * f * t)) * env(t, 0.005, 0.1, 0.8, 0.15, d.sec - 0.2); break; }      // sub bass
      case 7: { const double f = noteHz(static_cast<int>(d.a)); double s = 0; for (int h = 1; h <= 12; ++h) s += std::sin(2 * kPi * f * h * t) / h; lp += (s * 0.5 - lp) * (0.02 + 0.3 * std::exp(-t / 0.2)); v = lp * env(t, 0.005, 0.15, 0.7, 0.12, d.sec - 0.2); break; }   // saw bass with filter decay
      case 8: { const double f = noteHz(static_cast<int>(d.a)); v = (std::fmod(f * t, 1.0) < 0.5 ? 0.5 : -0.5) * env(t, 0.01, 0.1, 0.6, 0.1, d.sec - 0.15); break; }   // square lead
      case 9: { const double f = noteHz(static_cast<int>(d.a)); double s = 0; for (int h = 1; h <= 6; ++h) s += std::sin(2 * kPi * f * h * t) / (h * h); v = s * std::exp(-t / 0.18); break; }   // pluck
      case 10: { const int root = static_cast<int>(d.a); const int iv[3] = {0, static_cast<int>(d.b), static_cast<int>(d.c)}; double s = 0, sr = 0; for (int k = 0; k < 3; ++k) { const double f = noteHz(root + iv[k]); s += std::sin(2 * kPi * f * t + 0.1 * std::sin(2 * kPi * 0.3 * t)); sr += std::sin(2 * kPi * f * 1.003 * t); } v = s / 3 * env(t, 0.6, 0.5, 0.8, 0.6, d.sec - 0.8); vr = sr / 3 * env(t, 0.6, 0.5, 0.8, 0.6, d.sec - 0.8); stereo = true; break; }   // pad
      case 11: { const double f = noteHz(static_cast<int>(d.a)); v = (std::sin(2 * kPi * f * t) + 0.5 * std::sin(2 * kPi * 2 * f * t) * std::exp(-t / 0.3) + 0.25 * std::sin(2 * kPi * 3 * f * t) * std::exp(-t / 0.15)) * std::exp(-t / 0.5); break; }   // e-piano
      case 12: { const double f = d.a * std::pow(d.b / d.a, t / d.sec); ph += 2 * kPi * f / fs; v = std::sin(ph) * env(t, 0.05, 0.1, 1.0, 0.1, d.sec - 0.1) * 0.7; break; }   // pitch ramp
      case 13: { const double f = d.a * (1 + 2 * std::exp(-t / 0.1)); ph += 2 * kPi * f / fs; const float w = rng.next(); lp += (w - lp) * 0.05; v = (std::sin(ph) + 0.5 * lp) * std::exp(-t / (d.sec * 0.35)); break; }   // impact
      case 14: { v = rng.next() * d.a * std::exp(-t / (d.sec * 0.3)); break; }
      case 15: { const double fc = d.a * std::pow(d.b / d.a, t / d.sec); const float w = rng.next(); lp += (w - lp) * std::min(0.99, 2 * kPi * fc / fs); lp2 += (lp - lp2) * std::min(0.99, 2 * kPi * fc / fs); v = (lp2 - lp * 0.5) * 2 * env(t, 0.1, 0.1, 1.0, 0.2, d.sec - 0.2); break; }   // filtered noise sweep
      case 16: { const double f = d.a + (d.b - d.a) * (0.5 + 0.5 * std::sin(2 * kPi * 0.7 * t)); ph += 2 * kPi * f / fs; v = std::sin(ph) * 0.6 * env(t, 0.05, 0.1, 1.0, 0.2, d.sec - 0.3); break; }   // siren
      case 17: { const float w = rng.next(); lp += (w - lp) * std::min(0.99, 2 * kPi * d.a / fs); const double g = 0.4 + 0.3 * std::sin(2 * kPi * 0.17 * t) + 0.2 * std::sin(2 * kPi * 0.41 * t); v = lp * g * 2 * env(t, 0.5, 0.1, 1.0, 0.5, d.sec - 0.6); break; }   // wind / rain
      case 18: { const double beat = std::fmod(t, 0.9); const double k1 = beat < 0.15 ? std::sin(2 * kPi * d.a * beat) * std::exp(-beat / 0.05) : 0, k2 = beat > 0.25 && beat < 0.4 ? std::sin(2 * kPi * d.a * 0.9 * (beat - 0.25)) * std::exp(-(beat - 0.25) / 0.05) : 0; v = (k1 + 0.7 * k2) * 0.9; break; }
      case 19: { const float w = rng.next(); lp += (w - lp) * std::min(0.99, 2 * kPi * d.a / fs); v = (w - lp) * std::pow(t / d.sec, 3.0) * (t < d.sec - 0.02 ? 1.0 : 0.0); break; }   // reverse cymbal
      case 20: { v = std::sin(2 * kPi * d.a * t) * lin(d.b) * (t < 0.01 ? t / 0.01 : t > d.sec - 0.01 ? (d.sec - t) / 0.01 : 1.0); break; }
      case 21: { const float w = rng.next(); b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f; v = (b0 + b1 + b2 + w * 0.1848f) * 0.25 * lin(d.a); break; }
      case 22: { v = rng.next() * 0.5 * lin(d.a); break; }
      case 23: { const double f = d.a * std::pow(d.b / d.a, t / d.sec); ph += 2 * kPi * f / fs; v = std::sin(ph) * lin(-12); break; }
      case 24: { v = t < 0.002 ? 1.0 - t / 0.002 : 0.0; break; }
      case 25: case 28: { const double period = 60.0 / d.a; const double beat = std::fmod(t, period); const int idx = static_cast<int>(t / period); v = beat < 0.03 ? std::sin(2 * kPi * (idx % 4 == 0 ? 1800 : (d.kind == 28 ? 1200 + 150 * (idx % 4) : 1000)) * beat) * std::exp(-beat / 0.01) : 0; break; }
      case 26: { v = std::sin(2 * kPi * d.a * t) * lin(d.b); vr = 0; stereo = true; break; }
      case 27: { v = 0; vr = std::sin(2 * kPi * d.a * t) * lin(d.b); stereo = true; break; }
      default: v = 0; break;
    }
    L[i] = static_cast<float>(std::max(-1.0, std::min(1.0, v))); R[i] = static_cast<float>(std::max(-1.0, std::min(1.0, stereo ? vr : v)));
  }
  return true;
}
bool writeAll(const std::string& dir, double fs, std::string& err) {
  std::error_code ec; std::filesystem::create_directories(dir, ec); std::vector<float> L, R;
  for (int id = 1; id <= kCount; ++id) { const std::string path = dir + "/" + fileName(id); if (std::filesystem::exists(path)) continue; if (!render(id, fs, L, R)) { err = "render failed"; return false; } if (!am::arr::writeWav24(path, L, R, fs, err)) return false; }
  return true;
}
}  // namespace am::samples
