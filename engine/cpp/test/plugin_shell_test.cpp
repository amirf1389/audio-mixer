// PLUG-IN BUILDER end to end: a design is checked, exported as a new .vst3 bundle from the shell, and that bundle is loaded by the console's
// own VST3 host as any DAW would: its name, vendor and class id are the design's, its knobs are the design's exposed ones (with the block's
// wording), the chain changes the sound, BYPASS passes it untouched, the state round-trips. Bad designs are refused with a reason.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "fx/fx.hpp"
#include "plugin_shell/design.hpp"
#include "vst3/vst3_host.hpp"

using namespace am;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main(int argc, char** argv) {
  const std::string shell = argc > 1 ? argv[1] : "AMFB Plugin Shell.vst3"; const std::string out = (std::filesystem::temp_directory_path() / "amfb-builder-test").string();
  std::filesystem::remove_all(out);
  // the design: CRUSHER (its first knob exposed as "Grit", the second fixed) then TUBE AMP (its first knob exposed as "Heat")
  auto knobs = [](const char* k) { std::vector<std::string> v; for (const auto& p : fx::make(k, 48000)->params()) v.push_back(p.id); return v; };
  const auto cr = knobs("crusher"), tu = knobs("tubeamp"); CHECK(cr.size() >= 2 && !tu.empty());
  const std::string uid = shell::newUid("Grit Box"); CHECK(uid.size() == 32);
  const std::string json = "{\"v\":1,\"name\":\"Grit Box\",\"vendor\":\"AMFB Audio\",\"uid\":\"" + uid + "\",\"category\":\"Fx|Distortion\",\"version\":\"1.2.0\","
    "\"blocks\":[{\"kind\":\"crusher\",\"set\":{\"" + cr[1] + "\":0.8}},{\"kind\":\"tubeamp\",\"set\":{}}],"
    "\"params\":[{\"title\":\"Grit\",\"block\":0,\"id\":\"" + cr[0] + "\",\"def\":0.7},{\"title\":\"Heat\",\"block\":1,\"id\":\"" + tu[0] + "\",\"def\":0.6}]}";
  shell::Design d; std::string err; CHECK(shell::parse(json, d, err)); if (!err.empty()) std::printf("design: %s\n", err.c_str());
  CHECK(d.blocks.size() == 2 && d.params.size() == 2);
  // refusals
  shell::Design bad; CHECK(!shell::parse("{\"name\":\"x\",\"uid\":\"" + uid + "\",\"blocks\":[{\"kind\":\"nope\"}]}", bad, err) && err.find("nope") != std::string::npos);
  CHECK(!shell::parse("{\"name\":\"x\",\"uid\":\"123\",\"blocks\":[{\"kind\":\"crusher\"}]}", bad, err));
  CHECK(!shell::parse("{\"name\":\"x\",\"uid\":\"" + uid + "\",\"blocks\":[{\"kind\":\"crusher\"}],\"params\":[{\"block\":0,\"id\":\"no-such-knob\"}]}", bad, err));
  // export and load as a DAW would
  const std::string bundle = shell::exportBundle(shell, d, out, err); CHECK(!bundle.empty()); if (bundle.empty()) { std::printf("export: %s\n", err.c_str()); return 1; }
  CHECK(bundle.find("Grit Box.vst3") != std::string::npos);
  const auto found = vst3::scan({out}); CHECK(found.size() == 1);
  std::vector<vst3::ClassInfo> classes; CHECK(vst3::inspect(found[0], classes, err)); CHECK(classes.size() == 1);
  if (!classes.empty()) { CHECK(classes[0].name == "Grit Box" && classes[0].vendor == "AMFB Audio" && classes[0].subCategories == "Fx|Distortion" && classes[0].version == "1.2.0");
    std::string cid = classes[0].cid; for (char& c : cid) c = static_cast<char>(std::tolower(c)); CHECK(cid == uid); }
  auto p = vst3::Plugin::load(found[0], "", 48000, 512, err); CHECK(p); if (!p) { std::printf("load: %s\n", err.c_str()); return 1; }
  const auto params = p->params(); CHECK(params.size() == 3 && params[0].title == "Grit" && params[1].title == "Heat" && params[2].bypass);
  CHECK(std::fabs(params[0].normalized - 0.7) < 1e-6); CHECK(!p->paramText(params[0].id, 0.5).empty());
  const int ch = p->outputChannels(); std::vector<float> inL(512), inR(512), oL(512), oR(512);
  for (int i = 0; i < 512; ++i) inL[i] = inR[i] = 0.3f * static_cast<float>(std::sin(2 * 3.14159265 * 440 * i / 48000.0));
  const float* ins[2] = {inL.data(), inR.data()}; float* outs[2] = {oL.data(), oR.data()};
  auto diff = [&] { double d = 0; for (int i = 0; i < 512; ++i) d += std::fabs(oL[i] - inL[i]); return d / 512; };
  for (int k = 0; k < 4; ++k) p->process(ins, outs, 512); const double processed = diff();
  CHECK(processed > 1e-3); bool fin = true; for (float v : oL) fin = fin && std::isfinite(v); CHECK(fin);
  p->setBypass(true); p->process(ins, outs, 512); CHECK(diff() < 1e-7); p->setBypass(false);
  p->setParam(params[0].id, 0.2); const auto st = p->state(); p->setParam(params[0].id, 0.9);
  CHECK(p->setState(st) && std::fabs(p->param(params[0].id) - 0.2) < 1e-4);
  std::printf("plugin_shell_test: design checked, exported as %s (%d ch), loaded by the host: name / vendor / id / knobs OK, changes the sound (%.4f), bypass, state OK\n", bundle.c_str(), ch, processed);
  std::printf(fails ? "plugin_shell_test: %d failed\n" : "plugin_shell_test: all passed\n", fails); return fails ? 1 : 0;
}
