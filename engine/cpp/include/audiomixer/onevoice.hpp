// ONE VOICE: many Windows microphones (and USB headsets) deliver one mono capsule on BOTH input channels. With the factory layout (CH 1 reads
// input 1, CH 2 reads input 2) that voice is then heard twice: 6 dB louder than it should be, and the room's feedback comes that much sooner.
// The runtime measures how alike inputs 1 and 2 are (Snapshot::in12*, half-second windows); this rule decides, from three windows in a row
// (1.5 s) of real sound that is the same on both (correlation over 0.995, levels within 0.5 dB), that one strip is enough. A stereo source
// (two microphones, a keyboard, music) is never the same on both sides for that long, so it is left alone.
#pragma once
#include <cmath>
#include <cstdint>

#include "audiomixer/runtime.hpp"

namespace am {

struct OneVoice {
  std::uint64_t seen = 0; int hits = 0;
  static bool same(double corr, double diffDb, double levelDb) { return levelDb > -55.0 && corr > 0.995 && std::fabs(diffDb) < 0.5; }
  /** call with each snapshot: true once the same voice has been on inputs 1 and 2 for three windows in a row */
  bool update(const Snapshot& s) {
    if (s.in12Windows == seen) return false;
    seen = s.in12Windows; hits = same(s.in12Corr, s.in12DiffDb, s.in12LevelDb) ? hits + 1 : 0;
    return hits >= 3;
  }
  void reset() { hits = 0; }
};

}  // namespace am
