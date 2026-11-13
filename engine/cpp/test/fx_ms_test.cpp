// MS COLOUR EQ, ISO EQ 10, MOD MACHINE: measured with sines.
#include "fx/fx.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
using namespace am::fx;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++fails; } } while (0)
static double run(Effect& e, double f, double aL, double aR, double* outR = nullptr) {   // rms of L (and R) after settling, in dB
  const double fs = 48000; const int N = 48000; std::vector<float> L(N), R(N);
  for (int i = 0; i < N; ++i) { const double s = std::sin(2 * 3.14159265358979 * f * i / fs); L[i] = static_cast<float>(aL * s); R[i] = static_cast<float>(aR * s); }
  for (int i = 0; i < N; i += 256) e.process(L.data() + i, R.data() + i, static_cast<std::size_t>(std::min(256, N - i)));
  double el = 0, er = 0; for (int i = N / 2; i < N; ++i) { el += L[i] * L[i]; er += R[i] * R[i]; }
  const double ref = 0.5 * 0.5 * 0.5 * (N / 2.0);
  if (outR) *outR = 10 * std::log10(er / ref + 1e-30); return 10 * std::log10(el / ref + 1e-30);
}
int main() {
  { auto e = make("mseq", 48000); e->set("xformer", 0); const double d = run(*e, 1000, 0.5, 0.5); CHECK(std::fabs(d) < 0.1, "flat mseq %f", d); }
  { auto e = make("mseq", 48000); e->set("xformer", 0); e->set("lowgain", 1.0); e->set("lowms", 0.0); e->set("lowfreq", 0.6);   // +12 dB low, side only
    double r; const double mono = run(*e, 80, 0.5, 0.5, &r); CHECK(std::fabs(mono) < 0.2, "side-only band moved a mono signal %f", mono);
    auto e2 = make("mseq", 48000); e2->set("xformer", 0); e2->set("lowgain", 1.0); e2->set("lowms", 0.0); e2->set("lowfreq", 0.6);
    const double side = run(*e2, 80, 0.5, -0.5); CHECK(side > 8, "side-only band did not lift the side %f", side); }
  { auto e = make("mseq", 48000); e->set("xformer", 0); e->set("solo", 0.6); double r; const double s = run(*e, 1000, 0.5, 0.5, &r); CHECK(s < -60, "SIDE solo of a mono signal %f", s); }
  { auto e = make("mseq", 48000); e->set("xformer", 0); e->set("width", 0); double r; run(*e, 1000, 0.5, -0.5, &r); CHECK(r < -60, "width 0 kept the side %f", r); }
  { auto e = make("isoeq", 48000); e->set("b1k", 1.0); const double d = run(*e, 1000, 0.5, 0.5); CHECK(std::fabs(d - 12) < 0.6, "iso 1k +12 gave %f", d);
    auto f = make("isoeq", 48000); f->set("colour", 0); const double z = run(*f, 1000, 0.5, 0.5); CHECK(std::fabs(z) < 0.05, "flat iso %f", z); }
  { auto e = make("mod", 48000); e->set("mode", 0.7); e->set("depth", 1.0); e->set("rate", 0.6); const double d = run(*e, 1000, 0.5, 0.5); CHECK(d < -2, "tremolo did not lower the level %f", d); }
  for (const char* k : {"mod"}) for (double m : {0.0, 0.3, 0.5, 0.9}) { auto e = make(k, 48000); e->set("mode", m); e->set("feedback", 1.0); const double d = run(*e, 440, 0.5, 0.5); CHECK(std::isfinite(d) && d < 6, "mode %f level %f", m, d); }
  { auto e = make("opto", 48000); e->set("reduction", 0.8); e->set("gain", 0); const double d = run(*e, 1000, 0.5, 0.5); CHECK(d < -6, "opto did not level a loud tone %f", d); }
  { auto e = make("glue", 48000); e->set("threshold", 0.5); e->set("ratio", 1.0); e->set("makeup", 0); e->set("colour", 0); const double d = run(*e, 1000, 0.5, 0.5); CHECK(d < -6 && d > -20, "glue %f", d); }
  { auto e = make("tape", 48000); const double d = run(*e, 1000, 0.5, 0.5); CHECK(std::isfinite(d) && std::fabs(d) < 6, "tape level %f", d); }
  { auto e = make("deesser", 48000); e->set("threshold", 0.6); e->set("range", 1.0); const double hi = run(*e, 9000, 0.5, 0.5); auto f = make("deesser", 48000); f->set("threshold", 0.6); f->set("range", 1.0); const double lo = run(*f, 400, 0.5, 0.5);
    CHECK(hi < -6, "de-esser left 9 kHz at %f", hi); CHECK(std::fabs(lo) < 0.5, "de-esser touched 400 Hz %f", lo); }
  { auto e = make("transient", 48000); e->set("attack", 0.5); const double d = run(*e, 1000, 0.5, 0.5); CHECK(std::fabs(d) < 0.3, "transient at centre is not neutral on a steady tone %f", d); }
  { auto e = make("maximizer", 48000); e->set("threshold", 1.0); e->set("ceiling", 1.0); const double fs = 48000; std::vector<float> L(48000), R(48000);
    for (int i = 0; i < 48000; ++i) { const double v = 0.9 * std::sin(2 * 3.14159265 * 200 * i / fs) * (i % 4800 < 200 ? 1.0 : 0.3); L[i] = R[i] = static_cast<float>(v); }
    for (int i = 0; i < 48000; i += 256) e->process(L.data() + i, R.data() + i, static_cast<std::size_t>(std::min(256, 48000 - i)));
    double pk = 0; for (float v : L) pk = std::max(pk, static_cast<double>(std::fabs(v))); CHECK(pk <= 1.0001, "maximizer let %f through", pk); }
  { auto e = make("multiband", 48000); e->set("low", 0); e->set("mid", 0); e->set("high", 0); for (double f : {80.0, 1000.0, 9000.0}) { auto g = make("multiband", 48000); g->set("low", 0); g->set("mid", 0); g->set("high", 0); const double d = run(*g, f, 0.3, 0.3); CHECK(std::fabs(d - 20 * std::log10(0.3 / 0.5)) < 0.1, "multiband not transparent at %f Hz: %f", f, d); } }
  { auto e = make("imager", 48000); e->set("lowwidth", 0.5); e->set("highwidth", 0.5); double r; const double d = run(*e, 1000, 0.5, 0.2, &r); CHECK(std::fabs(d) < 0.05, "imager at 100 %% moved L %f", d); }
  { auto e = make("imager", 48000); e->set("lowwidth", 0.0); e->set("crossover", 1.0); double r; run(*e, 200, 0.5, -0.5, &r); CHECK(r < -18, "imager low width 0 kept the side bass %f", r); }
  { auto e = make("autopan", 48000); e->set("depth", 0); const double d = run(*e, 1000, 0.5, 0.5); CHECK(std::fabs(d) < 0.05, "autopan depth 0 %f", d); }
  { auto e = make("doubler", 48000); const double d = run(*e, 440, 0.5, 0.5); CHECK(std::isfinite(d) && d < 6, "doubler %f", d); }
  { auto e = make("iamfelement", 48000); e->set("azimuth", 0.75); e->set("spread", 0); double r; const double l = run(*e, 1000, 0.5, 0.5, &r); CHECK(r - l > 4, "IAMF element at 90 R, 1 kHz: L %f R %f", l, r);
    auto h = make("iamfelement", 48000); h->set("azimuth", 0.75); h->set("spread", 0); double r4; const double l4 = run(*h, 6000, 0.5, 0.5, &r4); CHECK(r4 - l4 > 10, "IAMF element at 90 R, 6 kHz: L %f R %f", l4, r4); }
  { auto e = make("iamfelement", 48000); e->set("spread", 0); double r; const double l = run(*e, 1000, 0.5, 0.5, &r); CHECK(std::fabs(r - l) < 0.1, "IAMF element in front is not centred: %f %f", l, r); }
  { auto e = make("iamfelement", 48000); e->set("azimuth", 0.25); e->set("render", 1.0); e->set("spread", 0); double r; const double l = run(*e, 1000, 0.5, 0.5, &r); CHECK(r < -60 && std::fabs(l - 3.01) < 0.3, "IAMF speakers hard left: L %f R %f", l, r); }
  { auto e = make("iamfelement", 48000); e->set("render", 1.0); e->set("spread", 0); e->set("distance", std::log(4.0 / 0.5) / std::log(40.0)); double r; const double l = run(*e, 200, 0.5, 0.5, &r); CHECK(std::fabs(l + 12.04) < 0.6, "IAMF 4 m should be -12 dB: %f", l); }
  { auto e = make("iamfrender", 48000); e->set("normalize", 1.0); e->set("target", (-24.0 + 27) / 13); const double fs = 48000; const int N = 48000 * 20; std::vector<float> L(N), R(N);
    const double a = 0.0316; for (int i = 0; i < N; ++i) L[i] = R[i] = static_cast<float>(a * std::sin(2 * 3.14159265 * 1000 * i / fs));
    for (int i = 0; i < N; i += 512) e->process(L.data() + i, R.data() + i, static_cast<std::size_t>(std::min(512, N - i)));
    double ms = 0; for (int i = N - 48000; i < N; ++i) ms += L[i] * L[i]; ms /= 48000; const double outLu = -0.691 + 10 * std::log10(2 * ms) + 0.7;   // +0.7: K-weighting at 1 kHz
    CHECK(std::fabs(outLu + 24) < 1.0, "IAMF renderer normalised to %f LKFS (want -24)", outLu); }
  std::printf(fails ? "fx_ms: %d failed\n" : "fx_ms: all passed\n", fails); return fails ? 1 : 0;
}
