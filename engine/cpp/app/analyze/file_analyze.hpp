// FILE ANALYZE + AI SETUP: an audio file (WAV, FLAC, AIFF built in; MP3, M4A / AAC, OGG through the decoders arr::readAudio already uses)
// measured offline, and a setup suggested for it by fixed, explainable rules (no network, no learned model):
//   integrated loudness (ITU-R BS.1770-4 K-weighting, 400 ms blocks, -70 LUFS absolute and -10 LU relative gates), loudness range (EBU
//   Tech 3342: 3 s windows, -20 LU gate, 10th..95th percentile), true peak (4x oversampled), sample peak, RMS, crest factor, clipped runs,
//   octave-band balance (31 Hz .. 16 kHz) against a pink reference (equal energy per octave).
// suggest(): gain to the target (-14 LUFS streaming, -23 broadcast, or a custom value), a corrective EQ (the monitor's Auto EQ, as the
// 10 graphic-EQ bands of a strip), a compressor and a limiter, each with the reason it was chosen and the predicted numbers after.
#pragma once
#include <string>
#include <vector>

#include "measure/measure.hpp"

namespace am::fa {

constexpr int kBands = 10;   // band k centred on 31.25 * 2^k Hz (31 .. 16 kHz), the console's graphic EQ
struct ChannelStats { double peakDb = -120, truePeakDb = -120, rmsDb = -120; };
struct Analysis {
  bool ok = false; std::string error, path;
  double rate = 0, seconds = 0;
  double lufs = -120, lra = 0, truePeakDb = -120, peakDb = -120, rmsDb = -120, crestDb = 0;
  int clippedRuns = 0;
  ChannelStats ch[2];
  double bandDb[kBands] = {};      // band level, dBFS (mean square in the band)
  double balanceDb[kBands] = {};   // the band against the pink reference (0 = on the reference)
};
// L, R at `rate` (mono: pass the same buffer twice and set mono so it counts once in the loudness)
Analysis analyse(const std::vector<float>& L, const std::vector<float>& R, double rate, bool mono = false);
Analysis analyseFile(const std::string& path);

struct Setup {
  double targetLufs = -14, ceilingDb = -1;
  double gainDb = 0;                                  // trim
  bool eqOn = false; double eqDb[kBands] = {}; std::vector<am::measure::Filter> filters;
  bool compOn = false; double compThr = -20, compRatio = 1, compKnee = 6, compAttack = 10, compRelease = 120;
  bool limOn = true; double limCeiling = -1, limRelease = 80;
  double afterLufs = -120, afterTruePeakDb = -120, afterLra = 0;   // predicted
  std::vector<std::string> reasons;                   // one line per decision, in plain words
};
// target: "stream" (-14), "broadcast" (-23), or "custom" with customLufs
Setup suggest(const Analysis& a, const std::string& target, double customLufs = -16);

double kWeightedLufs(const std::vector<float>& L, const std::vector<float>& R, double rate, bool mono = false);   // integrated only
double truePeak(const std::vector<float>& x);         // linear, 4x oversampled

}  // namespace am::fa
