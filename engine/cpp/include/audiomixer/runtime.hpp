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

#include "audiomixer/amp.hpp"
#include "audiomixer/dsp.hpp"
#include "audiomixer/engine.hpp"
#include "audiomixer/worker_pool.hpp"

namespace am {
// the null device's FILM CLOCK (product films): on, it processes only the samples handed to it, whatever the wall clock says
void setNullManualClock(bool on);
void advanceNullClock(std::uint64_t samples);
std::uint64_t nullClockProcessed();


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
  // how long the input has been EXACT digital silence from the system, in seconds (0 = it carries something). A real microphone always
  // has some noise: seconds of exact zeros mean the system mutes it (the mute key, Windows' mute, microphone privacy for desktop apps)
  virtual double inputSilentSeconds() const { return 0.0; }
  // the driver's own name for each output channel ("Out 1", "S/PDIF Out L" ...), when it tells (ASIO); empty when it does not
  virtual std::vector<std::string> outputNames() const { return {}; }
  // LOW LATENCY: the input and output latency the driver reports, in frames (buffer included); 0 / 0 when it says nothing
  virtual void latencyFrames(long& in, long& out) const { in = 0; out = 0; }
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
  std::string probeError;            // ASIO: why the driver could not be read just now (busy in another program, unplugged, other bitness); empty = read
};
// WINDOWS SOUND (the system's own volume of the default output, through the documented endpoint-volume interface: no driver, no registry):
// read and set its mute. Used so that the master fader at -inf also silences what Windows itself plays. false on other systems or on failure.
bool systemMuteGet(bool& muted);
bool systemMuteSet(bool muted);
// WINDOWS VOLUME: the default output's own volume slider (0..1, the scalar Windows shows as 0..100), through the same documented endpoint
// interface as the mute. false where there is no such thing (not Windows, no output device)
bool systemVolumeGet(float& level);
bool systemVolumeSet(float level);
// WHAT A DEVICE IS, from the system's own form factor when it has one (WASAPI: 1 speakers, 2 line, 3 headphones, 4 microphone, 5 headset,
// 6 handset, 8 S/PDIF, 9 display audio; -1 = not known) and otherwise from the name. capture: the entry is an input.
std::string classifyDevice(const std::string& backend, const std::string& name, int formFactor, bool capture);
// SPEAKERS ON A DIGITAL CABLE: the first output pair (0-based, from 2 up: 1-2 already carry the mix) whose channel names say S/PDIF,
// optical (TOSLINK), coaxial or AES/EBU; -1 when there is none. ADAT (eight channels to a converter) is not a speaker pair.
int digitalOutputPair(const std::vector<std::string>& outputNames);
// EVERY AUDIO ENDPOINT of the system, whatever its state: what listDevices leaves out because it cannot be opened now (headphones on an
// empty jack, a microphone switched off in Windows' Sound settings) is listed here with the reason, for DIAGNOSTICS. Windows: WASAPI
// active + unplugged + disabled (not-present ghosts are left out); other systems: the active devices of listDevices().
struct EndpointInfo { std::string id, name, flow /* "out" | "in" */, state /* active | unplugged | disabled */, kind; int channels = 0; double rate = 0; bool isDefault = false; };
std::vector<EndpointInfo> listEndpoints();
std::vector<DeviceInfo> listDevices(bool probeAsio = true);   // probeAsio false: ASIO drivers are listed from the registry without loading them (2 in / 2 out assumed): the list a hung driver cannot block
                       // every device of every backend built in ("null" last)
// A cheap signature of what is plugged in (WASAPI / DirectSound endpoint ids + ASIO driver names from the registry, ALSA card names): no driver
// is loaded, so it can be polled every second or two for hot-plug; when it changes, call listDevices().
std::string deviceFingerprint();

// the meters, copied out consistently
struct Snapshot {
  struct StripMeter { double pk, rms, hold; std::uint32_t clipN; double compGrDb, gateDb, inPk, amGainDb, polCorr; bool polInv; double rideDb, rideGateDb, rideLufs; bool rideSusp; };   // ride*: the LOUDNESS MANAGER (auto fader offset, auto on/off gain, the input's short-term LUFS, suspended by a hand on the fader)   // polCorr: the strip against the rest of the mix (-1..1, AUTO PHASE strips), polInv: its polarity now   // compGrDb <= 0 (the channel compressor), gateDb <= 0 (the channel gate), amGainDb <= 0 (automix share)
  StripMeter strips[kMaxStrips]; StripMeter master[2]; StripMeter buses[kMaxBuses];
  double monitorPk[2];                       // what the control room hears, after the monitor section
  double monitorBandDb[10];                  // the monitor's source mix (pre monitor EQ) in octave bands, dBFS, slow average
  double momentary, shortTerm, integrated, lra, truePeak, correlation, autoLevelDb;
  double duckGrDb;                           // DUCKER: the targets' gain now (<= 0 dB)
  std::uint64_t blocks, frames, xruns;
  double ampPk[64], ampGrDb[64];             // the amplifier stage of each output: its peak after processing and the limiter's gain reduction (dB)
  double loadPct, blockUs, maxBlockUs, jitterUs;   // DSP time of the last block as % of its duration, its microseconds, the worst since start, the period jitter
  // ONE VOICE: how alike the device's inputs 1 and 2 are over the last half second (a mono microphone delivered on both sides reads corr ~1,
  // diff ~0 dB); levelDb -120 while there is no signal; windows counts the finished measurements
  double in12Corr, in12DiffDb, in12LevelDb; std::uint64_t in12Windows;
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
  int inputChannels() const { return nIn_; }    // the open device's inputs / outputs (0 while closed)
  int outputChannels() const { return nOut_; }
  bool exclusive() const { return exclusive_; }              // WASAPI opened exclusively (Windows' mixer and its volume are bypassed)
  const std::string& outDevice() const { return outDevice_; } // the output as asked ("" = the system's default)
  std::string sampleFormat() const;
  std::vector<std::string> outputNames() const;   // see Backend::outputNames
  double inputSilentSeconds() const;   // see Backend::inputSilentSeconds; 0 when nothing is open
  void latencyFrames(long& in, long& out) const;   // see Backend::latencyFrames (audiomixer/latency.hpp turns it into a report)
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
  void setOutputTrimDb(double db);   // GAIN MODE: the listening level after the meters (Mixer::setOutputTrimDb)
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
  // DUCKER · VOICE OVER MUSIC (audiomixer/ducker.hpp, Mixer::setDucker): role 1 = key (voice), 2 = target (music), 0 = neither
  void setDucker(const DuckParams& p);
  void setDuckRole(int strip, int role);
  // AUTO DCA analysis (audiomixer/classify.hpp): startAnalysis() measures every strip's input from the next block; stopAnalysis() ends
  // it, and once the audio thread has stopped writing, analysisReady() is true and analysisFeatures() may be read (from any thread)
  void startAnalysis();
  void stopAnalysis();
  bool analysisReady() const { return anaReady_.load(std::memory_order_acquire); }
  FeatureSummary analysisFeatures(int strip) const { return analysisReady() && strip >= 0 && strip < mixer_.strips() ? mixer_.features(strip).summary() : FeatureSummary{}; }
  Mixer& mixerForTest() { return mixer_; }
  // AUTO PHASE: the strip is compared with the rest of the mix (correlation, ~1 s); when it stays out of phase (below -0.35 for 1 s,
  // both carrying signal) its polarity is flipped, then left alone for 3 s. The current polarity is in the strip's meter (polInv).
  void setAutoPolarity(int strip, bool on);
  void setAutoLevel(bool on, double targetLufs, double maxGainDb);
  void setAutoLevelLimits(double minDb, double upDbS, double downDbS);   // Mixer::setAutoLevelLimits (LOUDNESS MANAGER master target)
  // LOUDNESS MANAGER (audiomixer/loudness_ride.hpp): per-strip AUTO FADER + AUTO ON/OFF; rideTouch() when a hand moves the fader
  void setRide(int strip, const RideParams& p);
  void rideTouch(int strip, double seconds = 5);
  void setOutputBits(int bits);                       // 0 = float out (no dither); 16 / 24 = TPDF dither to that depth
  void setDcBlock(bool on);
  void setAnalogChannels(bool on);   // DSP MODE ANALOG on every strip (Mixer::setAnalogChannels)
  void resetProfiler();
  // the monitor section (control room): what goes to output channels 0 / 1 after the master meters. source 0 = master, 1..kMaxBuses = that bus
  // (post level, pre return); level in dB, dim = -20 dB, mono sums L+R, mute = silence. The master meters and loudness are not affected.
  void setMonitor(int source, double db, bool dim, bool mono, bool mute);
  // DUAL LISTEN (control room only): the monitor hears source A (setMonitor's source) and source B together. blend 0 = all A, 1 = all B,
  // equal power in between; listen 0 = the blend, 1 = A alone, 2 = B alone (the A/B switch). Off: source A only, as before.
  void setMonitorDual(bool on, int sourceB, double blend, int listen);
  static void dualGains(double blend, int listen, double& ga, double& gb) noexcept;   // the two source gains of the dual mix
  // the monitor EQ (control room only, never the master / show outputs): up to kMonEqMax peaking filters after the monitor level.
  // AI EQ AUTO sets them from the monitor analysis (monitorBandDb: the dual mix in kMonBands octave bands, 31.5 Hz .. 16 kHz, pre-EQ).
  static constexpr int kMonEqMax = 8, kMonBands = 10;
  static double monitorBandHz(int k) noexcept { return 31.25 * static_cast<double>(1 << k); }
  void setMonitorEqOn(bool on);
  void setMonitorEqFilter(int i, double freq, double q, double gainDb);   // gainDb 0 = that filter is flat
  // the routing matrix. Inputs: strip s reads device input channel `input` (-1 = nothing; the default is input s). Outputs: device output
  // channel `out` carries `source`: 0 off, 1 / 2 master L / R, 3 / 4 monitor L / R, 5 + 2k / 6 + 2k bus k L / R. The default is monitor on 0 / 1 and
  // bus k on 2 + 2k / 3 + 2k. Several outputs may carry the same source; an input may feed several strips.
  static constexpr int kMaxOutputs = 64;
  static constexpr int kSrcOff = 0, kSrcMasterL = 1, kSrcMasterR = 2, kSrcMonL = 3, kSrcMonR = 4, kSrcBus0L = 5;
  static constexpr int kSrcMasterMono = kSrcBus0L + 2 * 32;   // (L + R) / 2 of the master: a mono subwoofer or a mono fill (after the bus sources, so their numbers stay)
  void setInputRoute(int strip, int input);
  void setOutputRoute(int out, int source);
  // THE AMPLIFIER STAGE of output `out` (see amp.hpp): kept across open() / close(); off by default (the output passes bit-exact)
  void setAmp(int out, const AmpParams& p);
  AmpParams amp(int out) const;
  double ampResponseDb(int out, double hz) const;
  void setTrimDb(int strip, double db);
  // INSERTS: external processors (VST3 plug-ins) on a strip, after the built-in channel DSP and before the fader. The chain is swapped
  // whole from the UI thread; the audio thread keeps its own reference for the block, so a removed plug-in stays alive until the block ends.
  // bypassed(): the insert is switched off; in DSP MODE FAST it is then not called at all (TRUE BYPASS: bit-exact, no delay)
  struct Insert { virtual ~Insert() = default; virtual void process(float* mono, std::size_t frames) = 0; virtual bool bypassed() const { return false; } };
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
  ExtraSource* extraSource() const { return extra_.load(); }   // so a short test (the ROUND TRIP click) can borrow the slot and give it back
  Tap* tap() const { return tap_.load(); }
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
  // APP CAPTURE (audiomixer/app_capture.hpp): up to 8 programs at once, each on its own strips. A second pulled source, filled like the
  // network inputs: input kAppInBase + 2k / + 2k + 1 = program k left / right, kAppInBase + 16 + k = program k as one mono channel.
  static constexpr int kAppInBase = 272, kAppSlots = 8, kAppInputs = 3 * kAppSlots;
  static bool isAppInput(int input) { return input >= kAppInBase && input < kAppInBase + kAppInputs; }
  void setAppInput(NetInput* n) { appIn_.store(n); }
  void setNetTap(Tap* t) { netTap_.store(t); }
  unsigned blockFrames() const { return static_cast<unsigned>(left_.size()); }
  // MEASUREMENT: the taps see this one strip as it comes from the device (before its trim, EQ, dynamics and inserts) instead of after them,
  // so a measuring microphone is not coloured by the channel's own processing. -1 = off (the default). What the mix hears does not change.
  void setTapRawStrip(int strip) { tapRaw_.store(strip); }
  int tapRawStrip() const { return tapRaw_.load(); }
  // DSP MODE FAST: TRUE BYPASS (switched-off inserts are not called), SILENT STRIPS skipped (under -120 dBFS for a second, no insert:
  // nothing left ringing), the strips' DSP shared over a worker pool (WorkerPool: bit-identical to one thread), the master limiter's
  // detector at 4x true peak (Mixer::setTruePeakLimiter) and the meters published at about 100 Hz instead of every block.
  // The block is the device's block as before: the mix path has no FIFO of its own. Off (the default) = exactly as before.
  void setFast(bool on);
  bool fast() const { return fastWanted_.load(); }
  int fastHelpers() const { return pool_.helpers(); }
  std::uint64_t fastSharedRuns() const { return pool_.runsShared(); }   // blocks whose strips went out to the helpers
  void setFastHelpers(int n) { fastHelpersWanted_ = n; }   // before setFast(true): -1 = WorkerPool::defaultThreads() (tests and the bench pick)
  Snapshot snapshot() const;
  void onBlock(const float* const* in, float* const* out, std::size_t frames);   // the audio callback; public so a test can clock the runtime itself
 private:
  struct Cmd { int op; int a; double x, y, z; int b = 0; double w = 0, v = 0, u = 0; void* p = nullptr; };
  void post(const Cmd& c);
  void publish();
  static void stripJob(void* self, int k);
  void runStrip(int s, std::size_t frames);
  std::atomic<bool> anaReady_{false};
  bool fast_ = false; std::atomic<bool> fastWanted_{false}, fastOnAudio_{false}; int fastHelpersWanted_ = -1; WorkerPool pool_;   // FAST (fast_: audio thread)
  int jobs_[kMaxStrips] = {}; const float* jobSrc_[kMaxStrips] = {}; bool jobFx_[kMaxStrips] = {}; std::size_t jobFrames_ = 0; std::size_t quiet_[kMaxStrips] = {}; unsigned pubSkip_ = 0;
  std::shared_ptr<const InsertChain> held_[kMaxStrips];   // the chains a parallel block runs (kept until the strip's next block)

  Mixer mixer_; std::unique_ptr<Backend> backend_; std::string backendName_, outDevice_; bool exclusive_ = false;
  int nIn_ = 0, nOut_ = 0; bool running_ = false;
  AmpChannel amp_[64]; AmpParams ampUi_[64]; std::vector<float> mono_;   // the audio thread's stage, and the UI's copy of what was set
  std::vector<float> silence_, left_, right_;
  int monSrc_ = 0; double monGainTarget_ = 1, monGain_ = 1; bool monMono_ = false; double monPk_[2] = {0, 0};   // audio thread only
  bool monDual_ = false; int monSrcB_ = 0; double monGa_ = 1, monGb_ = 0, monGaT_ = 1, monGbT_ = 0;   // DUAL LISTEN, audio thread only
  bool monEqOn_ = false; dsp::Biquad monEq_[kMonEqMax][2]; double monEqGain_[kMonEqMax] = {}; dsp::Biquad monAna_[kMonBands]; double monBand_[kMonBands] = {};
  std::atomic<long> toneLeft_{0}, toneTotal_{0}; double tonePhase_ = 0;
  std::vector<float> monL_, monR_, extL_, extR_; std::atomic<ExtraSource*> extra_{nullptr}; std::atomic<Tap*> tap_{nullptr}, netTap_{nullptr}; std::atomic<NetInput*> netIn_{nullptr}, appIn_{nullptr}; std::atomic<int> tapRaw_{-1}; std::vector<std::vector<float>> netBuf_, appBuf_; int inRoute_[kMaxStrips]; int outRoute_[kMaxOutputs]; double trim_[kMaxStrips]; // the insert chain of each strip, swapped from the UI thread and read on the audio thread: a shared_ptr with the free atomic
  // functions (std::atomic_load / atomic_store), which every standard library has; std::atomic<std::shared_ptr> is missing from libc++ (Android, macOS)
  std::shared_ptr<const InsertChain> inserts_[kMaxStrips];                            // audio thread only (set in open, then by ops)
  std::vector<dsp::Channel> ch_; std::vector<std::vector<float>> chBuf_;
  std::vector<float> sumBuf_;   // AUTO PHASE: the mix of every strip, mono, for the comparison
  bool autoPol_[kMaxStrips] = {}; double apXY_[kMaxStrips] = {}, apXX_[kMaxStrips] = {}, apYY_[kMaxStrips] = {}, apBad_[kMaxStrips] = {}, apHold_[kMaxStrips] = {}, apCorr_[kMaxStrips] = {};
  std::vector<std::vector<float>> busBuf_;   // 2 * kMaxBuses blocks for the aux bus outputs   // one channel DSP and one scratch block per strip (sized in open())
  // control ring: many producers (under a mutex), one consumer (the audio thread)
  static constexpr unsigned kRing = 65536;   // room for a whole console pushed at once (300 strips x their EQ bands, sends and dynamics)
  std::vector<Cmd> ring_ = std::vector<Cmd>(kRing); std::atomic<unsigned> head_{0}, tail_{0}; std::mutex producers_;   // on the heap: 65536 commands do not fit a thread stack
  // published meters (sequence counter)
  mutable std::atomic<unsigned> seq_{0}; Snapshot pub_{}; std::uint64_t blocks_ = 0, frames_ = 0;
  double loadPct_ = 0, blockUs_ = 0, maxBlockUs_ = 0, jitterUs_ = 0; std::int64_t lastStartNs_ = 0;   // profiler (audio thread)
  double dAB_ = 0, dAA_ = 0, dBB_ = 0; std::size_t dN_ = 0; double in12Corr_ = 0, in12DiffDb_ = 0, in12LevelDb_ = -120; std::uint64_t in12Windows_ = 0;   // ONE VOICE (audio thread)
};

}  // namespace am
