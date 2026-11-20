// See include/audiomixer/worker_pool.hpp.
#include "audiomixer/worker_pool.hpp"

#include <algorithm>
#include <chrono>

#include "audiomixer/fpu.hpp"
#if defined(_WIN32)
#include "audiomixer/sysopt.hpp"
#endif
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define AM_PAUSE() _mm_pause()
#else
#define AM_PAUSE() std::this_thread::yield()
#endif

namespace am {

int WorkerPool::defaultThreads() { const unsigned c = std::thread::hardware_concurrency(); return c <= 1 ? 0 : static_cast<int>(std::min(8u, c - 1)); }

void WorkerPool::start(int helpers) {
  stop(); quit_.store(false); slowRuns_ = 0;
  for (int k = 0; k < std::clamp(helpers, 0, 8); ++k) threads_.emplace_back([this] { loop(); });
}
void WorkerPool::stop() {
  if (threads_.empty()) return;
  quit_.store(true); gen_.fetch_add(1, std::memory_order_release); wake();
  for (auto& t : threads_) t.join();
  threads_.clear();
}

void WorkerPool::wake() {
#if defined(__cpp_lib_atomic_wait)
  gen_.notify_all();
#endif
}

bool WorkerPool::claim(std::uint32_t gen, int& index) {
  std::uint64_t v = next_.load(std::memory_order_acquire);
  for (;;) {
    if (static_cast<std::uint32_t>(v >> 32) != gen) return false;      // another run already: nothing of this one is left
    const int i = static_cast<int>(v & 0xffffffffu);
    if (i >= n_.load(std::memory_order_acquire)) return false;
    if (next_.compare_exchange_weak(v, v + 1, std::memory_order_acq_rel)) { index = i; return true; }
  }
}

void WorkerPool::loop() {
  am::denormalsOff();   // flush-to-zero like the audio thread
#if defined(_WIN32)
  am::sysopt::tuneAudioThread();   // HighQoS and the performance cores (SYSTEM OPTIMIZATION: FAST CORES FOR AUDIO)
#endif
  std::uint32_t seen = gen_.load(std::memory_order_acquire);
  for (;;) {
    std::uint32_t g = gen_.load(std::memory_order_acquire);
    for (int spin = 0; g == seen && spin < 256; ++spin) { AM_PAUSE(); g = gen_.load(std::memory_order_acquire); }   // the next block is usually near
#if defined(__cpp_lib_atomic_wait)
    if (g == seen) { gen_.wait(seen, std::memory_order_acquire); continue; }
#else
    if (g == seen) { std::this_thread::sleep_for(std::chrono::microseconds(200)); continue; }   // no atomic wait in this library: a short nap
#endif
    seen = g;
    if (quit_.load(std::memory_order_acquire)) return;
    int i;
    while (claim(g, i)) { job_.load(std::memory_order_acquire)(ctx_.load(std::memory_order_acquire), i); done_.fetch_add(1, std::memory_order_acq_rel); }
  }
}

void WorkerPool::run(Job job, void* ctx, int n, double timeoutUs) {
  if (n <= 0) return;
  if (threads_.empty() || n == 1 || slowRuns_ > 0) { if (slowRuns_ > 0) --slowRuns_; for (int i = 0; i < n; ++i) job(ctx, i); return; }   // single thread
  ++shared_;
  const std::uint32_t g = gen_.load(std::memory_order_relaxed) + 1;
  job_.store(job, std::memory_order_relaxed); ctx_.store(ctx, std::memory_order_relaxed); n_.store(n, std::memory_order_relaxed); done_.store(0, std::memory_order_relaxed);
  next_.store(static_cast<std::uint64_t>(g) << 32, std::memory_order_release);
  gen_.store(g, std::memory_order_release); wake();   // a futex wake for a helper that sleeps; no lock, nothing allocated
  int i, mine = 0;
  while (claim(g, i)) { job(ctx, i); ++mine; }
  if (done_.load(std::memory_order_acquire) + mine >= n) return;
  const auto t0 = std::chrono::steady_clock::now(); bool slow = false;
  while (done_.load(std::memory_order_acquire) + mine < n) {
    AM_PAUSE();
    if (!slow && std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() > timeoutUs) slow = true;
  }
  if (slow) slowRuns_ = 1000;   // fall back to one thread for a while
}

}  // namespace am
