// W-SHAPE TUNER: the additions (movable bands, LOUD COMP, AUTO W, GAIN MATCH, STEREO / MID / SIDE, TRAIN) and that an old saved
// state (only the first eight controls) still sounds exactly as it did.
#include "fx/fx.hpp"
#include "fx/presets.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
using namespace am::fx;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++fails; } } while (0)
static const double kFs = 48000, kPi = 3.14159265358979323846;

static double magDb(const Biquad& q, double f) { const double w = 2 * 3.14159265358979 * f / kFs, cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
  const double nr = q.b0 + q.b1 * cr + q.b2 * c2r, ni = q.b1 * ci + q.b2 * c2i, dr = 1 + q.a1 * cr + q.a2 * c2r, di = q.a1 * ci + q.a2 * c2i; return 10 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-12); }
// THE OLD W-SHAPE (before the additions), written out again: five fixed bands, pink-weighted compensation, OUTPUT
struct OldW { Biquad f[2][5]; double g = 1;
  explicit OldW(const double* k) { const double a = k[0], q = 1.4 - k[6] * 0.9;
    for (int c = 0; c < 2; ++c) { f[c][0].design(Biquad::LowShelf, kFs, 90, 0.6, a * k[1] * 9); f[c][1].design(Biquad::Peak, kFs, 350, q, -a * k[2] * 6); f[c][2].design(Biquad::Peak, kFs, 1800, q, a * k[3] * 6);
      f[c][3].design(Biquad::Peak, kFs, 5000, q * 1.2, -a * k[4] * 6); f[c][4].design(Biquad::HighShelf, kFs, 10000, 0.6, a * k[5] * 9); }
    double sum = 0; int n = 0; for (double fr = 40; fr <= 16000; fr *= std::pow(2.0, 1.0 / 3)) { double d = 0; for (int i = 0; i < 5; ++i) d += magDb(f[0][i], fr); sum += std::pow(10.0, d / 10); ++n; }
    g = std::pow(10.0, (-10 * std::log10(sum / n) + (k[7] - 0.5) * 24) / 20); }
  void process(float* L, float* R, std::size_t n) { float* ch[2] = {L, R}; for (int c = 0; c < 2; ++c) for (std::size_t i = 0; i < n; ++i) { float x = ch[c][i]; for (int b = 0; b < 5; ++b) x = f[c][b].run(x); ch[c][i] = static_cast<float>(x * g); } } };

template <class P> static void runBlocks(P& p, std::vector<float>& L, std::vector<float>& R, std::size_t blk = 256) { for (std::size_t i = 0; i < L.size(); i += blk) p.process(L.data() + i, R.data() + i, std::min(blk, L.size() - i)); }
static double rmsDb(const std::vector<float>& x, std::size_t from) { double e = 0; for (std::size_t i = from; i < x.size(); ++i) e += static_cast<double>(x[i]) * x[i]; return 10 * std::log10(e / static_cast<double>(x.size() - from) + 1e-30); }
static void sine(std::vector<float>& L, std::vector<float>& R, double f, double a, std::size_t n) { L.resize(n); R.resize(n); for (std::size_t i = 0; i < n; ++i) L[i] = R[i] = static_cast<float>(a * std::sin(2 * kPi * f * static_cast<double>(i) / kFs)); }
struct Lcg { std::uint32_t s; explicit Lcg(std::uint32_t seed) : s(seed) {} double next() { s = s * 1664525u + 1013904223u; return (static_cast<double>(s >> 8) / 16777216.0) * 2 - 1; } };
static void pink(std::vector<float>& L, std::vector<float>& R, std::size_t n, double gain, std::uint32_t seed) {   // Paul Kellet's pink filter on white noise
  Lcg r(seed); double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0; L.resize(n); R.resize(n);
  for (std::size_t i = 0; i < n; ++i) { const double w = r.next(); b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
    b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980; const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926; L[i] = R[i] = static_cast<float>(p * gain); } }
static std::unique_ptr<WShape> fresh() { return std::make_unique<WShape>(kFs); }

int main() {
  const double testHz[] = {30, 60, 90, 150, 250, 350, 500, 1000, 1800, 2500, 3500, 5000, 7000, 10000, 14000, 18000};
  { // 1. DEFAULTS = THE OLD SOUND: the new effect at its defaults (an old state: only the first eight controls) against the old one, within 0.01 dB
    const double k[8] = {0.6, 0.55, 0.4, 0.4, 0.4, 0.5, 0.6, 0.5}; double worst = 0; bool exact = true;
    for (double f : testHz) { std::vector<float> L, R, L2, R2; sine(L, R, f, 0.25, 24000); L2 = L; R2 = R;
      auto w = fresh(); const char* ids[8] = {"amount", "bass", "lowmid", "presence", "upper", "air", "smooth", "output"}; for (int i = 0; i < 8; ++i) w->set(ids[i], k[i]);   // what an old saved state sets
      OldW o(k); runBlocks(*w, L, R); runBlocks(o, L2, R2);
      worst = std::max(worst, std::fabs(rmsDb(L, 12000) - rmsDb(L2, 12000))); for (std::size_t i = 0; i < L.size(); ++i) if (L[i] != L2[i] || R[i] != R2[i]) exact = false; }
    CHECK(worst < 0.01, "defaults differ from the old W-SHAPE by %f dB", worst);
    // and a full W (every gain at the top) also matches
    const double kf[8] = {1, 1, 1, 1, 1, 1, 0.3, 0.6}; double worst2 = 0;
    for (double f : testHz) { std::vector<float> L, R, L2, R2; sine(L, R, f, 0.25, 24000); L2 = L; R2 = R; auto w = fresh(); const char* ids[8] = {"amount", "bass", "lowmid", "presence", "upper", "air", "smooth", "output"};
      for (int i = 0; i < 8; ++i) w->set(ids[i], kf[i]); OldW o(kf); runBlocks(*w, L, R); runBlocks(o, L2, R2); worst2 = std::max(worst2, std::fabs(rmsDb(L, 12000) - rmsDb(L2, 12000))); }
    CHECK(worst2 < 0.01, "a full W differs from the old one by %f dB", worst2);
    std::printf("wshape_test: defaults reproduce the old response (worst %.5f dB, full W %.5f dB, sample-exact %s)\n", worst, worst2, exact ? "yes" : "no"); }
  { // 2. MOVABLE BANDS: the knob's centre is the old place; the ends are the ranges; a moved PRESENCE peaks where it is set
    CHECK(WShape::bandHz(0, 0.5) == 90 && WShape::bandHz(1, 0.5) == 350 && WShape::bandHz(2, 0.5) == 1800 && WShape::bandHz(3, 0.5) == 5000 && WShape::bandHz(4, 0.5) == 10000, "centres");
    CHECK(std::fabs(WShape::bandHz(0, 0) - 40) < 1e-9 && std::fabs(WShape::bandHz(0, 1) - 200) < 1e-9 && std::fabs(WShape::bandHz(2, 0) - 1000) < 1e-9 && std::fabs(WShape::bandHz(4, 1) - 16000) < 1e-6, "ends");
    for (int b = 0; b < 5; ++b) for (double v : {0.1, 0.3, 0.7, 0.9}) CHECK(std::fabs(WShape::bandKnob(b, WShape::bandHz(b, v)) - v) < 1e-9, "knob <-> Hz band %d", b);
    auto peakOf = [&](int band, const char* gainId, const char* freqId, double hzSet, double lo, double hi) {
      auto w = fresh(); for (const char* id : {"bass", "lowmid", "presence", "upper", "air"}) w->set(id, 0); w->set("amount", 1); w->set(gainId, 1); w->set("gainmatch", 0); w->set(freqId, WShape::bandKnob(band, hzSet));
      double best = -1e9, at = 0; for (double f = lo; f <= hi; f *= std::pow(2.0, 1.0 / 24)) { std::vector<float> L, R; sine(L, R, f, 0.2, 9600); runBlocks(*w, L, R); const double d = std::fabs(rmsDb(L, 4800) - 20 * std::log10(0.2 / std::sqrt(2.0)));
        if (d > best) { best = d; at = f; } w->reset(); } return at; };
    const double p = peakOf(2, "presence", "fpresence", 3000, 900, 5000), d1 = peakOf(1, "lowmid", "flowmid", 600, 150, 1200), d2 = peakOf(3, "upper", "fupper", 3500, 2000, 9000);
    CHECK(std::fabs(std::log2(p / 3000)) < 1.0 / 12, "PRESENCE set to 3 kHz peaks at %f", p);
    CHECK(std::fabs(std::log2(d1 / 600)) < 1.0 / 12, "LOW-MID set to 600 Hz dips at %f", d1);
    CHECK(std::fabs(std::log2(d2 / 3500)) < 1.0 / 12, "UPPER set to 3.5 kHz dips at %f", d2);
    // a shelf moves too: BASS at 180 Hz lifts 150 Hz much more than BASS at 45 Hz does
    auto lift = [&](double hzSet, double f) { auto w = fresh(); w->set("amount", 1); w->set("bass", 1); for (const char* id : {"lowmid", "presence", "upper", "air"}) w->set(id, 0); w->set("gainmatch", 0); w->set("fbass", WShape::bandKnob(0, hzSet));
      std::vector<float> L, R; sine(L, R, f, 0.2, 24000); runBlocks(*w, L, R); return rmsDb(L, 12000) - 20 * std::log10(0.2 / std::sqrt(2.0)); };
    CHECK(lift(180, 150) > lift(45, 150) + 4, "BASS shelf did not move: %f vs %f", lift(180, 150), lift(45, 150));
    std::printf("wshape_test: movable bands land where set (presence %.0f Hz, low-mid %.0f Hz, upper %.0f Hz)\n", p, d1, d2); }
  { // 3. LOUD COMP: the bass lift is bigger when the programme is quiet and smaller when it is loud; smooth (no step in the output)
    auto bassLift = [&](double amp, double lc, double* lfOut, double* jump) { auto w = fresh(); w->set("loudcomp", lc); w->set("gainmatch", 0); w->set("amount", 1); w->set("bass", 1);
      std::vector<float> L, R; sine(L, R, 60, amp, static_cast<std::size_t>(kFs * 6)); const std::vector<float> in = L; runBlocks(*w, L, R);
      if (lfOut) *lfOut = w->loudFactor();
      if (jump) { double m = 0; for (std::size_t i = 1; i < L.size(); ++i) m = std::max(m, std::fabs(static_cast<double>(L[i]) - L[i - 1])); *jump = m / amp; }
      return rmsDb(L, L.size() - 48000) - rmsDb(in, in.size() - 48000); };
    double lq = 0, ll = 0, jq = 0, jl = 0; const double off = bassLift(0.5, 0, nullptr, nullptr), quiet = bassLift(0.01, 1, &lq, &jq), loud = bassLift(0.9, 1, &ll, &jl);
    CHECK(quiet > loud + 4, "LOUD COMP: quiet lift %f, loud lift %f", quiet, loud);
    CHECK(quiet > off && loud < off, "LOUD COMP around the set curve: off %f quiet %f loud %f", off, quiet, loud);
    CHECK(lq > 1.5 && ll < 0.6, "loud factor quiet %f loud %f", lq, ll);
    CHECK(jq < 0.05 && jl < 0.05, "LOUD COMP stepped the output: %f %f (relative to the amplitude)", jq, jl);   // a 60 Hz sine moves at most 0.008 * gain per sample
    // a level change (quiet -> loud) is followed gradually, never in one step
    { auto w = fresh(); w->set("loudcomp", 1); w->set("amount", 1); w->set("bass", 1); std::vector<float> L, R; sine(L, R, 60, 1, static_cast<std::size_t>(kFs * 8));
      for (std::size_t i = 0; i < L.size(); ++i) { const float a = i < L.size() / 2 ? 0.02f : 0.8f; L[i] *= a; R[i] *= a; }
      double prev = 0, maxStep = 0; for (std::size_t i = 0; i < L.size(); i += 256) { w->process(L.data() + i, R.data() + i, 256); const double f = w->loudFactor(); if (i) maxStep = std::max(maxStep, std::fabs(f - prev)); prev = f; }
      CHECK(maxStep < 0.05, "LOUD COMP moved %f in one block", maxStep); }
    std::printf("wshape_test: LOUD COMP lifts the bass %.1f dB quiet, %.1f dB loud (%.1f dB off)\n", quiet, loud, off); }
  { // 4. AUTO W: a dark (tilted) noise is learned: the air is lifted, the bass is not, the low-mid is dipped more than the upper; deterministic
    auto dark = [](std::vector<float>& L, std::vector<float>& R, std::size_t n) { pink(L, R, n, 0.1, 7); double z = 0; const double a = 1 - std::exp(-2 * kPi * 400 / kFs);
      for (std::size_t i = 0; i < n; ++i) { z += a * (L[i] - z); L[i] = R[i] = static_cast<float>(0.3 * L[i] + 0.7 * 3 * z); } };
    auto learn = [&](double* knobs) { auto w = fresh(); w->set("learn", 1); std::vector<float> L, R; dark(L, R, static_cast<std::size_t>(kFs * (WShape::kLearnSeconds + 1))); runBlocks(*w, L, R);
      const auto ps = w->params(); for (int i = 0; i < 5; ++i) knobs[i] = ps[static_cast<std::size_t>(1 + i)].value; return std::make_pair(w->learnCount(), ps[17].value); };
    double k1[5], k2[5]; const auto r1 = learn(k1), r2 = learn(k2);
    CHECK(r1.first == 1 && r1.second == 0, "AUTO W finished once and switched itself off (%d, %f)", r1.first, r1.second);
    CHECK(k1[4] > 0.5 && k1[0] < 0.2 && k1[1] > k1[3], "AUTO W on dark noise: bass %f lowmid %f presence %f upper %f air %f", k1[0], k1[1], k1[2], k1[3], k1[4]);
    bool same = true; for (int i = 0; i < 5; ++i) if (k1[i] != k2[i]) same = false; CHECK(same, "AUTO W is not deterministic");
    // and the result moves the spectrum towards neutral: the spread of the five regions gets smaller after the learned curve
    auto spread = [&](const std::vector<float>& x) { Biquad bp[5]; double e[5] = {0}; for (int b = 0; b < 5; ++b) bp[b].design(Biquad::BandPass, kFs, WShape::bandHz(b, 0.5), 1.0, 0);
      for (std::size_t i = x.size() / 4; i < x.size(); ++i) for (int b = 0; b < 5; ++b) { const double y = bp[b].run(x[i]); e[b] += y * y; }
      double lv[5], m = 0; for (int b = 0; b < 5; ++b) { lv[b] = 10 * std::log10(e[b] + 1e-30); m += lv[b] / 5; } double s = 0; for (int b = 0; b < 5; ++b) s += (lv[b] - m) * (lv[b] - m); return std::sqrt(s / 5); };
    std::vector<float> L, R; dark(L, R, static_cast<std::size_t>(kFs * 4)); const double before = spread(L); auto w = fresh(); const char* ids[5] = {"bass", "lowmid", "presence", "upper", "air"}; for (int i = 0; i < 5; ++i) w->set(ids[i], k1[i]);
    runBlocks(*w, L, R); const double after = spread(L);
    CHECK(after < before - 1, "AUTO W did not bring the regions together: %f dB -> %f dB", before, after);
    // silence: nothing is learned, the gains stay
    { auto s = fresh(); s->set("learn", 1); std::vector<float> Z(static_cast<std::size_t>(kFs * 9), 0.f), Z2 = Z; runBlocks(*s, Z, Z2); CHECK(s->learnCount() == 0 && std::fabs(s->params()[1].value - 0.55) < 1e-12 && s->params()[17].value == 0, "AUTO W on silence"); }
    std::printf("wshape_test: AUTO W on dark noise -> bass %.2f lowmid %.2f presence %.2f upper %.2f air %.2f; region spread %.1f -> %.1f dB\n", k1[0], k1[1], k1[2], k1[3], k1[4], before, after); }
  { // 5. GAIN MATCH: pink noise keeps its level within 0.5 dB as AMOUNT goes 0 .. 1 (full W); off, the curve's own level shows
    std::vector<float> P, PR; pink(P, PR, static_cast<std::size_t>(kFs * 4), 0.05, 3);
    { Biquad hp[2], lp[2]; for (auto& q : hp) q.design(Biquad::HighPass, kFs, 40, 0.707, 0); for (auto& q : lp) q.design(Biquad::LowPass, kFs, 16000, 0.707, 0);   // programme in the band the compensation weighs: 40 Hz .. 16 kHz
      for (std::size_t i = 0; i < P.size(); ++i) { float x = P[i]; for (auto& q : hp) x = q.run(x); for (auto& q : lp) x = q.run(x); P[i] = PR[i] = x; } }
    const double in = rmsDb(P, P.size() / 4); double lo = 1e9, hi = -1e9;
    for (int a = 0; a <= 10; ++a) { auto w = fresh(); w->set("amount", a / 10.0); for (const char* id : {"bass", "lowmid", "presence", "upper", "air"}) w->set(id, 1); std::vector<float> L = P, R = PR; runBlocks(*w, L, R); const double d = rmsDb(L, L.size() / 4) - in; lo = std::min(lo, d); hi = std::max(hi, d); }
    CHECK(hi - lo < 0.5 && std::fabs(hi) < 0.5 && std::fabs(lo) < 0.5, "GAIN MATCH: level moved %f .. %f dB", lo, hi);
    auto w = fresh(); w->set("amount", 1); w->set("gainmatch", 0); for (const char* id : {"bass", "lowmid", "presence", "upper", "air"}) w->set(id, 1); std::vector<float> L = P, R = PR; runBlocks(*w, L, R);
    CHECK(rmsDb(L, L.size() / 4) - in > 1, "GAIN MATCH off should let the full W be louder: %f", rmsDb(L, L.size() / 4) - in);
    std::printf("wshape_test: GAIN MATCH keeps pink noise within %.2f .. %+.2f dB across AMOUNT 0..1\n", lo, hi); }
  { // 6. STEREO MODES: MID leaves a pure side signal exactly alone, SIDE leaves a mono one exactly alone, and each does work on its own part
    auto run = [&](double mode, bool side) { auto w = fresh(); w->set("amount", 1); w->set("stereo", mode); for (const char* id : {"bass", "presence", "air"}) w->set(id, 1);
      std::vector<float> L, R; sine(L, R, 60, 0.3, 24000); if (side) for (auto& r : R) r = -r; const std::vector<float> L0 = L, R0 = R; runBlocks(*w, L, R);
      bool same = true; for (std::size_t i = 0; i < L.size(); ++i) if (L[i] != L0[i] || R[i] != R0[i]) same = false; return same; };
    CHECK(run(0.5, true), "MID touched the side"); CHECK(run(1.0, false), "SIDE touched the mid");
    CHECK(!run(0.5, false), "MID did nothing to the mid"); CHECK(!run(1.0, true), "SIDE did nothing to the side");
    { auto w = fresh(); CHECK(w->text("stereo", 0) == "STEREO" && w->text("stereo", 0.5) == "MID" && w->text("stereo", 1) == "SIDE", "stereo names"); }
    std::printf("wshape_test: MID / SIDE leave the other part untouched, sample for sample\n"); }
  { // 7. TRAIN: +6 dB on the chosen band, nothing when OFF
    auto at = [&](double train, double f) { auto w = fresh(); w->set("train", train); w->set("gainmatch", 0); std::vector<float> L, R; sine(L, R, f, 0.2, 24000); runBlocks(*w, L, R); return rmsDb(L, 12000); };
    const double d = at(3 / 5.0, 1800) - at(0, 1800), other = at(3 / 5.0, 90) - at(0, 90);
    CHECK(std::fabs(d - 6) < 0.6 && std::fabs(other) < 1.0, "TRAIN on PRESENCE: %f dB there, %f dB at 90 Hz", d, other);
    auto w = fresh(); CHECK(w->text("train", 0) == "OFF" && w->text("train", 0.2) == "BASS" && w->text("train", 1) == "AIR", "train names");
    std::printf("wshape_test: TRAIN boosts the hidden band by %.2f dB\n", d); }
  { // 8. NO NaN, NO CLICKS: every continuous control moved at random every block on a 1 kHz tone
    auto w = fresh(); Lcg r(11); std::vector<float> L, R; sine(L, R, 1000, 0.5, static_cast<std::size_t>(kFs * 4)); bool finite = true; double maxStep = 0; float last = 0;
    const char* ids[] = {"amount", "bass", "lowmid", "presence", "upper", "air", "smooth", "fbass", "flowmid", "fpresence", "fupper", "fair", "loudcomp"};
    for (std::size_t i = 0; i < L.size(); i += 128) { for (const char* id : ids) { const double v = w->params()[0].value; (void)v; } w->set(ids[(i / 128) % 13], 0.5 + 0.5 * r.next()); w->process(L.data() + i, R.data() + i, 128);
      for (std::size_t j = i; j < i + 128; ++j) { if (!std::isfinite(L[j]) || !std::isfinite(R[j])) finite = false; if (j > 4800) maxStep = std::max(maxStep, static_cast<double>(std::fabs(L[j] - last))); last = L[j]; } }
    CHECK(finite, "NaN or Inf"); CHECK(maxStep < 0.6, "a click: a step of %f between samples (a 1 kHz tone at this level moves under 0.4)", maxStep);
    std::printf("wshape_test: random moves of every control, finite, largest sample step %.3f\n", maxStep); }
  { // 9. PRESETS: nine W-SHAPE presets after the 600 others, every value known to the effect
    int n = 0; for (const auto& p : presets()) if (p.kind == "wshape") { ++n; auto w = fresh(); for (const auto& [id, v] : p.values) { bool known = false; for (const auto& q : w->params()) if (q.id == id) known = true; CHECK(known && v >= 0 && v <= 1, "%s: %s", p.name.c_str(), id.c_str()); } CHECK(p.id > 600, "%s id %d", p.name.c_str(), p.id); }
    CHECK(n == 9 && presets().size() == 614 && preset(601)->name == "SPEECH" && preset(609)->name == "STRONG", "W-SHAPE presets: %d", n);
    auto w = fresh(); CHECK(w->params().size() == WShape::kParams, "params");
    std::printf("wshape_test: %d W-SHAPE presets (ids 601 .. 609)\n", n); }
  std::printf(fails ? "wshape_test: %d FAILED\n" : "wshape_test: all passed\n", fails);
  return fails ? 1 : 0;
}
