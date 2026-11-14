// The I/O FRAME's shared state, header-only so the engine core and the rack effects share one copy without a link dependency:
//  REFERENCE TAP  the master output as it leaves the console (mono, the last second), written by the mixer after the master stage, read by
//                 the echo cancellers (AEC, telephone hybrid) as the "far end" signal. Lock-free: one writer (the audio thread), readers on
//                 the same thread (rack inserts run inside the same callback), so a plain ring with an atomic write position is enough.
//  LOGIC          12 control inputs and 6 logic outputs (the GPIO of the frame): inputs are set by OSC, MIDI, a serial GPIO box or the
//                 screen; outputs are set by logic objects in the racks and read by the same paths. Atomics: any thread.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace am::frame {

constexpr std::size_t kRefLen = 65536;   // 1.36 s at 48 kHz (a power of two: the index wraps with a mask)
struct RefTap {
  std::array<float, kRefLen> buf{};
  std::atomic<std::size_t> w{0};          // total samples written
  void write(const float* L, const float* R, std::size_t n) {
    std::size_t p = w.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; ++i, ++p) buf[p & (kRefLen - 1)] = 0.5f * (L[i] + R[i]);
    w.store(p, std::memory_order_release);
  }
  // the sample written `age` samples before the newest one (age 0 = the newest)
  float at(std::size_t age) const { const std::size_t p = w.load(std::memory_order_acquire); return age < p ? buf[(p - 1 - age) & (kRefLen - 1)] : 0.f; }
  std::size_t written() const { return w.load(std::memory_order_acquire); }
};
inline RefTap& refTap() { static RefTap t; return t; }

constexpr int kLogicIn = 12, kLogicOut = 6;
struct Logic {
  std::array<std::atomic<bool>, kLogicIn> in{};
  std::array<std::atomic<bool>, kLogicOut> out{};
  std::atomic<unsigned> outSeq{0};        // bumps when an output changes (the interface and the GPIO paths poll it)
  void setOut(int i, bool v) { if (i < 0 || i >= kLogicOut) return; if (out[static_cast<std::size_t>(i)].exchange(v) != v) outSeq.fetch_add(1); }
  bool getIn(int i) const { return i >= 0 && i < kLogicIn && in[static_cast<std::size_t>(i)].load(); }
};
inline Logic& logic() { static Logic l; return l; }

}  // namespace am::frame
