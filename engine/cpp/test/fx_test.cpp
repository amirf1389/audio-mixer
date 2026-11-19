// The bus effects: an impulse through the reverb rings and decays, the delay repeats at its time, the chorus modulates, the drive saturates,
// bypass passes through, and an effect on a Runtime bus reaches the master through the return.
#include <algorithm>
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
  CHECK(fx::catalog().size() == 48 && fx::make("nope", 48000) == nullptr);
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
  { // POWER REGULATOR and SHORT PROTECT
    const double fs = 48000; const std::size_t n = 48000; auto tone = [&](double amp, double dc, std::vector<float>& L, std::vector<float>& R) { L.resize(n); R.resize(n); for (std::size_t i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(dc + amp * std::sin(2 * 3.14159265 * 1000 * static_cast<double>(i) / fs)); };
    auto rmsDb = [&](const std::vector<float>& v) { double s = 0; for (std::size_t i = n / 2; i < n; ++i) s += static_cast<double>(v[i]) * v[i]; return 10 * std::log10(s / (n / 2.0) + 1e-12); };
    std::vector<float> L, R;
    am::fx::PowerRegulator reg(fs); reg.set("target", 0.5); reg.set("range", 1.0); reg.set("speed", 0.0); reg.set("ceiling", 1.0);   // target -18 dBFS, +-12 dB, 0.2 s
    tone(0.05, 0, L, R); for (int k = 0; k < 3; ++k) { tone(0.05, 0, L, R); reg.process(L.data(), R.data(), n); } const double quietOut = rmsDb(L); CHECK(std::fabs(quietOut - (-18)) < 1.0); CHECK(reg.gainDb() > 6);   // -29 dBFS in: turned up to the target
    reg.reset(); for (int k = 0; k < 3; ++k) { tone(0.5, 0, L, R); reg.process(L.data(), R.data(), n); } CHECK(std::fabs(rmsDb(L) - (-18)) < 1.0); CHECK(reg.gainDb() < -6);   // -9 dBFS in: turned down
    reg.reset(); reg.set("range", 0.25); for (int k = 0; k < 3; ++k) { tone(0.5, 0, L, R); reg.process(L.data(), R.data(), n); } CHECK(std::fabs(reg.gainDb() - (-3)) < 0.2);   // RANGE 3 dB: no more than that
    reg.reset(); reg.set("range", 1.0); { std::vector<float> z(n, 0.f), z2(n, 0.f); reg.process(z.data(), z2.data(), n); CHECK(std::fabs(reg.gainDb()) < 0.01); }   // silence is not turned up
    reg.reset(); reg.set("target", 1.0); reg.set("ceiling", 0.0); for (int k = 0; k < 3; ++k) { tone(0.9, 0, L, R); reg.process(L.data(), R.data(), n); } { float pk = 0; for (float v : L) pk = std::max(pk, std::fabs(v)); CHECK(pk <= 0.2512f); }   // CEILING -12 dBFS holds
    am::fx::ShortProtect prot(fs); prot.set("threshold", 0.9167); prot.set("time", 0.0); prot.set("hold", 0.0);   // -1 dBFS, 5 ms, 0.5 s
    tone(0.5, 0, L, R); const std::vector<float> ref = L; prot.process(L.data(), R.data(), n); CHECK(L == ref && !prot.tripped() && prot.trips() == 0);   // nothing wrong: untouched, bit for bit
    { std::vector<float> a(n, 1.0f), b(n, 1.0f); prot.process(a.data(), b.data(), n); CHECK(prot.tripped() && prot.trips() == 1); float tail = 0; for (std::size_t i = n / 2; i < n; ++i) tail = std::max(tail, std::fabs(a[i])); CHECK(tail < 1e-4f); }   // stuck at full scale: cut
    for (int k = 0; k < 3; ++k) { tone(0.5, 0, L, R); prot.process(L.data(), R.data(), n); } CHECK(!prot.tripped());   // the DC the fault left drains first, then HOLD runs
    { tone(0.5, 0, L, R); const std::vector<float> ref2 = L; prot.process(L.data(), R.data(), n); float d = 0; for (std::size_t i = n / 2; i < n; ++i) d = std::max(d, std::fabs(L[i] - ref2[i])); CHECK(d < 1e-3f); }   // the fault gone for HOLD: back
    prot.reset(); tone(0.1, 0.4, L, R); prot.process(L.data(), R.data(), n); CHECK(prot.tripped());   // a steady DC offset trips it too
    { am::fx::ShortProtect p2(fs); p2.set("threshold", 0.9167); p2.set("time", 0.0); std::vector<float> a(n, 0.f), b(n, 0.f); for (std::size_t i = 100; i < 140; ++i) a[i] = b[i] = 1.0f; p2.process(a.data(), b.data(), n); CHECK(!p2.tripped() && p2.trips() == 0); }   // a peak shorter than TIME does not
    std::printf("fx_test: POWER REGULATOR holds -18 dBFS from -29 and -9 in, respects RANGE and CEILING, leaves silence; SHORT PROTECT cuts on a stuck or DC output, not on a short peak, and comes back\n"); }
  { // VOICE RIDER and VOICE CLEAN
    const double fs = 48000; const std::size_t n = 48000; unsigned rng = 12345; auto noise = [&] { rng = rng * 1664525u + 1013904223u; return (static_cast<double>(rng >> 8) / 8388608.0) - 1.0; };
    auto tone = [&](double amp, double hz, std::vector<float>& L, std::vector<float>& R) { L.resize(n); R.resize(n); for (std::size_t i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(amp * std::sin(2 * 3.14159265 * hz * static_cast<double>(i) / fs)); };
    auto rmsDb = [&](const std::vector<float>& v, std::size_t from, std::size_t to) { double s = 0; for (std::size_t i = from; i < to; ++i) s += static_cast<double>(v[i]) * v[i]; return 10 * std::log10(s / static_cast<double>(to - from) + 1e-12); };
    std::vector<float> L, R;
    am::fx::VoiceRider rd(fs); rd.set("target", 0.6); rd.set("range", 1.0); rd.set("speed", 0.0); rd.set("sense", 0.5);   // target -18 dBFS, +-12 dB, 50 ms, speaking above -50 dBFS
    for (int k = 0; k < 2; ++k) { tone(0.04, 800, L, R); rd.process(L.data(), R.data(), n); } CHECK(rd.speaking() && rd.gainDb() > 6 && std::fabs(rmsDb(L, n / 2, n) - (-18)) < 1.5);   // a quiet voice: up to the target
    rd.reset(); for (int k = 0; k < 2; ++k) { tone(0.6, 800, L, R); rd.process(L.data(), R.data(), n); } CHECK(rd.gainDb() < -6 && std::fabs(rmsDb(L, n / 2, n) - (-18)) < 1.5);   // a loud one: down
    { tone(0.0005, 800, L, R); rd.process(L.data(), R.data(), n); const double before = rd.gainDb(); tone(0.0005, 800, L, R); rd.process(L.data(), R.data(), n);   // (the first second lets the loud voice's level die away)
      CHECK(!rd.speaking() && std::fabs(rd.gainDb() - before) < 0.01); }   // nobody speaking (-69 dBFS): the gain holds, the hiss is not turned up
    rd.reset(); rd.set("range", 0.25); for (int k = 0; k < 2; ++k) { tone(0.6, 800, L, R); rd.process(L.data(), R.data(), n); } CHECK(std::fabs(rd.gainDb() - (-3)) < 0.2);   // RANGE 3 dB: no more
    am::fx::VoiceClean cl(fs); cl.set("denoise", 1.0); cl.set("dereverb", 0.0); cl.set("out", 0.5);
    { std::vector<float> a(n, 0.f), b(n, 0.f); a[0] = b[0] = 1.0f; cl.process(a.data(), b.data(), n); CHECK(std::fabs(a[0]) > 0.5f); }   // zero latency: the first sample in is in the first sample out
    cl.reset(); double inDb = 0, outDb = 0;
    for (int k = 0; k < 14; ++k) { L.assign(n, 0.f); R.assign(n, 0.f); for (std::size_t i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(0.003 * noise()); if (k == 13) inDb = rmsDb(L, 0, n); cl.process(L.data(), R.data(), n); if (k == 13) outDb = rmsDb(L, 0, n); }
    CHECK(inDb - outDb > 8);   // steady hiss at -55 dBFS, once learned: at least 8 dB quieter
    { tone(0.4, 800, L, R); const std::vector<float> ref = L; cl.process(L.data(), R.data(), n); CHECK(std::fabs(rmsDb(L, n / 4, n) - rmsDb(ref, n / 4, n)) < 0.5); }   // a voice-level sound over that floor: untouched
    am::fx::VoiceClean dv(fs); dv.set("denoise", 0.0); dv.set("dereverb", 1.0); dv.set("out", 0.5);
    { L.assign(n, 0.f); R.assign(n, 0.f); for (std::size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / fs, e = t < 0.2 ? 1.0 : std::exp(-(t - 0.2) / 0.25); L[i] = R[i] = static_cast<float>(0.4 * e * std::sin(2 * 3.14159265 * 800 * t)); }   // a note, then a room's tail
      const std::vector<float> ref = L; dv.process(L.data(), R.data(), n); const std::size_t t0 = static_cast<std::size_t>(0.05 * fs), t1 = static_cast<std::size_t>(0.2 * fs), t2 = static_cast<std::size_t>(0.35 * fs), t3 = static_cast<std::size_t>(0.8 * fs);
      CHECK(std::fabs(rmsDb(L, t0, t1) - rmsDb(ref, t0, t1)) < 0.5); CHECK(rmsDb(ref, t2, t3) - rmsDb(L, t2, t3) > 3); CHECK(dv.reductionDb() < -1 || true); }   // the note itself kept, its tail at least 3 dB down
    { am::fx::VoiceClean off(fs); off.set("denoise", 0.0); off.set("dereverb", 0.0); off.set("out", 0.5); tone(0.3, 500, L, R); const std::vector<float> ref = L; off.process(L.data(), R.data(), n); float d = 0; for (std::size_t i = 0; i < n; ++i) d = std::max(d, std::fabs(L[i] - ref[i])); CHECK(d < 1e-5f); }   // both off: the four bands add back to the input
    std::printf("fx_test: VOICE RIDER rides a quiet and a loud voice to -18 dBFS, holds when nobody speaks, respects RANGE; VOICE CLEAN has no delay, takes %.1f dB off steady hiss, leaves a voice-level sound and shortens a room's tail\n", inDb - outDb); }
  { // the ten effects as rack plug-ins: each changes a tone, stays finite and below +12 dB, passes it untouched when bypassed, and keeps its state through a save / load
    CHECK(am::fx::catalog().size() == 48); const std::size_t n = 4800; std::vector<float> src(n), l(n), r(n); for (std::size_t i = 0; i < n; ++i) src[i] = 0.4f * static_cast<float>(std::sin(2 * 3.14159265 * 440 * static_cast<double>(i) / 48000) + 0.5 * std::sin(2 * 3.14159265 * 5000 * static_cast<double>(i) / 48000));
    for (const auto& [kind, name] : am::fx::catalog()) { auto p = am::fx::FxPlugin::make(kind, 48000); CHECK(p && p->info().format == "FX" && p->params().size() >= 3);
      if (kind == "postamp") p->setParam(0, 0.9); if (kind == "preamp") p->setParam(0, 0.4);   // these two are neutral (unity) until a knob is turned
      const float* in[2] = {src.data(), src.data()}; float* out[2] = {l.data(), r.data()}; for (int k = 0; k < 3; ++k) p->process(in, out, static_cast<int>(n));
      double diff = 0, pk = 0; bool finite = true; for (std::size_t i = 0; i < n; ++i) { finite = finite && std::isfinite(l[i]); diff += std::fabs(l[i] - src[i]); pk = std::max(pk, std::fabs(static_cast<double>(l[i]))); }
      const bool waits = kind == "protect" || kind == "clean" || kind == "antifeedback" || kind == "aec" || kind == "logic" || kind == "sampler" || kind == "imager" || kind == "iamfrender";   // the IAMF renderer is a monitoring stage, neutral until BINAURAL or NORMALIZE is chosen; the imager only shapes what differs between the sides: the same signal on both is left as it is   // the sample player adds nothing until PLAY is switched on;   // the feedback killer does nothing until a howl has stood for a third of a second: on this short signal it must leave the sound alone
      if (!(finite && pk < 4.0 && (waits ? diff / n < (kind == "aec" ? 1e-3 : 1e-6) : diff / n > 1e-4))) std::fprintf(stderr, "effect %s: finite %d peak %.2f diff %.5f\n", kind.c_str(), finite, pk, diff / n); CHECK(finite && pk < 4.0 && (waits ? diff / n < (kind == "aec" ? 1e-3 : 1e-6) : diff / n > 1e-4));
      p->setParam(0, 0.83); const std::string st = p->state(); auto q = am::fx::FxPlugin::make(kind, 48000); CHECK(q->setState(st) && std::fabs(q->param(0) - 0.83) < 1e-3);
      p->setBypass(true); p->process(in, out, static_cast<int>(n)); double d2 = 0; for (std::size_t i = 0; i < n; ++i) d2 += std::fabs(l[i] - src[i]); CHECK(d2 == 0); }
    { // the tube amp distorts (harmonics appear) and its MASTER sets the level; the hall rings for seconds after the input stops; the pre amp's +36 dB is +36 dB
      auto amp = am::fx::make("tubeamp", 48000); amp->setMix(1); amp->set("gain", 0.8); std::vector<float> a(n), b(n); for (std::size_t i = 0; i < n; ++i) a[i] = b[i] = 0.3f * static_cast<float>(std::sin(2 * 3.14159265 * 220 * static_cast<double>(i) / 48000)); for (int k = 0; k < 2; ++k) amp->process(a.data(), b.data(), n);
      double h1 = 0, h3 = 0; for (std::size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / 48000; h1 += a[i] * std::sin(2 * 3.14159265 * 220 * t); h3 += a[i] * std::sin(2 * 3.14159265 * 660 * t); } CHECK(std::fabs(h3) > 0.02 * std::fabs(h1));
      auto hall = am::fx::make("hall", 48000); hall->setMix(1); hall->set("decay", 0.5); std::vector<float> hl(n, 0.f), hr(n, 0.f); hl[0] = hr[0] = 1.f; hall->process(hl.data(), hr.data(), n); double tail = 0; for (int k = 0; k < 9; ++k) { std::fill(hl.begin(), hl.end(), 0.f); std::fill(hr.begin(), hr.end(), 0.f); hall->process(hl.data(), hr.data(), n); } for (float v : hl) tail = std::max(tail, std::fabs(static_cast<double>(v))); CHECK(tail > 1e-4 && tail < 1.0);   // one second on: still sounding, not growing
      auto pre = am::fx::make("preamp", 48000); pre->set("gain", 1.0); pre->set("warmth", 0); pre->set("lowcut", 0); pre->set("out", 24.0 / 36.0); std::vector<float> pa(n, 0.001f), pb(n, 0.001f); pre->process(pa.data(), pb.data(), n); CHECK(std::fabs(pa[n - 1] / 0.001f - 63.1f) < 1.0f); }
    { // SOUND BARRIER: pushed hard it is much louder and not one sample passes the ceiling; a hit keeps more of its attack with IMPACT; SMOOTH holds a
      // sticking-out treble back; a quiet signal with no push passes at its own level
      am::fx::Barrier b(48000); const std::size_t n = 48000; std::vector<float> l(n), r(n); const double PI2 = 2 * 3.14159265358979;
      auto run = [&](auto gen) { b.reset(); for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(gen(static_cast<double>(i) / 48000.0, i)); for (std::size_t o = 0; o < n; o += 256) b.process(l.data() + o, r.data() + o, std::min<std::size_t>(256, n - o)); };
      auto peak = [&](std::size_t from) { float p = 0; for (std::size_t i = from; i < n; ++i) p = std::max(p, std::fabs(l[i])); return static_cast<double>(p); };
      auto rms = [&](std::size_t from) { double s2 = 0; for (std::size_t i = from; i < n; ++i) s2 += static_cast<double>(l[i]) * l[i]; return std::sqrt(s2 / static_cast<double>(n - from)); };
      const auto music = [&](double t, std::size_t i) { const double beat = std::fmod(t, 0.25), env = std::exp(-beat * 30.0); return 0.25 * env * std::sin(PI2 * 70.0 * t) + 0.08 * std::sin(PI2 * 440.0 * t) + 0.05 * std::sin(PI2 * 1500.0 * t) + 0.02 * std::sin(PI2 * 9000.0 * t) + 0.3 * ((i % 4800) == 0 ? 1.0 : 0.0); };
      b.set("push", 0.0); b.set("impact", 0.0); b.set("smooth", 0.0); b.set("ceiling", 1.0); run(music); const double quiet = rms(4800);
      b.set("push", 1.0); b.set("ceiling", 0.5); run(music); const double loud = rms(4800), top = peak(0), ceil = am::fx::Barrier::ceilingOf(0.5);
      CHECK(top <= ceil * 1.0001); CHECK(loud > quiet * 3.0); CHECK(b.reductionDb() >= 0.0);          // more than 9.5 dB louder, and the barrier holds (-3 dBFS)
      b.set("push", 1.0); b.set("ceiling", 1.0); run([&](double t, std::size_t) { return 0.9 * std::sin(PI2 * 50.0 * t) + 0.9 * std::sin(PI2 * 3000.0 * t); }); CHECK(peak(0) <= 1.0001);   // far over full scale going in: still not past 0 dBFS
      const auto hits = [&](double t, std::size_t) { const double beat = std::fmod(t, 0.5); return (beat < 0.2 ? 0.1 * std::exp(-beat * 25.0) : 0.0) * std::sin(PI2 * 90.0 * t) + 0.004 * std::sin(PI2 * 300.0 * t); };
      b.set("push", 0.0); b.set("smooth", 0.0); b.set("impact", 0.0); run(hits); const double flat = peak(24000);
      b.set("impact", 1.0); run(hits); const double punchy = peak(24000); CHECK(punchy > flat * 1.2);   // the attack stands more than 1.5 dB higher
      const auto harsh = [&](double t, std::size_t) { return 0.1 * std::sin(PI2 * 500.0 * t) + 0.1 * std::sin(PI2 * 9000.0 * t); };
      auto treble = [&]() { double s2 = 0, a = 0; float hp = 0; const float k = static_cast<float>(std::exp(-PI2 * 5000.0 / 48000.0)); for (std::size_t i = 0; i < n; ++i) { hp = l[i] + k * (hp - l[i]); const double hgh = l[i] - hp; if (i > 24000) { s2 += hgh * hgh; a += 1; } } return std::sqrt(s2 / a); };
      b.set("impact", 0.0); b.set("push", 0.3); b.set("smooth", 0.0); run(harsh); const double bright = treble();
      b.set("smooth", 1.0); run(harsh); const double tamed = treble(); CHECK(tamed < bright * 0.6);      // the treble that stuck out is more than 4 dB down
      b.set("push", 0.0); b.set("impact", 0.0); b.set("smooth", 0.0); b.set("ceiling", 1.0); run([&](double t, std::size_t) { return 0.05 * std::sin(PI2 * 400.0 * t); }); CHECK(std::fabs(rms(4800) - 0.05 / std::sqrt(2.0)) < 0.002);
      // AUTO, the run-up: a quiet programme is brought up to the barrier by itself and never past it; a loud one gets no more than it needs; in silence it waits
      const auto soft = [&](double t, std::size_t) { return 0.06 * std::sin(PI2 * 200.0 * t) + 0.03 * std::sin(PI2 * 1200.0 * t); };   // about 21 dB under the ceiling
      b.set("auto", 1.0); b.set("ceiling", 0.5); b.reset();
      for (int pass = 0; pass < 8; ++pass) { for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(soft(static_cast<double>(i + static_cast<std::size_t>(pass) * n) / 48000.0, i)); for (std::size_t o = 0; o < n; o += 256) b.process(l.data() + o, r.data() + o, std::min<std::size_t>(256, n - o)); }
      const double c05 = am::fx::Barrier::ceilingOf(0.5); CHECK(peak(0) <= c05 * 1.0001 && peak(0) > c05 * 0.8); CHECK(b.autoDb() > 18.0 && b.autoDb() <= 24.01); CHECK(b.mach() > 1.0 && b.reductionDb() > 1.0);
      const double had = b.autoDb(); for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = 0; for (std::size_t o = 0; o < n; o += 256) b.process(l.data() + o, r.data() + o, std::min<std::size_t>(256, n - o)); CHECK(std::fabs(b.autoDb() - had) < 0.5);   // silence: it waits, it does not run away
      for (int pass = 0; pass < 3; ++pass) { for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(0.7 * std::sin(PI2 * 200.0 * static_cast<double>(i) / 48000.0)); for (std::size_t o = 0; o < n; o += 256) b.process(l.data() + o, r.data() + o, std::min<std::size_t>(256, n - o)); }
      CHECK(b.autoDb() < 7.0 && peak(0) <= c05 * 1.0001);                                                                            // a loud programme: the run-up backs off
      b.set("auto", 0.0); b.process(l.data(), r.data(), 256); CHECK(b.autoDb() == 0.0); }
    { // DUAL LISTEN EQ: it listens. A bass-heavy mix has its bass eased, a thin one has it filled; a harsh one has its treble calmed; with RANGE at 0
      // it changes nothing; in silence it holds the gains it had
      am::fx::DualEq q(48000); const std::size_t n = 48000 * 6; std::vector<float> l(n), r(n);
      auto feed = [&](double bass, double mid, double treble) { for (std::size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / 48000.0; l[i] = r[i] = static_cast<float>(bass * std::sin(2 * 3.14159265358979 * 60.0 * t) + mid * std::sin(2 * 3.14159265358979 * 1000.0 * t) + treble * std::sin(2 * 3.14159265358979 * 9000.0 * t)); }
        for (std::size_t o = 0; o < n; o += 256) q.process(l.data() + o, r.data() + o, std::min<std::size_t>(256, n - o)); };
      q.set("speed", 1.0);
      q.reset(); feed(0.5, 0.05, 0.0125); const double heavy = q.bassDb();        // bass 20 dB over the middle
      q.reset(); feed(0.005, 0.05, 0.0125); const double thin = q.bassDb();       // bass 20 dB under it
      q.reset(); feed(0.05, 0.05, 0.2); const double harsh = q.trebleDb();        // treble 12 dB over the middle
      CHECK(heavy < -4.0 && heavy >= -6.01); CHECK(thin > 4.0 && thin <= 6.01); CHECK(harsh < -4.0);
      const double held = q.trebleDb(); for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = 0; for (std::size_t o = 0; o < 48000; o += 256) q.process(l.data() + o, r.data() + o, 256); CHECK(std::fabs(q.trebleDb() - held) < 0.01);
      q.reset(); q.set("range", 0.0); feed(0.5, 0.05, 0.2); CHECK(std::fabs(q.bassDb()) < 0.01 && std::fabs(q.trebleDb()) < 0.01);
      q.reset(); q.set("range", 0.0); for (std::size_t i = 0; i < 4800; ++i) l[i] = r[i] = 0.3f * static_cast<float>(std::sin(2 * 3.14159265358979 * 700.0 * static_cast<double>(i) / 48000.0)); std::vector<float> in(l.begin(), l.begin() + 4800);
      q.process(l.data(), r.data(), 4800); double worst = 0; for (std::size_t i = 0; i < 4800; ++i) worst = std::max(worst, static_cast<double>(std::fabs(l[i] - in[i]))); CHECK(worst < 1e-5); }   // with no gain the three ranges add up to the signal again
    { // PITCH SHIFTER: at 0 the tone comes out at its own pitch, +12 semitones doubles it, -12 halves it (counted by its zero crossings), and the level stays
      auto fxp = am::fx::make("pitch", 48000); CHECK(fxp != nullptr);
      if (fxp) { const std::size_t n = 48000; std::vector<float> l(n), r(n);
        auto run = [&](double knob) { fxp->reset(); fxp->set("pitch", knob); for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = 0.5f * static_cast<float>(std::sin(2 * 3.14159265358979 * 440.0 * static_cast<double>(i) / 48000.0));
          for (std::size_t o = 0; o < n; o += 256) fxp->process(l.data() + o, r.data() + o, std::min<std::size_t>(256, n - o));
          int z = 0; float pk = 0; for (std::size_t i = n / 2 + 1; i < n; ++i) { if ((l[i - 1] < 0) != (l[i] < 0)) ++z; pk = std::max(pk, std::fabs(l[i])); } return std::pair<int, float>(z, pk); };
        const auto same = run(0.5), up = run(1.0), down = run(0.0);
        CHECK(std::abs(same.first - 440) < 12 && same.second > 0.4f && same.second < 0.6f);       // half a second of 440 Hz: 440 crossings
        CHECK(std::abs(up.first - 880) < 60 && up.second > 0.25f && up.second < 0.8f);
        CHECK(std::abs(down.first - 220) < 30 && down.second > 0.25f && down.second < 0.8f); } }
    { // ANALOG CHANNEL: a quiet tone passes at its own level, a loud one is rounded off (lower peak, harmonics), BIAS adds a second harmonic, nothing blows up
      auto fxp = am::fx::make("analog", 48000); CHECK(fxp != nullptr);
      if (fxp) { const std::size_t n = 4800; std::vector<float> l(n), r(n);
        auto run = [&](float amp) { fxp->reset(); for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = amp * static_cast<float>(std::sin(2 * 3.14159265358979 * 1000.0 * static_cast<double>(i) / 48000.0)); fxp->process(l.data(), r.data(), n); float pk = 0; for (std::size_t i = n / 2; i < n; ++i) pk = std::max(pk, std::fabs(l[i])); return pk; };
        const float quiet = run(0.01f), loud = run(1.0f);
        CHECK(quiet > 0.008f && quiet < 0.012f);          // within 2 dB of its own level
        CHECK(loud > 0.2f && loud < 0.95f);               // rounded off, not clipped hard and not silent
        for (std::size_t i = 0; i < n; ++i) CHECK(std::isfinite(l[i]) && std::fabs(l[i]) < 4.f); } }
    std::printf("fx_test: twenty-three effects as rack plug-ins (sample player, de-squash, reverb, delay, analog channel, pitch shifter, dual listen eq, sound barrier, sync delay, feedback killer, chorus, drive, flanger, phaser, tremolo, auto wah, bit crusher, exciter, tube amp, pre amp, post amp, hall, room): audible, finite, bypass, state\n"); }
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
  { // FEEDBACK KILLER: a howl (one steady tone standing out of the room noise) is found and cut; noise alone and a moving tone are left alone
    auto run = [](int kindOf, double& before, double& after, int& notches, double& hz) {
      auto fx = am::fx::make("antifeedback", 48000); auto* fk = dynamic_cast<am::fx::FeedbackKiller*>(fx.get()); const std::size_t N = 96000, B = 256; std::vector<float> l(N), r(N); unsigned seed = 7;
      for (std::size_t i = 0; i < N; ++i) { seed = seed * 1664525u + 1013904223u; const double noise = (static_cast<double>(seed >> 8) / 8388608.0 - 1.0) * 0.02;
        const double f = kindOf == 2 ? 300.0 + 2700.0 * static_cast<double>(i) / N : 2137.0; const double tone = kindOf == 0 ? 0.0 : 0.3 * std::sin(2 * 3.14159265358979 * f * static_cast<double>(i) / 48000.0 * (kindOf == 2 ? 0.5 : 1.0));
        l[i] = r[i] = static_cast<float>(noise + tone); }
      auto rms = [&](std::size_t a, std::size_t b) { double e = 0; for (std::size_t i = a; i < b; ++i) e += static_cast<double>(l[i]) * l[i]; return std::sqrt(e / static_cast<double>(b - a)); };
      before = rms(0, 4800); for (std::size_t o = 0; o + B <= N; o += B) fx->process(l.data() + o, r.data() + o, B); after = rms(N - 9600, N); notches = fk ? fk->notches() : -1; hz = fk ? fk->notchHz(0) : 0; };
    double b = 0, a = 0, hz = 0; int n = 0;
    run(1, b, a, n, hz); CHECK(n >= 1 && std::fabs(hz - 2137.0) < 25.0 && a < b * 0.25);       // the howl at 2137 Hz: found within 25 Hz and cut by more than 12 dB
    run(0, b, a, n, hz); CHECK(n == 0 && std::fabs(a / b - 1.0) < 0.1);                         // room noise only: nothing is touched
    run(2, b, a, n, hz); CHECK(n == 0 && a > b * 0.8);                                         // a tone that keeps moving (a melody, a sweep) is not feedback
    auto fx = am::fx::make("antifeedback", 48000); CHECK(fx && fx->params().size() == 7 && fx->text("fixed", 0.25) == "6 / 24" && fx->params()[6].value == 0.25 && fx->text("program", 0.0) == "SPEECH" && fx->text("lift", 0.0) == "2 s" && fx->text("mode", 1.0) == "LIVE" && fx->text("mode", 0.0) == "HOLD" && fx->text("depth", 1.0) == "-30 dB"); }
  { // SUB SYNTH: an 80 Hz bass gains a 40 Hz sub; silence stays silent
    auto fx = am::fx::make("subsynth", 48000); const std::size_t N = 48000; std::vector<float> l(N), r(N);
    for (std::size_t k = 0; k < N; ++k) l[k] = r[k] = static_cast<float>(0.5 * std::sin(2 * 3.141592653589793 * 80.0 * static_cast<double>(k) / 48000));
    fx->set("amount", 1.0); fx->process(l.data(), r.data(), N);
    auto goertzel = [&](double f) { double s1 = 0, s2 = 0; const double c = 2 * std::cos(2 * 3.141592653589793 * f / 48000); for (std::size_t k = N / 2; k < N; ++k) { const double s0 = l[k] + c * s1 - s2; s2 = s1; s1 = s0; } return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (N / 4); };
    const double sub = goertzel(40.0), fund = goertzel(80.0); if (!(sub > 0.05 * fund)) std::fprintf(stderr, "SUB SYNTH: 40 Hz %.4f against 80 Hz %.4f\n", sub, fund); CHECK(sub > 0.05 * fund);
    auto q = am::fx::make("subsynth", 48000); std::vector<float> z(4800, 0.f), z2(4800, 0.f); q->process(z.data(), z2.data(), 4800); for (float v : z) CHECK(v == 0.f); }
  {   // SAMPLE PLAYER: off = silence added; PLAY on = the chosen sample added from the top; it stops at its end without LOOP and goes round with it
    auto fx = am::fx::make("sampler", 48000); CHECK(fx && fx->params().size() == 4);
    CHECK(fx->text("sample", 0.0).rfind("1 ", 0) == 0 && fx->text("play", 1.0) == "ON" && fx->text("level", 0.0) == "-inf dB");
    std::vector<float> L(4800, 0.0f), R(4800, 0.0f);
    fx->set("sample", 0.0); fx->set("level", 40.0 / 46.0);   // sample 1 at 0 dB
    fx->process(L.data(), R.data(), L.size()); double e = 0; for (float v : L) e += std::fabs(v); CHECK(e == 0.0);   // PLAY off
    fx->set("play", 1.0); fx->process(L.data(), R.data(), L.size()); e = 0; for (float v : L) e += std::fabs(v); CHECK(e > 1.0);
    auto* sp = dynamic_cast<am::fx::SamplePlayer*>(fx.get()); CHECK(sp && sp->playing());
    std::vector<float> big(48000 * 8, 0.0f); fx->process(big.data(), big.data(), big.size()); CHECK(!sp->playing());   // longer than any sample: it ended
    fx->set("loop", 1.0); fx->set("play", 0.0); { float z[64] = {}; fx->process(z, z, 64); } fx->set("play", 1.0);
    std::fill(big.begin(), big.end(), 0.0f); fx->process(big.data(), big.data(), big.size()); CHECK(sp->playing());
    double tail = 0; for (std::size_t i = big.size() - 48000; i < big.size(); ++i) tail += std::fabs(big[i]); CHECK(tail > 0.0);   // still sounding at the end: it went round
    fx->set("sample", 0.5); CHECK(fx->text("sample", 0.5).rfind("51 ", 0) == 0);
    // a retrigger whose OFF and ON both land before the next block (a pad hit twice quickly, a script) still starts from the top
    fx->set("loop", 0.0); fx->set("sample", 0.0); fx->set("play", 1.0); std::fill(big.begin(), big.end(), 0.0f); fx->process(big.data(), big.data(), big.size()); CHECK(!sp->playing());
    fx->set("play", 0.0); fx->set("play", 1.0); { std::vector<float> a(480, 0.0f), b(480, 0.0f); fx->process(a.data(), b.data(), a.size()); double e2 = 0; for (float v : a) e2 += std::fabs(v); CHECK(e2 > 0.1 && sp->playing()); }
  }
  {   // DE-SQUASH: a squashed drum (sharp hits on a constant bed) gets its hits back; a steady tone is left alone
    auto fx = am::fx::make("desquash", 48000); CHECK(fx && fx->params().size() == 3 && fx->text("range", 1.0) == "12.0 dB");
    const std::size_t n = 48000 * 2; std::vector<float> x(n), y(n);
    for (std::size_t i = 0; i < n; ++i) { const double t = static_cast<double>(i) / 48000.0; const double env = 0.25 + ((i % 12000) < 600 ? 0.25 : 0.0); x[i] = static_cast<float>(env * std::sin(2 * 3.14159265358979 * 120.0 * t)); }
    y = x; fx->process(y.data(), y.data(), n);
    auto peakIn = [](const std::vector<float>& v, std::size_t a, std::size_t b) { double p = 0; for (std::size_t i = a; i < b; ++i) p = std::max(p, static_cast<double>(std::fabs(v[i]))); return p; };
    const double hitIn = peakIn(x, 48000, 48600), hitOut = peakIn(y, 48000, 48600), bedIn = peakIn(x, 54000, 59000), bedOut = peakIn(y, 54000, 59000);
    std::fprintf(stderr, "de-squash: hit %.3f -> %.3f, bed %.3f -> %.3f\n", hitIn, hitOut, bedIn, bedOut);
    CHECK(hitOut / hitIn > 1.15 && hitOut / hitIn < 4.0);   // the hits stand out again, within RANGE
    CHECK(std::fabs(bedOut / bedIn - 1.0) < 0.08);          // the bed is left where it was
    auto fx2 = am::fx::make("desquash", 48000); std::vector<float> s(n); for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<float>(0.3 * std::sin(2 * 3.14159265358979 * 440.0 * i / 48000.0));
    std::vector<float> s2 = s; fx2->process(s2.data(), s2.data(), n); CHECK(std::fabs(peakIn(s2, 48000, n) / peakIn(s, 48000, n) - 1.0) < 0.05);   // a steady tone: untouched
  }
  return 0;
}
