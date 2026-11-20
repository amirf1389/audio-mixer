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
#include <condition_variable>
#include <mutex>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "FLAC/stream_encoder.h"

namespace am::rec {

struct Options { std::string folder; std::string session; bool master = true; std::vector<int> strips; bool float32 = false;
                 int format = 0;
                 int workers = 0; };   // threads that write and compress the files at once: 0 = every core but one (at least 1, at most 16)
inline constexpr int kMaxTracks = 100;  // MULTITRACK: at most 100 channel files (plus the master) in one recording   // 0 = 24-bit PCM, 1 = 32-bit float, 2 = 64-bit float, 3 = 32-bit PCM, 4 = FLAC 24-bit (float32 = true is the same as format 1); WAV over 4 GB becomes RF64   // strips: indices to record (mono, pre-fader)

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
  struct File { std::FILE* f = nullptr; FLAC__StreamEncoder* flac = nullptr; std::string path; int channels = 1; std::uint64_t frames = 0; std::size_t col = 0; };
  struct Scratch { std::vector<unsigned char> bytes; std::vector<FLAC__int32> flac; };
  void writerLoop();
  void encode(File& x, std::size_t t, std::size_t n, Scratch& sc);
  void poolLoop(int k);
  void writeHeader(File& x, bool close);
  Options opt_; double sampleRate_ = 48000; std::vector<File> out_; std::vector<std::string> files_; int chans_ = 0;   // chans_ = 2 (master) + strips
  std::vector<float> ring_; std::atomic<std::size_t> head_{0}, tail_{0}; std::size_t ringFrames_ = 0;              // interleaved frames of chans_
  std::atomic<bool> recording_{false}, quit_{false}; std::atomic<std::uint64_t> framesTaken_{0}, dropped_{0};
  std::thread writer_; std::vector<unsigned char> scratch_; std::vector<FLAC__int32> flacBuf_;
  // THE ENCODING POOL: the writer hands one stretch of audio to every worker; each writes and compresses its own share of the files
  std::vector<std::thread> pool_; std::vector<Scratch> poolScratch_; std::mutex poolM_; std::condition_variable poolGo_, poolDone_;
  std::uint64_t poolGen_ = 0; int poolBusy_ = 0; bool poolQuit_ = false; std::size_t jobT_ = 0, jobN_ = 0;
 public:
  int workers() const { return static_cast<int>(pool_.size()) + 1; }   // the writer thread works too
};

}  // namespace am::rec
