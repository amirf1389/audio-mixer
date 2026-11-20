// THE PHONE MIXER'S DECKS ARE HEARD (layout 2: CH 1-6 on strips 0-5, DECK A-D on 6-7, 8-9, 10-11, 12-13): a 14-strip mixer set up the
// way LocalMixerActivity does (gains, pans, mutes, the channel EQ "isoeq" in insert slot 4 = Engine.EQ_SLOT), a deck routed on strips
// 6/7 fed a sine through the deck ring and read the way the native loop does; the master must carry the deck.
#include "amix.h"
#include "sdk/amix_decks.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace amix_decks;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static double run(bool eq, int deck) {
  const int strips = 14, frames = 256; const double rate = 48000;
  amix_mixer* m = amix_mixer_create(strips, rate); if (!m) { std::printf("FAIL create\n"); ++fails; return 0; }
  for (int s = 0; s < strips; ++s) { amix_mixer_set_gain(m, s, 0); amix_mixer_set_mute(m, s, 0); }
  for (int s = 0; s < 6; ++s) amix_mixer_set_pan(m, s, 0);
  const int sl = 6 + 2 * deck; amix_mixer_set_pan(m, sl, -1); amix_mixer_set_pan(m, sl + 1, 1);
  amix_mixer_set_master(m, 0);
  if (eq) for (int t : {sl, sl + 1}) {   // ChannelEq on a deck: q 0, colour 0, output 0.5, ten bands at 0 dB (knob 0.5) .. +3 dB
    CHECK(amix_mixer_insert(m, t, 4, "isoeq") == 0);
    amix_mixer_insert_set(m, t, 4, "q", 0.0); amix_mixer_insert_set(m, t, 4, "colour", 0.0); amix_mixer_insert_set(m, t, 4, "output", 0.5);
    const char* ids[10] = {"b31", "b63", "b125", "b250", "b500", "b1k", "b2k", "b4k", "b8k", "b16k"};
    for (int i = 0; i < 10; ++i) amix_mixer_insert_set(m, t, 4, ids[i], 0.5 + 3.0 / 24); }
  Side sd; sd.deck[deck].stripL = sl;
  std::vector<float> chunk(2 * frames), L(frames), R(frames), dk(2 * kDecks * frames);
  float* bl[kDecks]; float* br[kDecks]; for (int d = 0; d < kDecks; ++d) { bl[d] = dk.data() + d * frames; br[d] = dk.data() + (kDecks + d) * frames; }
  std::vector<float> mic(frames, 0.f); double peak = 0; long n = 0;
  for (int b = 0; b < 200; ++b) {
    for (int i = 0; i < frames; ++i, ++n) { const float v = 0.5f * static_cast<float>(std::sin(2 * M_PI * 1000 * n / rate)); chunk[2 * i] = v; chunk[2 * i + 1] = v; }
    deckPushFrames(sd.deck[deck], chunk.data(), frames, rate, rate);
    std::vector<const float*> ptrs(strips, nullptr); ptrs[0] = mic.data();   // CH 1: the microphone (silent), the rest as the native loop leaves them
    readDecks(&sd, ptrs.data(), strips, frames, bl, br);
    CHECK(ptrs[sl] == bl[deck] && ptrs[sl + 1] == br[deck]);
    const int done = amix_mixer_process(m, ptrs.data(), frames, L.data(), R.data());
    if (b > 50) for (int i = 0; i < done; ++i) peak = std::max(peak, static_cast<double>(std::max(std::fabs(L[i]), std::fabs(R[i])))); }
  amix_mixer_destroy(m); return peak;
}

int main() {
  for (int d = 0; d < kDecks; ++d) for (bool eq : {false, true}) {
    const double p = run(eq, d); std::printf("phone_decks_test: deck %c%s on strips %d-%d -> master peak %.3f\n", 'A' + d, eq ? " + isoeq slot 4" : "", 6 + 2 * d, 7 + 2 * d, p);
    CHECK(p > 0.1); }
  std::printf(fails ? "phone_decks_test: %d FAILED\n" : "phone_decks_test: ok\n", fails); return fails ? 1 : 0;
}
