// 100 built-in sound samples, synthesised (no recordings, nothing to license): drums, percussion, bass and lead notes, pads, risers,
// impacts, noises and calibration tones. The same names and sounds on every machine; written as 24-bit WAV on demand (SAMPLES page,
// ARRANGE, MEDIA) and rendered straight into memory for the pads.
#pragma once
#include <string>
#include <vector>

namespace am::samples {

struct Info { int id; std::string name, category; double seconds; };
const std::vector<Info>& list();                                            // 100 entries, ids 1..100
bool render(int id, double sampleRate, std::vector<float>& L, std::vector<float>& R);   // deterministic
bool writeAll(const std::string& dir, double sampleRate, std::string& err);  // <dir>/NNN-name.wav for every sample (skips files that exist)
std::string fileName(int id);                                               // "007-Snare Tight.wav"

}  // namespace am::samples
