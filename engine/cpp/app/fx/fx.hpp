// Built-in bus effects for the EFFECTS page, ported from the web console's rack (plate reverb, tape delay, stereo chorus, tube drive):
// each is a BusProcessor that runs in place on a bus before its return into the master and its output. Parameters are 0..1 (normalized)
// with a plain-value text, like a plug-in's, so the same generic controls serve them. All state is per instance, no allocation in process().
#pragma once
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

// The simple modulation / colour effects share one shape: up to three knobs (0..1) and a per-channel state.
class Simple : public Effect {
 public:
  explicit Simple(double fs) : fs_(fs > 0 ? fs : 48000) {}
  std::vector<Param> params() const override { std::vector<Param> v; for (int i = 0; i < count(); ++i) v.push_back({knobId(i), knobTitle(i), "", k_[i].load(), knobDef(i)}); return v; }
  void set(const std::string& id, double v) override { for (int i = 0; i < count(); ++i) if (id == knobId(i)) k_[i] = v < 0 ? 0 : v > 1 ? 1 : v; }
  void setSampleRate(double fs) override { fs_ = fs > 0 ? fs : 48000; reset(); }
 protected:
  virtual int count() const = 0; virtual const char* knobId(int i) const = 0; virtual const char* knobTitle(int i) const = 0; virtual double knobDef(int i) const = 0;
  double fs_; std::atomic<double> k_[3]{{0.5}, {0.5}, {0.5}};
};
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

}  // namespace am::fx
