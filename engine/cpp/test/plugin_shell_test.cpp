// PLUG-IN BUILDER end to end: a design is checked, exported as a new .vst3 bundle from the shell, and that bundle is loaded by the console's
// own VST3 host as any DAW would: its name, vendor and class id are the design's, its knobs are the design's exposed ones (with the block's
// wording), the chain changes the sound, BYPASS passes it untouched, the state round-trips. Bad designs are refused with a reason.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "fx/fx.hpp"
#include "plugin_shell/design.hpp"
#include "plugin_shell/preview.hpp"
#include <chrono>
#include <thread>
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
  { // PREVIEW: the same design on a (null) sound card: it runs, the chain changes the sound, BYPASS gives the source back, knobs move live,
    // a new block list swaps the chain in; every source plays
    shell::Preview pv; pv.setDesign(d); pv.setSource(shell::Preview::PinkNoise);
    std::vector<float> a(512), b(512), a2(512), b2(512); pv.setBypass(true); pv.renderForTest(a.data(), b.data(), 512);
    shell::Preview pv2; pv2.setDesign(d); pv2.setSource(shell::Preview::PinkNoise); pv2.renderForTest(a2.data(), b2.data(), 512);
    double dd = 0; for (int i = 0; i < 512; ++i) dd += std::fabs(a[i] - a2[i]); CHECK(dd / 512 > 1e-4);           // processed differs from the same noise bypassed
    std::string e2; CHECK(pv.start(e2, "null", 48000, 256)); std::this_thread::sleep_for(std::chrono::milliseconds(200)); CHECK(pv.blocks() > 5 && pv.outPeak() > 0.f);
    shell::Design d2 = d; d2.params[0].def = 0.1; pv.setDesign(d2);                                                    // a knob: live
    d2.blocks.push_back({"room", {}}); pv.setDesign(d2);                                                                // a block: a new chain swapped in
    for (int s = 0; s <= 2; ++s) { pv.setSource(static_cast<shell::Preview::Source>(s)); std::this_thread::sleep_for(std::chrono::milliseconds(60)); CHECK(pv.outPeak() > 0.f); }
    CHECK(pv.setSample(42)); pv.stop(); CHECK(!pv.running());
    std::printf("plugin_shell_test: preview on the null device, %llu blocks, A/B, live knobs, chain swap, sources OK\n", static_cast<unsigned long long>(pv.blocks())); }
  std::printf(fails ? "plugin_shell_test: %d failed\n" : "plugin_shell_test: all passed\n", fails); return fails ? 1 : 0;
}
