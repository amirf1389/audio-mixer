// FILE ANALYZE + AI SETUP: a -20 dBFS 997 Hz sine reads -20 LUFS / -20 dBTP / 3 dB crest and gets +6 dB for streaming, no EQ, no compressor;
// pink noise is balanced to the reference within 2.5 dB per octave; a clipped file is found clipped and limited; a WAV written and read back
// gives the same numbers (a mono file counts once: -23 LUFS); the broadcast target is -23.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "analyze/file_analyze.hpp"
#include "arrange/arrange.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am::fa;

int main() {
  const double fs = 48000; const std::size_t n = static_cast<std::size_t>(fs * 10);
  std::vector<float> sine(n); for (std::size_t i = 0; i < n; ++i) sine[i] = static_cast<float>(0.1 * std::sin(2 * 3.14159265358979 * 997 * i / fs));
  Analysis a = analyse(sine, sine, fs);
  std::printf("sine: %.2f LUFS, TP %.2f, crest %.2f, LRA %.2f\n", a.lufs, a.truePeakDb, a.crestDb, a.lra);
  CHECK(a.ok && std::fabs(a.lufs + 20) < 0.3 && std::fabs(a.truePeakDb + 20) < 0.2 && std::fabs(a.crestDb - 3.01) < 0.2 && a.lra < 0.5 && a.clippedRuns == 0);
  Setup s = suggest(a, "stream"); CHECK(std::fabs(s.gainDb - 6) < 0.3 && !s.eqOn && !s.compOn && s.limOn && s.afterTruePeakDb <= -1 + 1e-9 && !s.reasons.empty());
  CHECK(std::fabs(suggest(a, "broadcast").gainDb + 3) < 0.3 && std::fabs(suggest(a, "custom", -18).targetLufs + 18) < 1e-9);
  // pink noise (Paul Kellet's filter on white noise): every octave within 2.5 dB of the reference
  std::vector<float> pink(n); { unsigned r = 12345; double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (std::size_t i = 0; i < n; ++i) { r = r * 1664525u + 1013904223u; const double w = (static_cast<double>(r >> 8) / 16777216.0) * 2 - 1;
      b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
      pink[i] = static_cast<float>(0.05 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362)); b6 = w * 0.115926; } }
  Analysis p = analyse(pink, pink, fs); std::printf("pink: %.2f LUFS, crest %.2f, bands", p.lufs, p.crestDb); for (double b : p.balanceDb) std::printf(" %+.1f", b); std::printf("\n");
  CHECK(p.ok && p.crestDb > 9); for (int k = 1; k < kBands; ++k) CHECK(std::fabs(p.balanceDb[k]) < 2.5);
  // a dark mix (pink through a one-pole low pass at 500 Hz): the EQ lifts the top
  { std::vector<float> dark(n); double y = 0; const double c = std::exp(-2 * 3.14159265358979 * 500 / fs); for (std::size_t i = 0; i < n; ++i) { y = (1 - c) * pink[i] + c * y; dark[i] = static_cast<float>(y * 3); }
    const Setup d = suggest(analyse(dark, dark, fs), "stream"); CHECK(d.eqOn && d.eqDb[8] > 0.9); }
  // clipped: a sine driven 6 dB into full scale
  std::vector<float> clip(n); for (std::size_t i = 0; i < n; ++i) clip[i] = static_cast<float>(std::clamp(2.0 * std::sin(2 * 3.14159265358979 * 220 * i / fs), -1.0, 1.0));
  Analysis c = analyse(clip, clip, fs); Setup cs = suggest(c, "stream"); bool said = false; for (auto& r : cs.reasons) said |= r.find("clipped") != std::string::npos;
  std::printf("clipped: runs %d, TP %.2f, LUFS %.2f, gain %.2f\n", c.clippedRuns, c.truePeakDb, c.lufs, cs.gainDb);
  CHECK(c.clippedRuns > 100 && c.peakDb > -0.01 && cs.limOn && said && cs.afterTruePeakDb <= -1 + 1e-9);
  // a WAV on disk read back through the decoder the console uses
  const std::string path = "file_analyze_test.wav"; std::string err; CHECK(am::arr::writeWav24(path, sine, sine, fs, err));
  Analysis f = analyseFile(path); std::remove(path.c_str());  CHECK(f.ok && std::fabs(f.lufs - analyse(sine, sine, fs, true).lufs) < 0.05 && std::fabs(f.lufs + 23.01) < 0.3 && std::fabs(f.seconds - 10) < 0.01);
  CHECK(!analyseFile("/no/such/file.flac").ok);
  CHECK(!suggest(analyse(std::vector<float>(n, 0.f), std::vector<float>(n, 0.f), fs), "stream").limOn);
  std::printf("file_analyze: all passed\n"); return 0;
}
