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
  double fs_; std::atomic<double> k_[6]{{0.5}, {0.5}, {0.5}, {0.5}, {0.5}, {0.5}};
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
  double notchHz(int i) const { return i >= 0 && i < kNotches && n_[i].on ? n_[i].hz : 0.0; }
 protected: int count() const override { return 4; } const char* knobId(int i) const override { static const char* k[] = {"sens", "depth", "width", "mode"}; return k[i]; } const char* knobTitle(int i) const override { static const char* k[] = {"SENSITIVITY", "DEPTH", "WIDTH", "MODE"}; return k[i]; } double knobDef(int i) const override { static const double d[] = {0.5, 0.5, 0.5, 0.0}; return d[i]; }
 private:
  static constexpr int kNotches = 6, kN = 1024;
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
 private: struct Buf { std::vector<float> L, R; int id = 0; std::string file; }; void load(); std::shared_ptr<const Buf> buf_; std::size_t pos_ = 0; bool wasOn_ = false; std::atomic<bool> playing_{false}; int loaded_ = 0; };
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

}  // namespace am::fx
