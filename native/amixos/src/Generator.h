// The TOOLS page's audio: a test-signal generator (sine at any frequency, pink and white noise, a polarity pulse) and a metronome click, both
// summed into the master as a Runtime::ExtraSource; and a SourceFanout so this and the ARRANGE player share the runtime's single extra source.
// Parameters are atomics written by the UI thread and read on the audio thread; render() neither allocates nor locks.
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

#include "audiomixer/runtime.hpp"

class Generator : public am::Runtime::ExtraSource {
 public:
  enum Type : int { Sine = 0, Pink = 1, White = 2, Pulse = 3 };
  std::atomic<bool> testOn{false}, clickOn{false};
  std::atomic<int> testType{Sine}, beatsPerBar{4};
  std::atomic<double> testFreq{1000}, testDb{-18}, bpm{120}, clickDb{-12};
  std::atomic<int> beat{0};                       // the beat being sounded (1-based) for the UI, 0 when the click is off
  void render(float* L, float* R, std::size_t frames, double fs) override;
 private:
  double phase_ = 0, pulseT_ = 0, clickT_ = 0, clickEnv_ = 0, clickPhase_ = 0, clickFreq_ = 1000; int beatIdx_ = 0; bool wasClicking_ = false;
  float b0_ = 0, b1_ = 0, b2_ = 0, b3_ = 0, b4_ = 0, b5_ = 0, b6_ = 0;   // pink filter state
  std::uint32_t rng_ = 0x9e3779b9u;
  float noise() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return static_cast<float>(rng_) / 2147483648.f - 1.f; }
};

struct SourceFanout : am::Runtime::ExtraSource {
  std::atomic<am::Runtime::ExtraSource*> a{nullptr}, b{nullptr};
  SourceFanout() { tL_.assign(kMax, 0.f); tR_.assign(kMax, 0.f); }
  void render(float* L, float* R, std::size_t frames, double fs) override {
    am::Runtime::ExtraSource* pa = a.load(); am::Runtime::ExtraSource* pb = b.load();
    if (pa) pa->render(L, R, frames, fs); else for (std::size_t i = 0; i < frames; ++i) { L[i] = 0.f; R[i] = 0.f; }
    if (pb && frames <= kMax) { pb->render(tL_.data(), tR_.data(), frames, fs); for (std::size_t i = 0; i < frames; ++i) { L[i] += tL_[i]; R[i] += tR_[i]; } }
  }
 private:
  static constexpr std::size_t kMax = 16384; std::vector<float> tL_, tR_;
};
