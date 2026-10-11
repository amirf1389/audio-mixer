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
  double fs_; std::size_t cap_; int subSize_;
  double c1_[5], c2_[5], z_[8] = {0, 0, 0, 0, 0, 0, 0, 0}, tp_[3][12];
  float h_[2][11] = {};
  double subAcc_ = 0; int subN_ = 0;
  std::vector<double> sub_, blocks_, shorts_;
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
struct Strip { double gainDb = 0, gainLin = 1, pan = 0; bool mute = false, solo = false; Meter meter; };
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
  // inputs[strip] -> outL / outR (frames samples each). Meters, loudness and phase are updated once per call.
  void process(const float* const* inputs, std::size_t frames, float* outL, float* outR);
  const Strip& strip(int i) const { return strips_[i]; }
  const MeterState& masterMeter(int ch) const { return master_[ch].state(); }
  const Loudness& loudness() const { return loudness_; }
  const PhaseResult& phase() const { return phase_; }
  void clearClips();
 private:
  int n_; double fs_, timeMs_ = 0;
  std::vector<Strip> strips_;
  double masterDb_ = 0, masterLin_ = 1;
  bool limOn_ = false; double limCeil_, limGain_ = 1, limReleaseMs_ = 100;
  ClipConfig clip_; Meter master_[2]; Loudness loudness_; PhaseResult phase_{0, 0, 0, 0, true};
};

}  // namespace am
