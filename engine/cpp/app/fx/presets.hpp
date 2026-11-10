// The FX preset library: 300 named settings for the bus effects (plate reverb, tape delay, stereo chorus, tube drive), generated from
// musical families (rooms, halls, plates, chambers, slaps, echoes, tempo delays, ensembles, drives), each a kind + parameter values 0..1
// + mix. Deterministic, no files: the console and the daemon list the same 300.
#pragma once
#include <string>
#include <vector>

namespace am::fx {

struct Preset { int id; std::string name, kind, category; std::vector<std::pair<std::string, double>> values; double mix; };
const std::vector<Preset>& presets();            // 300 entries, ids 1..300
const Preset* preset(int id);

}  // namespace am::fx
