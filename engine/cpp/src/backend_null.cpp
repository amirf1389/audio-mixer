// The null backend: a thread that delivers blocks at the real-time rate of a sample clock, with silence (or DeviceConfig::nullInput) as input and the output thrown away.
#include <atomic>
#include <chrono>
#include <thread>

#include "backends.hpp"

namespace am {
// FILM CLOCK: with the manual clock on, the null device processes only the samples it has been given (advanceNullClock), not the wall
// clock's: a film capture that takes longer than real time per frame still sees exactly 1/fps of audio per frame (meters, beat, decay).
static std::atomic<bool> g_manual{false};
static std::atomic<std::uint64_t> g_allowed{0}, g_done{0};
void setNullManualClock(bool on) { g_manual = on; g_allowed = g_done.load(); }
void advanceNullClock(std::uint64_t samples) { g_allowed += samples; }
std::uint64_t nullClockProcessed() { return g_done.load(); }
namespace {
class NullBackend : public Backend {
 public:
  ~NullBackend() override { stop(); }
  const char* name() const override { return "null"; }
  bool open(const DeviceConfig& cfg, AudioCallback cb, std::string&) override {
    cfg_ = cfg; cb_ = std::move(cb);
    in_.assign(static_cast<std::size_t>(cfg.inChannels), std::vector<float>(cfg.frames, 0.0f)); out_.assign(static_cast<std::size_t>(cfg.outChannels), std::vector<float>(cfg.frames, 0.0f));
    for (auto& v : in_) inp_.push_back(v.data());
    for (auto& v : out_) outp_.push_back(v.data());
    return true;
  }
  bool start(std::string&) override { stop_ = false; thread_ = std::thread([this] { loop(); }); return true; }
  void stop() override { stop_ = true; if (thread_.joinable()) thread_.join(); }
  std::uint64_t xruns() const override { return late_; }
  bool deviceLost() const override { return lost_.load(); }
 private:
  void loop() {
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(cfg_.frames / cfg_.sampleRate));
    auto next = clock::now(); std::uint64_t frame = 0;
    while (!stop_) {
      if (cfg_.nullLoseAfterFrames && frame >= cfg_.nullLoseAfterFrames) { lost_ = true; break; }   // the simulated device was pulled out: no more blocks
      if (cfg_.nullInput) cfg_.nullInput(const_cast<float* const*>(inp_.data()), cfg_.frames, frame);
      else for (auto& v : in_) std::fill(v.begin(), v.end(), 0.0f);
      cb_(inp_.data(), const_cast<float* const*>(outp_.data()), cfg_.frames);
      frame += cfg_.frames; next += period; g_done += cfg_.frames;
      if (g_manual) { while (!stop_ && g_manual && g_done.load() + cfg_.frames > g_allowed.load()) std::this_thread::sleep_for(std::chrono::microseconds(200)); next = clock::now(); continue; }
      if (clock::now() > next + period * 4) { ++late_; next = clock::now(); } else std::this_thread::sleep_until(next);    // more than 4 blocks late: counted, the clock restarts
    }
  }
  DeviceConfig cfg_; AudioCallback cb_; std::vector<std::vector<float>> in_, out_; std::vector<const float*> inp_; std::vector<float*> outp_;
  std::thread thread_; std::atomic<bool> stop_{false}, lost_{false}; std::atomic<std::uint64_t> late_{0};
};
}  // namespace
std::unique_ptr<Backend> makeNullBackend() { return std::make_unique<NullBackend>(); }
}  // namespace am
