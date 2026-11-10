#include "presets.hpp"

#include <cstdio>

namespace am::fx {
namespace {
std::vector<Preset> build() {
  std::vector<Preset> out; int id = 0;
  auto add = [&](const std::string& name, const std::string& kind, const std::string& cat, std::vector<std::pair<std::string, double>> v, double mix) { out.push_back({++id, name, kind, cat, std::move(v), mix}); };
  // ---- REVERB: 110 (spaces x sizes x tones)
  struct Space { const char* name; double decayBase; double damp; double width; double pre; };
  const Space spaces[] = {{"Booth", 0.08, 0.7, 0.4, 0.0}, {"Vocal Room", 0.18, 0.55, 0.7, 0.05}, {"Studio", 0.24, 0.5, 0.8, 0.08}, {"Drum Room", 0.22, 0.4, 0.9, 0.03}, {"Chamber", 0.32, 0.5, 0.85, 0.1}, {"Plate", 0.36, 0.3, 1.0, 0.0}, {"Bright Plate", 0.34, 0.15, 1.0, 0.0}, {"Dark Plate", 0.38, 0.75, 0.9, 0.0}, {"Hall", 0.5, 0.45, 1.0, 0.15}, {"Concert Hall", 0.58, 0.4, 1.0, 0.2}, {"Cathedral", 0.78, 0.5, 1.0, 0.25}, {"Arena", 0.7, 0.35, 1.0, 0.3}, {"Spring", 0.26, 0.2, 0.6, 0.0}, {"Ambience", 0.12, 0.6, 1.0, 0.0}};
  const char* sizes[] = {"Small", "Medium", "Large"}; const double sizeK[] = {0.75, 1.0, 1.3};
  for (const Space& sp : spaces) for (int sz = 0; sz < 3; ++sz) {
    const double decay = std::min(1.0, sp.decayBase * sizeK[sz]);
    add(std::string(sizes[sz]) + " " + sp.name, "reverb", "Reverb", {{"decay", decay}, {"damp", sp.damp}, {"pre", sp.pre}, {"width", sp.width}}, 1.0);
  }
  const char* vox[] = {"Vocal Air", "Lead Vocal", "Backing Vocals", "Speech Warmth", "Podcast Space", "Snare Plate", "Kick Room", "Overhead Hall", "Piano Hall", "Acoustic Guitar", "Strings Wash", "Synth Pad Space", "Brass Room", "Choir Cathedral", "Film Trailer", "Gated 80s", "Reverse Feel", "Lo-Fi Tape Room", "Tight Ambience", "Infinite Wash"};
  const double voxP[][4] = {{0.22, 0.25, 0.1, 1.0}, {0.3, 0.45, 0.12, 0.9}, {0.4, 0.5, 0.08, 1.0}, {0.14, 0.7, 0.04, 0.6}, {0.1, 0.65, 0.0, 0.5}, {0.33, 0.3, 0.0, 1.0}, {0.16, 0.5, 0.0, 0.8}, {0.45, 0.4, 0.1, 1.0}, {0.5, 0.45, 0.15, 1.0}, {0.28, 0.5, 0.06, 0.9}, {0.7, 0.55, 0.2, 1.0}, {0.85, 0.6, 0.25, 1.0}, {0.3, 0.4, 0.05, 0.9}, {0.8, 0.5, 0.3, 1.0}, {0.9, 0.45, 0.35, 1.0}, {0.2, 0.2, 0.0, 1.0}, {0.6, 0.8, 0.5, 1.0}, {0.25, 0.85, 0.0, 0.6}, {0.08, 0.5, 0.0, 1.0}, {1.0, 0.3, 0.2, 1.0}};
  for (int i = 0; i < 20; ++i) add(vox[i], "reverb", "Reverb", {{"decay", voxP[i][0]}, {"damp", voxP[i][1]}, {"pre", voxP[i][2]}, {"width", voxP[i][3]}}, 1.0);
  const char* mixes[] = {"Dry Touch", "Subtle", "Present", "Wet", "Drenched"}; const double mixV[] = {0.15, 0.3, 0.5, 0.75, 1.0};
  for (int i = 0; i < 5; ++i) { add(std::string("Hall ") + mixes[i], "reverb", "Reverb", {{"decay", 0.5}, {"damp", 0.45}, {"pre", 0.15}, {"width", 1.0}}, mixV[i]); add(std::string("Plate ") + mixes[i], "reverb", "Reverb", {{"decay", 0.36}, {"damp", 0.3}, {"pre", 0.0}, {"width", 1.0}}, mixV[i]); add(std::string("Room ") + mixes[i], "reverb", "Reverb", {{"decay", 0.2}, {"damp", 0.5}, {"pre", 0.05}, {"width", 0.8}}, mixV[i]); }
  for (int d = 1; d <= 13; ++d) { char b[48]; std::snprintf(b, sizeof b, "Decay %.1f s", 0.3 + (d / 13.0) * (d / 13.0) * 11.7); add(b, "reverb", "Reverb", {{"decay", d / 13.0}, {"damp", 0.5}, {"pre", 0.1}, {"width", 1.0}}, 1.0); }
  const char* tones[] = {"Bright", "Open", "Natural", "Warm", "Dark"}; const double toneD[] = {0.1, 0.3, 0.5, 0.7, 0.9};
  for (int i = 0; i < 5; ++i) { add(std::string("Hall ") + tones[i], "reverb", "Reverb", {{"decay", 0.5}, {"damp", toneD[i]}, {"pre", 0.15}, {"width", 1.0}}, 1.0); add(std::string("Plate ") + tones[i], "reverb", "Reverb", {{"decay", 0.36}, {"damp", toneD[i]}, {"pre", 0.0}, {"width", 1.0}}, 1.0); }
  const double pres[] = {0.0, 0.1, 0.2, 0.35, 0.5}; for (int i = 0; i < 5; ++i) { char b[48]; std::snprintf(b, sizeof b, "Hall Pre %d ms", static_cast<int>(pres[i] * 200)); add(b, "reverb", "Reverb", {{"decay", 0.5}, {"damp", 0.45}, {"pre", pres[i]}, {"width", 1.0}}, 1.0); std::snprintf(b, sizeof b, "Chamber Pre %d ms", static_cast<int>(pres[i] * 200)); add(b, "reverb", "Reverb", {{"decay", 0.32}, {"damp", 0.5}, {"pre", pres[i]}, {"width", 0.85}}, 1.0); }
  // ---- DELAY: 100 (tempo delays x feel, slaps, echoes, ping-pongs)
  const int bpms[] = {60, 70, 80, 90, 100, 110, 120, 128, 140, 150, 160, 174};
  struct Note { const char* name; double beats; }; const Note notes[] = {{"1/4", 1.0}, {"1/8", 0.5}, {"1/8 dotted", 0.75}, {"1/16", 0.25}};
  for (int bpm : bpms) for (const Note& n : notes) {
    const double ms = 60000.0 / bpm * n.beats; const double t = std::max(0.0, std::min(1.0, (ms - 20) / 1980.0)); if (ms > 2000) continue;
    char b[64]; std::snprintf(b, sizeof b, "%d BPM %s", bpm, n.name); add(b, "delay", "Delay", {{"time", t}, {"fb", 0.4}, {"tone", 0.5}, {"pingpong", n.beats < 1.0 ? 1.0 : 0.0}}, 0.35);
  }
  const char* slaps[] = {"Slap 60 ms", "Slap 80 ms", "Slap 100 ms", "Slap 120 ms", "Doubler 25 ms", "Thicken 40 ms", "Haas Wide 15 ms", "Rockabilly Slap"}; const double slapMs[] = {60, 80, 100, 120, 25, 40, 15, 110};
  for (int i = 0; i < 8; ++i) add(slaps[i], "delay", "Delay", {{"time", std::max(0.0, (slapMs[i] - 20) / 1980.0)}, {"fb", i == 7 ? 0.25 : 0.05}, {"tone", 0.6}, {"pingpong", i == 6 ? 1.0 : 0.0}}, 0.4);
  const char* echoes[] = {"Dub Echo", "Tape Echo Warm", "Space Echo", "Long Echo Dark", "Bright Echo", "Analog Chain", "Vocal Throw", "Guitar Lead Echo", "Dotted Dream", "Ping-Pong Wide", "Triplet Bounce", "Infinite Loop", "Clean Digital", "Lo-Fi Radio", "Ambient Trail", "Drum Slap Back", "Snare Tail", "Keys Shimmer", "Pad Smear", "Dialog Spacer"};
  const double echoP[][4] = {{0.19, 0.75, 0.3, 1}, {0.17, 0.55, 0.35, 0}, {0.21, 0.6, 0.4, 1}, {0.35, 0.65, 0.2, 0}, {0.15, 0.5, 0.9, 0}, {0.2, 0.5, 0.45, 0}, {0.24, 0.3, 0.5, 1}, {0.18, 0.45, 0.55, 0}, {0.26, 0.5, 0.5, 1}, {0.22, 0.5, 0.6, 1}, {0.14, 0.45, 0.5, 1}, {0.3, 0.95, 0.5, 1}, {0.2, 0.4, 1.0, 0}, {0.16, 0.6, 0.15, 0}, {0.4, 0.7, 0.3, 1}, {0.05, 0.1, 0.7, 0}, {0.08, 0.3, 0.5, 0}, {0.25, 0.5, 0.8, 1}, {0.45, 0.6, 0.3, 1}, {0.1, 0.2, 0.6, 0}};
  for (int i = 0; i < 20; ++i) add(echoes[i], "delay", "Delay", {{"time", echoP[i][0]}, {"fb", echoP[i][1]}, {"tone", echoP[i][2]}, {"pingpong", echoP[i][3]}}, 0.4);
  for (int k = 0; out.size() < 110 + 100; ++k) { const int pct = 4 + k * 2; char b[48]; std::snprintf(b, sizeof b, "Feedback %d %%", pct); add(b, "delay", "Delay", {{"time", 0.18}, {"fb", std::min(1.0, pct / 100.0)}, {"tone", 0.5}, {"pingpong", 0}}, 0.35); }
  // ---- CHORUS: 50
  const char* chorusN[] = {"Classic Ensemble", "Slow Swirl", "Fast Shimmer", "Subtle Width", "Deep Detune", "Juno Style", "Vocal Double", "Guitar Clean", "Bass Chorus", "Keys Sparkle", "Rotary Feel", "Vibrato Light", "Vibrato Deep", "Strings Ensemble", "80s Pop", "Dreamy Pad", "Mono-Safe Thin", "Ultra Wide", "Gentle Motion", "Seasick"};
  const double chorusP[][3] = {{0.2, 0.5, 0.5}, {0.05, 0.6, 0.6}, {0.6, 0.35, 0.5}, {0.15, 0.2, 0.7}, {0.1, 0.9, 0.5}, {0.18, 0.55, 0.5}, {0.12, 0.3, 0.3}, {0.25, 0.4, 0.5}, {0.08, 0.3, 0.2}, {0.3, 0.45, 0.8}, {0.5, 0.7, 1.0}, {0.4, 0.3, 0.0}, {0.45, 0.8, 0.0}, {0.1, 0.5, 0.9}, {0.22, 0.6, 0.6}, {0.04, 0.7, 1.0}, {0.2, 0.3, 0.0}, {0.15, 0.5, 1.0}, {0.07, 0.25, 0.5}, {0.9, 1.0, 0.5}};
  for (int i = 0; i < 20; ++i) add(chorusN[i], "chorus", "Chorus", {{"rate", chorusP[i][0]}, {"depth", chorusP[i][1]}, {"spread", chorusP[i][2]}}, 0.5);
  for (int r = 0; r < 10; ++r) { char b[48]; std::snprintf(b, sizeof b, "Rate %.2f Hz", 0.1 + (r / 9.0) * 4.9); add(b, "chorus", "Chorus", {{"rate", r / 9.0}, {"depth", 0.5}, {"spread", 0.5}}, 0.5); }
  for (int d = 0; d < 10; ++d) { char b[48]; std::snprintf(b, sizeof b, "Depth %d %%", d * 10 + 10); add(b, "chorus", "Chorus", {{"rate", 0.2}, {"depth", (d + 1) / 10.0}, {"spread", 0.5}}, 0.5); }
  for (int s = 0; s < 10; ++s) { char b[48]; std::snprintf(b, sizeof b, "Spread %d %%", s * 10 + 10); add(b, "chorus", "Chorus", {{"rate", 0.2}, {"depth", 0.5}, {"spread", (s + 1) / 10.0}}, 0.5); }
  // ---- DRIVE: 40
  const char* driveN[] = {"Warm Tube", "Tape Saturation", "Console Glue", "Bass Growl", "Vocal Grit", "Drum Crunch", "Guitar Crunch", "Fuzz", "Broken Speaker", "Radio Voice", "Lo-Fi Crush", "Subtle Harmonics", "Mastering Colour", "Snare Bite", "Kick Punch", "Synth Edge", "Overdriven Preamp", "Hot Mic", "Dark Drive", "Bright Drive"};
  const double driveP[][3] = {{0.2, 0.6, 0.7}, {0.3, 0.5, 0.65}, {0.12, 0.7, 0.75}, {0.4, 0.3, 0.6}, {0.45, 0.55, 0.6}, {0.5, 0.6, 0.6}, {0.6, 0.5, 0.55}, {0.9, 0.4, 0.45}, {0.85, 0.2, 0.5}, {0.5, 0.25, 0.6}, {0.8, 0.15, 0.5}, {0.08, 0.8, 0.8}, {0.1, 0.9, 0.78}, {0.55, 0.65, 0.6}, {0.35, 0.3, 0.65}, {0.65, 0.8, 0.55}, {0.7, 0.5, 0.5}, {0.4, 0.7, 0.6}, {0.5, 0.1, 0.6}, {0.5, 1.0, 0.6}};
  for (int i = 0; i < 20; ++i) add(driveN[i], "drive", "Drive", {{"drive", driveP[i][0]}, {"tone", driveP[i][1]}, {"out", driveP[i][2]}}, 1.0);
  for (int d = 0; d < 20; ++d) { char b[48]; std::snprintf(b, sizeof b, "Drive %.1f dB", (d + 1) / 20.0 * 36); add(b, "drive", "Drive", {{"drive", (d + 1) / 20.0}, {"tone", 0.6}, {"out", 0.7 - 0.2 * (d / 19.0)}}, 1.0); }
  return out;
}
}  // namespace
const std::vector<Preset>& presets() { static const std::vector<Preset> all = build(); return all; }
const Preset* preset(int id) { const auto& all = presets(); return id >= 1 && id <= static_cast<int>(all.size()) ? &all[static_cast<std::size_t>(id - 1)] : nullptr; }
}  // namespace am::fx
