// Runs engine/tests/vectors.txt against the C++ engine:  engine_test <vectors.txt>   (exit 0 = every number within tolerance)
#include "audiomixer/engine.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using std::size_t;
static int failures = 0, checked = 0, cases = 0;
constexpr double PI2 = 2.0 * 3.14159265358979323846;

static std::vector<std::string> words(const std::string& s) { std::istringstream is(s); std::vector<std::string> v; std::string w; while (is >> w) v.push_back(w); return v; }
static std::vector<double> numbers(const std::string& s) { std::vector<double> v; for (const auto& w : words(s)) v.push_back(std::strtod(w.c_str(), nullptr)); return v; }

static std::vector<float> gen(const std::string& spec, double fs, size_t n, int ch) {
  std::vector<std::string> p; { std::istringstream is(spec); std::string t; while (std::getline(is, t, ':')) p.push_back(t); }
  auto N = [&](size_t k) { return std::strtod(p[k].c_str(), nullptr); };
  std::vector<float> out(n, 0.0f); const std::string& k = p[0];
  if (k == "silence") return out;
  for (size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i);
    if (k == "sine") out[i] = static_cast<float>(N(2) * std::sin(PI2 * N(1) * t / fs));
    else if (k == "sinelr") out[i] = static_cast<float>((ch ? N(4) : N(2)) * std::sin(PI2 * (ch ? N(3) : N(1)) * t / fs));
    else if (k == "inv") out[i] = static_cast<float>((ch ? -N(2) : N(2)) * std::sin(PI2 * N(1) * t / fs));
    else if (k == "square") out[i] = static_cast<float>(N(2) * (std::sin(PI2 * N(1) * t / fs) >= 0 ? 1 : -1));
    else if (k == "clipsine") { const double v = N(2) * std::sin(PI2 * N(1) * t / fs); out[i] = static_cast<float>(v > 1 ? 1 : v < -1 ? -1 : v); }
    else if (k == "alt") out[i] = static_cast<float>((static_cast<long long>(std::floor(t / (N(4) * fs))) % 2 ? N(3) : N(2)) * std::sin(PI2 * N(1) * t / fs));
    else if (k == "fade") { const double tt = std::min(1.0, t / (N(4) * fs)); out[i] = static_cast<float>((N(2) + (N(3) - N(2)) * tt) * std::sin(PI2 * N(1) * t / fs)); }
    else if (k == "noise") { /* handled below */ }
    else { std::cerr << "unknown signal " << spec << "\n"; std::exit(2); }
  }
  if (k == "noise") { std::uint32_t s = static_cast<std::uint32_t>(static_cast<long long>(N(1))) + static_cast<std::uint32_t>(ch); for (size_t i = 0; i < n; ++i) { s = s * 1664525u + 1013904223u; out[i] = static_cast<float>(N(2) * ((s >> 8) / 16777216.0 * 2 - 1)); } }
  return out;
}
static std::vector<std::int16_t> gen16(const std::string& spec, double fs, size_t frames, int ch) {
  std::vector<std::int16_t> out(frames * static_cast<size_t>(ch));
  for (int c = 0; c < ch; ++c) { auto x = gen(spec, fs, frames, c & 1); for (size_t i = 0; i < frames; ++i) { const double v = static_cast<double>(x[i]) * 32767; out[i * static_cast<size_t>(ch) + static_cast<size_t>(c)] = static_cast<std::int16_t>(static_cast<long>(v + (v >= 0 ? 0.5 : -0.5))); } }
  return out;
}
static void check(const std::string& id, const char* what, double got, double want, double tol, bool rel) {
  ++checked;
  if (std::isinf(want) || std::isinf(got)) { if (got != want) { ++failures; std::printf("FAIL %s %s: got %g want %g\n", id.c_str(), what, got, want); } return; }
  const double lim = rel ? tol * std::max(1.0, std::fabs(want)) : tol;
  if (!(std::fabs(got - want) <= lim)) { ++failures; std::printf("FAIL %s %s: got %.9g want %.9g (tolerance %g)\n", id.c_str(), what, got, want, lim); }
}

int main(int argc, char** argv) {
  std::ifstream f(argc > 1 ? argv[1] : "vectors.txt"); if (!f) { std::cerr << "cannot open the vectors file\n"; return 2; }
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto bar = line.find('|'); std::vector<double> e; if (bar != std::string::npos) e = numbers(line.substr(bar + 1));
    const auto h = words(line.substr(0, bar)); if (h.size() < 2) continue;
    const std::string& kind = h[0], &id = h[1];
    if (kind == "lufs") {
      ++cases; const double fs = std::atof(h[2].c_str()); const size_t block = static_cast<size_t>(std::atoi(h[3].c_str())), n = static_cast<size_t>(fs * std::atof(h[4].c_str()) + 0.5);
      auto l = gen(h[5], fs, n, 0), r = gen(h[5], fs, n, 1); am::Loudness m(fs);
      for (size_t i = 0; i < n; i += block) m.feed(&l[i], &r[i], std::min(block, n - i));
      check(id, "momentary", m.momentary(), e[0], 0.02, false); check(id, "short-term", m.shortTerm(), e[1], 0.02, false); check(id, "integrated", m.integrated(), e[2], 0.02, false);
      check(id, "lra", m.range(), e[3], 0.02, false); check(id, "true peak", m.truePeak(), e[4], 0.01, false);
    } else if (kind == "meter") {
      ++cases; const double fs = std::atof(h[2].c_str()); const size_t win = static_cast<size_t>(std::atoi(h[3].c_str())), n = static_cast<size_t>(fs * std::atof(h[4].c_str()) + 0.5);
      auto x = gen(h[6], fs, n, 0); am::Meter m(am::ClipConfig::fromDb(std::atof(h[5].c_str()))); double t = 0;
      for (size_t i = 0; i + win <= n; i += win) { t += static_cast<double>(win) * 1000.0 / fs; m.push(&x[i], win, t); }
      const auto& s = m.state();
      check(id, "pk", s.pk, e[0], 1e-6, true); check(id, "rms", s.rms, e[1], 1e-6, true); check(id, "hold", s.hold, e[2], 1e-6, true); check(id, "clipN", s.clipN, e[3], 0, false);
      check(id, "latched", s.latched, e[4], 0, false); check(id, "clipPk", s.clipPk, e[5], 1e-6, true);
    } else if (kind == "phase") {
      ++cases; const size_t n = static_cast<size_t>(std::atoi(h[2].c_str())); auto l = gen(h[6], 48000, n, 0), r = gen(h[6], 48000, n, 1);
      const auto p = am::phaseCorrelation(l.data(), r.data(), n, std::atof(h[3].c_str()), std::atoi(h[4].c_str()) != 0, std::atoi(h[5].c_str()) != 0);
      check(id, "corr", p.corr, e[0], 1e-5, false); check(id, "peak", p.peak, e[1], 1e-6, true); check(id, "rmsL", p.rmsL, e[2], 1e-6, true); check(id, "rmsR", p.rmsR, e[3], 1e-6, true);
    } else if (kind == "scope") {
      ++cases; const double fs = std::atof(h[2].c_str()); const size_t n = static_cast<size_t>(std::atoi(h[3].c_str())); auto a = gen(h[7], fs, n, 0);
      am::ScopeConfig c; c.timebaseMs = std::atof(h[4].c_str()); c.triggerDb = std::atof(h[5].c_str()); c.rising = std::atoi(h[6].c_str()) != 0; const auto s = am::scopeSweep(a.data(), n, fs, c);
      check(id, "N", static_cast<double>(s.n), e[0], 0, false); check(id, "start", static_cast<double>(s.start), e[1], 0, false); check(id, "triggered", s.triggered, e[2], 0, false); check(id, "p2p", s.p2p, e[3], 1e-6, true);
      check(id, "rms", s.rms, e[4], 1e-6, true); check(id, "crest", s.crestDb, e[5], 1e-4, false); check(id, "freq", s.freq, e[6], 1e-6, true);
    } else if (kind == "vu") {
      ++cases; am::VuMeter v; const double ref = std::atof(h[2].c_str());
      for (int i = 0, s = std::atoi(h[4].c_str()); i < s; ++i) v.step(std::atof(h[3].c_str()), ref);
      for (int i = 0, s = std::atoi(h[6].c_str()); i < s; ++i) v.step(std::atof(h[5].c_str()), ref);
      check(id, "needle", v.needle(), e[0], 1e-9, true); check(id, "vel", v.velocity(), e[1], 1e-9, true); check(id, "led", v.peakLed(), e[2], 0, false);
    } else if (kind == "s16") {
      ++cases; const int ch = std::atoi(h[2].c_str()); const size_t frames = static_cast<size_t>(std::atoi(h[4].c_str())), half = frames / 2; auto x = gen16(h[5], 48000, frames, ch);
      am::S16Meter m(ch, std::atof(h[3].c_str())); size_t k = 0;
      for (int snap = 0; snap < 2; ++snap) {
        const size_t a = snap ? half : 0, b = snap ? frames : half; m.push(&x[a * static_cast<size_t>(ch)], (b - a) * static_cast<size_t>(ch)); const auto s = m.snapshot();
        for (int c = 0; c < ch; ++c) check(id, "peak", s.peakDb[static_cast<size_t>(c)], e[k++], 0.06, false);
        for (int c = 0; c < ch; ++c) check(id, "rms", s.rmsDb[static_cast<size_t>(c)], e[k++], 0.06, false);
        check(id, "clip", s.clip, e[k++], 0, false); check(id, "clipRuns", s.clipRuns, e[k++], 0, false); check(id, "clipping", s.clipping, e[k++], 0, false); check(id, "frames", s.frames, e[k++], 0, false);
      }
    } else if (kind == "mixer") {
      ++cases; const double fs = std::atof(h[2].c_str()); const int blocks = std::atoi(h[3].c_str()), ns = std::atoi(h[8].c_str()); const size_t n = static_cast<size_t>(blocks) * 512;
      am::Mixer m(ns, fs); m.setMasterDb(std::atof(h[4].c_str())); m.setLimiter(std::atoi(h[5].c_str()) != 0, std::atof(h[6].c_str()), std::atof(h[7].c_str()));
      std::vector<std::vector<float>> in;
      for (int s = 0; s < ns; ++s) { std::getline(f, line); const auto t = words(line); in.push_back(gen(t[1], fs, n, 0)); m.setGainDb(s, std::atof(t[2].c_str())); m.setPan(s, std::atof(t[3].c_str())); m.setMute(s, t[4] == "1"); m.setSolo(s, t[5] == "1"); }
      std::getline(f, line); const auto ex = numbers(line.substr(line.find('|') + 1));
      std::vector<float> ol(n), orr(n);
      for (int b = 0; b < blocks; ++b) { std::vector<const float*> p; for (int s = 0; s < ns; ++s) p.push_back(in[static_cast<size_t>(s)].data() + static_cast<size_t>(b) * 512); m.process(p.data(), 512, ol.data() + static_cast<size_t>(b) * 512, orr.data() + static_cast<size_t>(b) * 512); }
      double pkl = 0, pkr = 0, ssl = 0, ssr = 0; for (size_t i = 0; i < n; ++i) { pkl = std::max(pkl, std::fabs(static_cast<double>(ol[i]))); pkr = std::max(pkr, std::fabs(static_cast<double>(orr[i]))); ssl += static_cast<double>(ol[i]) * ol[i]; ssr += static_cast<double>(orr[i]) * orr[i]; }
      size_t k = 0; check(id, "peak L", pkl, ex[k++], 2e-4, true); check(id, "peak R", pkr, ex[k++], 2e-4, true); check(id, "rms L", std::sqrt(ssl / static_cast<double>(n)), ex[k++], 2e-4, true); check(id, "rms R", std::sqrt(ssr / static_cast<double>(n)), ex[k++], 2e-4, true);
      for (int s = 0; s < ns; ++s) { const auto& st = m.strip(s).meter.state(); check(id, "strip pk", st.pk, ex[k++], 2e-4, true); check(id, "strip rms", st.rms, ex[k++], 2e-4, true); check(id, "strip hold", st.hold, ex[k++], 2e-4, true); check(id, "strip clipN", st.clipN, ex[k++], 0, false); }
      for (int c = 0; c < 2; ++c) { const auto& st = m.masterMeter(c); check(id, "master pk", st.pk, ex[k++], 2e-4, true); check(id, "master rms", st.rms, ex[k++], 2e-4, true); check(id, "master clipN", st.clipN, ex[k++], 0, false); }
      check(id, "momentary", m.loudness().momentary(), ex[k++], 0.02, false); check(id, "integrated", m.loudness().integrated(), ex[k++], 0.02, false); check(id, "true peak", m.loudness().truePeak(), ex[k++], 0.01, false);
    }
  }
  std::printf("C++ engine: %d cases, %d numbers checked, %d failed\n", cases, checked, failures);
  return failures ? 1 : 0;
}
