// The VST3 host against the self-written test plug-in (amixtest.vst3, built by CMake next to this test): scan, inspect, load, buses, params,
// parameter changes reaching the audio thread, bypass, state round trip, and the MonoInsert on a Runtime strip.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "vst3/vst3_host.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";   // the folder that holds amixtest.vst3
  const auto found = vst3::scan({dir}); CHECK(found.size() == 1 && found[0].find("amixtest.vst3") != std::string::npos);
  std::vector<vst3::ClassInfo> classes; std::string err; CHECK(vst3::inspect(found[0], classes, err)); CHECK(classes.size() == 1 && classes[0].name == "AMIX Test Gain" && classes[0].cid.size() == 32 && classes[0].subCategories == "Fx|Tools");
  CHECK(!vst3::inspect(dir + "/nope.vst3", classes, err) && !err.empty());
  auto p = vst3::Plugin::load(found[0], "", 48000, 256, err); CHECK(p); CHECK(p->inputChannels() == 1 && p->outputChannels() == 1 && p->latencySamples() == 0 && p->hasBypass());
  const auto params = p->params(); CHECK(params.size() == 2 && params[0].title == "Gain" && std::fabs(params[0].normalized - 0.5) < 1e-9 && params[1].bypass);
  CHECK(p->paramText(params[0].id, 0.75) == "1.50 x");
  std::vector<float> in(256, 0.5f), out(256, 0.f); const float* ins[1] = {in.data()}; float* outs[1] = {out.data()};
  p->process(ins, outs, 256); CHECK(std::fabs(out[10] - 0.5f) < 1e-6f);                                           // unity
  p->setParam(params[0].id, 0.75); CHECK(std::fabs(p->param(params[0].id) - 0.75) < 1e-9); p->process(ins, outs, 256); CHECK(std::fabs(out[10] - 0.75f) < 1e-6f);   // gain 1.5 arrived through the ring
  p->setBypass(true); CHECK(p->bypass()); p->process(ins, outs, 256); CHECK(std::fabs(out[10] - 0.5f) < 1e-6f); p->setBypass(false);
  const std::string st = p->state(); CHECK(!st.empty());
  p->setParam(params[0].id, 0.25); p->process(ins, outs, 256); CHECK(std::fabs(out[10] - 0.25f) < 1e-6f);
  CHECK(p->setState(st) && std::fabs(p->param(params[0].id) - 0.75) < 1e-9); p->process(ins, outs, 256); CHECK(std::fabs(out[10] - 0.75f) < 1e-6f);
  CHECK(!p->setState("not base64 of a state"));
  // on a Runtime strip, after the channel DSP, before the fader: the strip meter shows the plug-in's gain
  { Runtime rt(2); DeviceConfig c; c.backend = "null"; c.frames = 64; CHECK(rt.open(c, err));
    auto plug = std::shared_ptr<vst3::Plugin>(vst3::Plugin::load(found[0], classes[0].cid, 48000, 64, err)); CHECK(plug);
    auto chain = std::make_shared<Runtime::InsertChain>(); chain->push_back(std::make_shared<vst3::MonoInsert>(plug, 64)); rt.setInserts(0, chain);
    const std::size_t n = 64; std::vector<float> tone(n); for (std::size_t i = 0; i < n; ++i) tone[i] = 0.5f * ((i / 8) % 2 ? 1.f : -1.f); std::vector<float> q(n, 0.f), o0(n), o1(n); const float* i2[2] = {tone.data(), q.data()}; float* o2[2] = {o0.data(), o1.data()};
    for (int k = 0; k < 200; ++k) rt.onBlock(i2, o2, n); CHECK(std::fabs(rt.snapshot().strips[0].pk - 0.5) < 0.02);
    plug->setParam(plug->params()[0].id, 0.25); for (int k = 0; k < 800; ++k) rt.onBlock(i2, o2, n); CHECK(std::fabs(rt.snapshot().strips[0].pk - 0.25) < 0.02);   // 0.5 x = -6 dB on the strip
    rt.setInserts(0, nullptr); for (int k = 0; k < 800; ++k) rt.onBlock(i2, o2, n); CHECK(std::fabs(rt.snapshot().strips[0].pk - 0.5) < 0.02); }
  if (argc > 2) {   // AmF DE-HISS.vst3 (our product plug-in from the shell): loads, stereo, reports its latency, the delay is that latency, it takes hiss off
    std::vector<vst3::ClassInfo> dc; CHECK(vst3::inspect(argv[2], dc, err)); CHECK(dc.size() == 1 && dc[0].name == "AmF DE-HISS" && dc[0].subCategories == "Fx|Restoration");
    auto d = vst3::Plugin::load(argv[2], "", 48000, 256, err); CHECK(d); CHECK(d->inputChannels() == d->outputChannels() && d->outputChannels() >= 1 && d->latencySamples() == 1024 && d->hasBypass());
    const auto dp = d->params(); CHECK(dp.size() == 10 && dp[0].title == "REDUCTION"); CHECK(d->paramText(dp[0].id, 0.3) == "12.0 dB");
    std::vector<float> l(256), r(256), ol(256), orr(256); const float* di[2] = {l.data(), r.data()}; float* dout[2] = {ol.data(), orr.data()}; long at = -1; long t = 0;
    for (int b = 0; b < 12; ++b, t += 256) { for (int i = 0; i < 256; ++i) l[i] = r[i] = (t + i == 300) ? 1.f : 0.f; d->process(di, dout, 256); for (int i = 0; i < 256; ++i) if (std::fabs(ol[i]) > 0.5f) at = t + i; }
    CHECK(at == 300 + 1024);
    for (const auto& q : dp) { if (q.title == "LEARN") d->setParam(q.id, 1.0); if (q.title == "FREQ FROM") d->setParam(q.id, 0.0); }   // full band
    unsigned s = 1; auto nz = [&] { s = s * 1664525u + 1013904223u; return 0.01f * (static_cast<float>(s >> 8) / 8388608.f - 1.f); };
    for (int b = 0; b < 400; ++b) { for (int i = 0; i < 256; ++i) { l[i] = nz(); r[i] = nz(); } d->process(di, dout, 256); }
    for (const auto& q : dp) if (q.title == "LEARN") d->setParam(q.id, 0.0);
    double ein = 0, eout = 0; for (int b = 0; b < 400; ++b) { for (int i = 0; i < 256; ++i) { l[i] = nz(); r[i] = nz(); ein += double(l[i]) * l[i]; } d->process(di, dout, 256); if (b > 100) for (int i = 0; i < 256; ++i) eout += double(ol[i]) * ol[i]; }
    const double down = 10 * std::log10(ein * 299.0 / 400.0 / (eout + 1e-30)); if (!(down > 10)) std::fprintf(stderr, "de-hiss down %.2f dB\n", down); CHECK(down > 11.9);   // REDUCTION 12 dB, full band
    std::printf("vst3_test: AmF DE-HISS.vst3 loads, stereo, latency 1024 = measured delay, LEARN then %.1f dB off the hiss\n", down); }
  std::printf("vst3_test: scan / inspect / load, mono bus, params + text, ring to the audio thread, bypass, state round trip, MonoInsert on a strip OK (%s)\n", classes[0].name.c_str());
  return 0;
}
