// Lock-free single-producer / single-consumer ring. One thread writes (a capture thread),
// one thread reads (the audio callback). No locks, no allocation after construction.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace amfb {

template <class T>
class SpscRing {
 public:
  explicit SpscRing(std::size_t minCapacity) {
    std::size_t c = 2;
    while (c < minCapacity) c <<= 1;
    buf_.resize(c);
    mask_ = c - 1;
  }
  std::size_t capacity() const noexcept { return mask_ + 1; }
  std::size_t available() const noexcept { return w_.load(std::memory_order_acquire) - r_.load(std::memory_order_acquire); }

  // producer side
  std::size_t write(const T* src, std::size_t n) noexcept {
    const std::size_t w = w_.load(std::memory_order_relaxed);
    const std::size_t space = capacity() - (w - r_.load(std::memory_order_acquire));
    if (n > space) n = space;
    const std::size_t i = w & mask_, first = n < capacity() - i ? n : capacity() - i;
    std::memcpy(&buf_[i], src, first * sizeof(T));
    std::memcpy(&buf_[0], src + first, (n - first) * sizeof(T));
    w_.store(w + n, std::memory_order_release);
    return n;
  }
  // consumer side
  std::size_t read(T* dst, std::size_t n) noexcept {
    const std::size_t r = r_.load(std::memory_order_relaxed);
    const std::size_t have = w_.load(std::memory_order_acquire) - r;
    if (n > have) n = have;
    const std::size_t i = r & mask_, first = n < capacity() - i ? n : capacity() - i;
    std::memcpy(dst, &buf_[i], first * sizeof(T));
    std::memcpy(dst + first, &buf_[0], (n - first) * sizeof(T));
    r_.store(r + n, std::memory_order_release);
    return n;
  }
  std::size_t discard(std::size_t n) noexcept {
    const std::size_t r = r_.load(std::memory_order_relaxed);
    const std::size_t have = w_.load(std::memory_order_acquire) - r;
    if (n > have) n = have;
    r_.store(r + n, std::memory_order_release);
    return n;
  }

 private:
  std::vector<T> buf_;
  std::size_t mask_ = 0;
  alignas(64) std::atomic<std::size_t> w_{0};
  alignas(64) std::atomic<std::size_t> r_{0};
};

}  // namespace amfb
