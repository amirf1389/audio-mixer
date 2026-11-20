// THE PHONE'S SHARED MIXING STEP (app/sdk/amix_mixstep.hpp), the one the AAudio and the OpenSL ES callbacks both run: an input on strip 1
// and a deck on strips 2/3 must reach the master, the multitrack ring gets every strip, outputs past 2 stay silent, an oversized block is
// answered with silence, and no input + no deck gives silence.
#include "amix.h"
#include "sdk/amix_mixstep.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace amix_decks;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static double peakOf(const std::vector<float>& v, int ch, int outCh) { double p = 0; for (std::size_t i = static_cast<std::size_t>(ch); i < v.size(); i += static_cast<std::size_t>(outCh)) p = std::max(p, static_cast<double>(std::fabs(v[i]))); return p; }

int main() {
  const int strips = 6, frames = 192, outCh = 4; const double rate = 48000;
  amix_mixer* m = amix_mixer_create(strips, rate); if (!m) { std::printf("FAIL create\n"); return 1; }
  for (int s = 0; s < strips; ++s) { amix_mixer_set_gain(m, s, 0); amix_mixer_set_mute(m, s, 0); amix_mixer_set_pan(m, s, 0); }
  amix_mixer_set_master(m, 0);
  Side sd; MixStep step; step.prepare(m, &sd, 256, 2);
  std::vector<float> out(static_cast<std::size_t>(frames) * outCh), in(static_cast<std::size_t>(frames)), chunk(2 * static_cast<std::size_t>(frames));

  // nothing in: silence
  for (int b = 0; b < 20; ++b) CHECK(step.run(nullptr, 0, 0, frames, out.data(), outCh) == frames);
  CHECK(peakOf(out, 0, outCh) < 1e-6);

  // the input alone (mono on strip 1)
  long n = 0; double inPk = 0;
  for (int b = 0; b < 60; ++b) { for (int i = 0; i < frames; ++i, ++n) in[static_cast<std::size_t>(i)] = 0.3f * static_cast<float>(std::sin(2 * M_PI * 440 * n / rate));
    step.run(in.data(), 1, frames, frames, out.data(), outCh); if (b > 20) inPk = std::max(inPk, peakOf(out, 0, outCh)); }
  CHECK(inPk > 0.05);

  // a deck on strips 2/3 and the input together; multitrack armed
  sd.deck[0].stripL = 2; sd.multiOn = true; double deckPk = 0, r2 = 0, r3 = 0;
  for (int b = 0; b < 60; ++b) {
    for (int i = 0; i < frames; ++i, ++n) { const float v = 0.4f * static_cast<float>(std::sin(2 * M_PI * 1000 * n / rate)); chunk[2 * static_cast<std::size_t>(i)] = v; chunk[2 * static_cast<std::size_t>(i) + 1] = v; in[static_cast<std::size_t>(i)] = 0.f; }
    deckPushFrames(sd.deck[0], chunk.data(), frames, rate, rate);
    CHECK(step.run(in.data(), 1, frames, frames, out.data(), outCh) == frames);
    if (b > 20) { deckPk = std::max(deckPk, std::max(peakOf(out, 0, outCh), peakOf(out, 1, outCh))); r2 = std::max(r2, peakOf(out, 2, outCh)); r3 = std::max(r3, peakOf(out, 3, outCh)); } }
  CHECK(deckPk > 0.05); CHECK(r2 == 0 && r3 == 0);
  CHECK(sd.deck[0].played.load() > 0);
  std::vector<float> mt(static_cast<std::size_t>(frames) * strips); const std::size_t got = sd.multi.pop(mt.data(), strips, static_cast<std::size_t>(frames));
  CHECK(got == static_cast<std::size_t>(frames));
  double d2 = 0; for (std::size_t i = 0; i < got; ++i) d2 = std::max(d2, static_cast<double>(std::fabs(mt[i * strips + 2]))); CHECK(d2 > 0.1);   // the deck's own strip in the multitrack

  // a block larger than prepared: silence, never a write past the buffers
  std::vector<float> big(512 * 2, 1.f); CHECK(step.run(nullptr, 0, 0, 512, big.data(), 2) == 0); CHECK(peakOf(big, 0, 2) == 0);

  amix_levels lv; amix_mixer_levels(m, &lv); CHECK(lv.master[0].peak > 0.0);   // the meters moved
  amix_mixer_destroy(m);
  std::printf("mixstep: input %.3f deck %.3f %s\n", inPk, deckPk, fails ? "FAILED" : "ok");
  return fails ? 1 : 0;
}
