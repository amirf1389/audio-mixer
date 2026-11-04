// PcmRing: a single-producer single-consumer ring of int16 samples. One thread pushes, another pops; neither ever waits on the other (lock-free),
// so the audio thread can use it without blocking. push() / pop() move as many samples as fit / are there and return that count.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace am {

class PcmRing {
 public:
  explicit PcmRing(std::size_t capacity) : buf_(capacity + 1) {}   // one slot stays empty to tell full from empty
  std::size_t capacity() const { return buf_.size() - 1; }
  std::size_t size() const {
    const std::size_t w = w_.load(std::memory_order_acquire), r = r_.load(std::memory_order_acquire);
    return w >= r ? w - r : buf_.size() - r + w;
  }
  std::size_t space() const { return capacity() - size(); }
  std::size_t push(const std::int16_t* src, std::size_t n) {
    std::size_t w = w_.load(std::memory_order_relaxed), done = 0;
    const std::size_t r = r_.load(std::memory_order_acquire), cap = buf_.size();
    while (done < n) {
      const std::size_t next = w + 1 == cap ? 0 : w + 1;
      if (next == r) break;
      buf_[w] = src[done++];
      w = next;
    }
    w_.store(w, std::memory_order_release);
    return done;
  }
  std::size_t pop(std::int16_t* dst, std::size_t n) {
    std::size_t r = r_.load(std::memory_order_relaxed), done = 0;
    const std::size_t w = w_.load(std::memory_order_acquire), cap = buf_.size();
    while (done < n && r != w) {
      dst[done++] = buf_[r];
      r = r + 1 == cap ? 0 : r + 1;
    }
    r_.store(r, std::memory_order_release);
    return done;
  }

 private:
  std::vector<std::int16_t> buf_;
  std::atomic<std::size_t> w_{0}, r_{0};
};

}  // namespace am
