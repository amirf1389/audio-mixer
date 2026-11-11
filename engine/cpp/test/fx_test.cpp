// The bus effects: an impulse through the reverb rings and decays, the delay repeats at its time, the chorus modulates, the drive saturates,
// bypass passes through, and an effect on a Runtime bus reaches the master through the return.
#include <cmath>
#include <set>
#include <cstdio>
#include <string>
#include <vector>

#include "fx/fx.hpp"
#include "fx/fx_plugin.hpp"
#include "fx/presets.hpp"
#include "audiomixer/runtime.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;
static float peak(const std::vector<float>& v, std::size_t a, std::size_t b) { float p = 0; for (std::size_t i = a; i < b && i < v.size(); ++i) p = std::max(p, std::fabs(v[i])); return p; }

int main() {
  CHECK(fx::catalog().size() == 16 && fx::make("nope", 48000) == nullptr);
  { auto r = fx::make("reverb", 48000); CHECK(r && std::string(r->kind()) == "reverb" && r->params().size() == 4 && r->text("decay", 0) == "0.3 s");
    std::vector<float> L(48000, 0.f), R(48000, 0.f); L[0] = R[0] = 1.f; r->set("decay", 0.2); r->process(L.data(), R.data(), L.size());
    CHECK(peak(L, 1000, 6000) > 0.02f && peak(L, 30000, 48000) < peak(L, 2000, 6000) * 0.5f);   // rings, then decays
    std::vector<float> L2(4800, 0.3f), R2(4800, 0.3f); r->setBypass(true); r->process(L2.data(), R2.data(), L2.size()); CHECK(L2[100] == 0.3f); }
  { auto d = fx::make("delay", 48000); d->set("time", (0.5 - 0.02) / 1.98); d->set("fb", 0.5); d->set("tone", 1.0); CHECK(d->text("time", (0.5 - 0.02) / 1.98) == "500 ms");
    std::vector<float> L(96000, 0.f), R(96000, 0.f); L[0] = 1.f; d->process(L.data(), R.data(), L.size());
    CHECK(peak(L, 23500, 24500) > 0.3f && peak(L, 10000, 20000) < 0.05f && peak(L, 47500, 48500) > 0.1f && peak(L, 47500, 48500) < peak(L, 23500, 24500));   // first repeat at 0.5 s, the second quieter
    d->set("pingpong", 1); d->reset(); std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f); L[0] = 1.f; d->process(L.data(), R.data(), L.size()); CHECK(peak(R, 47500, 48500) > 0.05f); }   // the second repeat crossed to the right
  { auto c = fx::make("chorus", 48000); std::vector<float> L(48000), R(48000); for (std::size_t i = 0; i < L.size(); ++i) L[i] = R[i] = std::sin(static_cast<float>(i) * 0.05f); c->process(L.data(), R.data(), L.size());
    bool differs = false; for (std::size_t i = 10000; i < 48000; i += 997) if (std::fabs(L[i] - R[i]) > 1e-3f) differs = true; CHECK(differs && peak(L, 1000, 48000) > 0.5f); }   // the two sides modulate apart
  { auto dr = fx::make("drive", 48000); dr->set("drive", 1.0); dr->set("out", 0.8); std::vector<float> L(4800), R(4800); for (std::size_t i = 0; i < L.size(); ++i) L[i] = R[i] = 0.5f * std::sin(static_cast<float>(i) * 0.1f); dr->process(L.data(), R.data(), L.size());
    float crest = 0; { float pk = peak(L, 100, 4800), sq = 0; for (std::size_t i = 100; i < 4800; ++i) sq += L[i] * L[i]; crest = pk / std::sqrt(sq / 4700); } CHECK(crest < 1.35f); }   // a sine's crest is 1.41; saturation flattens it
  // on a Runtime bus: strip 0 sends to bus 1, the reverb rings on the bus, the return carries the tail to the master after the input stops
  { Runtime rt(2); DeviceConfig c; c.backend = "null"; c.frames = 64; std::string err; CHECK(rt.open(c, err));
    auto rv = fx::make("reverb", 48000); rv->set("decay", 0.3); rt.setBusProcessor(0, rv.get()); rt.setSend(0, 0, 0, true); rt.setBusLevel(0, 0); rt.setBusReturn(0, 0); rt.setGainDb(0, -120);   // pre-fader send, fader down: only the bus return reaches the master
    const std::size_t n = 64; std::vector<float> tone(n), z(n, 0.f), o0(n), o1(n); for (std::size_t i = 0; i < n; ++i) tone[i] = 0.5f * ((i / 8) % 2 ? 1.f : -1.f); const float* ins[2] = {tone.data(), z.data()}; const float* quiet[2] = {z.data(), z.data()}; float* outs[2] = {o0.data(), o1.data()};
    for (int k = 0; k < 400; ++k) rt.onBlock(ins, outs, n); CHECK(rt.snapshot().master[0].pk > 0.05);
    for (int k = 0; k < 40; ++k) rt.onBlock(quiet, outs, n); CHECK(peak(o0, 0, n) > 0.001f);   // the tail is still sounding 50 ms after the input stopped
    rt.setBusProcessor(0, nullptr); for (int k = 0; k < 2; ++k) rt.onBlock(quiet, outs, n); }
  std::printf("fx_test: reverb rings and decays, delay repeats on time (ping-pong crosses), chorus modulates, drive saturates, bypass, bus return OK\n");
  { // the ten effects as rack plug-ins: each changes a tone, stays finite and below +12 dB, passes it untouched when bypassed, and keeps its state through a save / load
    CHECK(am::fx::catalog().size() == 16); const std::size_t n = 4800; std::vector<float> src(n), l(n), r(n); for (std::size_t i = 0; i < n; ++i) src[i] = 0.4f * static_cast<float>(std::sin(2 * 3.14159265 * 440 * static_cast<double>(i) / 48000) + 0.5 * std::sin(2 * 3.14159265 * 5000 * static_cast<double>(i) / 48000));
    for (const auto& [kind, name] : am::fx::catalog()) { auto p = am::fx::FxPlugin::make(kind, 48000); CHECK(p && p->info().format == "FX" && p->params().size() >= 3);
      if (kind == "postamp") p->setParam(0, 0.9); if (kind == "preamp") p->setParam(0, 0.4);   // these two are neutral (unity) until a knob is turned
      const float* in[2] = {src.data(), src.data()}; float* out[2] = {l.data(), r.data()}; for (int k = 0; k < 3; ++k) p->process(in, out, static_cast<int>(n));
      double diff = 0, pk = 0; bool finite = true; for (std::size_t i = 0; i < n; ++i) { finite = finite && std::isfinite(l[i]); diff += std::fabs(l[i] - src[i]); pk = std::max(pk, std::fabs(static_cast<double>(l[i]))); }
      if (!(finite && pk < 4.0 && diff / n > 1e-4)) std::fprintf(stderr, "effect %s: finite %d peak %.2f diff %.5f\n", kind.c_str(), finite, pk, diff / n); CHECK(finite && pk < 4.0 && diff / n > 1e-4);
      p->setParam(0, 0.83); const std::string st = p->state(); auto q = am::fx::FxPlugin::make(kind, 48000); CHECK(q->setState(st) && std::fabs(q->param(0) - 0.83) < 1e-3);
      p->setBypass(true); p->process(in, out, static_cast<int>(n)); double d2 = 0; for (std::size_t i = 0; i < n; ++i) d2 += std::fabs(l[i] - src[i]); CHECK(d2 == 0); }
    { // the tube amp distorts (harmonics appear) and its MASTER sets the level; the hall rings for seconds after the input stops; the pre amp's +36 dB is +36 dB
      auto amp = am::fx::make("tubeamp", 48000); amp->setMix(1); amp->set("gain", 0.8); std::vector<float> a(n), b(n); for (std::size_t i = 0; i < n; ++i) a[i] = b[i] = 0.3f * static_cast<float>(std::sin(2 * 3.14159265 * 220 * static_cast<double>(i) / 48000)); for (int k = 0; k < 2; ++k) amp->process(a.data(), b.data(), n);
      double h1 = 0, h3 = 0; for (std::size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / 48000; h1 += a[i] * std::sin(2 * 3.14159265 * 220 * t); h3 += a[i] * std::sin(2 * 3.14159265 * 660 * t); } CHECK(std::fabs(h3) > 0.02 * std::fabs(h1));
      auto hall = am::fx::make("hall", 48000); hall->setMix(1); hall->set("decay", 0.5); std::vector<float> hl(n, 0.f), hr(n, 0.f); hl[0] = hr[0] = 1.f; hall->process(hl.data(), hr.data(), n); double tail = 0; for (int k = 0; k < 9; ++k) { std::fill(hl.begin(), hl.end(), 0.f); std::fill(hr.begin(), hr.end(), 0.f); hall->process(hl.data(), hr.data(), n); } for (float v : hl) tail = std::max(tail, std::fabs(static_cast<double>(v))); CHECK(tail > 1e-4 && tail < 1.0);   // one second on: still sounding, not growing
      auto pre = am::fx::make("preamp", 48000); pre->set("gain", 1.0); pre->set("warmth", 0); pre->set("lowcut", 0); pre->set("out", 24.0 / 36.0); std::vector<float> pa(n, 0.001f), pb(n, 0.001f); pre->process(pa.data(), pb.data(), n); CHECK(std::fabs(pa[n - 1] / 0.001f - 63.1f) < 1.0f); }
    std::printf("fx_test: fifteen effects as rack plug-ins (reverb, delay, chorus, drive, flanger, phaser, tremolo, auto wah, bit crusher, exciter, tube amp, pre amp, post amp, hall, room): audible, finite, bypass, state\n"); }
  { const auto& all = am::fx::presets(); CHECK(all.size() == 300); std::set<std::string> names; int perKind[4] = {0, 0, 0, 0};
    for (const auto& p : all) { names.insert(p.name); auto e = am::fx::make(p.kind, 48000); CHECK(e); for (const auto& [id, v] : p.values) { CHECK(v >= 0 && v <= 1); e->set(id, v); } e->setMix(p.mix); bool known = false; for (const auto& prm : e->params()) for (const auto& [id, v] : p.values) if (prm.id == id) known = true; CHECK(known);
      perKind[p.kind == "reverb" ? 0 : p.kind == "delay" ? 1 : p.kind == "chorus" ? 2 : 3]++; }
    CHECK(names.size() == 300 && perKind[0] == 110 && perKind[1] == 100 && perKind[2] == 50 && perKind[3] == 40 && am::fx::preset(1) && !am::fx::preset(301));
    std::printf("fx_test: 300 presets, unique names, every parameter known to its effect\n"); }
  { // SYNC DELAY: 120 BPM and a quarter note is half a second; an eighth is a quarter second; WIDTH 1 sends the first echo left and the second right
    auto echoAt = [](double note, double width, std::size_t at, int side) { auto d = am::fx::make("syncdelay", 48000); d->set("tempo", (120.0 - 60.0) / 140.0); d->set("note", note); d->set("feedback", 0.5); d->set("tone", 1.0); d->set("width", width); d->setMix(1.0);
      std::vector<float> l(60000, 0.f), r(60000, 0.f); l[0] = r[0] = 1.f; d->process(l.data(), r.data(), l.size()); return std::fabs(side == 0 ? l[at] : r[at]); };
    CHECK(echoAt(0.2, 0, 24000, 0) > 0.5 && echoAt(0.2, 0, 24000, 1) > 0.5 && echoAt(0.2, 0, 12000, 0) < 1e-6 && echoAt(0.2, 0, 48000, 0) > 0.1);   // 1/4: first echo at 0.5 s, the next at 1 s
    CHECK(echoAt(0.6, 0, 12000, 0) > 0.5 && echoAt(0.6, 0, 24000, 0) > 0.1 && echoAt(0.6, 0, 6000, 0) < 1e-6);                                       // 1/8: 0.25 s
    CHECK(echoAt(0.2, 1, 24000, 0) > 0.5 && echoAt(0.2, 1, 24000, 1) < 1e-6 && echoAt(0.2, 1, 48000, 1) > 0.1 && echoAt(0.2, 1, 48000, 0) < 0.05);   // ping-pong
    auto d = am::fx::make("syncdelay", 48000); CHECK(d && d->params().size() == 5 && d->text("tempo", (120.0 - 60.0) / 140.0) == "120 BPM" && d->text("note", 0.2) == "1/4" && d->text("note", 1.0) == "1/16"); }
  return 0;
}
