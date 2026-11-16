// THE SAMPLE PADS in the remote service: the console's 100 built-in one-shots, played into the master while the service runs the sound
// (phones linked: the console's own engine is stopped, so its pads come here). The same player as the console's SAMPLES page: 16 voices,
// round-robin stealing, a gain. The bank is rendered at the device rate on the control thread; the audio thread only reads it.
#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "samples/samples.hpp"

namespace am {

class Pads final : public Runtime::ExtraSource {
 public:
  static constexpr int kCount = 100, kVoices = 16;
  struct Clip { std::vector<float> L, R; };
  using Bank = std::array<Clip, kCount>;
  std::atomic<double> gainDb{-6.0};
  std::atomic<int> active{0};

  /** render the bank at [rate] (control thread; a few hundred ms once per rate) */
  void prepare(double rate) {
    if (rate <= 0 || rate == rate_) return;
    auto b = std::make_shared<Bank>(); for (int id = 1; id <= kCount; ++id) samples::render(id, rate, (*b)[id - 1].L, (*b)[id - 1].R);
    std::atomic_store(&bank_, std::shared_ptr<const Bank>(b)); rate_ = rate;
  }
  bool ready() const { return std::atomic_load(&bank_) != nullptr; }
  bool trigger(int id) { if (id < 1 || id > kCount) return false; const unsigned w = w_.load(); if (w - r_.load() >= kRing) return false; ring_[w % kRing] = id; w_.store(w + 1); return true; }
  void stopAll() { stop_.store(true); }

  void render(float* L, float* R, std::size_t frames, double) override {
    for (std::size_t i = 0; i < frames; ++i) { L[i] = 0.f; R[i] = 0.f; }
    const std::shared_ptr<const Bank> b = std::atomic_load(&bank_); if (!b) return;
    if (stop_.exchange(false)) for (Voice& v : voices_) v.id = 0;
    while (r_.load() != w_.load()) { const int id = ring_[r_.load() % kRing]; r_.store(r_.load() + 1);
      int slot = -1; for (int k = 0; k < kVoices; ++k) if (voices_[k].id == 0) { slot = k; break; }
      if (slot < 0) slot = static_cast<int>(next_++ % kVoices);
      voices_[slot].id = id; voices_[slot].pos = 0; }
    const float g = static_cast<float>(std::pow(10.0, gainDb.load() / 20.0)); int act = 0;
    for (Voice& v : voices_) { if (v.id == 0) continue; const Clip& c = (*b)[static_cast<std::size_t>(v.id - 1)]; const std::size_t n = c.L.size(); ++act;
      for (std::size_t i = 0; i < frames && v.pos < n; ++i, ++v.pos) { L[i] += c.L[v.pos] * g; R[i] += c.R[v.pos] * g; }
      if (v.pos >= n) v.id = 0; }
    active.store(act);
  }

 private:
  struct Voice { int id = 0; std::size_t pos = 0; };
  static constexpr unsigned kRing = 64;
  std::shared_ptr<const Bank> bank_; double rate_ = 0;
  std::array<int, kRing> ring_{}; std::atomic<unsigned> w_{0}, r_{0}; std::atomic<bool> stop_{false};
  std::array<Voice, kVoices> voices_{}; unsigned next_ = 0;
};

}  // namespace am
