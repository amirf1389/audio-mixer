// WORKER POOL for DSP MODE FAST: the strips of one audio block shared out over a few helper threads, real-time safe.
//   * Made and ended off the audio thread (start / stop); run() itself allocates nothing and takes no lock.
//   * The caller (the audio thread) works too: it claims jobs like any helper, so a block never waits for a helper that is asleep or late
//     to wake; it only waits (spinning) for jobs a helper has already started.
//   * Jobs are claimed from one atomic counter tagged with the run's number: a helper that wakes late can never take a job of a later run.
//   * A wait longer than the timeout (a helper pre-empted mid-job) marks the pool as slow: the next 1000 runs stay on the caller alone
//     (single-thread), then the helpers are tried again.
//   * Each job writes only its own data, and the caller sums afterwards in a fixed order: the result is bit-identical to one thread.
// The helpers spin briefly for the next block, then sleep on the counter (C++20 atomic wait); they turn denormals off and, on Windows,
// ask for the performance cores (sysopt::tuneAudioThread, the SYSTEM OPTIMIZATION "fast cores" setting).
#pragma once
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace am {

class WorkerPool {
 public:
  using Job = void (*)(void* ctx, int index);
  WorkerPool() = default;
  ~WorkerPool() { stop(); }
  WorkerPool(const WorkerPool&) = delete; WorkerPool& operator=(const WorkerPool&) = delete;
  static int defaultThreads();   // cores - 1, at most 8, 0 on a single-core machine
  void start(int helpers);       // not on the audio thread; 0 = none (run() then runs everything itself)
  void stop();
  int helpers() const { return static_cast<int>(threads_.size()); }
  // job(ctx, i) for every i in [0, n): returns once all have finished. timeoutUs: how long to wait for a helper's job before the pool is
  // marked slow (the wait itself goes on: a started job is never run twice).
  void run(Job job, void* ctx, int n, double timeoutUs = 2000);
  bool degraded() const { return slowRuns_ > 0; }
  std::uint64_t runsShared() const { return shared_; }   // runs that went out to the helpers (diagnostics, tests)
 private:
  void loop();
  void wake();
  bool claim(std::uint32_t gen, int& index);
  std::vector<std::thread> threads_;
  std::atomic<std::uint32_t> gen_{0}; std::atomic<std::uint64_t> next_{0}; std::atomic<int> done_{0}; std::atomic<bool> quit_{false};
  std::atomic<Job> job_{nullptr}; std::atomic<void*> ctx_{nullptr}; std::atomic<int> n_{0};
  int slowRuns_ = 0; std::uint64_t shared_ = 0;   // the caller's own (the audio thread)
};

}  // namespace am
