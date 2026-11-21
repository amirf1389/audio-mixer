// DUCKER · VOICE OVER MUSIC and the AUTO DCA classifier, on synthetic signals: a speech-like burst (band-limited noise, syllables
// with gaps) over steady broadband music: depth, attack / hold / release timings, no steps in the gain, voice lift, key mute;
// the classifier tells speech-like bursts, broadband music, a plucked bass and silence apart; and through the Runtime's queue.
#include <cmath>
#include <cstdio>
#include <vector>

#include "audiomixer/classify.hpp"
#include "audiomixer/ducker.hpp"
#include "audiomixer/engine.hpp"
#include "audiomixer/runtime.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;
static const double kFs = 48000, kPi2 = 6.283185307179586;
static unsigned rngS = 12345u; static double noise() { rngS = rngS * 1664525u + 1013904223u; return (static_cast<double>(rngS >> 8) / 8388608.0) - 1.0; }
struct Bq { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0; double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
static Bq bq(double f, bool high) { const double w = kPi2 * f / kFs, c = std::cos(w), al = std::sin(w) / 1.414, a0 = 1 + al; Bq q{}; if (high) { q.b0 = (1 + c) / 2 / a0; q.b1 = -(1 + c) / a0; } else { q.b0 = (1 - c) / 2 / a0; q.b1 = (1 - c) / a0; } q.b2 = q.b0; q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0; return q; }
// speech-like: noise 400 .. 2500 Hz (two HP, two LP) in syllables of 4 Hz: 160 ms sounding (half-sine) then 90 ms of gap
static std::vector<float> speech(double sec, double amp) { std::vector<float> v(static_cast<std::size_t>(sec * kFs)); Bq h1 = bq(400, true), h2 = bq(400, true), l1 = bq(2500, false), l2 = bq(2500, false);
  for (std::size_t i = 0; i < v.size(); ++i) { const double t = std::fmod(i / kFs, 0.25); const double env = t < 0.16 ? std::sin(3.14159265 * t / 0.16) : 0.0; v[i] = static_cast<float>(amp * env * l2.run(l1.run(h2.run(h1.run(noise()))))); } return v; }
// music: a chord of harmonic tones (110 / 165 / 220 Hz, 12 harmonics) plus a hiss of broadband noise, steady
static std::vector<float> music(double sec, double amp) { std::vector<float> v(static_cast<std::size_t>(sec * kFs));
  for (std::size_t i = 0; i < v.size(); ++i) { double x = 0; for (double f0 : {110.0, 165.0, 220.0}) for (int k = 1; k <= 12; ++k) x += std::sin(kPi2 * f0 * k * i / kFs) / k; v[i] = static_cast<float>(amp * (x / 8 + 0.25 * noise())); } return v; }
static std::vector<float> bass(double sec, double amp) { std::vector<float> v(static_cast<std::size_t>(sec * kFs));
  for (std::size_t i = 0; i < v.size(); ++i) { const double t = std::fmod(i / kFs, 0.5); v[i] = static_cast<float>(amp * std::exp(-t * 9) * std::sin(kPi2 * 55 * i / kFs)); } return v; }
static FeatureSummary measure(const std::vector<float>& x, const std::vector<float>* partner = nullptr) { ChannelFeatures f; f.reset(kFs); for (std::size_t i = 0; i < x.size(); i += 256) f.add(x.data() + i, std::min<std::size_t>(256, x.size() - i), partner ? partner->data() + i : nullptr); return f.summary(); }

int main() {
  // 1. the envelope alone: a key that switches on at 0.5 s and off at 1.5 s
  { Ducker d; DuckParams p; p.on = true; p.liftDb = 3; d.set(p, kFs); std::vector<double> g(static_cast<std::size_t>(4 * kFs)); double maxStep = 0, prev = 0;
    for (std::size_t i = 0; i < g.size(); ++i) { const double t = i / kFs; const double key = t >= 0.5 && t < 1.5 ? 0.3 : 0.0; g[i] = 20 * std::log10(d.process(key)); if (i) maxStep = std::max(maxStep, std::fabs(g[i] - prev)); prev = g[i]; }
    auto at = [&](double t) { return g[static_cast<std::size_t>(t * kFs)]; };
    std::printf("ducker: before %.2f, +20 ms %.2f, +100 ms %.2f, end %.2f, hold +250 ms %.2f, +300+600 ms %.2f, +300+2000 ms %.2f dB, largest step %.4f dB\n", at(0.49), at(0.52), at(0.6), at(1.49), at(1.75), at(2.4), at(3.8), maxStep);
    CHECK(std::fabs(at(0.49)) < 1e-9);
    CHECK(at(0.52) < -7.0 && at(0.52) > -8.5);   // one attack time constant: 63 % of the way to -12
    CHECK(at(0.6) < -11.9);                       // five time constants: there
    CHECK(at(1.75) < -11.9);                      // the hold keeps it down
    CHECK(at(2.4) > -12 * 0.40 && at(2.4) < -12 * 0.33);   // one release time constant after the hold: 37 % of the depth left
    CHECK(at(3.8) > -0.5);                        // back
    CHECK(maxStep < 0.05);                        // smooth: no sample-to-sample step a click could come from
    DuckParams q = p; q.depthDb = -20; q.attackMs = 5; d.set(q, kFs); for (int i = 0; i < 4800; ++i) d.process(0.5); CHECK(d.gainDb() < -19.9); CHECK(std::fabs(20 * std::log10(d.lift()) - 3) < 0.01);
    q.on = false; d.set(q, kFs); CHECK(d.process(0.5) == 1.0 && d.gainDb() == 0); }
  // 2. in the Mixer: voice burst (strip 0, key) over music (strip 1, target)
  { Mixer m(4, kFs); m.setLimiter(false); const auto mu = music(5.5, 0.3); auto vo = std::vector<float>(mu.size(), 0.f); { const auto b = speech(1.5, 0.6); std::copy(b.begin(), b.end(), vo.begin() + static_cast<long>(kFs)); }
    DuckParams p; p.on = true; p.thresholdDb = -30; p.liftDb = 4; m.setDucker(p); m.setDuckRole(0, 1); m.setDuckRole(1, 2);
    std::vector<float> z(256, 0.f), L(256), R(256); double minG = 0, gAt = 0, gEnd = 0;
    for (std::size_t i = 0; i + 256 <= mu.size(); i += 256) { const float* in[4] = {vo.data() + i, mu.data() + i, z.data(), z.data()}; m.process(in, 256, L.data(), R.data());
      minG = std::min(minG, m.duckGainDb()); if (i == static_cast<std::size_t>(1.8 * kFs) / 256 * 256) gAt = m.duckGainDb(); gEnd = m.duckGainDb(); }
    std::printf("mixer ducker: deepest %.2f dB, during the speech %.2f dB, at the end %.2f dB\n", minG, gAt, gEnd);
    CHECK(minG < -11.9 && gAt < -11.0 && gEnd > -0.5);   // the gaps between syllables (90 ms) sit inside the hold: no pumping
    m.setDuckRole(0, 0); m.setDuckRole(1, 0); }
  // the music strip's own meter shows the duck; a muted key ducks nothing
  { Mixer m(2, kFs); m.setLimiter(false); DuckParams p; p.on = true; m.setDucker(p); m.setDuckRole(0, 1); m.setDuckRole(1, 2); m.setMute(0, true);
    const auto vo = speech(1, 0.6); const auto mu = music(1, 0.3); std::vector<float> L(256), R(256);
    for (std::size_t i = 0; i + 256 <= vo.size(); i += 256) { const float* in[2] = {vo.data() + i, mu.data() + i}; m.process(in, 256, L.data(), R.data()); }
    CHECK(std::fabs(m.duckGainDb()) < 1e-9); }
  // 3. the classifier
  { const auto sp = speech(4, 0.5), mu = music(4, 0.3), ba = bass(4, 0.5); std::vector<float> si(static_cast<std::size_t>(4 * kFs), 0.f);
    const FeatureSummary fs = measure(sp), fm = measure(mu), fb = measure(ba), fz = measure(si);
    const ChannelVerdict vs = classifyChannel(fs, {true, false, "CH 1"}), vm = classifyChannel(fm, {true, false, "CH 2"}), vb = classifyChannel(fb, {true, false, "CH 3"}), vz = classifyChannel(fz, {true, false, "CH 4"});
    std::printf("speech: %s (%s)\nmusic: %s (%s)\nbass: %s (%s)\nsilence: %s\n", channelClassName(vs.cls), vs.why.c_str(), channelClassName(vm.cls), vm.why.c_str(), channelClassName(vb.cls), vb.why.c_str(), channelClassName(vz.cls));
    CHECK(vs.cls == ChannelClass::Voice && vm.cls == ChannelClass::Music && vb.cls == ChannelClass::Instrument && vz.cls == ChannelClass::Silent);
    CHECK(fs.voiceRatio > 0.7 && fs.pauseFrac > 0.15 && fm.pauseFrac < 0.06 && fm.voiceRatio < 0.5);
    CHECK(classifyChannel(fm, {false, false, "SPOTIFY"}).cls == ChannelClass::Music && classifyChannel(fs, {true, false, "LEAD VOX"}).cls == ChannelClass::Voice);
    CHECK(classifyChannel(fm, {true, false, "GUITAR"}).cls == ChannelClass::Music || classifyChannel(fm, {true, false, "GUITAR"}).instrument >= 4);
    { static const char* const w[] = {"MIC", nullptr}; CHECK(nameHas("Mic 2", w) && !nameHas("MICHAEL", w)); }
    // a true stereo pair (left / right not the same) pulls toward music
    const auto mu2 = music(4, 0.3); std::vector<float> r(mu2.size()); for (std::size_t i = 0; i < r.size(); ++i) r[i] = static_cast<float>(0.3 * noise()); const FeatureSummary st = measure(mu2, &r);
    CHECK(st.stereoCorr < 0.9 && classifyChannel(st, {true, true, ""}).music > classifyChannel(fm, {true, false, ""}).music);
    // deterministic: the same input gives the same numbers
    const FeatureSummary fs2 = measure(sp); CHECK(fs2.voiceRatio == fs.voiceRatio && fs2.pauseFrac == fs.pauseFrac); }
  // 4. the Mixer's analysis and the Runtime's command queue (clocked by hand, no device)
  { Runtime rt(4); const auto sp = speech(3, 0.5), mu = music(3, 0.3); std::vector<float> z(256, 0.f);
    rt.startAnalysis(); CHECK(!rt.analysisReady());
    Mixer& m = rt.mixerForTest(); m.setAnalysis(true);   // what the queued command does on the audio thread
    std::vector<float> L(256), R(256); for (std::size_t i = 0; i + 256 <= sp.size(); i += 256) { const float* in[4] = {sp.data() + i, mu.data() + i, z.data(), z.data()}; m.process(in, 256, L.data(), R.data()); }
    m.setAnalysis(false);
    const ChannelVerdict a = classifyChannel(m.features(0).summary(), {}), b = classifyChannel(m.features(1).summary(), {}), c = classifyChannel(m.features(2).summary(), {});
    CHECK(a.cls == ChannelClass::Voice && b.cls == ChannelClass::Music && c.cls == ChannelClass::Silent); }
  std::printf("ducker_test: envelope timings and depth, smooth gain, voice lift, key mute, classifier VOICE / MUSIC / INSTRUMENT / SILENT, analysis OK\n");
  return 0;
}
