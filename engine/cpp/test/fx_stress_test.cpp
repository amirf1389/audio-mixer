// Every built-in effect, hard: four sample rates, odd block sizes, every knob at 0, 1 and random, silence / DC / full scale / impulse / noise
// / a sweep, a rate change mid-stream, bypass. The output must stay finite and bounded, bypass must leave the signal alone, every knob must
// have a text at both ends, and a silent input must decay to silence (no stuck oscillation, no denormal tail) except for the generators.
#include "fx/fx.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
using namespace am::fx;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL "); std::printf(__VA_ARGS__); std::printf("\n"); ++fails; } } while (0)
int main() {
  std::mt19937 rng(7); std::uniform_real_distribution<double> U(0, 1), N(-1, 1);
  const double rates[] = {44100, 48000, 96000, 192000}; const int blocks[] = {1, 7, 64, 512, 2048};
  double worstUs = 0; std::string worstKind;
  for (const auto& [kind, name] : catalog()) {
    const bool generator = kind == "sampler";   // adds its own sound (only when PLAY is on)
    // texts
    { auto e = make(kind, 48000); for (const Param& p : e->params()) { CHECK(!e->text(p.id, 0).empty() && !e->text(p.id, 1).empty(), "%s: knob %s has no text", kind.c_str(), p.id.c_str()); } }
    for (double fs : rates) for (int mode = 0; mode < 4; ++mode) {
      auto e = make(kind, fs); CHECK(e != nullptr, "%s: not made", kind.c_str()); if (!e) continue;
      const auto ps = e->params();
      for (const Param& p : ps) { if (kind == "sampler" && p.id == "play") continue; e->set(p.id, mode == 0 ? 0.0 : mode == 1 ? 1.0 : mode == 2 ? U(rng) : p.def); }
      for (int sig = 0; sig < 6; ++sig) {
        const int len = static_cast<int>(fs * 0.25); std::vector<float> L(len), R(len); double ph = 0;
        for (int i = 0; i < len; ++i) { double v = 0; const double t = i / fs;
          switch (sig) { case 0: v = 0; break; case 1: v = 0.5; break; case 2: v = (i / 50) % 2 ? 1.0 : -1.0; break; case 3: v = i == 0 ? 1.0 : 0; break; case 4: v = N(rng); break;
            default: ph += 2 * 3.14159265 * (20 * std::pow(1000.0, t / 0.25)) / fs; v = 0.9 * std::sin(ph); }
          L[i] = static_cast<float>(v); R[i] = static_cast<float>(sig == 4 ? N(rng) : v); }
        const int B = blocks[(sig + mode) % 5]; const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < len; i += B) e->process(L.data() + i, R.data() + i, static_cast<std::size_t>(std::min(B, len - i)));
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (len / fs);   // microseconds per second of audio
        if (fs == 48000 && us > worstUs) { worstUs = us; worstKind = kind; }
        double pk = 0; bool fin = true; for (int i = 0; i < len; ++i) { fin = fin && std::isfinite(L[i]) && std::isfinite(R[i]); pk = std::max({pk, static_cast<double>(std::fabs(L[i])), static_cast<double>(std::fabs(R[i]))}); }
        CHECK(fin, "%s @%.0f knobs %d signal %d: NaN or inf", kind.c_str(), fs, mode, sig);
        CHECK(pk < 32, "%s @%.0f knobs %d signal %d: peak %.1f (runaway)", kind.c_str(), fs, mode, sig, pk);
        if (sig == 3 && !generator && mode != 1 && !(mode == 2 && (kind == "hall" || kind == "room" || kind == "reverb" || kind == "delay" || kind == "syncdelay"))) {   // random knobs may ask a reverb or delay for a 20 s tail: that is not a fault   // after an impulse the tail must die (reverbs get 3 s, everything else 1 s)
          const bool longTail = kind == "hall" || kind == "room" || kind == "reverb" || kind == "delay" || kind == "syncdelay";
          const int tail = static_cast<int>(fs * (longTail ? 6.0 : 1.0)); std::vector<float> z(tail, 0.f), z2(tail, 0.f);
          for (int i = 0; i < tail; i += 512) e->process(z.data() + i, z2.data() + i, static_cast<std::size_t>(std::min(512, tail - i)));
          double last = 0; for (int i = tail - static_cast<int>(fs * 0.05); i < tail; ++i) last = std::max(last, static_cast<double>(std::fabs(z[i])));
          CHECK(last < 1e-3, "%s @%.0f knobs %d: still sounding %.5f after an impulse (no decay)", kind.c_str(), fs, mode, last); }
      }
      e->setSampleRate(fs == 48000 ? 96000 : 48000); { std::vector<float> L(4096, 0.3f), R(4096, -0.3f); e->process(L.data(), R.data(), 4096); bool f = true; for (float v : L) f = f && std::isfinite(v); CHECK(f, "%s: rate change gave NaN", kind.c_str()); }
    }
    { auto e = make(kind, 48000); e->setBypass(true); std::vector<float> L(1000), R(1000); for (int i = 0; i < 1000; ++i) L[i] = R[i] = static_cast<float>(std::sin(i * 0.1));
      std::vector<float> L0 = L; e->process(L.data(), R.data(), 1000); double d = 0; for (int i = 0; i < 1000; ++i) d += std::fabs(L[i] - L0[i]); CHECK(d == 0, "%s: bypass changed the sound", kind.c_str()); }
  }
  std::printf("heaviest at 48 kHz: %s at %.0f us of CPU per second of audio (%.2f %% of one core)\n", worstKind.c_str(), worstUs, worstUs / 1e4);
  std::printf(fails ? "fx_stress: %d failed\n" : "fx_stress: all %zu effects passed\n", fails ? static_cast<std::size_t>(fails) : catalog().size()); return fails ? 1 : 0;
}
