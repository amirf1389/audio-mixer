// THE STANDALONE PHONE MIXER'S DECKS AND RECORDER (app/sdk/amix_decks.hpp, used by the Android app's native audio loop): a song at
// 44.1 kHz resampled to 48 kHz and read in audio-callback blocks keeps its pitch, length and level; pause (unrouted) keeps the place;
// a flush drops what is buffered; the master recorder hands back exactly what the mixer made.
#include "sdk/amix_decks.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace amix_decks;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// frequency of a block by zero crossings
static double freqOf(const std::vector<float>& v, double rate) { int z = 0; for (std::size_t i = 1; i < v.size(); ++i) if ((v[i - 1] < 0) != (v[i] < 0)) ++z; return z / 2.0 / (v.size() / rate); }

int main() {
  Side sd; Deck& a = sd.deck[0]; a.stripL = 2;
  const double src = 44100, dst = 48000, f = 1000; const int secs = 1;
  // feed one second of a 1 kHz sine (L) and a 500 Hz sine (R) at 44.1 kHz, in decoder-sized chunks, while the "callback" reads 256-frame blocks
  std::vector<float> chunk(2 * 1152); std::vector<float> outL, outR; int fed = 0; const int total = static_cast<int>(src * secs);
  float L[256], R[256], L2[256], R2[256]; float* bl[2] = {L, L2}; float* br[2] = {R, R2}; bool tailed = false;
  while (fed < total || deckPushFrames(a, nullptr, 0, src, dst) > 0) {
    if (fed < total && deckPushFrames(a, nullptr, 0, src, dst) < 20000) {
      const int n = std::min(1152, total - fed);
      for (int i = 0; i < n; ++i) { chunk[2 * i] = 0.5f * static_cast<float>(std::sin(2 * M_PI * f * (fed + i) / src)); chunk[2 * i + 1] = 0.25f * static_cast<float>(std::sin(2 * M_PI * 500 * (fed + i) / src)); }
      deckPushFrames(a, chunk.data(), n, src, dst); fed += n; }
    if (fed >= total && !tailed) { std::vector<float> z(2 * 4096, 0.f); deckPushFrames(a, z.data(), 4096, src, dst); tailed = true; }   // end of the song: the resampler's tail out (as DeckPlayer does)
    const float* ptrs[6] = {}; readDecks(&sd, ptrs, 6, 256, bl, br);
    CHECK(ptrs[2] == L && ptrs[3] == R);
    if (fed >= total && a.ring.readable() == 0 && a.pending.empty()) break;
    outL.insert(outL.end(), L, L + 256); outR.insert(outR.end(), R, R + 256);
  }
  const double played = static_cast<double>(a.played.load());
  // the whole song comes out at 48 kHz (plus the silence that pushed the resampler's tail out)
  CHECK(played >= dst * secs - 64); CHECK(played < dst * secs + 4096 * dst / src + 512);
  { double tailPeak = 0; for (std::size_t i = static_cast<std::size_t>(dst * secs) - 960; i < static_cast<std::size_t>(dst * secs) - 480 && i < outL.size(); ++i) tailPeak = std::max(tailPeak, static_cast<double>(std::fabs(outL[i]))); CHECK(tailPeak > 0.45); }   // the last 10-20 ms of the song are there
  std::vector<float> mid(outL.begin() + 4800, outL.begin() + 4800 + 24000), midR(outR.begin() + 4800, outR.begin() + 4800 + 24000);
  double peak = 0, peakR = 0; for (float x : mid) peak = std::max(peak, static_cast<double>(std::fabs(x))); for (float x : midR) peakR = std::max(peakR, static_cast<double>(std::fabs(x)));
  const double fl = freqOf(mid, dst), fr = freqOf(midR, dst);
  CHECK(std::fabs(fl - 1000) < 5); CHECK(std::fabs(fr - 500) < 5); CHECK(std::fabs(peak - 0.5) < 0.01); CHECK(std::fabs(peakR - 0.25) < 0.01);
  std::printf("decks_test: 1 s at 44.1 kHz -> %.0f frames at 48 kHz; left %.1f Hz peak %.3f, right %.1f Hz peak %.3f\n", played, fl, peak, fr, peakR);

  // PAUSE: unrouted, the reader leaves the deck alone (its place is kept); routed again it carries on
  for (int i = 0; i < 4800; ++i) { chunk[2 * (i % 1152)] = 0.1f; chunk[2 * (i % 1152) + 1] = 0.1f; if (i % 1152 == 1151) deckPushFrames(a, chunk.data(), 1152, dst, dst); }
  const std::size_t before = a.ring.readable(); a.stripL = -1;
  { const float* ptrs[6] = {}; readDecks(&sd, ptrs, 6, 256, bl, br); CHECK(ptrs[2] == nullptr); }
  CHECK(a.ring.readable() == before);
  a.stripL = 2; { const float* ptrs[6] = {}; readDecks(&sd, ptrs, 6, 256, bl, br); CHECK(a.ring.readable() == before - 256); }
  // FLUSH with nobody reading: at once
  sd.lastRead.store(0); deckFlushNow(sd, 0); CHECK(a.ring.readable() == 0 && a.pending.empty());
  // a deck routed past the last strip is ignored (no write out of range)
  a.stripL = 5; deckPushFrames(a, chunk.data(), 256, dst, dst); { const float* ptrs[6] = {}; readDecks(&sd, ptrs, 6, 256, bl, br); for (const float* p : ptrs) CHECK(p == nullptr); }

  // MASTER RECORDER: armed, every frame comes back in order; full, the overflow is counted, never written past the end
  std::vector<float> mL(1000), mR(1000); for (int i = 0; i < 1000; ++i) { mL[i] = i / 1000.f; mR[i] = -i / 1000.f; }
  recordMaster(&sd, mL.data(), mR.data(), 1000); CHECK(sd.rec.readable() == 0);   // not armed: nothing
  sd.recording = true; recordMaster(&sd, mL.data(), mR.data(), 1000);
  std::vector<float> back(2000); CHECK(sd.rec.popInter(back.data(), 1000) == 1000);
  bool same = true; for (int i = 0; i < 1000; ++i) same = same && back[2 * i] == mL[i] && back[2 * i + 1] == mR[i]; CHECK(same);
  std::vector<float> big(300000, 0.f); recordMaster(&sd, big.data(), big.data(), 300000); CHECK(sd.rec.readable() == sd.rec.cap); CHECK(sd.recDropped.load() == 300000 - sd.rec.cap);
  { // MULTITRACK ring: every strip's own input comes out in order, interleaved; nothing while it is off; a full ring counts the loss
    Side ms; std::vector<float> a1(64, 0.25f), b1(64, -0.5f); const float* p[3] = {a1.data(), nullptr, b1.data()};
    recordStrips(&ms, p, 3, 64); CHECK(ms.multi.readable() == 0);
    ms.multiOn = true; recordStrips(&ms, p, 3, 64);
    std::vector<float> o(3 * 64); CHECK(ms.multi.pop(o.data(), 3, 64) == 64);
    CHECK(o[0] == 0.25f && o[1] == 0.f && o[2] == -0.5f && o[3 * 63 + 2] == -0.5f);
    for (int k = 0; k < 2100; ++k) recordStrips(&ms, p, 3, 64);
    CHECK(ms.multiDropped.load() > 0); }
  std::printf(fails ? "decks_test: %d FAILED\n" : "decks_test: all passed\n", fails);
  return fails ? 1 : 0;
}
