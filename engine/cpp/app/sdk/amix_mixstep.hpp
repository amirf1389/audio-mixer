// THE PHONE MIXER'S ONE MIXING STEP, shared by every native audio path of the Android SDK (android/amix-sdk/src/main/cpp/amix_jni.cpp:
// the AAudio data callback and the OpenSL ES buffer-queue callback), so decks, inputs, multitrack and meters behave the same on both.
// One block: the interleaved input (input k -> strip k), the decks onto their strips (readDecks), the multitrack copy (recordStrips),
// amix_mixer_process, the master recorder, and the master L / R interleaved into the output (channels 3.. silent). Everything it uses is
// sized in prepare(); run() never allocates, never locks. Tested on the host by test/mixstep_test.cpp.
#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

#include "amix.h"
#include "sdk/amix_decks.hpp"

namespace amix_decks {
struct MixStep {
  amix_mixer* m = nullptr; Side* side = nullptr; std::size_t cap = 0; int maxIn = 1;
  std::vector<float> inBuf, L, R, dk; std::vector<const float*> ptrs;

  // capFrames: the largest block run() will take; maxIn: the most input channels it will be given. Call before the audio starts.
  void prepare(amix_mixer* mixer, Side* sd, std::size_t capFrames, int maxInputs) {
    m = mixer; side = sd; cap = capFrames; maxIn = std::max(1, maxInputs);
    inBuf.assign(static_cast<std::size_t>(maxIn) * cap, 0.f); L.assign(cap, 0.f); R.assign(cap, 0.f); dk.assign(2 * kDecks * cap, 0.f);
    ptrs.assign(static_cast<std::size_t>(m ? amix_mixer_strips(m) : 0), nullptr);
    if (m) { std::vector<float> warm(cap, 0.f); const float* none[AMIX_MAX_STRIPS] = {}; amix_mixer_process(m, none, static_cast<int>(cap), warm.data(), warm.data()); }   // the mixer's own buffers sized once, here
  }
  // in: `got` frames of `inCh` interleaved channels (nullptr / 0 = no input; frames past `got` are silence); out: `frames` x `outCh`
  // interleaved floats. Returns the frames the mixer produced; a block larger than the prepared size is answered with silence.
  int run(const float* in, int inCh, int got, int frames, float* out, int outCh) {
    const std::size_t oc = static_cast<std::size_t>(outCh);
    if (!m || frames <= 0 || static_cast<std::size_t>(frames) > cap) { if (out && frames > 0) std::fill(out, out + oc * static_cast<std::size_t>(frames), 0.f); return 0; }
    const int ic = in ? std::min(std::max(inCh, 0), maxIn) : 0; if (got < 0) got = 0; if (got > frames) got = frames;
    for (int c = 0; c < ic; ++c) { float* d = inBuf.data() + static_cast<std::size_t>(c) * cap;
      for (int i = 0; i < frames; ++i) d[i] = i < got ? in[static_cast<std::size_t>(i) * static_cast<std::size_t>(inCh) + static_cast<std::size_t>(c)] : 0.f; }
    const int n = static_cast<int>(ptrs.size());
    for (int s = 0; s < n; ++s) ptrs[static_cast<std::size_t>(s)] = s < ic ? inBuf.data() + static_cast<std::size_t>(s) * cap : nullptr;   // input k on strip k
    if (side) { float* bl[kDecks]; float* br[kDecks];
      for (int d = 0; d < kDecks; ++d) { bl[d] = dk.data() + static_cast<std::size_t>(d) * cap; br[d] = dk.data() + static_cast<std::size_t>(kDecks + d) * cap; }
      readDecks(side, ptrs.data(), n, frames, bl, br);   // the decks on their strips
      recordStrips(side, ptrs.data(), n, frames); }      // MULTITRACK: each strip's own input, before the mixer
    const int done = amix_mixer_process(m, ptrs.data(), frames, L.data(), R.data());
    if (side) recordMaster(side, L.data(), R.data(), done);
    for (int i = 0; i < frames; ++i) { float* f = out + static_cast<std::size_t>(i) * oc;
      f[0] = i < done ? L[static_cast<std::size_t>(i)] : 0.f; if (outCh > 1) f[1] = i < done ? R[static_cast<std::size_t>(i)] : 0.f; for (int c = 2; c < outCh; ++c) f[c] = 0.f; }
    return done;
  }
};
}  // namespace amix_decks
