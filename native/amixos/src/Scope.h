// The WAVEFORM and PHASE pages' data: a Runtime::Tap that keeps the last second of the master (L, R) and of one strip (pre-fader) in
// rings written by the audio thread; the UI reads a triggered window (oscilloscope), the recent L / R pairs (goniometer) and the figures
// (Vpp, Vrms, frequency from zero crossings, crest). A TapFanout lets the recorder and this scope share the runtime's single tap.
#pragma once
#include <atomic>
#include <complex>
#include <cstdint>
#include <vector>

#include "audiomixer/runtime.hpp"

class ScopeTap : public am::Runtime::Tap {
 public:
  ScopeTap();
  void setStrip(int s) { strip_.store(s); }
  void setSampleRate(double fs) { fs_ = fs > 0 ? fs : 48000; }
  double sampleRate() const { return fs_; }
  void onBlock(const float* const* strips, int nStrips, const float* L, const float* R, std::size_t frames, double sampleRate) override;
  // a window of `points` samples ending at the newest sample, `spanSamples` long, triggered on the first rising zero crossing of the master L
  // within the last half of the ring when `trigger` is set (so a periodic signal stands still). out: ch 0 = master L (or mono), 1 = R, 2 = strip
  void window(int ch, std::size_t spanSamples, int points, bool trigger, std::vector<float>& out) const;
  void pairs(int points, std::vector<float>& lr) const;   // the most recent L / R pairs, interleaved
  struct Figures { double vpp = 0, vrms = 0, freq = 0, crest = 0; };
  Figures figures(int ch, std::size_t spanSamples) const;
  // a 1/3-octave spectrum of the newest 4096 samples: 31 bands, 20 Hz .. 20 kHz, dBFS (a full-scale sine reads about 0 in its band)
  static constexpr int kBands = 31;
  void spectrum(int ch, std::vector<float>& bandsDb) const;
  // CALIBRATE: with the polarity pulse on the master and a loopback cable into the scope strip, the delay from the master's pulse to the
  // strip's echo within the last second; false when no pulse or no echo (echo threshold: 1/40 of the master pulse)
  bool roundTrip(double& ms, double& returnDb) const;
  // THE ANALYSER SOURCE (the WATERFALL): one mono signal kept in its own ring, chosen apart from the scope's strip. kAnaMaster = the master
  // (L + R) / 2, 0 .. n-1 a strip (pre-fader), kAnaBus + k bus k, kAnaMonitor the control-room monitor.
  static constexpr int kAnaMaster = -1, kAnaBus = 1000, kAnaMonitor = 2000;
  static constexpr std::size_t kAnaRing = 1 << 14;   // 16384 samples: room for the largest FFT
  void setAnalysisSource(int s) { if (s != anaSrc_.load()) { anaSrc_.store(s); } }
  int analysisSource() const { return anaSrc_.load(); }
  // the power spectrum of the newest n samples (n a power of two, at most kAnaRing): Hann window, the shared FFT in `work` (n points,
  // the caller's), power[k] for k = 0 .. n / 2 scaled so that a full-scale sine reads 1. No allocation.
  void analysisPower(std::complex<double>* work, std::size_t n, float* power) const;
  void onBuses(const float* const* busOut, int nBuses, const float* monL, const float* monR, std::size_t frames) override;
 private:
  void anaWrite(const float* a, const float* b, std::size_t frames);
  std::vector<float> ana_; std::atomic<std::size_t> anaHead_{0}; std::atomic<int> anaSrc_{kAnaMaster};
  static constexpr std::size_t kRing = 1 << 16;           // 65536 samples: 1.36 s at 48 k
  std::vector<float> ring_[3]; std::atomic<std::size_t> head_{0}; std::atomic<int> strip_{0}; double fs_ = 48000;
};

struct TapFanout : am::Runtime::Tap {
  std::atomic<am::Runtime::Tap*> a{nullptr}, b{nullptr}, c{nullptr};   // a: recorder, b: scope, c: ACOUSTICS
  void onBuses(const float* const* busOut, int nBuses, const float* monL, const float* monR, std::size_t frames) override {
    if (am::Runtime::Tap* t = a.load()) t->onBuses(busOut, nBuses, monL, monR, frames);
    if (am::Runtime::Tap* t = b.load()) t->onBuses(busOut, nBuses, monL, monR, frames);
    if (am::Runtime::Tap* t = c.load()) t->onBuses(busOut, nBuses, monL, monR, frames);
  }
  void onBlock(const float* const* strips, int nStrips, const float* L, const float* R, std::size_t frames, double fs) override {
    if (am::Runtime::Tap* t = a.load()) t->onBlock(strips, nStrips, L, R, frames, fs);
    if (am::Runtime::Tap* t = b.load()) t->onBlock(strips, nStrips, L, R, frames, fs);
    if (am::Runtime::Tap* t = c.load()) t->onBlock(strips, nStrips, L, R, frames, fs);
  }
};
