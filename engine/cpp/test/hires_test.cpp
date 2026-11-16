// HIGH-RESOLUTION AUDIO (app/hires): resampling accuracy with r8brain and libsoxr, file round trips in every format, dither statistics.
// argv[1]: the amix_soxr library just built (so the soxr path is tested without installing it).
#include "hires/hires.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am::hr;

static std::vector<double> sine(double f, double rate, std::size_t n, int ch, double amp = 0.5) {
  std::vector<double> x(n * static_cast<std::size_t>(ch)); for (std::size_t i = 0; i < n; ++i) for (int c = 0; c < ch; ++c) x[i * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)] = amp * std::sin(2 * M_PI * f * static_cast<double>(i) / rate + c * 0.3);
  return x;
}
// the worst error against the ideal sine, in dB below the signal, away from both ends (the filter's settling)
static double errDb(const std::vector<double>& y, double f, double rate, int ch, double amp = 0.5) {
  const std::size_t n = y.size() / static_cast<std::size_t>(ch), skip = static_cast<std::size_t>(rate * 0.05); double worst = 0;
  for (std::size_t i = skip; i + skip < n; ++i) for (int c = 0; c < ch; ++c) worst = std::max(worst, std::fabs(y[i * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)] - amp * std::sin(2 * M_PI * f * static_cast<double>(i) / rate + c * 0.3)));
  return 20 * std::log10(std::max(worst, 1e-30) / amp);
}

int main(int argc, char** argv) {
  if (argc > 1) {
#ifdef _WIN32
    _putenv_s("AMIX_SOXR_PATH", argv[1]);
#else
    setenv("AMIX_SOXR_PATH", argv[1], 1);
#endif
  }
  std::string err, used;
  // 1. r8brain: 44.1 -> 96 kHz and 192 -> 44.1 kHz, exact lengths, a 1 kHz tone within -120 dB of the ideal
  for (const auto& [from, to] : {std::pair<double, double>{44100, 96000}, {192000, 44100}, {48000, 768000}}) {
    const auto x = sine(1000, from, static_cast<std::size_t>(from / 2), 2); std::vector<double> y;
    CHECK(resample(x, 2, from, to, "r8brain", 2, y, err, &used)); CHECK(used == "r8brain");
    CHECK(y.size() / 2 == static_cast<std::size_t>(std::llround(from / 2 * to / from)));
    const double e = errDb(y, 1000, to, 2); std::printf("r8brain %.0f -> %.0f: %.1f dB\n", from, to, e); CHECK(e < -120);
  }
  // 2. libsoxr from the library built next to this test
  CHECK(soxrAvailable());
  { const auto x = sine(1000, 44100, 22050, 2); std::vector<double> y; CHECK(resample(x, 2, 44100, 96000, "soxr", 1, y, err, &used)); CHECK(used == "soxr");
    CHECK(y.size() / 2 == 48000); const double e = errDb(y, 1000, 96000, 2); std::printf("soxr 44100 -> 96000: %.1f dB\n", e); CHECK(e < -110); }
  { std::vector<double> y; CHECK(resample(sine(1000, 44100, 1000, 1), 1, 44100, 48000, "auto", 1, y, err, &used)); CHECK(used == "soxr"); }
  { std::vector<double> y; CHECK(!resample(sine(1000, 44100, 100, 1), 1, 44100, 48000, "nonsense", 1, y, err)); CHECK(!err.empty()); }
  // 3. streaming in odd blocks gives what the one-shot gives
  { auto r = Resampler::make(48000, 96000, 1, "r8brain", 1, err); CHECK(r); const auto x = sine(997, 48000, 24000, 1); std::vector<std::vector<double>> out;
    for (std::size_t at = 0; at < x.size(); at += 777) { const std::size_t n = std::min<std::size_t>(777, x.size() - at); const double* p = x.data() + at; r->process(&p, static_cast<int>(n), out); }
    r->flush(out); CHECK(out[0].size() >= 48000); std::vector<double> y(out[0].begin(), out[0].begin() + 48000); CHECK(errDb(y, 997, 96000, 1) < -120); }
  // 4. files: every format at 384 kHz, back within one step of its word length (no dither), float exact
  const std::string dir = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
  const auto x = sine(5000, 384000, 38400, 2, 0.7);
  for (const std::string& f : writeFormats()) {
    const std::string path = dir + "/amix-hires-test." + (f.rfind("flac", 0) == 0 ? "flac" : "wav");
    CHECK(write(path, x, 2, 384000, f, "none", err)); Audio a; CHECK(read(path, a, err)); std::remove(path.c_str());
    CHECK(a.info.channels == 2 && a.info.rate == 384000 && a.info.frames == 38400 && a.samples.size() == x.size());
    const bool isF = f.find('f', 1) != std::string::npos && f.rfind("wavf", 0) == 0; const int bits = std::stoi(f.substr(f.rfind("flac", 0) == 0 ? 4 : isF ? 4 : 3));
    double worst = 0; for (std::size_t i = 0; i < x.size(); ++i) worst = std::max(worst, std::fabs(a.samples[i] - x[i]));
    const double allowed = isF ? (bits == 64 ? 0.0 : 1e-7) : 1.01 / std::ldexp(1.0, bits - 1);
    std::printf("%s: bits %d, container %s, worst %.3g (allowed %.3g)\n", f.c_str(), a.info.bits, a.info.container.c_str(), worst, allowed);
    CHECK(a.info.bits == bits && a.info.isFloat == isF && worst <= allowed);
  }
  CHECK(!write(dir + "/x.wav", x, 2, 384000, "mp3", "none", err));
  { Audio a; CHECK(!read(dir + "/no-such-file.wav", a, err)); }
  // 5. dither: TPDF on silence has the textbook noise (1/6 LSB^2 + the rounding); noise shaping moves it out of the low band
  { const int n = 4096; Ditherer t(1, 16, "tpdf"), s(1, 16, "shaped"), z(1, 16, "none"); std::vector<double> a(n), b(n); double pt = 0; int nonzero = 0;
    for (int i = 0; i < n; ++i) { const double x0 = 1e-7 * std::sin(i * 0.01); a[i] = t.next(x0, 0); b[i] = s.next(x0, 0); pt += a[i] * a[i]; nonzero += z.next(x0, 0) != 0; }
    auto band = [&](const std::vector<double>& v) { double p = 0; for (int k = 1; k < n / 8; ++k) { double re = 0, im = 0; for (int i = 0; i < n; ++i) { re += v[i] * std::cos(2 * M_PI * k * i / n); im -= v[i] * std::sin(2 * M_PI * k * i / n); } p += re * re + im * im; } return p; };   // the lowest eighth of the band (0 .. 2.7 kHz at 44.1)
    pt /= n; const double lowT = band(a), lowS = band(b); std::printf("dither: TPDF %.3f LSB^2, low band TPDF %.3g vs shaped %.3g\n", pt, lowT, lowS);
    CHECK(nonzero == 0 && pt > 0.15 && pt < 0.4 && lowS < lowT * 0.5); }
  // 6. the libraries report themselves
  { const std::string j = infoJson(); CHECK(j.find("\"r8brain\"") != std::string::npos && j.find("libFLAC") != std::string::npos && j.find("\"soxr\"") != std::string::npos); }
  std::printf("hires: all checks passed\n");
  return 0;
}
