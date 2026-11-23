#include "presets.hpp"

#include <algorithm>
#include <cmath>
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
  // ---- PING-PONG: 300 (ids 301 .. 600) for the PING-PONG DELAY: tempo bounces, named characters, and one series per control
  { const std::size_t first = out.size();
    auto pp = [&](const std::string& name, double lMs, double rMs, double fb, double tone, double spread, double lowcut, double mix) {
      add(name, "pingpong", "Ping-Pong", {{"left", (std::max(20.0, std::min(1500.0, lMs)) - 20.0) / 1480.0}, {"right", (std::max(20.0, std::min(1500.0, rMs)) - 20.0) / 1480.0}, {"feedback", fb}, {"tone", tone}, {"spread", spread}, {"lowcut", lowcut}}, mix); };
    struct Pat { const char* name; double l, r; };   // the two legs in quarter notes
    const Pat pats[] = {{"1/8 + 1/8", 0.5, 0.5}, {"1/8 + 1/8 dotted", 0.5, 0.75}, {"1/8 dotted + 1/8", 0.75, 0.5}, {"1/4 + 1/4", 1.0, 1.0}, {"1/4 + 1/8", 1.0, 0.5}, {"1/16 + 1/16", 0.25, 0.25}, {"1/8 triplet", 1.0 / 3.0, 1.0 / 3.0}, {"1/4 + 1/8 dotted", 1.0, 0.75}};
    for (int bpm : bpms) for (const Pat& q : pats) { const double beat = 60000.0 / bpm; char b[64]; std::snprintf(b, sizeof b, "Ping-Pong %d BPM %s", bpm, q.name); pp(b, beat * q.l, beat * q.r, 0.45, 0.6, 1.0, 0.15, 0.35); }   // 96
    struct Named { const char* name; double l, r, fb, tone, spread, cut, mix; };
    const Named named[] = {{"Table Tennis", 180, 180, 0.55, 0.8, 1.0, 0.2, 0.4}, {"Long Rally", 240, 240, 0.8, 0.65, 1.0, 0.2, 0.35}, {"Short Rally", 110, 110, 0.6, 0.75, 1.0, 0.25, 0.35}, {"Smash", 70, 95, 0.35, 0.9, 1.0, 0.3, 0.4},
      {"Serve And Return", 300, 150, 0.5, 0.7, 1.0, 0.2, 0.35}, {"Wide Slap", 90, 120, 0.15, 0.7, 1.0, 0.1, 0.4}, {"Stereo Slapback", 115, 140, 0.1, 0.6, 1.0, 0.1, 0.45}, {"Dub Bounce", 375, 375, 0.75, 0.3, 1.0, 0.35, 0.4},
      {"Dub Swing", 375, 250, 0.7, 0.3, 1.0, 0.35, 0.4}, {"Reggae Skank", 280, 420, 0.6, 0.4, 0.9, 0.4, 0.35}, {"Vocal Throw Wide", 430, 430, 0.4, 0.55, 1.0, 0.3, 0.3}, {"Lead Vocal Bounce", 330, 330, 0.3, 0.6, 0.8, 0.3, 0.25},
      {"Whisper Bounce", 260, 390, 0.5, 0.35, 1.0, 0.45, 0.3}, {"Guitar Bounce", 320, 480, 0.45, 0.55, 1.0, 0.2, 0.3}, {"Edge Dotted", 375, 500, 0.4, 0.65, 0.9, 0.2, 0.3}, {"Clean Arpeggio", 250, 375, 0.5, 0.75, 1.0, 0.15, 0.3},
      {"Keys Sparkle Bounce", 200, 300, 0.5, 0.95, 1.0, 0.3, 0.3}, {"Synth Pluck Bounce", 187, 187, 0.6, 0.85, 1.0, 0.2, 0.35}, {"Trance Gate", 136, 136, 0.65, 0.8, 1.0, 0.25, 0.35}, {"House Stab", 122, 244, 0.5, 0.7, 1.0, 0.3, 0.3},
      {"Snare Throw", 250, 250, 0.35, 0.5, 1.0, 0.4, 0.3}, {"Rimshot Bounce", 160, 240, 0.45, 0.7, 1.0, 0.45, 0.35}, {"Hi-Hat Scatter", 62, 93, 0.5, 0.95, 1.0, 0.7, 0.3}, {"Percussion Cloud", 83, 125, 0.75, 0.6, 1.0, 0.5, 0.35},
      {"Ambient Ping", 600, 600, 0.7, 0.35, 1.0, 0.3, 0.4}, {"Slow Canyon", 900, 900, 0.6, 0.3, 1.0, 0.25, 0.4}, {"Grand Canyon", 1200, 1500, 0.55, 0.25, 1.0, 0.25, 0.4}, {"Cave Drip", 700, 1050, 0.65, 0.4, 1.0, 0.5, 0.35},
      {"Infinite Bounce", 300, 300, 1.0, 0.6, 1.0, 0.3, 0.3}, {"Almost Forever", 450, 450, 0.95, 0.45, 1.0, 0.35, 0.3}, {"Dark Tunnel", 340, 510, 0.7, 0.1, 1.0, 0.2, 0.4}, {"Bright Glass", 210, 210, 0.5, 1.0, 1.0, 0.4, 0.3},
      {"Lo-Fi Bounce", 290, 290, 0.6, 0.15, 0.8, 0.6, 0.4}, {"Telephone Bounce", 230, 230, 0.5, 0.25, 0.7, 0.9, 0.4}, {"Radio Play", 150, 225, 0.4, 0.3, 0.6, 0.7, 0.35}, {"Narrow Bounce", 250, 250, 0.45, 0.6, 0.3, 0.15, 0.35},
      {"Centre Echo", 250, 250, 0.45, 0.6, 0.0, 0.15, 0.35}, {"Half Wide", 250, 250, 0.45, 0.6, 0.5, 0.15, 0.35}, {"Speech Safe", 140, 140, 0.2, 0.5, 0.6, 0.5, 0.2}, {"Announcer Hall", 380, 380, 0.3, 0.45, 0.7, 0.4, 0.2},
      {"Karaoke Bounce", 210, 210, 0.45, 0.55, 0.8, 0.3, 0.35}, {"Wedding Speech", 170, 255, 0.2, 0.5, 0.5, 0.5, 0.15}, {"Stadium Call", 520, 780, 0.5, 0.4, 1.0, 0.35, 0.35}, {"Game Arcade", 95, 95, 0.7, 0.9, 1.0, 0.4, 0.35}};
    for (const Named& q : named) pp(q.name, q.l, q.r, q.fb, q.tone, q.spread, q.cut, q.mix);                                                                        // 44
    for (int k = 0; k < 40; ++k) { const int ms = 30 + k * 30; char b[48]; std::snprintf(b, sizeof b, "Bounce %d ms", ms); pp(b, ms, ms, 0.45, 0.6, 1.0, 0.15, 0.35); }   // 40: even bounces, 30 .. 1200 ms
    for (int k = 0; k < 30; ++k) { const int l = 120 + (k % 6) * 60, r = l + 40 + (k / 6) * 45; char b[64]; std::snprintf(b, sizeof b, "Swing L %d / R %d ms", l, r); pp(b, l, r, 0.45, 0.6, 1.0, 0.15, 0.35); }   // 30: uneven legs
    for (int k = 0; k < 30; ++k) { const int pct = 4 + k * 3; char b[48]; std::snprintf(b, sizeof b, "Bounces %d %%", pct); pp(b, 250, 250, std::min(1.0, pct / 100.0), 0.6, 1.0, 0.15, 0.35); }                 // 30: how long it goes on
    for (int k = 0; k < 20; ++k) { const int pct = k * 5 + 5; char b[48]; std::snprintf(b, sizeof b, "Side To Side %d %%", pct); pp(b, 250, 250, 0.45, 0.6, pct / 100.0, 0.15, 0.35); }                          // 20: the spread
    for (int k = 0; k < 20; ++k) { const double t = (k + 1) / 20.0; char b[48]; std::snprintf(b, sizeof b, "Bounce Tone %.1f kHz", (600.0 + t * t * 11400.0) / 1000.0); pp(b, 250, 250, 0.5, t, 1.0, 0.15, 0.35); }   // 20: dark .. bright
    for (int k = 0; k < 20; ++k) { const double c = k / 19.0; char b[48]; if (k == 0) std::snprintf(b, sizeof b, "Bounce Full Range"); else std::snprintf(b, sizeof b, "Bounce Low Cut %d Hz", static_cast<int>(20.0 + c * 780.0 + 0.5)); pp(b, 250, 250, 0.5, 0.6, 1.0, c, 0.35); }   // 20: thin .. full
    (void)first;
  }
  // ---- W-SHAPE TUNER: 9 (ids 601 .. 609), after everything else so every older preset keeps its number
  { struct W { const char* name; double amount, bass, lowmid, presence, upper, air, smooth, fbass, flowmid, fpres, fupper, fair, loud; };
    const W ws[] = {{"SPEECH", 0.5, 0.2, 0.65, 0.7, 0.45, 0.3, 0.6, 0.55, 0.45, 0.5, 0.5, 0.45, 0.0},
      {"PODCAST", 0.55, 0.45, 0.6, 0.6, 0.55, 0.4, 0.65, 0.45, 0.45, 0.45, 0.5, 0.5, 0.0},
      {"MUSIC", 0.6, 0.6, 0.4, 0.4, 0.4, 0.55, 0.6, 0.5, 0.5, 0.5, 0.5, 0.5, 0.0},
      {"LIVE PA", 0.55, 0.35, 0.7, 0.5, 0.65, 0.3, 0.5, 0.6, 0.5, 0.5, 0.45, 0.4, 0.0},
      {"SMALL SPEAKER", 0.7, 0.8, 0.5, 0.5, 0.3, 0.4, 0.6, 0.75, 0.55, 0.5, 0.5, 0.4, 0.0},
      {"CAR", 0.65, 0.4, 0.65, 0.55, 0.45, 0.6, 0.6, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5},
      {"BACKGROUND (LOW LEVEL)", 0.6, 0.7, 0.3, 0.35, 0.3, 0.7, 0.7, 0.5, 0.5, 0.5, 0.5, 0.5, 0.8},
      {"GENTLE", 0.3, 0.55, 0.4, 0.4, 0.4, 0.5, 0.9, 0.5, 0.5, 0.5, 0.5, 0.5, 0.0},
      {"STRONG", 1.0, 0.8, 0.6, 0.6, 0.6, 0.7, 0.4, 0.5, 0.5, 0.5, 0.5, 0.5, 0.0}};
    for (const W& w : ws) add(w.name, "wshape", "W-Shape", {{"amount", w.amount}, {"bass", w.bass}, {"lowmid", w.lowmid}, {"presence", w.presence}, {"upper", w.upper}, {"air", w.air}, {"smooth", w.smooth}, {"output", 0.5},
      {"fbass", w.fbass}, {"flowmid", w.flowmid}, {"fpresence", w.fpres}, {"fupper", w.fupper}, {"fair", w.fair}, {"loudcomp", w.loud}, {"gainmatch", 1.0}, {"stereo", 0.0}, {"train", 0.0}}, 1.0);
  }
  // ---- AmF DYNAMIC EQ: a few starting points (bands as {type, freq Hz, gain dB, Q, dynamic, threshold dB, range dB, slope dB/oct})
  {
    struct B { int type; double hz, db, q; bool dyn; double thr, range; int slope; };
    struct D { const char* name; std::vector<B> bands; };
    const D ds[] = {
      {"DYN EQ VOCAL DE-HARSH", {{3, 80, 0, 0.7, false, 0, 0, 24}, {0, 3200, 0, 2.5, true, -30, -6, 24}, {0, 7000, 0, 3.0, true, -32, -8, 24}, {2, 11000, 1.5, 0.7, false, 0, 0, 24}}},
      {"DYN EQ KICK / BASS CLEAR", {{3, 30, 0, 0.7, false, 0, 0, 36}, {0, 250, 0, 1.5, true, -24, -6, 24}, {0, 60, 2, 1.0, false, 0, 0, 24}}},
      {"DYN EQ MIX GLUE", {{1, 120, 0, 0.7, true, -20, -3, 24}, {0, 500, -1, 1.0, false, 0, 0, 24}, {2, 9000, 0, 0.7, true, -26, -3, 24}}},
      {"DYN EQ SPEECH CLEAN", {{3, 100, 0, 0.7, false, 0, 0, 48}, {0, 300, -2, 1.2, false, 0, 0, 24}, {0, 6500, 0, 4.0, true, -34, -10, 24}, {4, 15000, 0, 0.7, false, 0, 0, 24}}},
      {"DYN EQ GUITAR TAME", {{3, 90, 0, 0.7, false, 0, 0, 18}, {0, 2500, 0, 2.0, true, -28, -5, 24}, {5, 4200, 0, 8.0, false, 0, 0, 24}}},
    };
    const auto lg = [](double x, double base) { return std::log(x) / std::log(base); };
    for (const D& d : ds) { std::vector<std::pair<std::string, double>> v; int n = 0;
      for (const B& b : d.bands) { const std::string p = "b" + std::to_string(++n) + "_"; int si = 0; const int sl[] = {6, 12, 18, 24, 30, 36, 48, 72, 96}; for (int i = 0; i < 9; ++i) if (sl[i] == b.slope) si = i;
        v.insert(v.end(), {{p + "on", 1.0}, {p + "type", b.type / 6.0}, {p + "freq", lg(b.hz / 20, 1000)}, {p + "gain", b.db / 60 + 0.5}, {p + "q", lg(b.q / 0.1, 300)}, {p + "slope", si / 8.0},
          {p + "dyn", b.dyn ? 1.0 : 0.0}, {p + "thresh", 1 + b.thr / 60}, {p + "range", b.range / 60 + 0.5}}); }
      add(d.name, "dyneq", "Dynamic EQ", std::move(v), 1.0); }
  }
  { // ---- AmF DE-HISS: 4 (reduction dB, threshold dB, from Hz, HF only, attack ms, release ms)
    struct H { const char* name; double red, thr, hz; bool hf; double att, rel; };
    const H hs[] = {{"DE-HISS TAPE HISS", 14, 6, 3000, false, 5, 150}, {"DE-HISS MIC PREAMP HISS", 10, 4, 1500, false, 8, 200}, {"DE-HISS CASSETTE", 18, 8, 2500, true, 4, 120}, {"DE-HISS LIGHT", 6, 3, 4000, true, 10, 250}};
    for (const H& h : hs) add(h.name, "dehiss", "De-Hiss", {{"reduce", h.red / 40}, {"thresh", (h.thr + 6) / 24}, {"freq", std::log(h.hz / 20) / std::log(1000.0)}, {"hfonly", h.hf ? 1.0 : 0.0},
      {"attack", std::log(h.att) / std::log(100.0)}, {"release", std::log(h.rel / 10) / std::log(100.0)}, {"auto", 1.0}}, 1.0);
  }
  return out;
}
}  // namespace
const std::vector<Preset>& presets() { static const std::vector<Preset> all = build(); return all; }
const Preset* preset(int id) { const auto& all = presets(); return id >= 1 && id <= static_cast<int>(all.size()) ? &all[static_cast<std::size_t>(id - 1)] : nullptr; }
}  // namespace am::fx
