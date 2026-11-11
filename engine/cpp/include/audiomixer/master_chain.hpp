// MASTER CHAIN: what sits on the stereo master before its width, fader and limiter. Two racks:
//   PROGRAM EQ  ten bands of its own (the RTA EQ page's rack; the strips keep their own EQ for the microphones)
//   MASTERING   after the EQ: low shelf (100 Hz) and air shelf (10 kHz), a stereo-linked glue compressor, soft saturation, output gain into
//               the master limiter (the "maximiser": the limiter's ceiling stays the true stop)
// set() is called from the interface thread, process() on the audio thread (a try_lock picks the new values up; it never waits).
#pragma once
#include <atomic>
#include <cmath>
#include <mutex>

#include "audiomixer/dsp.hpp"
#include "audiomixer/engine.hpp"

namespace am {
class MasterChain : public BusProcessor {
 public:
  static constexpr int kBands = 10;
  struct Params {
    double fs = 48000; bool eqOn = true; dsp::EqBand eq[kBands];
    bool on = false; double lowDb = 0, airDb = 0, thrDb = -12, ratio = 2, attackMs = 30, releaseMs = 200, makeupDb = 0, drive = 0, gainDb = 0;
  };
  void set(const Params& p) { { std::lock_guard<std::mutex> g(mx_); pending_ = p; } version_.fetch_add(1); }
  Params params() const { std::lock_guard<std::mutex> g(mx_); return pending_; }
  double reductionDb() const { return gr_.load(); }   // the compressor's gain reduction now (dB, >= 0)
  void process(float* L, float* R, std::size_t n) override {
    const unsigned v = version_.load();
    if (v != applied_ && mx_.try_lock()) { p_ = pending_; mx_.unlock(); applied_ = v; design(); }
    const bool eq = p_.eqOn && anyEq_;
    if (!eq && !p_.on) { gr_.store(0); return; }
    double grMax = 0;
    for (std::size_t i = 0; i < n; ++i) {
      double l = L[i], r = R[i];
      if (eq) for (int b = 0; b < kBands; ++b) { if (!bandOn_[b]) continue; l = eqL_[b].tick(l); r = eqR_[b].tick(r); }
      if (p_.on) {
        l = airL_.tick(lowL_.tick(l)); r = airR_.tick(lowR_.tick(r));
        const double pk = std::max(std::fabs(l), std::fabs(r)); env_ += (pk - env_) * (pk > env_ ? att_ : rel_);
        const double lvl = env_ > 1e-9 ? 20.0 * std::log10(env_) : -180.0, over = lvl - p_.thrDb, red = over > 0 ? over * (1.0 - 1.0 / p_.ratio) : 0.0; if (red > grMax) grMax = red;
        const double g = std::pow(10.0, (p_.makeupDb - red) / 20.0); l *= g; r *= g;
        if (k_ > 1.0) { l = std::tanh(k_ * l) * kNorm_; r = std::tanh(k_ * r) * kNorm_; }
        l *= out_; r *= out_;
      }
      L[i] = static_cast<float>(l); R[i] = static_cast<float>(r);
    }
    gr_.store(grMax);
  }
 private:
  void design() {
    anyEq_ = false;
    for (int b = 0; b < kBands; ++b) { const dsp::EqBand& e = p_.eq[b]; bandOn_[b] = e.type != dsp::FilterType::Off; anyEq_ = anyEq_ || bandOn_[b]; dsp::Biquad d; d.design(e.type, p_.fs, e.freq, e.q, e.gainDb);
      for (dsp::Biquad* q : {&eqL_[b], &eqR_[b]}) { q->b0 = d.b0; q->b1 = d.b1; q->b2 = d.b2; q->a1 = d.a1; q->a2 = d.a2; } }   // the state is kept: no click on a move
    dsp::Biquad lo, hi; lo.design(p_.lowDb != 0 ? dsp::FilterType::LowShelf : dsp::FilterType::Off, p_.fs, 100, 0.707, p_.lowDb); hi.design(p_.airDb != 0 ? dsp::FilterType::HighShelf : dsp::FilterType::Off, p_.fs, 10000, 0.707, p_.airDb);
    for (dsp::Biquad* q : {&lowL_, &lowR_}) { q->b0 = lo.b0; q->b1 = lo.b1; q->b2 = lo.b2; q->a1 = lo.a1; q->a2 = lo.a2; }
    for (dsp::Biquad* q : {&airL_, &airR_}) { q->b0 = hi.b0; q->b1 = hi.b1; q->b2 = hi.b2; q->a1 = hi.a1; q->a2 = hi.a2; }
    att_ = 1.0 - std::exp(-1.0 / (std::max(0.1, p_.attackMs) * 0.001 * p_.fs)); rel_ = 1.0 - std::exp(-1.0 / (std::max(5.0, p_.releaseMs) * 0.001 * p_.fs));
    k_ = 1.0 + 4.0 * std::max(0.0, std::min(1.0, p_.drive)); kNorm_ = 1.0 / std::tanh(k_); if (p_.drive <= 0) k_ = 1.0; out_ = std::pow(10.0, p_.gainDb / 20.0); if (p_.ratio < 1) p_.ratio = 1;
  }
  mutable std::mutex mx_; Params pending_, p_; std::atomic<unsigned> version_{0}; unsigned applied_ = 0; std::atomic<double> gr_{0};
  dsp::Biquad eqL_[kBands], eqR_[kBands], lowL_, lowR_, airL_, airR_; bool bandOn_[kBands] = {}, anyEq_ = false; double env_ = 0, att_ = 1, rel_ = 1, k_ = 1, kNorm_ = 1, out_ = 1;
};
}  // namespace am
