// Built-in bus effects for the EFFECTS page, ported from the web console's rack (plate reverb, tape delay, stereo chorus, tube drive):
// each is a BusProcessor that runs in place on a bus before its return into the master and its output. Parameters are 0..1 (normalized)
// with a plain-value text, like a plug-in's, so the same generic controls serve them. All state is per instance, no allocation in process().
#pragma once
#include <cmath>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audiomixer/engine.hpp"

namespace am::fx {

struct Param { std::string id, title, units; double value = 0, def = 0; };   // value: 0..1

class Effect : public BusProcessor {
 public:
  virtual ~Effect() = default;
  virtual const char* kind() const = 0;            // "reverb" | "delay" | "chorus" | "drive"
  virtual const char* name() const = 0;
  virtual std::vector<Param> params() const = 0;
  virtual void set(const std::string& id, double v) = 0;   // UI thread; picked up by the audio thread without locks (atomics; no buffer is ever resized by set())
  virtual std::string text(const std::string& id, double v) const = 0;
  virtual void setSampleRate(double fs) = 0;
  virtual void reset() = 0;                        // clears delay lines
  void setBypass(bool b) { bypass_.store(b); }
  bool bypass() const { return bypass_.load(); }
  void setMix(double wet) { mix_.store(wet < 0 ? 0 : wet > 1 ? 1 : wet); }   // 1 = fully wet (a send effect), 0 = dry
  double mix() const { return mix_.load(); }
 protected:
  std::atomic<bool> bypass_{false}; std::atomic<double> mix_{1.0};
};

std::unique_ptr<Effect> make(const std::string& kind, double fs);   // nullptr for an unknown kind
std::vector<std::pair<std::string, std::string>> catalog();         // {kind, name}

// Schroeder / Moorer plate: 4 parallel combs + 2 series allpasses per side, pre-delay, damping; DECAY 0.3..12 s, DAMP, PREDELAY, WIDTH
class Reverb final : public Effect {
 public:
  explicit Reverb(double fs);
  const char* kind() const override { return "reverb"; } const char* name() const override { return "PLATE REVERB"; }
  std::vector<Param> params() const override; void set(const std::string& id, double v) override; std::string text(const std::string& id, double v) const override;
  void setSampleRate(double fs) override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private:
  void design(); double fs_; std::atomic<double> decay_{0.35}, damp_{0.5}, pre_{0.1}, width_{1.0};
  struct Comb { std::vector<float> buf; std::size_t i = 0; float fb = 0, store = 0; };
  Comb comb_[2][4]; std::vector<float> ap_[2][2]; std::size_t api_[2][2] = {{0, 0}, {0, 0}}; std::vector<float> pre_buf_[2]; std::size_t prei_ = 0;
  std::atomic<float> fbGain_{0.8f}, dampA_{0.5f}; std::atomic<std::size_t> preSamples_{0};
};
// Tape delay: TIME 20..2000 ms, FEEDBACK, TONE (one-pole low-pass in the loop), PING-PONG
class TapeDelay final : public Effect {
 public:
  explicit TapeDelay(double fs);
  const char* kind() const override { return "delay"; } const char* name() const override { return "TAPE DELAY"; }
  std::vector<Param> params() const override; void set(const std::string& id, double v) override; std::string text(const std::string& id, double v) const override;
  void setSampleRate(double fs) override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private:
  double fs_; std::atomic<double> time_{0.3}, fb_{0.4}, tone_{0.5}, pingpong_{0}; std::vector<float> buf_[2]; std::size_t w_ = 0; float lp_[2] = {0, 0};
};
// Stereo chorus: RATE 0.1..5 Hz, DEPTH, SPREAD (LFO phase offset R)
class Chorus final : public Effect {
 public:
  explicit Chorus(double fs);
  const char* kind() const override { return "chorus"; } const char* name() const override { return "STEREO CHORUS"; }
  std::vector<Param> params() const override; void set(const std::string& id, double v) override; std::string text(const std::string& id, double v) const override;
  void setSampleRate(double fs) override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private:
  double fs_; std::atomic<double> rate_{0.25}, depth_{0.5}, spread_{0.5}; std::vector<float> buf_[2]; std::size_t w_ = 0; double phase_ = 0;
};
// Tube drive: DRIVE, TONE (post low-pass), OUTPUT; a tanh waveshaper with 2x oversampling by linear interpolation
class Drive final : public Effect {
 public:
  explicit Drive(double fs);
  const char* kind() const override { return "drive"; } const char* name() const override { return "TUBE DRIVE"; }
  std::vector<Param> params() const override; void set(const std::string& id, double v) override; std::string text(const std::string& id, double v) const override;
  void setSampleRate(double fs) override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private:
  double fs_; std::atomic<double> drive_{0.4}, tone_{0.6}, out_{0.7}; float lp_[2] = {0, 0}, last_[2] = {0, 0};
};

// The knob effects share one shape: up to six knobs (0..1) and a per-channel state.
class Simple : public Effect {
 public:
  explicit Simple(double fs) : fs_(fs > 0 ? fs : 48000) {}
  std::vector<Param> params() const override { std::vector<Param> v; for (int i = 0; i < count(); ++i) v.push_back({knobId(i), knobTitle(i), "", k_[i].load(), knobDef(i)}); return v; }
  void set(const std::string& id, double v) override { for (int i = 0; i < count(); ++i) if (id == knobId(i)) k_[i] = v < 0 ? 0 : v > 1 ? 1 : v; }
  void setSampleRate(double fs) override { fs_ = fs > 0 ? fs : 48000; reset(); }
 protected:
  virtual int count() const = 0; virtual const char* knobId(int i) const = 0; virtual const char* knobTitle(int i) const = 0; virtual double knobDef(int i) const = 0;
  double fs_; std::atomic<double> k_[8]{{0.5}, {0.5}, {0.5}, {0.5}, {0.5}, {0.5}, {0.5}, {0.5}};   // up to 8 knobs (count() must stay <= 8)
};
// SYNC DELAY: an echo in time with the song: TEMPO (60 .. 200 BPM) and NOTE (1/2, 1/4, dotted 1/8, 1/8, 1/8 triplet, 1/16) give the delay
// time; FEEDBACK, TONE (the echoes get darker) and WIDTH (0 = both sides together, 1 = the echoes bounce left / right)
class SyncDelay final : public Simple { public: explicit SyncDelay(double fs); const char* kind() const override { return "syncdelay"; } const char* name() const override { return "SYNC DELAY"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void setSampleRate(double fs) override; void process(float* L, float* R, std::size_t n) override;
  static double bpmOf(double v) { return 60.0 + (v < 0 ? 0 : v > 1 ? 1 : v) * 140.0; } static int noteOf(double v) { const int k = static_cast<int>((v < 0 ? 0 : v > 1 ? 1 : v) * 5.999); return k; } static double beatsOf(int note);
 protected: int count() const override { return 5; } const char* knobId(int i) const override { static const char* k[] = {"tempo", "note", "feedback", "tone", "width"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"TEMPO", "NOTE", "FEEDBACK", "TONE", "WIDTH"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.4286, 0.5, 0.35, 0.6, 0.5}; return d[i]; }
 private: std::vector<float> buf_[2]; std::size_t w_ = 0; float lp_[2] = {0, 0}; };
// FEEDBACK KILLER: finds the howl of a microphone that hears its own loudspeaker and notches it out. Every 1024 samples the spectrum of
// the sum is taken; a single narrow peak that stands far above the rest of the spectrum and stays on the same frequency for several
// frames in a row (a third of a second), with no overtone, is taken for feedback, and one of six notch filters is put on it. A peak that
// keeps ringing is cut deeper. A long, pure, steady note of an instrument (a flute, a test tone) looks the same and will be notched too.
//   SENSITIVITY  how far above the rest a peak must stand (24 dB .. 9 dB)      DEPTH  how deep a notch may go (-6 .. -30 dB)
//   WIDTH        how narrow the notches are (Q 60 .. 10)                       MODE   HOLD: a notch stays until RESET (a fixed installation);
//                                                                                     LIVE: a notch not needed for 8 seconds is given back
class FeedbackKiller final : public Simple { public: explicit FeedbackKiller(double fs); const char* kind() const override { return "antifeedback"; } const char* name() const override { return "FEEDBACK KILLER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void setSampleRate(double fs) override; void process(float* L, float* R, std::size_t n) override;
  int notches() const { return active_.load(); }                       // how many notches are set now
  int fixedCount() const;                                              // how many of the 24 are FIXED (kept until reset); the rest are LIVE
  bool notchFixed(int i) const { return i >= 0 && i < kNotches && i < fixedCount(); }
  double notchDepth(int i) const { return i >= 0 && i < kNotches && n_[i].on ? n_[i].depthDb : 0.0; }
  double notchHz(int i) const { return i >= 0 && i < kNotches && n_[i].on ? n_[i].hz : 0.0; }
 protected: int count() const override { return 7; } const char* knobId(int i) const override { static const char* k[] = {"sens", "depth", "width", "mode", "lift", "program", "fixed"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"SENSITIVITY", "DEPTH", "WIDTH", "MODE", "LIFT TIME", "PROGRAM", "FIXED FILTERS"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.5, 0.5, 0.5, 0.0, 0.25, 0.0, 0.25}; return d[i]; }
 private:
  static constexpr int kNotches = 24, kN = 1024;
  struct Notch { bool on = false; double hz = 0, depthDb = 0, b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z[2][2] = {{0, 0}, {0, 0}}; long idle = 0; };
  void design(Notch& n) const; void analyse();
  Notch n_[kNotches]; std::vector<float> frame_; std::size_t fill_ = 0; int lastBin_ = -1, streak_ = 0; std::atomic<int> active_{0}; };
// ANALOG CHANNEL: what an analogue channel strip does to a signal that is pushed. DRIVE pushes the level into a soft saturation
// (tanh; quiet signals keep their level), BIAS moves the working point off centre so that even harmonics appear beside the odd ones,
// TONE is the gentle treble loss of transformers and tape (3 kHz .. 21 kHz, one pole), OUTPUT trims the level (-12 .. +12 dB).
class Analog final : public Simple { public: explicit Analog(double fs); const char* kind() const override { return "analog"; } const char* name() const override { return "ANALOG CHANNEL"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 4; } const char* knobId(int i) const override { static const char* k[] = {"drive", "bias", "tone", "output"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"DRIVE", "BIAS", "TONE", "OUTPUT"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.35, 0.2, 0.8, 0.5}; return d[i]; }
 private: float lp_[2] = {0, 0}; };
// SOUND BARRIER: loud, with impact, and never past the ceiling. Four stages in a row:
//   IMPACT   the first milliseconds of each hit in the bass and middle are lifted (a fast level follower against a slow one), so that
//            drums keep their punch when everything is pushed
//   SMOOTH   the treble (over 5 kHz) is held back the more it sticks out, so that pushing does not turn harsh
//   PUSH     the whole signal is raised 0 .. 18 dB into a soft knee
//   CEILING  the barrier: a limiter that sees every peak 3 ms before it arrives and has the gain down in time; nothing leaves above
//            the ceiling (-6 .. 0 dBFS). The effect delays the signal by those 3 ms.
//   AUTO     the run-up: like a train brought up to the speed its line allows, the push follows the music by itself until the barrier
//            is working by the amount asked for (0 = off .. 6 dB), whatever the level coming in. It climbs over a second or two,
//            backs off faster, never adds more than 24 dB, and waits in silence. MACH is the level against the ceiling: 1.0 is at it.
class Barrier final : public Simple { public: explicit Barrier(double fs); const char* kind() const override { return "barrier"; } const char* name() const override { return "SOUND BARRIER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void setSampleRate(double fs) override; void process(float* L, float* R, std::size_t n) override;
  double reductionDb() const { return grDb_.load(); }   // how hard the barrier works now (0 = not at all)
  double autoDb() const { return autoDb_.load(); }      // what the run-up adds now
  double mach() const { return mach_.load(); }          // the level arriving at the barrier against the ceiling (1.0 = at it)
  static double ceilingOf(double v) { return std::pow(10.0, (-6.0 + (v < 0 ? 0 : v > 1 ? 1 : v) * 6.0) / 20.0); }
 protected: int count() const override { return 5; } const char* knobId(int i) const override { static const char* k[] = {"push", "impact", "smooth", "ceiling", "auto"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"PUSH", "IMPACT", "SMOOTH", "CEILING", "AUTO"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.33, 0.5, 0.5, 0.83, 0.0}; return d[i]; }
 private: std::vector<float> dl_[2]; std::size_t w_ = 0; int look_ = 144, hold_ = 0; float lp_[2] = {0, 0}, hp_[2] = {0, 0}, hp2_[2] = {0, 0}; double fast_ = 0, slow_ = 0, eHigh_ = 0, eBody_ = 0, g_ = 1, auto_ = 1, peakIn_ = 0; std::atomic<double> grDb_{0}, autoDb_{0}, mach_{0}; };
// DUAL LISTEN EQ: two frequency ranges, bass (under 200 Hz) and treble (over 4 kHz), that listen to the music and set themselves.
// The signal is split into bass, middle and treble (the three add up to the signal again exactly). The loudness of each is followed
// slowly, and the bass and treble gains move until bass and treble stand where BASS and TREBLE ask against the middle. A bass-heavy
// song is eased, a thin one is filled; a dull one is opened, a harsh one is calmed. In quiet passages it holds what it had.
//   BASS / TREBLE  where each range should stand against the middle (-6 .. +6 dB around a natural balance)
//   RANGE          how far a gain may move (0 .. 12 dB either way)        SPEED  how fast it follows (4 s .. 0.3 s)
class DualEq final : public Simple { public: explicit DualEq(double fs); const char* kind() const override { return "dualeq"; } const char* name() const override { return "DUAL LISTEN EQ"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  double bassDb() const { return gLowDb_.load(); } double trebleDb() const { return gHighDb_.load(); }   // what it is doing now
 protected: int count() const override { return 4; } const char* knobId(int i) const override { static const char* k[] = {"bass", "treble", "range", "speed"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"BASS", "TREBLE", "RANGE", "SPEED"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.5, 0.5, 0.5, 0.4}; return d[i]; }
 private: float lp_[2] = {0, 0}, hp_[2] = {0, 0}; double eLow_ = 0, eMid_ = 0, eHigh_ = 0, gl_ = 1, gh_ = 1; std::atomic<double> gLowDb_{0}, gHighDb_{0}; };
// SAMPLE PLAYER: plays one of the 100 built-in samples (SAMPLES page) into the channel, alongside what the channel carries.
//   SAMPLE  which one (1 .. 100)     PLAY  off / on: switching it on starts the sample from the top     LOOP  off / on     LEVEL  -40 .. +6 dB
// The sample is made on the control thread when it is chosen and handed to the audio thread whole (a shared pointer swap, no allocation there).
class SamplePlayer final : public Simple { public: explicit SamplePlayer(double fs); const char* kind() const override { return "sampler"; } const char* name() const override { return "SAMPLE PLAYER"; }
  void set(const std::string& id, double v) override; std::string text(const std::string& id, double v) const override; void reset() override; void setSampleRate(double fs) override; void process(float* L, float* R, std::size_t n) override;
  static int sampleOf(double v) { return 1 + static_cast<int>(std::lround((v < 0 ? 0 : v > 1 ? 1 : v) * 99.0)); }
  bool playing() const { return playing_.load(); }
  bool loadFile(const std::string& path, std::string& err);   // the user's own file (WAV, FLAC, AIFF, M4A, MP3 ...), resampled to this rate; SAMPLE then has no effect until cleared
  void clearFile();
  std::string fileName() const { const std::shared_ptr<const Buf> b = std::atomic_load(&buf_); return b ? b->file : std::string(); }
 protected: int count() const override { return 4; } const char* knobId(int i) const override { static const char* k[] = {"sample", "play", "loop", "level"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"SAMPLE", "PLAY", "LOOP", "LEVEL"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.0, 0.0, 0.0, 0.87}; return d[i]; }
 private: struct Buf { std::vector<float> L, R; int id = 0; std::string file; }; void load(); std::shared_ptr<const Buf> buf_; std::size_t pos_ = 0; bool wasOn_ = false; std::atomic<bool> playing_{false}; std::atomic<unsigned> trig_{0}; unsigned seenTrig_ = 0; int loaded_ = 0; };
// DE-SQUASH: gives back some of the life a heavy compressor took. It compares a fast envelope (the attacks) with a slow one (the body)
// and lets the attacks stand out again by up to RANGE dB; AMOUNT is how much of the difference it restores, SPEED how quick the attacks are.
// Quiet passages and steady tones are left as they are. Use it on a channel that shows the OVER-COMPRESSED alert.
class DeSquash final : public Simple { public: explicit DeSquash(double fs); const char* kind() const override { return "desquash"; } const char* name() const override { return "DE-SQUASH"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  double liftDb() const { return lift_.load(); }
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"amount", "range", "speed"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"AMOUNT", "RANGE", "SPEED"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.5, 0.5, 0.5}; return d[i]; }
 private: double fast_ = 0, slow_ = 0, g_ = 1; std::atomic<double> lift_{0}; };
// PITCH SHIFTER: moves the pitch without changing the speed. The signal runs into a short delay line that is read by two heads moving
// faster or slower than it is written; each head fades in and out (a raised cosine) while the other covers its jump back, half a window
// apart. PITCH -12 .. +12 semitones in whole steps, FINE 50 cents either way, WINDOW 20 .. 90 ms (short: tighter and rougher; long: smoother, more delay).
class Pitch final : public Simple { public: explicit Pitch(double fs); const char* kind() const override { return "pitch"; } const char* name() const override { return "PITCH SHIFTER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void setSampleRate(double fs) override; void process(float* L, float* R, std::size_t n) override;
  static double semitones(double pitch, double fine) { return std::round((pitch - 0.5) * 24.0) + (fine - 0.5); }
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"pitch", "fine", "window"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"PITCH", "FINE", "WINDOW"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.5, 0.5, 0.36}; return d[i]; }
 private: std::vector<float> buf_[2]; std::size_t w_ = 0; double ph_ = 0; int win_ = 2048; };
// FLANGER: a 1..8 ms delay swept by an LFO with feedback: RATE, DEPTH, FEEDBACK
class Flanger final : public Simple { public: explicit Flanger(double fs); const char* kind() const override { return "flanger"; } const char* name() const override { return "FLANGER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"rate", "depth", "feedback"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"RATE", "DEPTH", "FEEDBACK"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.2, 0.6, 0.5}; return d[i]; }
 private: std::vector<float> buf_[2]; std::size_t w_ = 0; double ph_ = 0; float fb_[2] = {0, 0}; };
// PHASER: six all-pass stages swept by an LFO: RATE, DEPTH, FEEDBACK
class Phaser final : public Simple { public: explicit Phaser(double fs); const char* kind() const override { return "phaser"; } const char* name() const override { return "PHASER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"rate", "depth", "feedback"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"RATE", "DEPTH", "FEEDBACK"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.25, 0.7, 0.4}; return d[i]; }
 private: float z_[2][6] = {}; float fb_[2] = {0, 0}; double ph_ = 0; };
// TREMOLO: the level moved by an LFO: RATE, DEPTH, SHAPE (sine .. square)
class Tremolo final : public Simple { public: explicit Tremolo(double fs); const char* kind() const override { return "tremolo"; } const char* name() const override { return "TREMOLO"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"rate", "depth", "shape"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"RATE", "DEPTH", "SHAPE"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.4, 0.6, 0.0}; return d[i]; }
 private: double ph_ = 0; };
// AUTO WAH: a resonant band-pass whose frequency follows the level: SENSITIVITY, RANGE, RESONANCE
class AutoWah final : public Simple { public: explicit AutoWah(double fs); const char* kind() const override { return "wah"; } const char* name() const override { return "AUTO WAH"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"sens", "range", "reso"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"SENSITIVITY", "RANGE", "RESONANCE"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.6, 0.6, 0.5}; return d[i]; }
 private: float lp_[2] = {0, 0}, bp_[2] = {0, 0}; double env_ = 0; };
// BIT CRUSHER: fewer bits and a lower sample rate: BITS, DOWNSAMPLE, TONE
class Crusher final : public Simple { public: explicit Crusher(double fs); const char* kind() const override { return "crusher"; } const char* name() const override { return "BIT CRUSHER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"bits", "down", "tone"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"BITS", "DOWNSAMPLE", "TONE"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.6, 0.2, 0.8}; return d[i]; }
 private: float hold_[2] = {0, 0}, lp_[2] = {0, 0}; double cnt_ = 0; };
// EXCITER: harmonics made from the band above a frequency, added to the signal: AMOUNT, FREQUENCY, HARMONICS
// SUB SYNTH: adds the octave below the bass. The low end (below RANGE) is found, a flip-flop switches at every rising zero crossing so
// it runs at half the frequency, it is shaped by the bass's own level (so it follows the note and is silent when the bass stops) and
// smoothed into a sine-like sub. AMOUNT sets how much of it joins the programme; the original is untouched.
class SubSynth final : public Simple { public: explicit SubSynth(double fs); const char* kind() const override { return "subsynth"; } const char* name() const override { return "SUB SYNTH"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"amount", "range", "tight"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"AMOUNT", "RANGE", "TIGHTNESS"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.4, 0.5, 0.5}; return d[i]; }
 private: double lp1_ = 0, lp2_ = 0, env_ = 0, o1_ = 0, o2_ = 0, last_ = 0; bool flip_ = false; };
class Exciter final : public Simple { public: explicit Exciter(double fs); const char* kind() const override { return "exciter"; } const char* name() const override { return "EXCITER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"amount", "freq", "harm"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"AMOUNT", "FREQUENCY", "HARMONICS"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.4, 0.5, 0.5}; return d[i]; }
 private: float lp_[2] = {0, 0}; };

// TUBE AMP: a guitar / bass / vocal amplifier in three parts: a pre-amp valve stage (asymmetric clipping with a bias), the tone stack
// (BASS, MID, TREBLE), a power stage that sags under load, PRESENCE, and a 4x12 cabinet roll-off. GAIN, BASS, MID, TREBLE, PRESENCE, MASTER
class TubeAmp final : public Simple { public: explicit TubeAmp(double fs); const char* kind() const override { return "tubeamp"; } const char* name() const override { return "TUBE AMP"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 6; } const char* knobId(int i) const override { static const char* k[] = {"gain", "bass", "mid", "treble", "presence", "master"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"GAIN", "BASS", "MID", "TREBLE", "PRESENCE", "MASTER"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.45, 0.5, 0.5, 0.55, 0.4, 0.5}; return d[i]; }
 private: float hp_[2] = {}, lo_[2] = {}, hi_[2] = {}, pr_[2] = {}, c1_[2] = {}, c2_[2] = {}, dc_[2] = {}; double sag_ = 0; };
// PRE AMP: the first gain stage of a channel: GAIN (-12 .. +36 dB), LOW CUT, WARMTH (even harmonics, like a transformer or a valve run gently), OUTPUT
class PreAmp final : public Simple { public: explicit PreAmp(double fs); const char* kind() const override { return "preamp"; } const char* name() const override { return "PRE AMP"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 4; } const char* knobId(int i) const override { static const char* k[] = {"gain", "lowcut", "warmth", "out"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"GAIN", "LOW CUT", "WARMTH", "OUTPUT"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.25, 0.2, 0.3, 0.6667}; return d[i]; }
 private: float hp_[2] = {}, dc_[2] = {}; };
// POST AMP: the last stage of a channel's rack: LEVEL (-24 .. +12 dB), TILT (dark .. bright around 1 kHz), CLIP (a soft ceiling that keeps peaks in)
class PostAmp final : public Simple { public: explicit PostAmp(double fs); const char* kind() const override { return "postamp"; } const char* name() const override { return "POST AMP"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"level", "tilt", "clip"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"LEVEL", "TILT", "CLIP"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.6667, 0.5, 0.0}; return d[i]; }
 private: float lp_[2] = {}; };
// POWER REGULATOR: keeps the power of the signal steady, the way a regulated supply keeps its voltage: it measures the level slowly (RMS) and
// turns the gain up or down toward TARGET (-30 .. -6 dBFS), by RANGE at most (0 .. 12 dB), at SPEED (0.2 .. 5 s); CEILING (-12 .. 0 dBFS)
// is a hard stop after it. Silence is not turned up (below -60 dBFS the gain holds).
class PowerRegulator final : public Simple { public: explicit PowerRegulator(double fs); const char* kind() const override { return "regulator"; } const char* name() const override { return "POWER REGULATOR"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  double gainDb() const { return gainDb_.load(); }
 protected: int count() const override { return 4; } const char* knobId(int i) const override { static const char* k[] = {"target", "range", "speed", "ceiling"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"TARGET", "RANGE", "SPEED", "CEILING"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.5, 0.5, 0.3, 0.9167}; return d[i]; }
 private: double ms_ = 0, g_ = 1; std::atomic<double> gainDb_{0}; };
// SHORT PROTECT: what an amplifier's protection does when its output is shorted or driven into a fault, done on the signal: when the level
// stays above THRESHOLD (-12 .. 0 dBFS) for longer than TIME (5 .. 500 ms), or a steady DC offset appears, the output is cut (a fast fade,
// no click) and stays cut until the fault has been gone for HOLD (0.5 .. 10 s); then it fades back in. Neutral while nothing is wrong.
// It works on the signal only: it cannot see a real short circuit on a speaker cable.
class ShortProtect final : public Simple { public: explicit ShortProtect(double fs); const char* kind() const override { return "protect"; } const char* name() const override { return "SHORT PROTECT"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  bool tripped() const { return tripped_.load(); } int trips() const { return trips_.load(); }
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"threshold", "time", "hold"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"THRESHOLD", "TIME", "HOLD"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.9167, 0.1, 0.2}; return d[i]; }
 private: double over_ = 0, safe_ = 0, dc_[2] = {}, g_ = 1; bool cut_ = false; std::atomic<bool> tripped_{false}; std::atomic<int> trips_{0}; };
// VOICE RIDER: an automatic hand on the fader for a voice. It listens in the voice band (150 Hz .. 4 kHz) and rides the gain so the voice
// sits at TARGET (-30 .. -10 dBFS), by RANGE at most (0 .. 12 dB), at SPEED (50 ms .. 1 s). Below SENSE (-70 .. -30 dBFS) nobody is
// speaking: the gain holds where it is, so the room noise between sentences is not turned up. No look-ahead: nothing is delayed.
class VoiceRider final : public Simple { public: explicit VoiceRider(double fs); const char* kind() const override { return "rider"; } const char* name() const override { return "VOICE RIDER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  double gainDb() const { return gainDb_.load(); } bool speaking() const { return speaking_.load(); }
 protected: int count() const override { return 4; } const char* knobId(int i) const override { static const char* k[] = {"target", "range", "speed", "sense"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"TARGET", "RANGE", "SPEED", "SENSE"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.6, 0.5, 0.2, 0.5}; return d[i]; }
 private: double hp_ = 0, lp_ = 0, ms_ = 0, g_ = 1; std::atomic<double> gainDb_{0}; std::atomic<bool> speaking_{false}; };
// VOICE CLEAN: less noise and less room on a voice, with no delay at all (no look-ahead, no block processing: the first sample in is the
// first sample out). The signal is split into four bands that add back to exactly the input. DENOISE: each band learns its own noise floor
// (the level it falls back to between sounds) and is turned down, by up to 24 dB, while it sits at that floor. DEREVERB: each band is
// turned down, by up to 12 dB, while it is only dying away (the tail a room adds after a sound stops). OUTPUT: -12 .. +12 dB.
// Signal processing, not machine learning: it cannot separate a voice from loud music or from another voice.
class VoiceClean final : public Simple { public: explicit VoiceClean(double fs); const char* kind() const override { return "clean"; } const char* name() const override { return "VOICE CLEAN"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  double reductionDb() const { return redDb_.load(); }
 protected: int count() const override { return 3; } const char* knobId(int i) const override { static const char* k[] = {"denoise", "dereverb", "out"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"DENOISE", "DEREVERB", "OUTPUT"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.5, 0.3, 0.5}; return d[i]; }
 private: static constexpr int kBands = 4; double lp_[2][3] = {}, env_[kBands] = {}, nf_[kBands] = {}, fast_[kBands] = {}, slow_[kBands] = {}, g_[kBands] = {1, 1, 1, 1}; std::atomic<double> redDb_{0}; };
// HALL REVERB: an eight-line feedback delay network (long, dense, slowly modulated): SIZE, DECAY (0.5 .. 20 s), DAMP, PRE-DELAY, MODULATION
// ROOM REVERB: the same network with four short lines and early reflections: SIZE, DECAY (0.1 .. 3 s), DAMP, EARLY
class Fdn : public Simple { public: Fdn(double fs, int lines, bool hall); std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int lines_; bool hall_; std::vector<float> d_[8], pre_, er_; std::size_t w_[8] = {}, pw_ = 0, ew_ = 0; float lp_[8] = {}; double ph_ = 0; };
class Hall final : public Fdn { public: explicit Hall(double fs) : Fdn(fs, 8, true) { for (int i = 0; i < 5; ++i) k_[i] = knobDef(i); } const char* kind() const override { return "hall"; } const char* name() const override { return "HALL REVERB"; }
 protected: int count() const override { return 5; } const char* knobId(int i) const override { static const char* k[] = {"size", "decay", "damp", "predelay", "mod"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"SIZE", "DECAY", "DAMP", "PRE-DELAY", "MODULATION"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.7, 0.45, 0.4, 0.2, 0.3}; return d[i]; } };
class Room final : public Fdn { public: explicit Room(double fs) : Fdn(fs, 4, false) { for (int i = 0; i < 4; ++i) k_[i] = knobDef(i); } const char* kind() const override { return "room"; } const char* name() const override { return "ROOM REVERB"; }
 protected: int count() const override { return 4; } const char* knobId(int i) const override { static const char* k[] = {"size", "decay", "damp", "early"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"SIZE", "DECAY", "DAMP", "EARLY"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.4, 0.3, 0.5, 0.5}; return d[i]; } };

// A plug-in with many controls (more than Simple's six): its controls are listed once as {id, title, default}.
class Multi : public Effect {
 public:
  struct Def { const char* id; const char* title; double def; };
  Multi(double fs, const Def* d, int n) : fs_(fs > 0 ? fs : 48000), d_(d), n_(n < 32 ? n : 32) { for (int i = 0; i < n_; ++i) k_[i] = d_[i].def; }
  std::vector<Param> params() const override { std::vector<Param> v; for (int i = 0; i < n_; ++i) v.push_back({d_[i].id, d_[i].title, "", k_[i].load(), d_[i].def}); return v; }
  void set(const std::string& id, double v) override { for (int i = 0; i < n_; ++i) if (id == d_[i].id) { k_[i] = v < 0 ? 0 : v > 1 ? 1 : v; dirty_ = true; } }
  void setSampleRate(double fs) override { fs_ = fs > 0 ? fs : 48000; dirty_ = true; reset(); }
 protected:
  int index(const std::string& id) const { for (int i = 0; i < n_; ++i) if (id == d_[i].id) return i; return -1; }
  double fs_; const Def* d_; int n_; std::atomic<double> k_[32]; std::atomic<bool> dirty_{true};
};
// One RBJ biquad (transposed direct form II), one channel.
struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  float run(float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return static_cast<float>(y); }
  void clear() { z1 = z2 = 0; }
  enum Kind { Peak, LowShelf, HighShelf, HighPass, LowPass, BandPass };
  void design(Kind k, double fs, double f, double q, double db); };

// MS COLOUR EQ: a parallel saturating mid-side equalizer in the manner of a mastering rack. The signal is split into MID (what both
// sides share) and SIDE (what differs); four bands (LOW shelf, CONTOUR and MID bells with BANDWIDTH, HIGH shelf) each have a
// M/S OFFSET that sends the band to the side only (-), both (centre) or the mid only (+). HPF / LPF trim the ends. SATURATION runs in
// parallel: DRIVE sets how hard a band around FOCUS is pushed through a valve-like curve before it is blended back; XFORMER adds the
// soft asymmetric colour of an output transformer to the whole signal. SOLO (OFF, MID, SIDE, DELTA = only what the unit changed),
// MONO (the side below this frequency is made mono, OFF .. 300 Hz) and WIDTH (0 .. 200 %). MOVE: an LFO (RATE, DEPTH) that breathes the width.
class MsEq final : public Multi { public: explicit MsEq(double fs); const char* kind() const override { return "mseq"; } const char* name() const override { return "MS COLOUR EQ"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  static double offsetMid(double v) { const double o = v * 2 - 1; return o < 0 ? 1 + o : 1; } static double offsetSide(double v) { const double o = v * 2 - 1; return o > 0 ? 1 - o : 1; }
 private: void design(); Biquad m_[6], s_[6], hp_[2], lp_[2], satM_, satS_, mono_; double ph_ = 0; float dcX_[2] = {0, 0}, dcY_[2] = {0, 0}; };
// ISO EQ 10: a ten-band graphic equalizer on the ISO octave centres (31.5 Hz .. 16 kHz), +-12 dB per band. Q: CONSTANT (every band one
// octave wide) or PROPORTIONAL (narrow at small boosts, wider as the slider moves, like a console graphic). COLOUR adds the gentle
// saturation of an analog output stage; OUTPUT -12 .. +12 dB.
class IsoEq final : public Multi { public: explicit IsoEq(double fs); const char* kind() const override { return "isoeq"; } const char* name() const override { return "ISO EQ 10"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  static constexpr double kFreq[10] = {31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
  static double gainDb(double v) { return (v - 0.5) * 24.0; }
 private: void design(); Biquad f_[2][10]; };
// MOD MACHINE: one modulation unit with five modes: CHORUS, FLANGER, PHASER, TREMOLO and VIBRATO, on a bucket-brigade style delay
// (the wet path is darkened by TONE and softly saturated by COLOUR like an analog delay chip). RATE 0.05 .. 10 Hz, DEPTH, FEEDBACK
// (flanger and phaser), SPREAD (the right side's LFO 0 .. 180 degrees behind the left: wide stereo motion).
class ModMachine final : public Multi { public: explicit ModMachine(double fs); const char* kind() const override { return "mod"; } const char* name() const override { return "MOD MACHINE"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  static int modeOf(double v) { const int m = static_cast<int>((v < 0 ? 0 : v > 1 ? 1 : v) * 4.999); return m; }
 private: std::vector<float> buf_[2]; std::size_t w_ = 0; double ph_ = 0; float fb_[2] = {0, 0}, lp_[2] = {0, 0}, ap_[2][6] = {}; };

// OPTO LEVELER: a leveling amplifier after the optical kind: PEAK REDUCTION sets how hard it works, GAIN makes up the level, MODE
// COMPRESS (about 3:1) or LIMIT (about 10:1), EMPHASIS lets the highs push the detector harder. The release follows the programme:
// quick after a short peak, slow (seconds) after a long passage, like a light cell that has to cool.
class Opto final : public Multi { public: explicit Opto(double fs); const char* kind() const override { return "opto"; } const char* name() const override { return "OPTO LEVELER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override; double grDb() const { return gr_.load(); }
 private: double env_ = 0, fast_ = 0, slow_ = 0, lp_ = 0; std::atomic<double> gr_{0}; };
// GLUE COMP: a stereo bus compressor with the soft knee and second-harmonic colour of a valve gain stage: THRESHOLD, RATIO (1.2 .. 10),
// ATTACK 0.1 .. 30 ms, RELEASE 50 ms .. 1.2 s, MAKEUP 0 .. 18 dB, COLOUR. Both sides share one detector, so the image does not move.
class Glue final : public Multi { public: explicit Glue(double fs); const char* kind() const override { return "glue"; } const char* name() const override { return "GLUE COMP"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override; double grDb() const { return gr_.load(); }
 private: double env_ = 0; std::atomic<double> gr_{0}; };
// TAPE MACHINE: a tape recorder's sound without the tape: DRIVE into a soft magnetic curve, SPEED 7.5 / 15 / 30 ips (the head bump and
// how early the highs roll off move with it), BIAS (dark .. bright), WOW & FLUTTER (a slow and a fast pitch wander), OUTPUT.
class Tape final : public Multi { public: explicit Tape(double fs); const char* kind() const override { return "tape"; } const char* name() const override { return "TAPE MACHINE"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  static int speedOf(double v) { return static_cast<int>((v < 0 ? 0 : v > 1 ? 1 : v) * 2.999); }
 private: void design(); Biquad bump_[2], roll_[2], bias_[2]; std::vector<float> buf_[2]; std::size_t w_ = 0; double ph1_ = 0, ph2_ = 0, dcx_[2] = {0, 0}, dcy_[2] = {0, 0}; };
// TRANSIENT SHAPER: ATTACK (-100 .. +100 %) makes the start of each note harder or softer, SUSTAIN (-100 .. +100 %) the ring after it, OUTPUT.
// It follows the difference between a fast and a slow envelope, so it works at any level (no threshold).
class Transient final : public Multi { public: explicit Transient(double fs); const char* kind() const override { return "transient"; } const char* name() const override { return "TRANSIENT SHAPER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private: double fast_ = 0, slow_ = 0, g_ = 1; };
// DE-ESSER: turns down only the highs above FREQUENCY (3 .. 12 kHz) when they pass THRESHOLD, by up to RANGE dB; LISTEN plays only what is taken away.
class DeEsser final : public Multi { public: explicit DeEsser(double fs); const char* kind() const override { return "deesser"; } const char* name() const override { return "DE-ESSER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override; double grDb() const { return gr_.load(); }
 private: void design(); Biquad hp_[2], det_; double env_ = 0; std::atomic<double> gr_{0}; };

// MAXIMIZER: a look-ahead brick-wall limiter: THRESHOLD (0 .. -12 dB: how much louder it makes the music), CEILING (-3 .. 0 dBFS, the
// highest sample it lets out), RELEASE 5 .. 500 ms. It sees 1.5 ms ahead, so the gain is already down when a peak arrives (no overshoot).
class Maximizer final : public Multi { public: explicit Maximizer(double fs); const char* kind() const override { return "maximizer"; } const char* name() const override { return "MAXIMIZER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void setSampleRate(double fs) override; void process(float* L, float* R, std::size_t n) override; double grDb() const { return gr_.load(); }
 private: std::vector<float> buf_[2]; std::vector<float> need_; std::size_t w_ = 0; double g_ = 1; std::atomic<double> gr_{0}; };
// MULTIBAND COMP: three bands (LOW / MID / HIGH) split at LOW X and HIGH X with an exact (complementary) split; each band has its own
// AMOUNT (how far its threshold comes down), all share SPEED; OUTPUT. Untouched it adds back to the original exactly.
class Multiband final : public Multi { public: explicit Multiband(double fs); const char* kind() const override { return "multiband"; } const char* name() const override { return "MULTIBAND COMP"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private: void design(); Biquad lo_[2][2], hi_[2][2]; double env_[3] = {0, 0, 0}; };
// STEREO IMAGER: width below and above the CROSSOVER set apart (LOW WIDTH, HIGH WIDTH 0 .. 200 %): keep the bass centred, open the top.
class Imager final : public Multi { public: explicit Imager(double fs); const char* kind() const override { return "imager"; } const char* name() const override { return "STEREO IMAGER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private: void design(); Biquad lp_; };
// AUTO PAN: moves the sound between the speakers: RATE 0.05 .. 10 Hz, DEPTH, SHAPE (sine .. triangle .. square). Equal-power.
class AutoPan final : public Multi { public: explicit AutoPan(double fs); const char* kind() const override { return "autopan"; } const char* name() const override { return "AUTO PAN"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private: double ph_ = 0; };
// VOCAL DOUBLER: two slightly retuned, slightly late copies of the voice, one each side, as if it were sung twice:
// DETUNE 0 .. 25 cents, DELAY 8 .. 40 ms, WIDTH, TONE (darker copies sit behind the lead).
class Doubler final : public Multi { public: explicit Doubler(double fs); const char* kind() const override { return "doubler"; } const char* name() const override { return "VOCAL DOUBLER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private: std::vector<float> buf_; std::size_t w_ = 0; double ph_[2] = {0, 0}; float lp_[2] = {0, 0}; };

// The two ears of a listener, for placing a sound around the head (IAMF ELEMENT / IAMF RENDERER). A model, not measured HRTFs:
// the time difference between the ears (Woodworth's sphere, head radius 8.75 cm), the head's shadow on the far ear (Brown and Duda's
// one-pole / one-zero shelf), a pinna notch that moves with elevation and the dulling of sounds from behind.
struct Ears {
  void setSampleRate(double fs);
  void reset();
  void place(double azDeg, double elDeg);              // control rate: works out the delays and filters for a direction
  void run(float x, float& l, float& r);               // one mono sample in, the two ears out
 private:
  struct Side { double b0 = 1, b1 = 0, a1 = 0, z = 0; double delay = 0; Biquad notch, back; };
  double fs_ = 48000; std::vector<float> buf_; std::size_t w_ = 0; Side e_[2];
};
// IAMF ELEMENT: places a channel in 3D, after the audio elements of IAMF (the open immersive format of the Eclipsa plug-ins):
// AZIMUTH -180 .. 180 degrees (0 in front, + to the right), ELEVATION -90 .. 90, DISTANCE 0.5 .. 20 m (level and air), SPREAD (how much
// of the original stereo stays around the point), RENDER: BINAURAL (headphones, through the ear model) or SPEAKERS (an equal-power pan).
class IamfElement final : public Multi { public: explicit IamfElement(double fs); const char* kind() const override { return "iamfelement"; } const char* name() const override { return "IAMF ELEMENT"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void setSampleRate(double fs) override; void process(float* L, float* R, std::size_t n) override;
  static double azOf(double v) { return (v - 0.5) * 360; } static double elOf(double v) { return (v - 0.5) * 180; } static double distOf(double v) { return 0.5 * std::pow(40.0, v); }
 private: Ears ears_; float air_[2] = {0, 0}; double placedAz_ = 1e9, placedEl_ = 1e9; };
// IAMF RENDERER: the monitoring end of the chain (put it on the master): LAYOUT STEREO (as is) or BINAURAL (the two speakers heard
// as two virtual speakers at -30 / +30 degrees through the ear model, for headphones), TARGET loudness -27 .. -14 LKFS and NORMALIZE (a
// slow gain, at most 12 dB either way, that brings the programme to TARGET; IAMF carries loudness so players can do the same), OUTPUT.
class IamfRenderer final : public Multi { public: explicit IamfRenderer(double fs); const char* kind() const override { return "iamfrender"; } const char* name() const override { return "IAMF RENDERER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void setSampleRate(double fs) override; void process(float* L, float* R, std::size_t n) override;
  double loudness() const { return lufs_.load(); }   // short-term loudness (3 s, K-weighted), LKFS
  static double targetOf(double v) { return -27 + v * 13; }
 private: void design(); Ears left_, right_; Biquad kShelf_[2], kHp_[2]; double ms_ = 0, g_ = 1; std::atomic<double> lufs_{-70}; };

// W-SHAPE TUNER: the "W" signature of a tuned speaker or in-ear monitor, made smooth: three gentle rises (BASS shelf at 90 Hz, PRESENCE
// bell at 1.8 kHz, AIR shelf at 10 kHz) with two soft valleys between them (LOW-MID DIP at 350 Hz, which clears the mud, and UPPER DIP
// at 5 kHz, which takes the edge off harshness). AMOUNT scales the whole curve, SMOOTH widens every band (wide = smooth, no peaky
// resonances). The level is kept the same when the curve changes (automatic loudness compensation on a pink spectrum), so a louder W
// never just sounds "better" because it is louder; OUTPUT trims after that.
class WShape final : public Multi { public: explicit WShape(double fs); const char* kind() const override { return "wshape"; } const char* name() const override { return "W-SHAPE TUNER"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  double compensationDb() const { return comp_.load(); }
  struct Band { Biquad::Kind kind; double f, q, db; };
  static void bands(const double* k, Band out[5]);     // the five bands for the knobs k[0..6]
 private: void design(); Biquad f_[2][5]; std::atomic<double> comp_{0}; };

// CONVERTER CLIP: the last stage before the file: the colour of an analog-to-digital and a digital-to-analog converter around a soft
// clipper. ADC and DAC (on / off) each add the faint harmonics and the gentle top-end band limit of a converter's analog stage; INPUT and
// DRIVE push into the clipper (a soft knee that ends exactly at CEILING, -3 .. 0 dBFS, never over it); OVS runs the clipper at 1x / 2x / 4x the
// rate so its edges do not fold back as aliasing; DE-CORRELATE lets the two sides drift apart in phase a little (width without a widener);
// DRIFT (on / off) makes the two channels wander by a hair, like analog parts warming up; DITHER (OFF / TPDF / SHAPED) and BITS (16 / 20 / 24)
// for the final word length; OUTPUT; MONITOR: LR, M (mid), S (side) or Δ (only what the unit added or took away).
class ConvClip final : public Multi { public: explicit ConvClip(double fs); const char* kind() const override { return "convclip"; } const char* name() const override { return "CONVERTER CLIP"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void setSampleRate(double fs) override; void process(float* L, float* R, std::size_t n) override;
  double grDb() const { return gr_.load(); }
  static int ovsOf(double v) { const int k = static_cast<int>((v < 0 ? 0 : v > 1 ? 1 : v) * 2.999); return 1 << k; }
  static int bitsOf(double v) { static const int b[] = {16, 20, 24}; return b[static_cast<int>((v < 0 ? 0 : v > 1 ? 1 : v) * 2.999)]; }
  static int pickOf(double v, int n) { return static_cast<int>((v < 0 ? 0 : v > 1 ? 1 : v) * (n - 0.001)); }
 private: void design(); Biquad up_[2][2], down_[2][2], adc_[2], dac_[2]; double ap_[2] = {0, 0}, apIn_[2] = {0, 0}, drift_[2] = {1, 1}, driftT_[2] = {1, 1}, err_[2] = {0, 0};
  std::uint32_t rng_ = 0x9e3779b9u; int ovsDesigned_ = 0; std::atomic<double> gr_{0}; double rnd(); };


// ── THE I/O FRAME's processing objects (fx_frame.cpp) ──────────────────────────────────────────────────────────────────────────────────
// An adaptive echo canceller core: NLMS on the reference tap (the console's own output) with a bulk delay for the device's round trip,
// a Geigel double-talk detector (adaptation freezes while the near end talks) and a residual echo suppressor.
struct Nlms {
  std::vector<float> w, x; std::size_t pos = 0; double px = 0; int taps = 0;
  void resize(int n) { taps = n; w.assign(static_cast<std::size_t>(n), 0.f); x.assign(static_cast<std::size_t>(2 * n), 0.f); pos = 0; px = 0; }
  float run(float ref, float mic, double mu, bool adapt);   // returns the error (the mic with the echo estimate taken out)
};
// AEC: for a microphone in a room with loudspeakers (conference, church, video call): takes out what the speakers put back into the mic.
// BULK DELAY (0 .. 250 ms: the sound card's round trip and the distance), TAIL (16 .. 85 ms of room echo), ADAPT (speed), SUPPRESS (the
// residual echo, 0 .. 30 dB), MIX (0 = off .. 1 = cancelled). Mono: the channel's left and right are summed, the result goes to both.
class EchoCanceller final : public Simple { public: explicit EchoCanceller(double fs); const char* kind() const override { return "aec"; } const char* name() const override { return "ECHO CANCELLER (AEC)"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
  double erleDb() const { return erle_.load(); } bool doubleTalk() const { return dt_.load(); }
 protected: int count() const override { return 5; } const char* knobId(int i) const override; const char* knobTitle(int i) const override; double knobDef(int i) const override;
 private: Nlms f_; int built_ = 0; double eD_ = 1e-9, eE_ = 1e-9, farPk_ = 0, nlpG_ = 1; long farHold_ = 0; std::atomic<double> erle_{0}; std::atomic<bool> dt_{false}; };
// TELEPHONE HYBRID: the caller's channel of a phone-in (a phone interface, a USB phone line, a call app on a loopback): the caller hears the
// console's output (the mix minus their own channel), and what they hear leaks back down the line; this takes it out (line echo canceller),
// band-limits the caller (NARROW 300 .. 3400 Hz telephone, WIDE 50 .. 7000 Hz HD voice), levels them (AGC) and sets their level.
class PhoneHybrid final : public Simple { public: explicit PhoneHybrid(double fs); const char* kind() const override { return "hybrid"; } const char* name() const override { return "TELEPHONE HYBRID"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 5; } const char* knobId(int i) const override; const char* knobTitle(int i) const override; double knobDef(int i) const override;
 private: Nlms f_; int built_ = 0; double hp_[2] = {0, 0}, lp_[4] = {0, 0, 0, 0}, env_ = 0, agc_ = 1; };
// LOGIC OBJECT: ties a channel to the frame's GPIO. SIGNAL PRESENT lights a logic output while the channel is above the threshold (with a
// hold); MUTE ON INPUT / OPEN ON INPUT / DUCK ON INPUT let a control input (a foot switch, a door contact, OSC, MIDI) mute, open or duck it.
class LogicObject final : public Simple { public: explicit LogicObject(double fs); const char* kind() const override { return "logic"; } const char* name() const override { return "LOGIC / GPIO"; }
  std::string text(const std::string& id, double v) const override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 protected: int count() const override { return 5; } const char* knobId(int i) const override; const char* knobTitle(int i) const override; double knobDef(int i) const override;
 private: double env_ = 0, g_ = 1; long hold_ = 0; };
}  // namespace am::fx
