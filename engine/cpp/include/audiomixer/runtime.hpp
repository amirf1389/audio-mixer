// Audio Mixer runtime: the mixer on a sound card.
//
//   am::Backend      one audio API: "alsa" (Linux), "wasapi" (Windows), "coreaudio" (macOS), "null" (a timed device that needs no hardware: tests, servers)
//   am::Runtime      opens a backend, runs the am::Mixer in its real-time callback, takes control from any thread without locking the audio thread and gives
//                    out a consistent snapshot of the meters
//
// Inputs of the device -> strips 0.. of the mixer (strip s listens to input channel s; strips without an input are silent); the stereo master -> output channels 0 and 1.
// The audio thread never allocates, never takes a lock the control side holds and never waits: parameter changes travel through a ring that it empties at the start of each block,
// and the meters are published with a sequence counter (a reader retries while a block is being published).
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audiomixer/engine.hpp"
#include "audiomixer/spatial.hpp"

namespace am {

struct DeviceConfig {
  std::string backend = "auto";          // auto (the first real backend of this system, else null), alsa, wasapi, coreaudio, null
  std::string inDevice, outDevice;       // empty = the default device; "-" = none (ALSA: "default", "hw:0,0", "plughw:1" ...)
  double sampleRate = 48000;
  unsigned frames = 256;                 // frames per block (the period)
  int inChannels = 2, outChannels = 2;
  // null backend only: what the "device" delivers as input (default silence); called on the audio thread before each block, frameIndex = first frame of the block
  std::function<void(float* const* in, std::size_t frames, std::uint64_t frameIndex)> nullInput;
};

// in: inChannels pointers to `frames` floats each; out: outChannels pointers. Runs on the audio thread.
using AudioCallback = std::function<void(const float* const* in, float* const* out, std::size_t frames)>;

class Backend {
 public:
  virtual ~Backend() = default;
  virtual const char* name() const = 0;
  virtual bool open(const DeviceConfig& cfg, AudioCallback cb, std::string& err) = 0;
  virtual bool start(std::string& err) = 0;
  virtual void stop() = 0;               // waits until the callback is no longer running
  virtual std::uint64_t xruns() const { return 0; }
};
std::vector<std::string> availableBackends();                 // the ones built into this binary, "null" last
std::unique_ptr<Backend> createBackend(const std::string& name);   // nullptr when it is not built in

// the meters, copied out consistently
struct Snapshot {
  struct StripMeter { double pk, rms, hold; std::uint32_t clipN; };
  StripMeter strips[kMaxStrips]; StripMeter master[2];
  double momentary, shortTerm, integrated, lra, truePeak, correlation;
  std::uint64_t blocks, frames, xruns;
};

class Runtime {
 public:
  explicit Runtime(int strips = 8);
  ~Runtime();
  bool open(const DeviceConfig& cfg, std::string& err);
  bool start(std::string& err);
  void stop();
  bool running() const { return running_; }
  const std::string& backendName() const { return backendName_; }
  // control: any thread, takes effect at the start of the next block
  void setGainDb(int strip, double db);
  void setPan(int strip, double pan);
  void setMute(int strip, bool on);
  void setSolo(int strip, bool on);
  void setMasterDb(double db);
  void setLimiter(bool on, double ceilingDb = -0.5, double releaseMs = 100);
  void setClipDb(double db);
  void clearClips();
  // 360 degree output (Resonance Audio, only when Spatial::available()): the stereo master after the meters becomes two virtual speakers in a binaural scene; off by default.
  // The audio is then delayed by Spatial::block() frames. Returns false when this binary has no Resonance Audio.
  bool setSpatial(bool on);
  void setSpatialPosition(double azimuthDeg, double elevationDeg, double distanceM, double spreadDeg = 30);
  Snapshot snapshot() const;
 private:
  struct Cmd { int op; int a; double x, y, z; };
  void post(const Cmd& c);
  void onBlock(const float* const* in, float* const* out, std::size_t frames);
  void publish();

  Mixer mixer_; std::unique_ptr<Backend> backend_; std::string backendName_;
  int nIn_ = 0, nOut_ = 0; bool running_ = false;
  std::unique_ptr<Spatial> spatial_; bool spatialOn_ = false; double spAz_ = 0, spEl_ = 0, spDist_ = 2, spSpread_ = 30;     // spatial_ is created in open(); spatialOn_ is only touched by the audio thread
  std::vector<float> silence_, left_, right_;
  // control ring: many producers (under a mutex), one consumer (the audio thread)
  static constexpr unsigned kRing = 1024;
  Cmd ring_[kRing]; std::atomic<unsigned> head_{0}, tail_{0}; std::mutex producers_;
  // published meters (sequence counter)
  mutable std::atomic<unsigned> seq_{0}; Snapshot pub_{}; std::uint64_t blocks_ = 0, frames_ = 0;
};

}  // namespace am
