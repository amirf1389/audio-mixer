// RECORD: the master and any armed strips to WAV files while the engine runs. The audio thread (Runtime::Tap) only copies samples into a
// lock-free ring; a writer thread drains it to disk (24-bit PCM or 32-bit float WAV, the header patched on close). A full ring drops the
// block and counts it, never blocks the audio. Files: <folder>/<session>-master.wav, <folder>/<session>-ch03.wav ... (session = time stamp).
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audiomixer/runtime.hpp"

namespace am::rec {

struct Options { std::string folder; std::string session; bool master = true; std::vector<int> strips; bool float32 = false; };   // strips: indices to record (mono, pre-fader)

class Recorder : public Runtime::Tap {
 public:
  Recorder();
  ~Recorder() override;
  bool start(const Options& o, double sampleRate, std::string& err);   // opens the files; the Runtime must be given this as its Tap by the caller
  void stop();                                                           // flushes and closes
  bool recording() const { return recording_.load(); }
  double seconds() const { return sampleRate_ > 0 ? static_cast<double>(framesTaken_.load()) / sampleRate_ : 0; }
  std::uint64_t dropped() const { return dropped_.load(); }             // blocks lost to a full ring (disk too slow)
  std::vector<std::string> files() const { return files_; }
  void onBlock(const float* const* strips, int nStrips, const float* L, const float* R, std::size_t frames, double sampleRate) override;
  static std::string stampNow();                                        // "2026-11-10_14-05-33"

 private:
  struct File { std::FILE* f = nullptr; std::string path; int channels = 1; std::uint64_t frames = 0; };
  void writerLoop();
  void writeHeader(File& x, bool close);
  Options opt_; double sampleRate_ = 48000; std::vector<File> out_; std::vector<std::string> files_; int chans_ = 0;   // chans_ = 2 (master) + strips
  std::vector<float> ring_; std::atomic<std::size_t> head_{0}, tail_{0}; std::size_t ringFrames_ = 0;              // interleaved frames of chans_
  std::atomic<bool> recording_{false}, quit_{false}; std::atomic<std::uint64_t> framesTaken_{0}, dropped_{0};
  std::thread writer_; std::vector<unsigned char> scratch_;
};

}  // namespace am::rec
