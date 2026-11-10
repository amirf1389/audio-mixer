// Built-in bus effects for the EFFECTS page, ported from the web console's rack (plate reverb, tape delay, stereo chorus, tube drive):
// each is a BusProcessor that runs in place on a bus before its return into the master and its output. Parameters are 0..1 (normalized)
// with a plain-value text, like a plug-in's, so the same generic controls serve them. All state is per instance, no allocation in process().
#pragma once
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
  virtual void set(const std::string& id, double v) = 0;   // UI thread; picked up by the audio thread without locks (plain doubles)
  virtual std::string text(const std::string& id, double v) const = 0;
  virtual void setSampleRate(double fs) = 0;
  virtual void reset() = 0;                        // clears delay lines
  void setBypass(bool b) { bypass_ = b; }
  bool bypass() const { return bypass_; }
  void setMix(double wet) { mix_ = wet < 0 ? 0 : wet > 1 ? 1 : wet; }   // 1 = fully wet (a send effect), 0 = dry
  double mix() const { return mix_; }
 protected:
  volatile bool bypass_ = false; volatile double mix_ = 1.0;
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
  void design(); double fs_; double decay_ = 0.35, damp_ = 0.5, pre_ = 0.1, width_ = 1.0;
  struct Comb { std::vector<float> buf; std::size_t i = 0; float fb = 0, store = 0; };
  Comb comb_[2][4]; std::vector<float> ap_[2][2]; std::size_t api_[2][2] = {{0, 0}, {0, 0}}; std::vector<float> pre_buf_[2]; std::size_t prei_ = 0;
  volatile float fbGain_ = 0.8f, dampA_ = 0.5f; volatile std::size_t preSamples_ = 0;
};
// Tape delay: TIME 20..2000 ms, FEEDBACK, TONE (one-pole low-pass in the loop), PING-PONG
class TapeDelay final : public Effect {
 public:
  explicit TapeDelay(double fs);
  const char* kind() const override { return "delay"; } const char* name() const override { return "TAPE DELAY"; }
  std::vector<Param> params() const override; void set(const std::string& id, double v) override; std::string text(const std::string& id, double v) const override;
  void setSampleRate(double fs) override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private:
  double fs_; double time_ = 0.3, fb_ = 0.4, tone_ = 0.5, pingpong_ = 0; std::vector<float> buf_[2]; std::size_t w_ = 0; float lp_[2] = {0, 0};
};
// Stereo chorus: RATE 0.1..5 Hz, DEPTH, SPREAD (LFO phase offset R)
class Chorus final : public Effect {
 public:
  explicit Chorus(double fs);
  const char* kind() const override { return "chorus"; } const char* name() const override { return "STEREO CHORUS"; }
  std::vector<Param> params() const override; void set(const std::string& id, double v) override; std::string text(const std::string& id, double v) const override;
  void setSampleRate(double fs) override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private:
  double fs_; double rate_ = 0.25, depth_ = 0.5, spread_ = 0.5; std::vector<float> buf_[2]; std::size_t w_ = 0; double phase_ = 0;
};
// Tube drive: DRIVE, TONE (post low-pass), OUTPUT; a tanh waveshaper with 2x oversampling by linear interpolation
class Drive final : public Effect {
 public:
  explicit Drive(double fs);
  const char* kind() const override { return "drive"; } const char* name() const override { return "TUBE DRIVE"; }
  std::vector<Param> params() const override; void set(const std::string& id, double v) override; std::string text(const std::string& id, double v) const override;
  void setSampleRate(double fs) override; void reset() override; void process(float* L, float* R, std::size_t n) override;
 private:
  double fs_; double drive_ = 0.4, tone_ = 0.6, out_ = 0.7; float lp_[2] = {0, 0}, last_[2] = {0, 0};
};

}  // namespace am::fx
