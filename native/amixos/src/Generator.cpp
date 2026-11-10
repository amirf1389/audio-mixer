#include "Generator.h"

#include <cmath>

void Generator::render(float* L, float* R, std::size_t frames, double fs) {
  const bool test = testOn.load(), click = clickOn.load();
  for (std::size_t i = 0; i < frames; ++i) { L[i] = 0.f; R[i] = 0.f; }
  if (!test && !click) { phase_ = 0; pulseT_ = 0; clickT_ = 0; clickEnv_ = 0; beatIdx_ = 0; if (wasClicking_) { beat.store(0); wasClicking_ = false; } return; }
  if (test) {
    const int type = testType.load(); const double f = testFreq.load(); const float g = static_cast<float>(std::pow(10.0, testDb.load() / 20.0)); const double inc = 2 * M_PI * f / fs;
    for (std::size_t i = 0; i < frames; ++i) {
      float v = 0.f;
      switch (type) {
        case Sine: v = static_cast<float>(std::sin(phase_)); phase_ += inc; if (phase_ > 2 * M_PI) phase_ -= 2 * M_PI; break;
        case White: v = noise() * 0.5f; break;
        case Pink: { const float w = noise(); b0_ = 0.99886f * b0_ + w * 0.0555179f; b1_ = 0.99332f * b1_ + w * 0.0750759f; b2_ = 0.96900f * b2_ + w * 0.1538520f; b3_ = 0.86650f * b3_ + w * 0.3104856f; b4_ = 0.55000f * b4_ + w * 0.5329522f; b5_ = -0.7616f * b5_ - w * 0.0168980f;
          v = (b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + w * 0.5362f) * 0.11f; b6_ = w * 0.115926f; break; }
        case Pulse: { pulseT_ += 1.0 / fs; if (pulseT_ >= 1.0) pulseT_ -= 1.0; v = pulseT_ < 0.002 ? static_cast<float>(1.0 - pulseT_ / 0.002) : 0.f; break; }   // a positive-going 2 ms pulse once a second: a speaker in polarity moves out
        default: break;
      }
      v *= g; L[i] += v; R[i] += v;
    }
  }
  if (click) {
    const double period = 60.0 / std::max(20.0, std::min(300.0, bpm.load())); const int bpb = std::max(1, std::min(12, beatsPerBar.load())); const float g = static_cast<float>(std::pow(10.0, clickDb.load() / 20.0));
    const double decay = std::exp(-1.0 / (0.025 * fs));
    for (std::size_t i = 0; i < frames; ++i) {
      if (clickT_ <= 0) { clickT_ += period; clickEnv_ = 1.0; clickPhase_ = 0; clickFreq_ = beatIdx_ == 0 ? 1800.0 : 1000.0; beat.store(beatIdx_ + 1); beatIdx_ = (beatIdx_ + 1) % bpb; }
      clickT_ -= 1.0 / fs;
      if (clickEnv_ > 1e-4) { const float v = static_cast<float>(std::sin(clickPhase_) * clickEnv_) * g; clickPhase_ += 2 * M_PI * clickFreq_ / fs; clickEnv_ *= decay; L[i] += v; R[i] += v; }
    }
    wasClicking_ = true;
  } else if (wasClicking_) { beat.store(0); wasClicking_ = false; clickT_ = 0; beatIdx_ = 0; }
}
