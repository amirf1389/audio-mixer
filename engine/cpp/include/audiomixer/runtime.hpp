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

#include "audiomixer/dsp.hpp"
#include "audiomixer/engine.hpp"

namespace am {

struct DeviceConfig {
  std::string backend = "auto";          // auto (the first real backend of this system, else null), alsa, wasapi, coreaudio, null
  std::string inDevice, outDevice;       // empty = the default device; "-" = none (ALSA: "default", "hw:0,0", "plughw:1" ...)
  double sampleRate = 48000;
  unsigned frames = 256;                 // frames per block (the period)
  int inChannels = 2, outChannels = 2;
  bool exclusive = false;                // WASAPI: take exclusive control of the device (no Windows mixer, no other program; float32 / int32 / int24 / int16, whatever the device accepts); an error says why it was refused
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

// One device a backend can open. `id` is the string to put in DeviceConfig::inDevice / outDevice for that backend
// (WASAPI endpoint id, ASIO driver name, ALSA PCM name). Channel counts of 0 mean that direction is not offered.
struct DeviceInfo {
  std::string backend, id, name;
  int inChannels = 0, outChannels = 0;
  double defaultRate = 48000;
  std::vector<double> sampleRates;   // empty: not known without opening the device
  bool defaultIn = false, defaultOut = false;
};
std::vector<DeviceInfo> listDevices();                       // every device of every backend built in ("null" last)

// the meters, copied out consistently
struct Snapshot {
  struct StripMeter { double pk, rms, hold; std::uint32_t clipN; double compGrDb, gateDb, inPk; };   // compGrDb <= 0 (the channel compressor), gateDb <= 0 (the channel gate)
  StripMeter strips[kMaxStrips]; StripMeter master[2]; StripMeter buses[kMaxBuses];
  double monitorPk[2];                       // what the control room hears, after the monitor section
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
  double sampleRate() const { return mixer_.sampleRate(); }   // the device rate after open()
  // control: any thread, takes effect at the start of the next block
  void setGainDb(int strip, double db);
  void setPan(int strip, double pan);
  void setMute(int strip, bool on);
  void setSolo(int strip, bool on);
  void setMasterDb(double db);
  void setLimiter(bool on, double ceilingDb = -0.5, double releaseMs = 100);
  void setClipDb(double db);
  void setPeakHoldMs(double ms);
  void clearClips();
  // A mix OS channel DSP (audiomixer/dsp.hpp), applied to a strip's input before the mixer: parametric EQ, crossover, delay, polarity, limiter.
  // Coefficients are computed on the audio thread at the start of the next block (a few cos / pow, no allocation).
  void setEqBand(int strip, int band, dsp::FilterType type, double freq, double q, double gainDb);
  void setDelayMs(int strip, double ms);
  void setPolarity(int strip, bool invert);
  void setCrossover(int strip, dsp::XoverType type, int hpOrder, double hpFreq, int lpOrder, double lpFreq);
  void setChannelLimiter(int strip, bool on, double ceilingDb, double releaseMs);
  void setCompressor(int strip, bool on, double thresholdDb, double ratio, double kneeDb, double attackMs, double releaseMs, double makeupDb);
  void setGate(int strip, bool on, double thresholdDb, double rangeDb, double attackMs, double holdMs, double releaseMs);
  // AUX SENDS (am::Mixer buses): bus k's stereo output goes to output channels 2 + 2k and 3 + 2k when the device has them
  void setSend(int strip, int bus, double db, bool pre);
  void setBusLevel(int bus, double db);
  void setBusMute(int bus, bool on);
  void setBusReturn(int bus, double db);
  // the monitor section (control room): what goes to output channels 0 / 1 after the master meters. source 0 = master, 1..kMaxBuses = that bus
  // (post level, pre return); level in dB, dim = -20 dB, mono sums L+R, mute = silence. The master meters and loudness are not affected.
  void setMonitor(int source, double db, bool dim, bool mono, bool mute);
  // the routing matrix. Inputs: strip s reads device input channel `input` (-1 = nothing; the default is input s). Outputs: device output
  // channel `out` carries `source`: 0 off, 1 / 2 master L / R, 3 / 4 monitor L / R, 5 + 2k / 6 + 2k bus k L / R. The default is monitor on 0 / 1 and
  // bus k on 2 + 2k / 3 + 2k. Several outputs may carry the same source; an input may feed several strips.
  static constexpr int kMaxOutputs = 64;
  static constexpr int kSrcOff = 0, kSrcMasterL = 1, kSrcMasterR = 2, kSrcMonL = 3, kSrcMonR = 4, kSrcBus0L = 5;
  void setInputRoute(int strip, int input);
  void setOutputRoute(int out, int source);
  void setTrimDb(int strip, double db);                     // input trim, before the channel DSP and the input meter (-24 .. +24 dB)
  static int defaultOutputSource(int out) { return out < 2 ? kSrcMonL + out : (out - 2 < 2 * kMaxBuses ? kSrcBus0L + (out - 2) : kSrcOff); }
  // an extra stereo source summed into the master before the master chain (the ARRANGE player): render() is called on the audio thread
  // with the block size; set nullptr to detach. The pointer must stay valid until setExtraSource(nullptr) has returned.
  struct ExtraSource { virtual ~ExtraSource() = default; virtual void render(float* L, float* R, std::size_t frames, double sampleRate) = 0; };
  void setExtraSource(ExtraSource* s) { extra_.store(s); }
  Snapshot snapshot() const;
  void onBlock(const float* const* in, float* const* out, std::size_t frames);   // the audio callback; public so a test can clock the runtime itself
 private:
  struct Cmd { int op; int a; double x, y, z; int b = 0; double w = 0, v = 0, u = 0; };
  void post(const Cmd& c);
  void publish();

  Mixer mixer_; std::unique_ptr<Backend> backend_; std::string backendName_;
  int nIn_ = 0, nOut_ = 0; bool running_ = false;
  std::vector<float> silence_, left_, right_;
  int monSrc_ = 0; double monGainTarget_ = 1, monGain_ = 1; bool monMono_ = false; double monPk_[2] = {0, 0};   // audio thread only
  std::vector<float> monL_, monR_, extL_, extR_; std::atomic<ExtraSource*> extra_{nullptr}; int inRoute_[kMaxStrips]; int outRoute_[kMaxOutputs]; double trim_[kMaxStrips];                            // audio thread only (set in open, then by ops)
  std::vector<dsp::Channel> ch_; std::vector<std::vector<float>> chBuf_;
  std::vector<std::vector<float>> busBuf_;   // 2 * kMaxBuses blocks for the aux bus outputs   // one channel DSP and one scratch block per strip (sized in open())
  // control ring: many producers (under a mutex), one consumer (the audio thread)
  static constexpr unsigned kRing = 1024;
  Cmd ring_[kRing]; std::atomic<unsigned> head_{0}, tail_{0}; std::mutex producers_;
  // published meters (sequence counter)
  mutable std::atomic<unsigned> seq_{0}; Snapshot pub_{}; std::uint64_t blocks_ = 0, frames_ = 0;
};

}  // namespace am
