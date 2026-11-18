#include "audiomixer/latency.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace am::latency {

constexpr double kPi = 3.14159265358979323846;   // M_PI is not standard C++

Level levelOf(double totalMs) { return totalMs < 10.0 ? Level::Low : totalMs <= 20.0 ? Level::Ok : Level::High; }
const char* levelName(Level l) { return l == Level::Low ? "LOW LATENCY" : l == Level::Ok ? "OK" : "HIGH"; }

static std::string ms(double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f ms", v); return b; }

Report assess(const Input& in) {
  Report r; const double rate = in.rate > 0 ? in.rate : 48000; const double blockMs = 1000.0 * std::max(1, in.blockFrames) / rate; r.blockMs = blockMs;
  r.reported = in.inFrames > 0 || in.outFrames > 0;
  if (r.reported) { r.inMs = 1000.0 * in.inFrames / rate; r.outMs = 1000.0 * in.outFrames / rate; }
  else {   // what a driver that says nothing typically adds around one block each way
    double extra = 0;
    if (in.backend == "wasapi") extra = in.exclusive ? 0.0 : 10.0;          // Windows' shared-mode engine runs in 10 ms periods
    else if (in.backend == "dsound") extra = 2 * blockMs + 10.0;            // DirectSound keeps several periods queued over the shared engine
    r.outMs = blockMs + extra; r.inMs = in.hasInput ? blockMs + extra : 0.0;
  }
  if (in.bluetoothOut) r.outMs += 150.0;   // Bluetooth codecs (SBC / AAC / aptX) add roughly 100-250 ms that no driver reports
  r.totalMs = r.inMs + r.outMs; if (!in.hasInput) r.totalMs = r.outMs;
  r.level = levelOf(r.totalMs); r.levelName = levelName(r.level);
  r.verdict = std::string(r.levelName) + ": " + ms(r.totalMs) + (in.hasInput ? " from input to output" : " to the output") + (r.reported ? " (the driver's figure)" : " (estimated: the driver does not report it)")
              + (r.level == Level::Low ? ", fine for live monitoring." : r.level == Level::Ok ? ", fine for playback; a performer may hear a slight delay." : ", fine for listening, too slow to monitor yourself live.");
  // ADVICE: the biggest gain first
  if (in.bluetoothOut) r.advice.push_back("The output is Bluetooth: it always adds about 150 ms. Use wired headphones or speakers for live monitoring.");
  if (in.backend == "dsound") r.advice.push_back("DirectSound is the slowest Windows path: switch to Windows audio (WASAPI) or, with an audio interface, its ASIO driver.");
  if (in.backend == "wasapi" && !in.exclusive) r.advice.push_back("WASAPI shared mode goes through Windows' mixer (about 10 ms each way): EXCLUSIVE mode, or the interface's ASIO driver, removes it.");
  if (r.level != Level::Low && in.blockFrames > 128 && in.xruns == 0) r.advice.push_back("A smaller buffer lowers it: try " + std::to_string(std::max(64, in.blockFrames / 2)) + " frames (" + ms(1000.0 * std::max(64, in.blockFrames / 2) / rate) + " per block).");
  if (in.xruns > 0) r.advice.push_back(std::to_string(in.xruns) + " dropout(s) so far: the buffer is too small for this computer now. Raise it one step, or close heavy programs and plug-ins.");
  if (in.maxBlockUs > 0 && in.maxBlockUs > 0.8 * blockMs * 1000.0) r.advice.push_back("The slowest block took " + ms(in.maxBlockUs / 1000.0) + " of its " + ms(blockMs) + ": very little headroom; a larger buffer or fewer plug-ins keeps it steady.");
  if (in.hybrid && !in.fastCores) r.advice.push_back("Hybrid processor with FAST CORES FOR AUDIO off: switch it on (SETTINGS → SYSTEM OPTIMIZATION) so the sound stays on the performance cores.");
  if (r.advice.empty()) r.advice.push_back(r.level == Level::Low ? "Nothing to change: this is a low-latency setup." : "This is about as low as this device goes; an audio interface with its own ASIO driver goes lower.");
  return r;
}

std::vector<float> testClick(double rate) {
  // a 40 ms logarithmic sweep 300 Hz -> 6 kHz under a Hann window: a sharp, unambiguous correlation peak, quiet enough to be polite
  const std::size_t n = static_cast<std::size_t>(std::max(256.0, 0.040 * (rate > 0 ? rate : 48000))); std::vector<float> v(n);
  const double f0 = 300, f1 = 6000, T = n / rate, k = std::log(f1 / f0);
  for (std::size_t i = 0; i < n; ++i) { const double t = i / rate; const double ph = 2 * kPi * f0 * T / k * (std::exp(t / T * k) - 1);
    const double w = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1)); v[i] = static_cast<float>(0.5 * w * std::sin(ph)); }
  return v;
}

long findDelay(const float* sent, std::size_t nSent, const float* rec, std::size_t nRec, double* confidence) {
  if (confidence) *confidence = 0;
  if (!sent || !rec || nSent < 16 || nRec < nSent) return -1;
  double es = 0; for (std::size_t i = 0; i < nSent; ++i) es += static_cast<double>(sent[i]) * sent[i]; if (es <= 0) return -1;
  // the recording's energy over a sliding window, so the score is a true correlation coefficient (a loud noise does not win)
  std::vector<double> cum(nRec + 1, 0.0); for (std::size_t i = 0; i < nRec; ++i) cum[i + 1] = cum[i] + static_cast<double>(rec[i]) * rec[i];
  long best = -1; double bestScore = 0;
  for (std::size_t lag = 0; lag + nSent <= nRec; ++lag) {
    const double er = cum[lag + nSent] - cum[lag]; if (er <= 1e-12) continue;
    double c = 0; for (std::size_t i = 0; i < nSent; ++i) c += static_cast<double>(sent[i]) * rec[lag + i];
    const double score = c / std::sqrt(es * er);   // polarity counts: a cable that inverts still gives a strong negative peak
    if (std::fabs(score) > std::fabs(bestScore)) { bestScore = score; best = static_cast<long>(lag); }
  }
  if (confidence) *confidence = std::fabs(bestScore);
  return best;
}

}  // namespace am::latency
