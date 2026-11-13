// SOUND SCORE: how a mix stands, 0 .. 100, from what the meters already measure. Six parts, each with what to do about it:
//   LOUDNESS   20  short-term loudness against the target (full within 1 LU, 4 points off per LU beyond)
//   PEAKS      20  true peak at or under -1 dBTP; clipping takes the part to 0
//   DYNAMICS   15  crest factor (peak - RMS) 8 .. 14 dB; a squashed mix (or channels flagged OVER-COMPRESSED) loses points
//   TONE       20  the 1/3-octave spectrum against a smooth, gently falling line (-1 .. -5 dB per octave); bumps and holes lose points
//   STEREO     15  correlation 0.3 .. 1 full; towards 0 thin, below 0 out of phase (mono listeners lose parts of the mix)
//   STABILITY  10  no dropouts (xruns) and no clipping channels
// Silence (below -60 LUFS) gives no score: there is nothing to judge.
#pragma once
#include <string>
#include <vector>

namespace am::measure {

struct ScoreInput {
  double shortTerm = -120, target = -14, truePeak = -120, peakDb = -120, rmsDb = -120, correlation = 1;
  std::vector<double> spectrum;   // 1/3-octave band levels in dB, 20 Hz .. 20 kHz (31 bands)
  int clipStrips = 0, overCompStrips = 0, xruns = 0; bool masterClip = false;
};
struct ScorePart { std::string name; double points = 0, max = 0; std::string tip; };
struct Score { bool valid = false; double total = 0; std::string grade; std::vector<ScorePart> parts; double tiltDbPerOct = 0, roughnessDb = 0; };

Score soundScore(const ScoreInput& in);

}  // namespace am::measure
