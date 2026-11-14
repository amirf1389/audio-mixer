// PLUG-IN BUILDER PREVIEW: hear a design before it is exported. A source (a built-in sample on a loop, pink noise, a sweep, the
// microphone, an audio file) runs through the same chain of AMFB blocks the exported plug-in will run, to the sound card's default output.
// Knob moves reach the running chain at once (Effect::set is lock-free); adding / removing / moving blocks builds a new chain on the
// interface thread and swaps it in (the old one is freed a second later, never on the audio thread). BYPASS plays the source untouched (A/B).
#pragma once
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "fx/fx.hpp"
#include "plugin_shell/design.hpp"

namespace am::shell {

class Preview {
 public:
  enum Source { Sample = 0, PinkNoise = 1, Sweep = 2, Microphone = 3, File = 4 };
  Preview();
  ~Preview();
  // backend "auto" = the system's own (WASAPI / Core Audio / ALSA); "null" = no sound card (the tests)
  bool start(std::string& err, const std::string& backend = "auto", double rate = 48000, unsigned frames = 512);
  void stop();
  bool running() const { return running_.load(); }
  void setDesign(const Design& d);                       // rebuilds only when the blocks change, else just sets the knobs
  void setSource(Source s);
  bool setSample(int id);                                 // one of the 100 built-in samples (1..100)
  bool setFile(const std::string& path, std::string& err);
  void setBypass(bool b) { bypass_ = b; }
  void setGainDb(double db) { gain_ = static_cast<float>(std::pow(10.0, db / 20.0)); }
  float inPeak() { return inPk_.exchange(0.f); }          // since the last call (the meters poll)
  float outPeak() { return outPk_.exchange(0.f); }
  std::uint64_t blocks() const { return blocks_.load(); }
  double rate() const { return rate_; }
  // for the tests: what the null backend's input delivers (the microphone source)
  void setNullInput(std::function<void(float* const*, std::size_t, std::uint64_t)> f) { nullIn_ = std::move(f); }
  // runs one block without a device (the tests): source -> chain -> out
  void renderForTest(float* L, float* R, std::size_t n) { render(nullptr, L, R, n); }
 private:
  struct Chain { std::vector<std::unique_ptr<am::fx::Effect>> fx; std::vector<std::string> kinds; };
  struct Clip { std::vector<float> L, R; };
  void render(const float* const* in, float* L, float* R, std::size_t n);
  void retire(Chain* c); void setClip(std::unique_ptr<Clip> c); void sweepGraves();
  std::unique_ptr<Backend> be_; std::atomic<bool> running_{false}; double rate_ = 48000;
  std::atomic<Chain*> chain_{nullptr}; std::mutex graveM_; std::vector<std::pair<std::unique_ptr<Chain>, long long>> grave_;
  std::atomic<const Clip*> clip_{nullptr}; std::vector<std::pair<std::unique_ptr<Clip>, long long>> clipGrave_; std::unique_ptr<Clip> clipOwn_;
  std::atomic<int> source_{Sample}; std::atomic<bool> bypass_{false}; std::atomic<float> gain_{1.f};
  std::atomic<float> inPk_{0.f}, outPk_{0.f}; std::atomic<std::uint64_t> blocks_{0};
  std::size_t pos_ = 0; double phase_ = 0, sweepT_ = 0; float pink_[7] = {0, 0, 0, 0, 0, 0, 0}; std::uint32_t rng_ = 22222;
  std::function<void(float* const*, std::size_t, std::uint64_t)> nullIn_; Design design_;
};

}  // namespace am::shell
