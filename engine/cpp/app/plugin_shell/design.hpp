// PLUG-IN BUILDER designs: what an exported AMFB plug-in is. The shell (shell.cpp, one VST3 binary) reads the design from its bundle
// (<Name>.vst3/Contents/Resources/design.json) and becomes that plug-in in any DAW: a chain of AMFB effect blocks, some knobs fixed at
// design time, the rest exposed to the DAW as automatable parameters.
//   { "v": 1, "name": "Vocal Glue", "vendor": "AMFB Audio", "uid": "<32 hex>", "category": "Fx|Dynamics", "version": "1.0.0",
//     "blocks": [ { "kind": "barrier", "set": { "thr": 0.4 } }, ... ],          up to 6 blocks, kinds from am::fx::catalog()
//     "params": [ { "title": "Drive", "block": 0, "id": "drive", "def": 0.5 }, ... ],   up to 16 exposed knobs (normalized 0..1)
//     "panel":  { "color": "#EA580C", "layout": [ { "param": 0, "x": 20, "y": 30, "kind": "knob" }, ... ] } }   the AMFB panel (optional)
#pragma once
#include <string>
#include <vector>

namespace am::shell {

struct Block { std::string kind; std::vector<std::pair<std::string, double>> set; };
struct Param { std::string title, id; int block = 0; double def = 0.5; };
struct Design {
  std::string name, vendor = "AMFB Audio", uid, category = "Fx", version = "1.0.0", panelJson;
  std::vector<Block> blocks; std::vector<Param> params;
};
constexpr int kMaxBlocks = 6, kMaxParams = 16;

// parses and checks a design: unknown effect kinds, an exposed knob the block does not have, more blocks / params than allowed,
// a uid that is not 32 hex digits. err says what is wrong (for the builder's screen).
bool parse(const std::string& json, Design& out, std::string& err);
std::string toJson(const Design& d);
std::string newUid(const std::string& seed);   // 32 hex digits, from the name and the time: a new class id for every new design

// EXPORT: copies the shell bundle to <outDir>/<Name>.vst3 with the binary renamed to the plug-in's name and the design inside.
// shellBundle: the AMFB Plugin Shell.vst3 next to the console. Returns the new bundle's path, or "" with err.
std::string exportBundle(const std::string& shellBundle, const Design& d, const std::string& outDir, std::string& err);
std::string safeName(const std::string& name);  // the name as a file name (letters, digits, space, - _ only)

}  // namespace am::shell
