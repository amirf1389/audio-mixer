// The I/O FRAME objects: the echo canceller takes a room echo out of a microphone, the hybrid passes the caller and band-limits them,
// the logic object drives a logic output from a signal and mutes a channel from a control input.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "audiomixer/frame.hpp"
#include "fx/fx.hpp"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); ++fails; } } while (0)

int main() {
  const double fs = 48000; const std::size_t B = 256; std::mt19937 rng(7); std::normal_distribution<float> nd(0.f, 0.1f);
  { // AEC: the speakers play noise (written to the reference tap, as the mixer does), the mic hears it 30 ms later through a small room
    am::fx::EchoCanceller aec(fs); aec.set("bulk", 20.0 / 250); aec.set("tail", (40.0 - 16) / 69); aec.set("suppress", 0); aec.set("mix", 1);
    std::vector<float> spk; const std::size_t delay = static_cast<std::size_t>(0.030 * fs);
    double inE = 0, outE = 0; std::vector<float> L(B), R(B), zero(B, 0.f);
    for (int blk = 0; blk < static_cast<int>(6 * fs / B); ++blk) {
      for (std::size_t i = 0; i < B; ++i) {   // mic = room echo of what the speakers played: 0.5 x (30 ms) + 0.25 x (37 ms) + 0.1 x (45 ms)
        const std::size_t t = spk.size() + i; auto s = [&](std::size_t d) { return t >= d && t - d < spk.size() ? spk[t - d] : 0.f; };
        L[i] = R[i] = 0.5f * s(delay) + 0.25f * s(delay + 336) + 0.1f * s(delay + 720);
      }
      std::vector<float> in(L);
      aec.process(L.data(), R.data(), B);
      if (blk > static_cast<int>(4 * fs / B)) for (std::size_t i = 0; i < B; ++i) { inE += in[i] * in[i]; outE += L[i] * L[i]; }   // the last 2 s: converged
      std::vector<float> out(B); for (auto& v : out) v = nd(rng); spk.insert(spk.end(), out.begin(), out.end());
      am::frame::refTap().write(out.data(), out.data(), B);   // this block's speaker signal, as the mixer writes it after the master
    }
    const double erle = 10 * std::log10(inE / std::max(1e-12, outE));
    CHECK(erle > 20, "AEC took out only %.1f dB of the echo (want > 20)", erle);
    std::printf("AEC: %.1f dB of echo removed (ERLE reported %.1f dB)\n", erle, aec.erleDb());
    // a near-end talker over silence on the speakers passes untouched
    std::vector<float> l2(B), r2(B), silent(B, 0.f); double nE = 0, oE = 0;
    for (int blk = 0; blk < 200; ++blk) { am::frame::refTap().write(silent.data(), silent.data(), B); for (std::size_t i = 0; i < B; ++i) l2[i] = r2[i] = std::sin(2 * 3.14159 * 300 * (blk * B + i) / fs) * 0.3f;
      std::vector<float> in(l2); aec.process(l2.data(), r2.data(), B); if (blk > 100) for (std::size_t i = 0; i < B; ++i) { nE += in[i] * in[i]; oE += l2[i] * l2[i]; } }
    CHECK(std::fabs(10 * std::log10(oE / nE)) < 1.0, "AEC changed a near-end talker by %.1f dB", 10 * std::log10(oE / nE));
  }
  { // HYBRID: a 1 kHz caller passes (about unity with AGC off, LEVEL 0 dB), a 6 kHz tone is cut in NARROW
    am::fx::PhoneHybrid h(fs); h.set("agc", 0); h.set("level", 0.5); std::vector<float> silent(B, 0.f);
    auto through = [&](double hz) { h.reset(); double iE = 0, oE = 0; std::vector<float> L(B), R(B);
      for (int blk = 0; blk < 200; ++blk) { am::frame::refTap().write(silent.data(), silent.data(), B); for (std::size_t i = 0; i < B; ++i) L[i] = R[i] = 0.3f * std::sin(2 * 3.14159 * hz * (blk * B + i) / fs);
        std::vector<float> in(L); h.process(L.data(), R.data(), B); if (blk > 100) for (std::size_t i = 0; i < B; ++i) { iE += in[i] * in[i]; oE += L[i] * L[i]; } }
      return 10 * std::log10(oE / iE); };
    const double at1k = through(1000), at6k = through(6000);
    CHECK(at1k > -4 && at1k < 1, "hybrid 1 kHz %.1f dB (want about 0)", at1k); CHECK(at6k < at1k - 6, "hybrid NARROW did not cut 6 kHz (%.1f dB vs %.1f)", at6k, at1k);
    std::printf("HYBRID: 1 kHz %.1f dB, 6 kHz %.1f dB\n", at1k, at6k);
  }
  { // LOGIC: SIGNAL PRESENT lights OUT 3; MUTE ON INPUT mutes the channel while IN 5 is closed
    auto& lg = am::frame::logic(); am::fx::LogicObject lo(fs); lo.set("mode", 0); lo.set("output", 2.0 / 6 + 0.01); lo.set("threshold", 0.5); lo.set("hold", 0);
    std::vector<float> L(B, 0.5f), R(B, 0.5f); lo.process(L.data(), R.data(), B); CHECK(lg.out[2].load(), "logic output not lit by a signal");
    std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f); for (int k = 0; k < 20; ++k) lo.process(L.data(), R.data(), B); CHECK(!lg.out[2].load(), "logic output stays lit in silence");
    am::fx::LogicObject m(fs); m.set("mode", 0.3); m.set("input", 4.0 / 12 + 0.01); lg.in[4] = true;
    for (int k = 0; k < 10; ++k) { std::fill(L.begin(), L.end(), 0.5f); m.process(L.data(), R.data(), B); }
    CHECK(std::fabs(L[B - 1]) < 1e-3, "MUTE ON INPUT did not mute (%.3f)", L[B - 1]); lg.in[4] = false;
    for (int k = 0; k < 10; ++k) { std::fill(L.begin(), L.end(), 0.5f); m.process(L.data(), R.data(), B); }
    CHECK(std::fabs(L[B - 1] - 0.5f) < 1e-3, "MUTE ON INPUT did not open again");
  }
  std::printf(fails ? "frame_test: %d failed\n" : "frame_test: all passed\n", fails); return fails ? 1 : 0;
}
