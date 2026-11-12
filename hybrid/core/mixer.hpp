// The sixteen-strip stereo mixer. Runs on the audio thread: no locks, no allocation, no system calls.
// Strip sources: the first `hwStrips` strips take the audio device's input pairs, every other strip
// reads its own ring, which a capture thread fills.
#pragma once
#include <memory>
#include <vector>
#include "meter.hpp"
#include "spsc_ring.hpp"

namespace amfb {

class HybridMixer {
 public:
  static constexpr std::size_t kChunk = 1024;   // frames worked on at once

  HybridMixer(SharedBlock* shared, double rate, int hwStrips) : sh_(shared), rate_(rate), hwStrips_(std::clamp(hwStrips, 0, kStrips)) {
    const std::size_t ringFrames = std::size_t(rate * 0.5);              // half a second per source
    for (int i = 0; i < kStrips; ++i) {
      rings_.push_back(std::make_unique<SpscRing<float>>(ringFrames * 2));
      st_[i].meter.setRate(rate);
    }
    master_.setRate(rate);
    target_ = std::size_t(rate * 0.02) * 2;                              // 20 ms kept in a ring
    sh_->sampleRate = float(rate);
    gen_ = sh_->paramGen.load() - 1;                                     // read the parameters on the first block
  }

  SpscRing<float>& ring(int strip) { return *rings_[std::size_t(strip)]; }   // interleaved stereo float
  int hwStrips() const { return hwStrips_; }
  double rate() const { return rate_; }

  void process(const float* const* in, int nIn, float* const* out, int nOut, std::size_t frames) noexcept {
    for (std::size_t done = 0; done < frames;) {
      const std::size_t n = std::min(kChunk, frames - done);
      chunk(in, nIn, out, nOut, done, n);
      done += n;
    }
    sh_->heartbeat.fetch_add(frames, std::memory_order_relaxed);
  }

 private:
  struct Strip {
    Eq7 eq; StereoMeter meter; Compressor comp;
    double gl = 0, gr = 0, tgl = 0, tgr = 0, width = 1;
    bool eqOn = true, started = false;
  };

  void loadParams() noexcept {
    bool anySolo = false;
    for (auto& p : sh_->p) anySolo = anySolo || p.solo.load(std::memory_order_relaxed);
    for (int i = 0; i < kStrips; ++i) {
      auto& p = sh_->p[i]; auto& s = st_[i];
      float f[kBands], g[kBands], q[kBands];
      for (int k = 0; k < kBands; ++k) {
        f[k] = p.eqFreq[k].load(std::memory_order_relaxed);
        g[k] = std::clamp(p.eqGain[k].load(std::memory_order_relaxed), -18.f, 18.f);
        q[k] = p.eqQ[k].load(std::memory_order_relaxed);
      }
      s.eq.design(rate_, f, g, q);
      s.eqOn = p.eqOn.load(std::memory_order_relaxed) != 0;
      s.comp.set(rate_, p.compLevel.load(std::memory_order_relaxed));
      s.width = std::clamp(double(p.width.load(std::memory_order_relaxed)), 0.0, 2.0);
      const bool on = !p.mute.load(std::memory_order_relaxed) && (!anySolo || p.solo.load(std::memory_order_relaxed));
      const double g0 = on ? dbToLin(std::clamp(double(p.gainDb.load(std::memory_order_relaxed)), -90.0, 12.0)) : 0.0;
      double bl, br; balanceGains(p.pan.load(std::memory_order_relaxed), bl, br);
      s.tgl = g0 * bl; s.tgr = g0 * br;
    }
    tMaster_ = dbToLin(std::clamp(double(sh_->masterGainDb.load(std::memory_order_relaxed)), -90.0, 12.0));
  }

  void chunk(const float* const* in, int nIn, float* const* out, int nOut, std::size_t off, std::size_t n) noexcept {
    const std::uint32_t gen = sh_->paramGen.load(std::memory_order_acquire);
    if (gen != gen_) { gen_ = gen; loadParams(); }
    for (std::size_t i = 0; i < n; ++i) busL_[i] = busR_[i] = 0;

    for (int s = 0; s < kStrips; ++s) {
      Strip& st = st_[s];
      bool live = false;
      if (s < hwStrips_) {                                               // an input pair of the audio device
        const int a = 2 * s, b = a + 1 < nIn ? a + 1 : a;
        live = a < nIn;
        for (std::size_t i = 0; i < n; ++i) { xl_[i] = live ? in[a][off + i] : 0.0; xr_[i] = live ? in[b][off + i] : 0.0; }
      } else {                                                           // a captured program
        SpscRing<float>& r = *rings_[std::size_t(s)];
        const std::size_t have = r.available();
        if (!st.started) st.started = have >= target_;                   // wait for the cushion before the first read
        if (st.started && have > target_ * 3 + n * 2) r.discard(((have - target_) / 2) * 2);   // the source runs fast: back to the cushion
        std::size_t got = st.started ? r.read(tmp_, n * 2) / 2 : 0;
        if (st.started && got < n) { st.started = false; if (got) sh_->underruns.fetch_add(1, std::memory_order_relaxed); }
        live = got > 0;
        for (std::size_t i = 0; i < n; ++i) { xl_[i] = i < got ? tmp_[2 * i] : 0.0; xr_[i] = i < got ? tmp_[2 * i + 1] : 0.0; }
      }
      sh_->m[s].active.store(live ? 1u : 0u, std::memory_order_relaxed);

      const double dl = (st.tgl - st.gl) / double(n), dr = (st.tgr - st.gr) / double(n);   // gains glide over the block
      const bool eq = st.eqOn && !st.eq.flat, ms = st.width != 1.0;
      for (std::size_t i = 0; i < n; ++i) {
        double l = xl_[i], r = xr_[i];
        if (eq) st.eq.run(l, r);
        if (ms) midSide(l, r, st.width);
        st.comp.run(l, r);
        st.gl += dl; st.gr += dr;
        l *= st.gl; r *= st.gr;
        st.meter.feed(l, r);
        busL_[i] += l; busR_[i] += r;
      }
      st.gl = st.tgl; st.gr = st.tgr;
      st.meter.publish(sh_->m[s], n);
      sh_->m[s].reductionDb.store(float(st.comp.reductionDb), std::memory_order_relaxed);
    }

    const double dm = (tMaster_ - gMaster_) / double(n);
    for (std::size_t i = 0; i < n; ++i) {
      gMaster_ += dm;
      const double l = busL_[i] * gMaster_, r = busR_[i] * gMaster_;
      master_.feed(l, r);
      if (nOut > 0) out[0][off + i] = float(l);
      if (nOut > 1) out[1][off + i] = float(r);
    }
    gMaster_ = tMaster_;
    for (int c = 2; c < nOut; ++c) for (std::size_t i = 0; i < n; ++i) out[c][off + i] = 0.f;
    master_.publish(sh_->m[kStrips], n);
  }

  SharedBlock* sh_; double rate_; int hwStrips_;
  std::vector<std::unique_ptr<SpscRing<float>>> rings_;
  Strip st_[kStrips]; StereoMeter master_;
  double gMaster_ = 0, tMaster_ = 1;
  std::uint32_t gen_ = 0; std::size_t target_ = 0;
  double xl_[kChunk], xr_[kChunk], busL_[kChunk], busR_[kChunk];
  float tmp_[kChunk * 2];
};

}  // namespace amfb
