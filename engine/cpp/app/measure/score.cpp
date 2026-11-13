#include "score.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace am::measure {
namespace {
double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
std::string f1(const char* f, double a) { char b[160]; std::snprintf(b, sizeof b, f, a); return b; }
}  // namespace

Score soundScore(const ScoreInput& in) {
  Score s; if (in.shortTerm < -60) return s; s.valid = true;
  // LOUDNESS
  { const double off = std::fabs(in.shortTerm - in.target), p = clampd(20 - std::max(0.0, off - 1) * 4, 0, 20);
    s.parts.push_back({"LOUDNESS", p, 20, off <= 1 ? "On target." : in.shortTerm > in.target ? f1("%.1f LU too loud: bring the master down or lower the limiter's gain.", off) : f1("%.1f LU too quiet: raise the master or use AUTO LEVEL.", off)}); }
  // PEAKS
  { double p = 20; std::string tip = "Peaks have room.";
    if (in.masterClip) { p = 0; tip = "The master clipped: lower the master or put the MAXIMIZER last with its ceiling at -1 dB."; }
    else if (in.truePeak > -1) { p = clampd(20 - (in.truePeak + 1) * 10, 0, 20); tip = f1("True peak %.1f dBTP: keep it under -1 dBTP so encoders (MP3, AAC, streaming) do not distort.", in.truePeak); }
    s.parts.push_back({"PEAKS", p, 20, tip}); }
  // DYNAMICS
  { const double crest = in.peakDb - in.rmsDb; double p = 15; std::string tip = f1("Crest factor %.1f dB: natural.", crest);
    if (crest < 8) { p = clampd(15 - (8 - crest) * 3, 0, 15); tip = f1("Crest factor %.1f dB: squashed. Ease the compressors or use DE-SQUASH.", crest); }
    else if (crest > 14) { p = clampd(15 - (crest - 14) * 1.5, 5, 15); tip = f1("Crest factor %.1f dB: very spiky; a little bus compression (GLUE COMP) would hold it together.", crest); }
    if (in.overCompStrips > 0) { p = std::max(0.0, p - 2.0 * in.overCompStrips); tip += " " + std::to_string(in.overCompStrips) + " channel(s) over-compressed."; }
    s.parts.push_back({"DYNAMICS", p, 15, tip}); }
  // TONE: a straight line through the bands that carry the music (40 Hz .. 16 kHz), then how far the spectrum strays from it
  { double p = 10; std::string tip = "Not enough spectrum to judge.";
    if (in.spectrum.size() >= 20) {
      std::vector<double> x, y; const double n = static_cast<double>(in.spectrum.size());
      for (std::size_t i = 0; i < in.spectrum.size(); ++i) { const double f = 20 * std::pow(1000.0, static_cast<double>(i) / (n - 1)); if (f < 40 || f > 16000 || in.spectrum[i] < -100) continue; x.push_back(std::log2(f)); y.push_back(in.spectrum[i]); }
      if (x.size() >= 10) {
        double mx = 0, my = 0; for (std::size_t i = 0; i < x.size(); ++i) { mx += x[i]; my += y[i]; } mx /= x.size(); my /= y.size();
        double sxy = 0, sxx = 0; for (std::size_t i = 0; i < x.size(); ++i) { sxy += (x[i] - mx) * (y[i] - my); sxx += (x[i] - mx) * (x[i] - mx); }
        const double slope = sxx > 0 ? sxy / sxx : 0; double r2 = 0, worst = 0; std::size_t wi = 0;
        for (std::size_t i = 0; i < x.size(); ++i) { const double r = y[i] - (my + slope * (x[i] - mx)); r2 += r * r; if (std::fabs(r) > std::fabs(worst)) { worst = r; wi = i; } }
        const double rough = std::sqrt(r2 / x.size()); s.tiltDbPerOct = slope; s.roughnessDb = rough;
        const double tiltOff = slope > -1 ? slope + 1 : slope < -5 ? -5 - slope : 0;
        p = clampd(20 - tiltOff * 3 - std::max(0.0, rough - 3) * 2.5 - std::max(0.0, std::fabs(worst) - 6) * 0.8, 0, 20);   // the average stray and the single worst bump or hole
        const double wf = std::pow(2.0, x[wi]); const std::string where = wf >= 1000 ? f1("%.1f kHz", wf / 1000) : f1("%.0f Hz", wf);
        if (slope > -1) tip = f1("Bright / thin (%.1f dB per octave): lower the top or raise the low end.", slope);
        else if (slope < -5) tip = f1("Dark / heavy (%.1f dB per octave): open the top (AIR) or ease the bass.", slope);
        else if (rough > 3) tip = (worst > 0 ? "A bump" : "A hole") + std::string(" around ") + where + f1(" (%.1f dB): a wide, gentle EQ move there will smooth it.", std::fabs(worst));
        else tip = "Smooth and balanced.";
      } }
    s.parts.push_back({"TONE", p, 20, tip}); }
  // STEREO
  { const double c = in.correlation; double p = 15; std::string tip = "Healthy stereo.";
    if (c < 0) { p = clampd(5 + c * 5, 0, 5); tip = "Out of phase: parts of the mix vanish in mono. Check polarity (Ø) and wide effects."; }
    else if (c < 0.3) { p = 5 + c / 0.3 * 10; tip = "Very wide: check it in mono; narrow the low end (STEREO IMAGER)."; }
    s.parts.push_back({"STEREO", p, 15, tip}); }
  // STABILITY
  { double p = 10; std::string tip = "No dropouts, no clipping channels.";
    if (in.xruns > 0) { p -= std::min(6.0, 2.0 * in.xruns); tip = "Dropouts: raise the buffer size in SETTINGS."; }
    if (in.clipStrips > 0) { p -= std::min(4.0, 1.0 * in.clipStrips); tip += " " + std::to_string(in.clipStrips) + " channel(s) clipping: lower their TRIM."; }
    s.parts.push_back({"STABILITY", std::max(0.0, p), 10, tip}); }
  for (const auto& q : s.parts) s.total += q.points; s.total = std::round(s.total);
  s.grade = s.total >= 90 ? "A" : s.total >= 80 ? "B" : s.total >= 70 ? "C" : s.total >= 60 ? "D" : "E";
  return s;
}

}  // namespace am::measure
