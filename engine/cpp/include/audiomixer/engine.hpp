// Audio Mixer engine (C++17): the measuring and mixing code of Audio Mixer as a library.
//
//   am::Meter / am::MeterState   peak / RMS / peak hold of blocks with CLIP detection (3 samples in a row at the threshold, or one at full scale; latched)
//   am::Loudness                 ITU-R BS.1770-4 / EBU R128: momentary, short-term, integrated, loudness range, 4x true peak
//   am::phaseCorrelation         stereo correlation with width and polarity inverts
//   am::scopeSweep               oscilloscope sweep: trigger, length, Vpp / RMS / crest / frequency
//   am::VuMeter                  VU needle ballistics
//   am::S16Meter                 the bridge's meter for interleaved 16-bit PCM
//   am::Mixer                    up to 32 mono strips (gain, pan, mute, solo) -> stereo master (gain, limiter) with meters, loudness and phase
//   am::Runtime (runtime.hpp)    the mixer on a sound card: ALSA, WASAPI, Core Audio, or a timed null device
//
// The numbers are those of the page and of the bridge (the reference code in index.html and bridge/levels.js); engine/tests/vectors.txt holds them and every port
// (embedded C, Rust, WebAssembly, this one) is held to the same file. Samples are float -1..1, levels linear unless named _db. Minus infinity is -infinity.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace am {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr int kMaxStrips = 32;

// ───────── level meter ─────────
struct ClipConfig {
  double lin = std::pow(10.0, -0.1 / 20);   // threshold, linear
  int run = 3;                               // samples in a row
  double holdMs = 1400, mergeMs = 250, litMs = 1500;   // peak hold, events closer than this are one, how long the badge lights
  static ClipConfig fromDb(double db);       // -12..0 dBFS
};
struct MeterState {
  double pk = 0, rms = 0, hold = 0, holdAt = 0, clipAt = 0, clipPk = 0;
  std::uint32_t clipN = 0;
  bool latched = false;
  bool lit(double nowMs, const ClipConfig& c) const { return nowMs - clipAt < c.litMs; }
  void clearClip() { clipN = 0; latched = false; clipPk = 0; clipAt = 0; }
};
// A reading window may come in pieces: add() any number of times, then end().
class Meter {
 public:
  explicit Meter(ClipConfig c = {}) : cfg_(c) {}
  void setClip(ClipConfig c) { cfg_ = c; }
  void add(const float* x, std::size_t n);
  void end(double nowMs);
  void push(const float* x, std::size_t n, double nowMs) { add(x, n); end(nowMs); }
  const MeterState& state() const { return s_; }
  MeterState& state() { return s_; }
 private:
  ClipConfig cfg_; MeterState s_;
  double wPk_ = 0, wSs_ = 0; std::size_t wN_ = 0; int run_ = 0, maxRun_ = 0;
};

// ───────── loudness ─────────
class Loudness {
 public:
  explicit Loudness(double sampleRate, std::size_t maxBlocks = 216000);   // the page keeps 6 hours of 100 ms blocks
  void feed(const float* left, const float* right, std::size_t n);
  void reset();                              // integrated, range and true peak start over
  double momentary() const { return momentary_; }   // LUFS (400 ms)
  double shortTerm() const { return shortTerm_; }   // LUFS (3 s)
  double integrated() const { return integrated_; } // LUFS, gated
  double range() const { return lra_; }             // LU
  double truePeak() const { return truePeak_; }     // dBTP
 private:
  void gate();
  // HISTOGRAMS, not lists: the gated integrated loudness and the loudness range are read from fixed 0.01 LU bins (-70 .. +5 LUFS) that
  // are allocated once; a session of any length costs the audio thread the same few microseconds per 100 ms (a list of every block,
  // copied and sorted on the audio thread, took 10-20 ms per update after two hours: drop-outs). Integrated stays exact (each bin keeps
  // the sum of its blocks' energies); the range is read to 0.01 LU.
  static constexpr int kBins = 7500; static constexpr double kLo = -70.0, kStep = 0.01;
  std::vector<std::uint32_t> bCount_, sCount_; std::vector<double> bEnergy_; std::uint64_t bTotal_ = 0, sTotal_ = 0; std::vector<float> scratch_;
  static int binOf(double lufs) { const int k = static_cast<int>((lufs - kLo) / kStep); return k < 0 ? -1 : k >= kBins ? kBins - 1 : k; }
  double fs_; std::size_t cap_; int subSize_;
  double c1_[5], c2_[5], z_[8] = {0, 0, 0, 0, 0, 0, 0, 0}, tp_[3][12];
  float h_[2][11] = {};
  double subAcc_ = 0; int subN_ = 0;
  std::vector<double> sub_;
  double momentary_ = -kInf, shortTerm_ = -kInf, integrated_ = -kInf, lra_ = 0, truePeak_ = -kInf;
};

// ───────── phase, scope, VU ─────────
struct PhaseResult { double corr, peak, rmsL, rmsR; bool silent; };
PhaseResult phaseCorrelation(const float* l, const float* r, std::size_t n, double widthPercent = 100, bool invertL = false, bool invertR = false);

struct ScopeConfig { double timebaseMs = 1.0, triggerDb = -12; bool rising = true; };   // ms per division (10 divisions)
struct ScopeResult { std::size_t n = 0, start = 0; bool triggered = false; double p2p = 0, rms = 0, crestDb = 0, freq = 0; };
ScopeResult scopeSweep(const float* a, std::size_t size, double sampleRate, const ScopeConfig& cfg);

class VuMeter {
 public:
  void step(double rms, double refDbfs);     // once per display frame; 0 VU = ref dBFS (-18 EBU, -20 SMPTE)
  double needle() const { return needle_; }
  double velocity() const { return vel_; }
  bool peakLed() const { return led_; }
 private:
  double needle_ = 0, vel_ = 0; bool led_ = false;
};

// ───────── Int16 interleaved meter (native streams) ─────────
struct S16Snapshot { std::vector<double> peakDb, rmsDb; std::uint32_t clip = 0, clipRuns = 0, frames = 0; bool clipping = false; std::vector<bool> clipCh; };
class S16Meter {
 public:
  S16Meter(int channels, double clipDb = -0.1);
  void setClipDb(double clipDb);             // the threshold may change while it runs (the runs in progress carry on)
  void push(const std::int16_t* interleaved, std::size_t samples);   // a partial last frame is ignored
  S16Snapshot snapshot();                    // levels since the last snapshot, then start over (clip runs carry over)
 private:
  int ch_; std::int32_t limit_;
  std::vector<std::int32_t> peak_, run_; std::vector<double> sumSq_; std::vector<char> clipCh_;
  std::uint32_t frames_ = 0, clips_ = 0, runs_ = 0;
};

// ───────── mixer ─────────
// 360 AUDIO: a strip placed around the listener's head for headphones (binaural): interaural time difference (Woodworth, head radius 8.75 cm),
// head shadow on the far ear (level and a one-pole low-pass that closes with the angle), a rear low-pass, distance gain. Our own model: no
// HRTF data set, no licensed format. Off = the strip pans as before (bit-exact).
struct Spatial { bool on = false; double az = 0, el = 0, dist = 1, spread = 0;   // degrees (0 front, +90 right, 180 behind), elevation -90..90, metres, 0..1 (1 = no direction)
  double tgtL = 0, tgtR = 0, dL = 0, dR = 0, gL = 0.7071, gR = 0.7071, aL = 1, aR = 1, aRear = 1, lpL = 0, lpR = 0, rrL = 0, rrR = 0; unsigned w = 0; float ring[512] = {}; };
struct Strip { Spatial sp; double gainDb = 0, gainLin = 1, pan = 0, depth = 1; bool mute = false, solo = false; Meter meter; double inPk = 0; bool automix = false; double amWeight = 1, amLevel = 0, amGain = 1; };   // inPk: pre-fader input peak of the last block; automix: gain-sharing member, amGain its share (linear)
// AUX SENDS: kMaxBuses stereo buses. Each strip sends to each bus at its own level, pre-fader (the raw input: monitor mixes) or post-fader
// (after gain, mute and solo: effects sends). A bus has a level, a mute, a meter and a RETURN level into the master (an effects return or a
// way to hear a monitor mix); its own stereo output is handed to the caller through process()'s busOut (the runtime maps it to output channels).
constexpr int kMaxBuses = 16;   // 8 AUX (monitor mixes, usually pre-fader) + 8 FX (effect returns, post-fader); the console names them so
struct Send { double lin = 0; bool pre = false; };
// an effect on a bus (reverb, delay ...), processed in place after the bus level and before its meter, return and output; audio thread only
struct BusProcessor { virtual ~BusProcessor() = default; virtual void process(float* L, float* R, std::size_t frames) = 0; };
struct Bus { double levelLin = 1, returnLin = 0; bool mute = false; Meter meter = Meter(); BusProcessor* fx = nullptr; };
class Mixer {
 public:
  Mixer(int strips, double sampleRate);
  int strips() const { return n_; }
  double sampleRate() const { return fs_; }
  void setGainDb(int strip, double db);      // -60 and below = silence, +10 at most
  void setPan(int strip, double pan);        // -1 left .. +1 right, constant power (-3 dB in the middle)
  void setMute(int strip, bool on);
  void setSolo(int strip, bool on);          // with any solo on, only the solo strips are heard
  void setMasterDb(double db);
  void setLimiter(bool on, double ceilingDb = -0.5, double releaseMs = 100);   // brick-wall peak limiter: instant attack
  void setClipDb(double db);
  void setPeakHoldMs(double ms);              // how long the peak-hold line stays (all meters); 0 = no hold
  void setSend(int strip, int bus, double db, bool pre);     // -60 and below = off, +10 at most
  void setBusLevel(int bus, double db);
  void setBusMute(int bus, bool on);
  void setBusReturn(int bus, double db);                     // -60 and below = none (default)
  double limiterReductionDb() const { return limGrDb_; }   // how far the master limiter pulled the last block down (dB, >= 0; 0 = idle)
  void setMasterProcessor(BusProcessor* p) { masterFx_ = p; }  // on the summed master (after the buses' returns and the extra source), before width, fader and limiter; nullptr = none
  void setBusProcessor(int bus, BusProcessor* p);            // nullptr = none; the caller keeps it alive while set
  void setWidth(double w);                                   // master stereo width: 0 mono, 1 as is, 2 wide (mid / side), before the master gain
  // AUTOMIX (gain sharing, Dugan-style): the member strips share one unity gain in proportion to their weighted input level, so an
  // open microphone that is spoken into takes the gain and the quiet ones fall back (10 ms attack, 300 ms release, -40 dB floor)
  void setAutomix(int strip, bool on, double weight);        // weight 0.25 .. 4 (1 = equal)
  double automixGainDb(int strip) const;
  // AUTO LEVEL ("no low, but loud"): a slow gain after the width stage that steers the short-term loudness toward a target (LUFS),
  // never above maxGainDb, +1 dB/s up and -6 dB/s down; the brick-wall limiter after it keeps the true peak in check
  void setAutoLevel(bool on, double targetLufs, double maxGainDb);
  double autoLevelGainDb() const { return autoDb_; }
  // OUTPUT STAGE (roadmap 12): an optional DC blocker (first-order high-pass at 5 Hz) before the auto level and the limiter, and TPDF
  // dither after the limiter when the device takes 16 or 24-bit integers (0 = float, no dither): the last thing before the converter.
  void setOutputBits(int bits) { outBits_ = bits == 16 || bits == 24 ? bits : 0; }
  int outputBits() const { return outBits_; }
  void setDcBlock(bool on) { dcOn_ = on; if (!on) { dcX_[0] = dcX_[1] = dcY_[0] = dcY_[1] = 0; } }   // off by default: the reference vectors are bit-exact
  const Bus& bus(int k) const { return buses_[static_cast<std::size_t>(k)]; }
  const Send& send(int strip, int bus) const { return sends_[static_cast<std::size_t>(strip)][static_cast<std::size_t>(bus)]; }
  // inputs[strip] -> outL / outR (frames samples each). Meters, loudness and phase are updated once per call.
  // busOut (optional): 2 * kMaxBuses pointers (L, R per bus, any may be null) that receive each bus's stereo output.
  // extL / extR: an optional stereo signal summed into the master before master gain / limiter / meters (the ARRANGE player)
  // SURROUND 5.0 (the SDK's surround mode): each strip has a depth, +1 = front, 0 = between, -1 = rear; pan places it left / right in that
  // row; a centred front strip also feeds the CENTRE. outs: L, R, C, Ls, Rs. Master gain, limiter and meters apply; buses, effects and the
  // extra sources stay with the stereo process(). The stereo meters follow L / R.
  void setSpatial(int strip, bool on, double azDeg, double elDeg, double distM, double spread);   // 360 audio (headphones) for this strip in the stereo process()
  const Spatial& spatial(int strip) const { return strips_[static_cast<std::size_t>(strip)].sp; }
  void setDepth(int strip, double depth);
  double depth(int strip) const { return strip >= 0 && strip < n_ ? strips_[static_cast<std::size_t>(strip)].depth : 1; }
  void process5(const float* const* inputs, std::size_t frames, float* const* outs);
  void process(const float* const* inputs, std::size_t frames, float* outL, float* outR, float* const* busOut = nullptr, const float* extL = nullptr, const float* extR = nullptr);
  const Strip& strip(int i) const { return strips_[i]; }
  const MeterState& masterMeter(int ch) const { return master_[ch].state(); }
  const Loudness& loudness() const { return loudness_; }
  std::uint64_t nonFiniteSamples() const { return nonFinite_; }   // NaN / inf samples the master guard turned into silence (diagnostics)
  const PhaseResult& phase() const { return phase_; }
  void clearClips();
 private:
  int n_; double fs_, timeMs_ = 0;
  std::vector<Strip> strips_;
  double masterDb_ = 0, masterLin_ = 1;
  double width_ = 1.0;   // master stereo width
  bool autoOn_ = false; double autoTarget_ = -16, autoMax_ = 12, autoDb_ = 0, autoSec_ = 0; int automixN_ = 0;
  int outBits_ = 0; bool dcOn_ = false; double dcX_[2] = {0, 0}, dcY_[2] = {0, 0}; std::uint32_t rng_ = 0x2545F491u;
  bool limOn_ = false; double limCeil_, limGain_ = 1, limReleaseMs_ = 100, limGrDb_ = 0;
  std::uint64_t nonFinite_ = 0;
  ClipConfig clip_; Meter master_[2]; Loudness loudness_; PhaseResult phase_{0, 0, 0, 0, true};
  Send sends_[kMaxStrips][kMaxBuses]{}; Bus buses_[kMaxBuses]; bool anySend_ = false; BusProcessor* masterFx_ = nullptr;
};

}  // namespace am
