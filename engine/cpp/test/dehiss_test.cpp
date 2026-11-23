// AmF DE-HISS: white noise drops by at least REDUCTION after LEARN, a sine over the hiss passes within 0.5 dB, the reported latency is
// the delay a click takes, bypass keeps that delay, LISTEN plays the difference, AUTO LEARN finds the hiss on its own; no NaN / denormals.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "fx/fx.hpp"
#include "fx/fx_plugin.hpp"

using namespace am;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s: ", __FILE__, __LINE__, #c); std::fprintf(stderr, " " __VA_ARGS__); std::fprintf(stderr, "\n"); ++fails; } } while (0)
static double rmsDb(const std::vector<float>& v, std::size_t a, std::size_t b) { double s = 0; for (std::size_t i = a; i < b; ++i) s += double(v[i]) * v[i]; return 10 * std::log10(s / double(b - a) + 1e-30); }
static void run(fx::DeHiss& e, std::vector<float>& L, std::vector<float>& R, std::size_t blk = 128) { for (std::size_t i = 0; i < L.size(); i += blk) e.process(L.data() + i, R.data() + i, std::min(blk, L.size() - i)); }

int main() {
  const double fs = 48000; const std::size_t n = 48000 * 2; std::mt19937 rng(7); std::normal_distribution<double> G(0, 1);
  auto noise = [&](double db, std::vector<float>& L, std::vector<float>& R) { const double a = std::pow(10.0, db / 20); L.resize(n); R.resize(n); for (std::size_t i = 0; i < n; ++i) { L[i] = static_cast<float>(a * G(rng)); R[i] = static_cast<float>(a * G(rng)); } };
  const auto lat = fx::DeHiss(fs).latencySamples(); CHECK(lat == 1024, "%u", lat);
  { // white noise: LEARN for 2 s, then the same hiss again is down by at least REDUCTION (6, 12, 24 dB; full band)
    for (double red : {6.0, 12.0, 24.0}) { fx::DeHiss e(fs); e.set("reduce", red / 40); e.set("freq", 0); e.set("auto", 0); std::vector<float> L, R;
      e.set("learn", 1); noise(-50, L, R); run(e, L, R); e.set("learn", 0);
      noise(-50, L, R); const std::vector<float> L0 = L; run(e, L, R); const double got = rmsDb(L0, n / 4, n) - rmsDb(L, n / 4, n);
      CHECK(got >= red && got < red + 1.5, "white noise down %.2f dB (set %.0f)", got, red); CHECK(e.reductionDb() < -red + 0.5f, "meter %.1f", e.reductionDb());
      std::printf("dehiss_test: white noise, REDUCTION %.0f dB: down %.2f dB\n", red, got); } }
  { // a sine over the learned hiss: within 0.5 dB (latency-aligned), for 200 Hz, 1 kHz, 8 kHz; with AUTO LEARN on as well
    for (int autoOn = 0; autoOn < 2; ++autoOn) for (double hz : {200.0, 1000.0, 8000.0}) { fx::DeHiss e(fs); e.set("reduce", 0.5); e.set("auto", autoOn); std::vector<float> L, R;
      e.set("learn", 1); noise(-70, L, R); run(e, L, R); e.set("learn", 0);
      std::vector<float> s(n); for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<float>(0.25 * std::sin(2 * 3.14159265358979 * hz * double(i) / fs)) + L[i]; std::vector<float> l = s, r = s; run(e, l, r);
      std::vector<float> ref(s.begin(), s.end() - lat); std::vector<float> out(l.begin() + lat, l.end()); const double d = rmsDb(out, n / 4, out.size()) - rmsDb(ref, n / 4, ref.size());
      double err = 0, pk = 0; for (std::size_t i = n / 4; i < out.size(); ++i) { err = std::max(err, double(std::fabs(out[i] - ref[i]))); pk = std::max(pk, double(std::fabs(ref[i]))); }
      CHECK(std::fabs(d) < 0.5, "sine %.0f Hz (auto %d): %.3f dB", hz, autoOn, d); CHECK(err < 0.06 * pk, "sine %.0f Hz waveform off by %.4f", hz, err); } 
    std::printf("dehiss_test: sines 200 Hz / 1 kHz / 8 kHz over learned hiss within 0.5 dB (with and without AUTO LEARN)\n"); }
  { // latency: a click comes out exactly latencySamples() later, also bypassed and through the rack plug-in; LISTEN = input - output
    for (int byp = 0; byp < 2; ++byp) { fx::DeHiss e(fs); e.setBypass(byp); std::vector<float> L(8192, 0.f), R(8192, 0.f); L[1000] = R[1000] = 1.f; run(e, L, R, 100);
      std::size_t at = 0; for (std::size_t i = 0; i < L.size(); ++i) if (std::fabs(L[i]) > std::fabs(L[at])) at = i; CHECK(at == 1000 + lat && std::fabs(L[at] - 1) < 0.01, "click at %zu (%f), bypass %d", at, L[at], byp); }
    auto p = fx::FxPlugin::make("dehiss", fs); CHECK(p && p->latencySamples() == lat && p->info().name == "AmF DE-HISS");
    fx::DeHiss a(fs), b(fs); b.set("listen", 1); std::vector<float> L, R; noise(-30, L, R); for (std::size_t i = 0; i < n; ++i) L[i] += static_cast<float>(0.2 * std::sin(0.05 * double(i))); R = L;
    std::vector<float> l1 = L, r1 = R, l2 = L, r2 = R; run(a, l1, r1); run(b, l2, r2); double worst = 0; for (std::size_t i = lat; i < n; ++i) worst = std::max(worst, double(std::fabs(l1[i] + l2[i] - L[i - lat])));
    CHECK(worst < 1e-4, "listen + output != input: %.2e", worst);
    std::printf("dehiss_test: latency %u samples = the measured delay (also bypassed), LISTEN NOISE is the exact difference\n", lat); }
  { // AUTO LEARN finds steady hiss without LEARN: after 3 s the same hiss is well down
    fx::DeHiss e(fs); e.set("freq", 0); e.set("reduce", 0.3); std::vector<float> L, R; for (int k = 0; k < 2; ++k) { noise(-55, L, R); run(e, L, R); } noise(-55, L, R); const std::vector<float> L0 = L; run(e, L, R);
    const double got = rmsDb(L0, n / 4, n) - rmsDb(L, n / 4, n); CHECK(got > 8, "auto learn: %.2f dB", got); std::printf("dehiss_test: AUTO LEARN takes %.1f dB off steady hiss\n", got); }
  { // no NaN, no denormals: random settings, loud noise, then silence with a tiny value
    std::uniform_real_distribution<double> U(0, 1); bool ok = true, sub = false;
    for (int run_ = 0; run_ < 10; ++run_) { fx::DeHiss e(run_ % 2 ? 44100 : 96000); for (const auto& p : e.params()) e.set(p.id, U(rng)); std::vector<float> L(4096), R(4096);
      for (int blk = 0; blk < 80; ++blk) { for (int i = 0; i < 4096; ++i) { L[i] = blk < 10 ? static_cast<float>(G(rng)) : blk == 10 && i == 0 ? 1e-38f : 0.f; R[i] = blk < 10 ? static_cast<float>(G(rng)) : 0.f; }
        e.process(L.data(), R.data(), 4096); for (int i = 0; i < 4096; ++i) { ok = ok && std::isfinite(L[i]) && std::isfinite(R[i]) && std::fabs(L[i]) < 100; if (blk > 30) sub = sub || std::fpclassify(L[i]) == FP_SUBNORMAL || std::fpclassify(R[i]) == FP_SUBNORMAL; } } }
    CHECK(ok && !sub, "finite %d subnormal %d", ok, sub); std::printf("dehiss_test: random settings: finite, no denormals\n"); }
  { fx::DeHiss e(fs); CHECK(e.params().size() == 9 && e.text("reduce", 0.3) == "12.0 dB" && e.text("freq", 2.0 / 3) == "2.00 kHz" && e.text("listen", 1) == "NOISE ONLY"); }
  if (fails) { std::printf("dehiss_test: %d failed\n", fails); return 1; }
  return 0;
}
