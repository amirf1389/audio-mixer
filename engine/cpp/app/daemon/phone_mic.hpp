// PHONE MIC: a phone used as a microphone / talkback into the console. The phone POSTs raw 16-bit little-endian mono PCM to /api/mic
// (?rate=8000..96000, default 48000) in short chunks; this NetInput plays it on network input 0 (Runtime::kNetInBase), resampled
// linearly to the engine's rate. The strip the phone picked (op "phoneMic") reads that input while the mic is on.
// Jitter: playback starts once kPrimeMs are buffered and pauses (silence, then primes again) when the phone falls behind; when it runs
// ahead by more than kMaxMs the oldest audio is dropped so the delay stays short. push() runs on an HTTP thread, pull() on the audio
// thread (PcmRing: single producer, single consumer, lock-free). Only one phone talks at a time (the daemon serialises pushes).
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "pcm_ring.hpp"

namespace am {

class PhoneMic final : public Runtime::NetInput {
 public:
  static constexpr int kPrimeMs = 60, kMaxMs = 400;
  PhoneMic() : ring_(96000) {}
  // the phone's chunk: returns the samples taken (a chunk that does not fit is cut)
  std::size_t push(const std::int16_t* s, std::size_t n, int rate) {
    if (rate < 8000 || rate > 96000) return 0;
    rate_.store(rate);
    const std::size_t maxN = static_cast<std::size_t>(rate) * kMaxMs / 1000;
    if (ring_.size() + n > maxN) drop_.store(ring_.size() + n - maxN);   // the audio thread drops the oldest: the reader owns the read side
    float pk = 0; for (std::size_t i = 0; i < n; ++i) pk = std::max(pk, std::abs(s[i] / 32768.0f));
    peak_.store(std::max(peak_.load() * 0.5f, pk)); ++chunks_;
    return ring_.push(s, n);
  }
  void pull(float* const* ch, int channels, std::size_t frames, double fs) override {
    for (int c = 0; c < channels; ++c) std::fill(ch[c], ch[c] + frames, 0.0f);
    if (channels < 1 || fs <= 0) return;
    const int rate = rate_.load();
    if (const std::size_t d = drop_.exchange(0)) { std::int16_t tmp[512]; std::size_t left = d; while (left) { const std::size_t k = ring_.pop(tmp, std::min<std::size_t>(left, 512)); if (!k) break; left -= k; } }
    if (!primed_) { if (ring_.size() < static_cast<std::size_t>(rate) * kPrimeMs / 1000) return; primed_ = true; }
    const double step = rate / fs; float* out = ch[0];
    for (std::size_t i = 0; i < frames; ++i) {
      while (pos_ >= 1.0) { a_ = b_; std::int16_t x; if (ring_.pop(&x, 1) != 1) { primed_ = false; ++underruns_; return; } b_ = x / 32768.0f; pos_ -= 1.0; }
      out[i] = static_cast<float>(a_ + (b_ - a_) * pos_); pos_ += step;
    }
  }
  float peak() const { return peak_.load(); }       // the last chunk's peak, 0..1 (decays chunk by chunk)
  unsigned chunks() const { return chunks_.load(); }
  unsigned underruns() const { return underruns_.load(); }
  std::size_t buffered() const { return ring_.size(); }
  int rate() const { return rate_.load(); }

 private:
  PcmRing ring_; std::atomic<int> rate_{48000}; std::atomic<std::size_t> drop_{0}; std::atomic<float> peak_{0}; std::atomic<unsigned> chunks_{0}, underruns_{0};
  bool primed_ = false; double pos_ = 1.0; float a_ = 0, b_ = 0;   // audio thread only
};

}  // namespace am
