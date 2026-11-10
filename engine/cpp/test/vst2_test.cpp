// The VST2 host against the self-written test plug-in (amixtest2.so / .dll next to the test): scan, inspect, load, params + text, bypass
// through the plug-in, chunk state round trip, and the host::MonoInsert on a Runtime strip.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "vst2/vst2_host.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  const auto found = vst2::scan({dir}); CHECK(found.size() == 1 && found[0].find("amixtest2") != std::string::npos);
  host::Info info; std::string err; CHECK(vst2::inspect(found[0], info, err)); CHECK(info.format == "VST2" && info.name == "AMIX Test Gain 2" && info.vendor == "AMFB Audio (test)" && info.uid == "414D5832");
  CHECK(!vst2::inspect(dir + "/nope.so", info, err) && !err.empty());
  auto p = vst2::Plugin::load(found[0], 48000, 256, err); CHECK(p); CHECK(p->inputChannels() == 2 && p->outputChannels() == 2 && p->hasBypass());
  const auto params = p->params(); CHECK(params.size() == 1 && params[0].title == "Gain" && params[0].units == "x" && std::fabs(params[0].normalized - 0.5) < 1e-6);
  CHECK(p->paramText(0, 0.75) == "1.50" && std::fabs(p->param(0) - 0.5) < 1e-6);   // the text probe leaves the value alone
  std::vector<float> in(256, 0.5f), oL(256), oR(256); const float* ins[2] = {in.data(), in.data()}; float* outs[2] = {oL.data(), oR.data()};
  p->process(ins, outs, 256); CHECK(std::fabs(oL[10] - 0.5f) < 1e-6f && std::fabs(oR[10] - 0.5f) < 1e-6f);
  p->setParam(0, 0.75); p->process(ins, outs, 256); CHECK(std::fabs(oL[10] - 0.75f) < 1e-6f);
  p->setBypass(true); p->process(ins, outs, 256); CHECK(std::fabs(oL[10] - 0.5f) < 1e-6f && p->bypass()); p->setBypass(false);
  const std::string st = p->state(); CHECK(!st.empty()); p->setParam(0, 0.25); p->process(ins, outs, 256); CHECK(std::fabs(oL[10] - 0.25f) < 1e-6f);
  CHECK(p->setState(st) && std::fabs(p->param(0) - 0.75) < 1e-6); p->process(ins, outs, 256); CHECK(std::fabs(oL[10] - 0.75f) < 1e-6f);
  { Runtime rt(2); DeviceConfig c; c.backend = "null"; c.frames = 64; CHECK(rt.open(c, err));
    std::shared_ptr<host::Plugin> plug = vst2::Plugin::load(found[0], 48000, 64, err); CHECK(plug);
    auto chain = std::make_shared<Runtime::InsertChain>(); chain->push_back(std::make_shared<host::MonoInsert>(plug, 64)); rt.setInserts(0, chain);
    const std::size_t n = 64; std::vector<float> tone(n); for (std::size_t i = 0; i < n; ++i) tone[i] = 0.5f * ((i / 8) % 2 ? 1.f : -1.f); std::vector<float> q(n, 0.f), o0(n), o1(n); const float* i2[2] = {tone.data(), q.data()}; float* o2[2] = {o0.data(), o1.data()};
    for (int k = 0; k < 200; ++k) rt.onBlock(i2, o2, n); CHECK(std::fabs(rt.snapshot().strips[0].pk - 0.5) < 0.02);
    plug->setParam(0, 0.25); for (int k = 0; k < 800; ++k) rt.onBlock(i2, o2, n); CHECK(std::fabs(rt.snapshot().strips[0].pk - 0.25) < 0.02);
    rt.setInserts(0, nullptr); }
  std::printf("vst2_test: scan / inspect / load (clean-room ABI), params + text, bypass via the plug-in, chunk state round trip, MonoInsert on a strip OK\n");
  return 0;
}
