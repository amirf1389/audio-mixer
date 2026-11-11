// AUX SENDS: pre / post sends, bus level, mute, return into the master, the bus's own output.  bus_test  (exit 0 = all good)
#include "audiomixer/engine.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
#include "audiomixer/master_chain.hpp"
static int fails = 0;
#define CHECK(c, w) do { if (!(c)) { std::fprintf(stderr, "FAIL %s (%d)\n", w, __LINE__); ++fails; } } while (0)
static bool near(double a, double b, double t) { return std::fabs(a - b) <= t; }
int main() {
  const double fs = 48000; const std::size_t n = 4800;
  std::vector<float> s0(n), s1(n, 0.0f), L(n), R(n), bL(n), bR(n), zL(n), zR(n);
  for (std::size_t i = 0; i < n; ++i) s0[i] = 0.5f * static_cast<float>(std::sin(2 * 3.14159265358979 * 1000 * static_cast<double>(i) / fs));   // -6 dB sine
  const float* in[2] = {s0.data(), s1.data()}; float* bus[2 * am::kMaxBuses] = {}; bus[0] = bL.data(); bus[1] = bR.data(); bus[2] = zL.data(); bus[3] = zR.data();
  auto peak = [](const std::vector<float>& v) { double p = 0; for (float x : v) p = std::max(p, std::fabs(static_cast<double>(x))); return p; };
  const double c45 = std::cos(3.14159265358979 / 4);
  // 1. no sends: buses silent, master as before
  { am::Mixer m(2, fs); m.process(in, n, L.data(), R.data(), bus); CHECK(peak(bL) == 0 && peak(bR) == 0, "no sends -> silent buses"); CHECK(near(peak(L), 0.5 * c45, 1e-3), "master unchanged"); }
  // 2. a pre-fader send at 0 dB ignores the strip gain and mute; a post-fader send follows both
  { am::Mixer m(2, fs); m.setGainDb(0, -6); m.setSend(0, 0, 0, true); m.setSend(0, 1, 0, false);
    m.process(in, n, L.data(), R.data(), bus);
    CHECK(near(peak(bL), 0.5 * c45, 1e-3), "pre send: input level, panned centre"); CHECK(near(peak(zL), 0.25 * c45, 2e-3), "post send: after the -6 dB fader");
    CHECK(near(m.bus(0).meter.state().pk, 0.5 * c45, 1e-3), "bus meter reads the bus");
    m.setMute(0, true); m.process(in, n, L.data(), R.data(), bus);
    CHECK(near(peak(bL), 0.5 * c45, 1e-3), "pre send survives the mute"); CHECK(peak(zL) == 0, "post send is muted with the strip"); CHECK(peak(L) == 0, "master muted"); }
  // 3. bus level, bus mute, return into the master
  { am::Mixer m(2, fs); m.setSend(0, 0, 0, true); m.setBusLevel(0, -6); m.process(in, n, L.data(), R.data(), bus);
    CHECK(near(peak(bL), 0.25 * c45, 2e-3), "bus level -6 dB");
    m.setBusMute(0, true); m.process(in, n, L.data(), R.data(), bus); CHECK(peak(bL) == 0, "bus mute");
    m.setBusMute(0, false); m.setBusLevel(0, 0); m.setMute(0, true); m.setBusReturn(0, 0); m.process(in, n, L.data(), R.data(), bus);
    CHECK(near(peak(L), 0.5 * c45, 1e-3), "a muted strip is still heard through the bus RETURN"); }
  // 4. a send below -60 dB is off; the bus output pointers may be null
  { am::Mixer m(2, fs); m.setSend(0, 0, -70, true); m.process(in, n, L.data(), R.data(), nullptr); CHECK(m.bus(0).meter.state().pk == 0, "-70 dB send is off"); }
  // 5. SURROUND 5.0 (process5): a centred front strip (depth +1) lands in L / R and the centre; depth -1 goes to the rears only; a pan hard left leaves the centre
  { am::Mixer m(2, fs); std::vector<float> o[5]; for (auto& v : o) v.assign(n, 0.f); float* outs[5] = {o[0].data(), o[1].data(), o[2].data(), o[3].data(), o[4].data()};
    m.setDepth(0, 1); m.process5(in, n, outs); CHECK(near(peak(o[0]), peak(o[1]), 1e-3) && peak(o[0]) > 0.25, "5.0 front L = R (centre takes a share, constant power)"); CHECK(peak(o[2]) > 0.3, "5.0 centre carries a centred strip"); CHECK(peak(o[3]) == 0 && peak(o[4]) == 0, "5.0 depth +1: rears silent");
    m.setDepth(0, -1); m.process5(in, n, outs); CHECK(peak(o[0]) == 0 && peak(o[2]) == 0, "5.0 depth -1: fronts silent"); CHECK(near(peak(o[3]), 0.5 * c45, 2e-3), "5.0 rear Ls");
    m.setDepth(0, 0); m.setPan(0, -1); m.process5(in, n, outs); CHECK(peak(o[2]) == 0, "5.0 hard-left pan leaves the centre"); CHECK(peak(o[0]) > peak(o[1]), "5.0 hard left favours L"); CHECK(near(m.depth(0), 0, 1e-9), "depth getter"); }
  // 6. 360 AUDIO: straight ahead = the centre pan; at +90 degrees the right ear is louder and the left ear hears it about 0.65 ms later; 2 m = half
  { am::Mixer m(2, fs); m.setSpatial(0, true, 0, 0, 1, 0); m.process(in, n, L.data(), R.data()); CHECK(near(peak(L), 0.5 * c45, 2e-3) && near(peak(R), peak(L), 1e-6), "360: front = centre");
    m.setSpatial(0, true, 0, 0, 2, 0); m.process(in, n, L.data(), R.data()); m.process(in, n, L.data(), R.data()); CHECK(near(peak(L), 0.25 * c45, 2e-3), "360: 2 m is half");
    am::Mixer q(2, fs); std::vector<float> ck(n, 0.f); ck[100] = 1.f; const float* cin[2] = {ck.data(), s1.data()}; q.setSpatial(0, true, 90, 0, 1, 0); q.process(cin, n, L.data(), R.data());
    std::size_t pl = 0, pr = 0; for (std::size_t i = 0; i < n; ++i) { if (std::fabs(L[i]) > std::fabs(L[pl])) pl = i; if (std::fabs(R[i]) > std::fabs(R[pr])) pr = i; }
    CHECK(pr == 100 && pl > pr + 25 && pl < pr + 40, "360: the far ear is 0.5 - 0.8 ms late"); CHECK(std::fabs(R[pr]) > 2 * std::fabs(L[pl]), "360: the far ear is quieter and duller");
    CHECK(q.spatial(0).on && !q.spatial(1).on, "360 getter"); }
  // 7. MASTER CHAIN: nothing switched on = untouched; +6 dB at 1 kHz on the program EQ doubles the 1 kHz sine; the mastering compressor pulls a loud master down and reports it
  { am::Mixer m(2, fs); am::MasterChain mc; m.setMasterProcessor(&mc); m.process(in, n, L.data(), R.data()); CHECK(near(peak(L), 0.5 * c45, 1e-6), "master chain idle = untouched");
    am::MasterChain::Params p; p.fs = fs; p.eq[5] = {am::dsp::FilterType::Peaking, 1000, 1.41, 6}; mc.set(p); m.process(in, n, L.data(), R.data()); m.process(in, n, L.data(), R.data()); CHECK(near(peak(L), 0.5 * c45 * 1.995, 0.01), "program EQ +6 dB at 1 kHz");
    p.eq[5].type = am::dsp::FilterType::Off; p.on = true; p.thrDb = -20; p.ratio = 4; p.attackMs = 1; mc.set(p); m.process(in, n, L.data(), R.data()); m.process(in, n, L.data(), R.data());
    CHECK(peak(L) < 0.5 * c45 * 0.7 && mc.reductionDb() > 3, "mastering compressor reduces and reports"); p.on = false; mc.set(p); m.process(in, n, L.data(), R.data()); CHECK(near(peak(L), 0.5 * c45, 1e-6), "mastering off = untouched"); }
  // 8. LIMITER METER and the extra mastering modules: +12 dB into a -6 dBFS ceiling shows about 9 dB of reduction and holds the ceiling; BASS MONO removes a 60 Hz side signal; the DE-ESSER pulls an 8 kHz tone down and leaves 1 kHz alone
  { am::Mixer m(2, fs); m.setGainDb(0, 10); m.setMasterDb(5); m.setLimiter(true, -6, 100); m.process(in, n, L.data(), R.data()); m.process(in, n, L.data(), R.data());
    CHECK(peak(L) <= 0.5012 + 1e-4, "limiter holds the ceiling"); CHECK(m.limiterReductionDb() > 5 && m.limiterReductionDb() < 12, "limiter reduction meter"); m.setLimiter(false, -6, 100); m.process(in, n, L.data(), R.data()); CHECK(m.limiterReductionDb() == 0, "limiter off: no reduction");
    auto run = [&](double hz, double pan, am::MasterChain::Params p) { am::Mixer q(2, fs); am::MasterChain c; q.setMasterProcessor(&c); p.fs = fs; p.on = true; p.toneOn = p.compOn = p.satOn = false; c.set(p); std::vector<float> t(n); for (std::size_t i = 0; i < n; ++i) t[i] = 0.5f * static_cast<float>(std::sin(2 * 3.14159265358979 * hz * static_cast<double>(i) / fs)); const float* ti[2] = {t.data(), s1.data()}; q.setPan(0, pan); for (int k = 0; k < 4; ++k) q.process(ti, n, L.data(), R.data()); return std::make_pair(peak(L), peak(R)); };
    am::MasterChain::Params off, bm; bm.bassMonoOn = true; const auto a = run(60, -1, off), b = run(60, -1, bm); CHECK(a.second < 0.01 && b.second > 0.15 && near(b.first, b.second, 0.06), "bass mono centres a hard-left 60 Hz");
    am::MasterChain::Params de; de.deessOn = true; de.deessThrDb = -30; const auto h0 = run(8000, 0, off), h1 = run(8000, 0, de), m0 = run(1000, 0, off), m1 = run(1000, 0, de); CHECK(h1.first < h0.first * 0.85, "de-esser reduces 8 kHz"); CHECK(near(m1.first, m0.first, 0.01), "de-esser leaves 1 kHz"); }
  { // the master chain's mid bell and the compressor's parallel mix (the VOLUME ENHANCER's MID and COMP MIX)
    auto lvl = [](am::MasterChain::Params p, double hz, double amp) { am::MasterChain c; p.fs = 48000; p.on = true; c.set(p); const std::size_t N = 24000; std::vector<float> l(N), r(N);
      for (std::size_t i = 0; i < N; ++i) l[i] = r[i] = static_cast<float>(amp * std::sin(2 * 3.14159265358979 * hz * static_cast<double>(i) / 48000.0)); c.process(l.data(), r.data(), N); double pk = 0; for (std::size_t i = N / 2; i < N; ++i) pk = std::max(pk, std::fabs(static_cast<double>(l[i]))); return pk; };
    am::MasterChain::Params flat; flat.compOn = false; flat.satOn = false; am::MasterChain::Params mid = flat; mid.midDb = 6;
    CHECK(near(lvl(mid, 1000, 0.1) / lvl(flat, 1000, 0.1), std::pow(10.0, 6.0 / 20), 0.03), "mid bell +6 dB at 1 kHz"); CHECK(near(lvl(mid, 60, 0.1) / lvl(flat, 60, 0.1), 1.0, 0.03) && near(lvl(mid, 12000, 0.1) / lvl(flat, 12000, 0.1), 1.0, 0.05), "mid bell leaves the ends alone");
    am::MasterChain::Params c1; c1.satOn = false; c1.thrDb = -20; c1.ratio = 4; am::MasterChain::Params c0 = c1; c0.compMix = 0; am::MasterChain::Params ch = c1; ch.compMix = 0.5;
    const double full = lvl(c1, 1000, 0.5), none = lvl(c0, 1000, 0.5), half = lvl(ch, 1000, 0.5);
    CHECK(near(none, 0.5, 0.005) && full < 0.25 && half > full + 0.05 && half < none - 0.05, "compressor mix: 0 = untouched, 1 = compressed, 0.5 in between"); }
  if (fails) return 1; std::printf("bus_test: ok (incl. 5.0 surround, 360 audio, master chain, limiter meter, de-esser, bass mono)\n"); return 0;
}
