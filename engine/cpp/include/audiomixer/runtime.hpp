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
  std::string backend = "auto";          // auto (the first real backend of this system, else null), alsa, asio, wasapi, dsound (DirectSound: Bluetooth and anything else, no lock), coreaudio, null
  std::string inDevice, outDevice;       // empty = the default device; "-" = none (ALSA: "default", "hw:0,0", "plughw:1" ...)
  double sampleRate = 48000;
  unsigned frames = 256;                 // frames per block (the period)
  int inChannels = 2, outChannels = 2;
  bool exclusive = false;                // WASAPI: take exclusive control of the device (no Windows mixer, no other program; float32 / int32 / int24 / int16, whatever the device accepts); an error says why it was refused
  int bits = 0;                          // WASAPI exclusive: the word length to try first: 0 = automatic, 16, 24, 32 (integer), -32 (float)
  bool preferInt32 = true;               // WASAPI exclusive: try 32-bit integer before 32-bit float (the device's own word length first)
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
  virtual std::string sampleFormat() const { return "32-bit float"; }   // what the device itself carries, e.g. "in 24-bit integer · out 32-bit integer" (after open)
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
  std::string kind;                  // what it is: headphones | headset | speakers | microphone | line | digital | interface | bluetooth | app | wrapper | unknown
};
// WINDOWS SOUND (the system's own volume of the default output, through the documented endpoint-volume interface: no driver, no registry):
// read and set its mute. Used so that the master fader at -inf also silences what Windows itself plays. false on other systems or on failure.
bool systemMuteGet(bool& muted);
bool systemMuteSet(bool muted);
// WHAT A DEVICE IS, from the system's own form factor when it has one (WASAPI: 1 speakers, 2 line, 3 headphones, 4 microphone, 5 headset,
// 6 handset, 8 S/PDIF, 9 display audio; -1 = not known) and otherwise from the name. capture: the entry is an input.
std::string classifyDevice(const std::string& backend, const std::string& name, int formFactor, bool capture);
std::vector<DeviceInfo> listDevices(bool probeAsio = true);   // probeAsio false: ASIO drivers are listed from the registry without loading them (2 in / 2 out assumed): the list a hung driver cannot block
                       // every device of every backend built in ("null" last)
// A cheap signature of what is plugged in (WASAPI / DirectSound endpoint ids + ASIO driver names from the registry, ALSA card names): no driver
// is loaded, so it can be polled every second or two for hot-plug; when it changes, call listDevices().
std::string deviceFingerprint();

// the meters, copied out consistently
struct Snapshot {
  struct StripMeter { double pk, rms, hold; std::uint32_t clipN; double compGrDb, gateDb, inPk, amGainDb, polCorr; bool polInv; };   // polCorr: the strip against the rest of the mix (-1..1, AUTO PHASE strips), polInv: its polarity now   // compGrDb <= 0 (the channel compressor), gateDb <= 0 (the channel gate), amGainDb <= 0 (automix share)
  StripMeter strips[kMaxStrips]; StripMeter master[2]; StripMeter buses[kMaxBuses];
  double monitorPk[2];                       // what the control room hears, after the monitor section
  double momentary, shortTerm, integrated, lra, truePeak, correlation, autoLevelDb;
  std::uint64_t blocks, frames, xruns;
  double loadPct, blockUs, maxBlockUs, jitterUs;   // DSP time of the last block as % of its duration, its microseconds, the worst since start, the period jitter
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
  std::string sampleFormat() const;
  // the integer word length the device takes on its output: 16 or 24 (dither at that depth), 0 for 32-bit integer / float (no dither:
  // a 32-bit converter's LSB is far below its own noise). From sampleFormat(): ASIO / WASAPI exclusive report the driver's own type.
  int deviceOutputBits() const;   // the open device's own sample format ("" when nothing is open); the engine itself mixes in 64-bit float
  double sampleRate() const { return mixer_.sampleRate(); }   // the device rate after open()
  // control: any thread, takes effect at the start of the next block
  void setGainDb(int strip, double db);
  void setPan(int strip, double pan);
  void setSpatial(int strip, bool on, double azDeg, double elDeg, double distM, double spread);   // 360 audio (headphones)
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
  void setBusProcessor(int bus, BusProcessor* p);   // an effect on a bus (app/fx); keep it alive while set, detach with nullptr before deleting
  double limiterReductionDb() const { return mixer_.limiterReductionDb(); }   // read from the interface thread: a meter value, one block old at most
  std::uint64_t nonFiniteSamples() const { return mixer_.nonFiniteSamples(); }   // NaN / inf the master guard silenced (diagnostics)
  void setMasterProcessor(BusProcessor* p);          // the master chain; the caller keeps it alive while set
  void setWidth(double w);                           // master stereo width 0..2
  void setAutomix(int strip, bool on, double weight);   // gain-sharing automixer membership
  // AUTO PHASE: the strip is compared with the rest of the mix (correlation, ~1 s); when it stays out of phase (below -0.35 for 1 s,
  // both carrying signal) its polarity is flipped, then left alone for 3 s. The current polarity is in the strip's meter (polInv).
  void setAutoPolarity(int strip, bool on);
  void setAutoLevel(bool on, double targetLufs, double maxGainDb);
  void setOutputBits(int bits);                       // 0 = float out (no dither); 16 / 24 = TPDF dither to that depth
  void setDcBlock(bool on);
  void resetProfiler();
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
  void setTrimDb(int strip, double db);
  // INSERTS: external processors (VST3 plug-ins) on a strip, after the built-in channel DSP and before the fader. The chain is swapped
  // whole from the UI thread; the audio thread keeps its own reference for the block, so a removed plug-in stays alive until the block ends.
  struct Insert { virtual ~Insert() = default; virtual void process(float* mono, std::size_t frames) = 0; };
  using InsertChain = std::vector<std::shared_ptr<Insert>>;
  void setInserts(int strip, std::shared_ptr<const InsertChain> chain);                     // input trim, before the channel DSP and the input meter (-24 .. +24 dB)
  static int defaultOutputSource(int out) { return out < 2 ? kSrcMonL + out : (out - 2 < 2 * kMaxBuses ? kSrcBus0L + (out - 2) : kSrcOff); }
  // an extra stereo source summed into the master before the master chain (the ARRANGE player): render() is called on the audio thread
  // with the block size; set nullptr to detach. The pointer must stay valid until setExtraSource(nullptr) has returned.
  struct ExtraSource { virtual ~ExtraSource() = default; virtual void render(float* L, float* R, std::size_t frames, double sampleRate) = 0; };
  void setExtraSource(ExtraSource* s) { extra_.store(s); }
  // a TAP sees every block on the audio thread (the recorder): the strips after trim / DSP / inserts and before the fader, then the master
  // after its chain and meters (what the show hears, before the monitor section). Must not block or allocate. nullptr detaches.
  struct Tap { virtual ~Tap() = default; virtual void onBlock(const float* const* strips, int nStrips, const float* L, const float* R, std::size_t frames, double sampleRate) = 0; };
  void setTap(Tap* t) { tap_.store(t); }
  // TEST TONE: a 1 kHz sine at -20 dBFS on outputs 1-2 for the given time, with a short fade in and out, added after the monitor level
  // (so it is heard whatever the faders say). For the Test button of the audio settings: "does sound come out of this device?"
  void testTone(double seconds = 1.0) { const long n = static_cast<long>((seconds < 0.1 ? 0.1 : seconds > 5 ? 5 : seconds) * sampleRate()); toneTotal_.store(n); toneLeft_.store(n); }
  bool toneActive() const { return toneLeft_.load() > 0; }
  // NETWORK AUDIO (engine/cpp/app/net). A second tap for the network sender, so that it and the recorder can run together, and up to
  // kNetInputs extra input channels filled by a network receiver: setInputRoute(strip, kNetInBase + k) makes a strip listen to network
  // channel k. pull() runs on the audio thread at the start of each block and must fill every channel (silence when it has nothing).
  // Both pointers must stay valid until the setter has been called with nullptr and one more block has passed.
  static constexpr int kNetInBase = 256, kNetInputs = 16;
  struct NetInput { virtual ~NetInput() = default; virtual void pull(float* const* ch, int channels, std::size_t frames, double sampleRate) = 0; };
  void setNetInput(NetInput* n) { netIn_.store(n); }
  void setNetTap(Tap* t) { netTap_.store(t); }
  unsigned blockFrames() const { return static_cast<unsigned>(left_.size()); }
  // MEASUREMENT: the taps see this one strip as it comes from the device (before its trim, EQ, dynamics and inserts) instead of after them,
  // so a measuring microphone is not coloured by the channel's own processing. -1 = off (the default). What the mix hears does not change.
  void setTapRawStrip(int strip) { tapRaw_.store(strip); }
  Snapshot snapshot() const;
  void onBlock(const float* const* in, float* const* out, std::size_t frames);   // the audio callback; public so a test can clock the runtime itself
 private:
  struct Cmd { int op; int a; double x, y, z; int b = 0; double w = 0, v = 0, u = 0; void* p = nullptr; };
  void post(const Cmd& c);
  void publish();

  Mixer mixer_; std::unique_ptr<Backend> backend_; std::string backendName_;
  int nIn_ = 0, nOut_ = 0; bool running_ = false;
  std::vector<float> silence_, left_, right_;
  int monSrc_ = 0; double monGainTarget_ = 1, monGain_ = 1; bool monMono_ = false; double monPk_[2] = {0, 0};   // audio thread only
  std::atomic<long> toneLeft_{0}, toneTotal_{0}; double tonePhase_ = 0;
  std::vector<float> monL_, monR_, extL_, extR_; std::atomic<ExtraSource*> extra_{nullptr}; std::atomic<Tap*> tap_{nullptr}, netTap_{nullptr}; std::atomic<NetInput*> netIn_{nullptr}; std::atomic<int> tapRaw_{-1}; std::vector<std::vector<float>> netBuf_; int inRoute_[kMaxStrips]; int outRoute_[kMaxOutputs]; double trim_[kMaxStrips]; // the insert chain of each strip, swapped from the UI thread and read on the audio thread: a shared_ptr with the free atomic
  // functions (std::atomic_load / atomic_store), which every standard library has; std::atomic<std::shared_ptr> is missing from libc++ (Android, macOS)
  std::shared_ptr<const InsertChain> inserts_[kMaxStrips];                            // audio thread only (set in open, then by ops)
  std::vector<dsp::Channel> ch_; std::vector<std::vector<float>> chBuf_;
  std::vector<float> sumBuf_;   // AUTO PHASE: the mix of every strip, mono, for the comparison
  bool autoPol_[kMaxStrips] = {}; double apXY_[kMaxStrips] = {}, apXX_[kMaxStrips] = {}, apYY_[kMaxStrips] = {}, apBad_[kMaxStrips] = {}, apHold_[kMaxStrips] = {}, apCorr_[kMaxStrips] = {};
  std::vector<std::vector<float>> busBuf_;   // 2 * kMaxBuses blocks for the aux bus outputs   // one channel DSP and one scratch block per strip (sized in open())
  // control ring: many producers (under a mutex), one consumer (the audio thread)
  static constexpr unsigned kRing = 8192;   // room for a whole console pushed at once (32 strips x their EQ bands, sends and dynamics)
  Cmd ring_[kRing]; std::atomic<unsigned> head_{0}, tail_{0}; std::mutex producers_;
  // published meters (sequence counter)
  mutable std::atomic<unsigned> seq_{0}; Snapshot pub_{}; std::uint64_t blocks_ = 0, frames_ = 0;
  double loadPct_ = 0, blockUs_ = 0, maxBlockUs_ = 0, jitterUs_ = 0; std::int64_t lastStartNs_ = 0;   // profiler (audio thread)
};

}  // namespace am
