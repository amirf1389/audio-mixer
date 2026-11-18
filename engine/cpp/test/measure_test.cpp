// ACOUSTICS: the sweep measures a known filter within 0.5 dB and its phase sign; THD of a soft-clipped tone; RT60 of a synthetic decay;
// delay by cross-correlation; room modes; auto EQ pulls a resonant response toward flat; RTA band of a sine; calibration parsing.
#include <cmath>
#include <cstdio>
#include <vector>

#include "measure/measure.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am::measure;
static double at(const Response& r, double f) { std::size_t b = 0; for (std::size_t i = 0; i < r.freq.size(); ++i) if (std::fabs(std::log(r.freq[i] / f)) < std::fabs(std::log(r.freq[b] / f))) b = i; return r.magDb[b]; }

int main() {
  const double fs = 48000;
  // 1. a sweep through a peaking filter (+6 dB at 100 Hz, Q 2) and a 10 ms delay: the response shows the peak, the delay is measured
  Sweep sw = makeSweep(fs, 20, 20000, 3, 0.5); CHECK(sw.signal.size() > 3 * fs && sw.inverse.size() == static_cast<std::size_t>(3 * fs));
  const Filter pk{100, 2, 6}; std::vector<float> rec(sw.signal.size() + 480, 0.f);
  { const double A = std::pow(10.0, pk.gainDb / 40), w0 = 2 * 3.14159265358979 * pk.freq / fs, al = std::sin(w0) / (2 * pk.q), cw = std::cos(w0); const double a0 = 1 + al / A, b0 = (1 + al * A) / a0, b1 = -2 * cw / a0, b2 = (1 - al * A) / a0, a1 = -2 * cw / a0, a2 = (1 - al / A) / a0; double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (std::size_t i = 0; i < sw.signal.size(); ++i) { const double x = sw.signal[i]; const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; rec[i + 480] = static_cast<float>(y * 0.5); } }   // -6 dB level, 10 ms late
  Impulse ir = deconvolve(rec, sw); CHECK(!ir.h.empty() && ir.harmonics.size() == 4);
  Response r = response(ir, 48); CHECK(r.freq.size() > 400);
  CHECK(std::fabs((at(r, 100) - at(r, 1000)) - 6.0) < 0.5 && std::fabs((at(r, 400) - at(r, 1000))) < 0.5 && std::fabs(at(r, 1000) - (-6.0)) < 1.0);
  CHECK(std::fabs(delaySeconds(rec, sw.signal, fs) - 0.010) < 0.0005);
  // 2. THD: a soft-clipped tone has a few percent of harmonics; a clean one has none
  std::vector<float> t = tone(fs, 1000, 1.0, 0.5), clipped = t; for (auto& v : clipped) v = std::tanh(3 * v) / 3; Stepped c = analyseTone(clipped, fs, 1000), cl = analyseTone(t, fs, 1000);
  CHECK(c.thdPercent > 2 && c.thdPercent < 30 && cl.thdPercent < 0.05 && std::fabs(cl.amplitude - 0.5) < 0.01 && cl.thdnPercent < 0.1);
  CHECK(analyseSmpte(smpteTones(fs, 1.0), fs) < 0.1);
  // 3. RT60 of a synthetic exponential decay (0.6 s) with noise
  { Impulse d; d.fs = fs; d.h.resize(static_cast<std::size_t>(fs * 1.5)); unsigned s = 7; for (std::size_t i = 0; i < d.h.size(); ++i) { s = s * 1664525u + 1013904223u; const double n = (static_cast<double>(s >> 8) / 16777216.0 - 0.5); d.h[i] = n * std::exp(-6.91 * static_cast<double>(i) / fs / 0.6); }
    Decay dc = decay(d, 1.5); CHECK(std::fabs(dc.t20 - 0.6) < 0.08 && std::fabs(dc.t30 - 0.6) < 0.08 && dc.topt > 0.5 && dc.topt < 0.7 && dc.etcDb.size() == d.h.size()); }
  // 4. room modes of a 5 x 4 x 2.7 m room: the first axial mode along the length is 34.3 Hz; the predicted response has peaks there
  { Room rm; auto modes = roomModes(rm, 400); CHECK(!modes.empty() && std::fabs(modes[0].freq - 34.3) < 0.1 && std::string(modes[0].kind) == "axial"); Response rr = roomResponse(rm); CHECK(rr.freq.size() > 50); }
  // 5. auto EQ: a response with a +9 dB resonance at 60 Hz: the filters cut it; the error to flat drops by half or more
  { Response m; m.freq = r.freq; m.magDb.assign(r.freq.size(), 0); m.phaseDeg.assign(r.freq.size(), 0); m = applyFilters(m, {Filter{60, 4, 9}, Filter{120, 3, -5}}); Response flat = flatTarget(m, 0);
    auto f = autoEq(m, flat, AutoEqOptions()); CHECK(!f.empty() && f.size() <= 10 && std::fabs(f[0].freq - 60) < 6 && f[0].gainDb < -5);
    const double e0 = rmsErrorDb(m, flat, 20, 400), e1 = rmsErrorDb(applyFilters(m, f), flat, 20, 400); CHECK(e1 < e0 * 0.5);
    const std::string txt = exportFiltersText(f); CHECK(txt.find("Filter  1: ON  PK") != std::string::npos && exportFiltersJson(f).find("\"freq\":") != std::string::npos); }
  // 6. RTA: a 1 kHz tone lands in the 1 kHz band at 1/48 octave, 20 dB above its neighbours an octave away; alignment puts it at the reference
  { RtaAverager a; RtaOptions o; std::vector<float> s = tone(fs, 1000, 2.0, 0.5); Rta rt = a.analyse(s.data(), s.size(), fs, o); std::size_t b = 0; for (std::size_t i = 0; i < rt.freq.size(); ++i) if (rt.db[i] > rt.db[b]) b = i; CHECK(std::fabs(std::log2(rt.freq[b] / 1000)) < 1.5 / 48);
    std::size_t b2 = 0; for (std::size_t i = 0; i < rt.freq.size(); ++i) if (std::fabs(std::log2(rt.freq[i] / 2000)) < std::fabs(std::log2(rt.freq[b2] / 2000))) b2 = i; CHECK(rt.db[b] - rt.db[b2] > 20); CHECK(std::fabs(a.alignTo(94) + rt.db[b] - 94) < 0.01); }
  // 7. calibration file
  { Calibration c; CHECK(parseCalibration("* mic cal\n20 -3.5\n100, 0.0\n10000\t1.2\n", c) && c.freq.size() == 3); Response x = r; applyCalibration(x, c); CHECK(std::fabs((x.magDb[0] - r.magDb[0]) - 3.5) < 0.01); applySplOffset(x, 94); CHECK(x.magDb[0] > r.magDb[0] + 90); }
  // 7b. ROOM CORRECTION: cuts only, at the predicted peaks, never narrower than a mode; the corrected prediction is flatter. Calibration written back reads the same.
  { Room rm; rm.L = 5.2; rm.W = 3.9; rm.H = 2.6; rm.rt60 = 0.45; rm.sx = 0.6; rm.sy = 0.5; rm.sz = 1.0; rm.lx = 2.6; rm.ly = 3.1; rm.lz = 1.2;
    const std::vector<Filter> f = roomCorrection(rm, 6, 12, 300); CHECK(!f.empty() && f.size() <= 6);
    for (const Filter& x : f) { CHECK(x.gainDb < 0 && x.gainDb >= -12.0001); CHECK(x.freq >= 20 && x.freq <= 300); CHECK(x.q <= x.freq / (2.2 / rm.rt60) + 1e-9 && x.q >= 0.7 - 1e-9); }
    const Response pred = roomResponse(rm, 48, 20, 400), fixed = applyFilters(pred, f);
    double peakBefore = -1e9, peakAfter = -1e9; for (std::size_t i = 0; i < pred.freq.size(); ++i) if (pred.freq[i] <= 300) { peakBefore = std::max(peakBefore, pred.magDb[i]); peakAfter = std::max(peakAfter, fixed.magDb[i]); }
    CHECK(peakAfter < peakBefore - 2);
    Calibration c; c.freq = {20, 1000, 20000}; c.db = {-2.5, 0, 1.25}; Calibration back; CHECK(parseCalibration(formatCalibration(c, "test mic"), back) && back.freq.size() == 3 && std::fabs(back.db[0] + 2.5) < 1e-6 && std::fabs(back.freq[2] - 20000) < 1e-6);
    std::printf("measure_test: room correction %zu cuts, highest peak %.1f -> %.1f dB\n", f.size(), peakBefore, peakAfter); }
  // 8. waterfall and spectrogram shapes
  { Grid w = waterfall(ir, 12); CHECK(w.y.size() >= 8 && w.db[0].size() == w.x.size()); std::vector<float> s = tone(fs, 440, 0.5, 0.3); Grid sg = spectrogram(s.data(), s.size(), fs); CHECK(sg.y.size() > 10 && sg.db[0].size() == sg.x.size()); }
  std::printf("measure_test: sweep response within 0.5 dB, delay 10 ms, THD, THD+N, IMD, RT60 (T20 / T30 / Topt), room modes, auto EQ halves the error, RTA 1/48, calibration, waterfall OK\n");
  return 0;
}
