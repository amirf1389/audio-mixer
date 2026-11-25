// THE PAD PLAYER: plays the 100 built-in samples, the 1000 sound effects and the 1000 custom pads into the master, as a Runtime::ExtraSource.
// The same player in the console (SAMPLES page) and in the remote service (phones linked), so a pad sounds the same from either.
//   CONTROL THREAD (UI / HTTP): load() renders or decodes a sound once at the device's rate and keeps it; trigger() queues it.
//   AUDIO THREAD: render() starts queued sounds on free voices (16, the oldest stolen) and mixes them; no locks, no allocation.
// Only sounds that were played are kept (the 1000 effects are never all in memory). Over kBudgetBytes the least recently played sounds
// that no voice is playing are let go; a sound let go (or replaced, or a rate change) is freed only after the audio thread has moved on
// two blocks and no voice points at it, so a block in progress never reads freed memory.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "samples/padbank.hpp"
#include "samples/samples.hpp"
#include "samples/sfx.hpp"

namespace am::pads {

class Player final : public Runtime::ExtraSource {
 public:
  struct Clip { std::vector<float> L, R; std::string source; std::size_t bytes() const { return (L.size() + R.size()) * sizeof(float); } };
  static constexpr int kVoices = 16, kSamples = 100, kSlots = kSamples + sfx::kCount + kPads;   // slot: sample n-1 | 100 + sfx n-1 | 1100 + pad n-1 (files)
  static constexpr std::size_t kBudgetBytes = 192u << 20;

  std::atomic<double> gainDb{-6.0};
  std::atomic<float> peak{0.f};
  std::atomic<int> active{0};

  static int slotOf(const std::string& source, int padN) {
    if (source.rfind("sample:", 0) == 0) { const int n = std::atoi(source.c_str() + 7); return n >= 1 && n <= kSamples ? n - 1 : -1; }
    if (source.rfind("sfx:", 0) == 0) { const int n = std::atoi(source.c_str() + 4); return n >= 1 && n <= sfx::kCount ? kSamples + n - 1 : -1; }
    if (source.rfind("file:", 0) == 0 && padN >= 1 && padN <= kPads) return kSamples + sfx::kCount + padN - 1;
    return -1;
  }

  /** the device's rate (control thread): a change lets every kept sound go and renders the 100 built-in samples again */
  void prepare(double rate) { if (!setRate(rate)) return; install(rate, renderBuiltIns(rate)); }
  /** the rate alone (control thread): every kept sound let go; a sound played before the built-ins are installed is rendered on demand */
  bool setRate(double rate) { if (rate <= 0 || rate == rate_) return false; stopAll(); for (int s = 0; s < kSlots; ++s) retire(s); rate_ = rate; return true; }
  /** the 100 built-in samples rendered at [rate] (ANY thread: touches no player state), for install() on the control thread; seconds of
   *  work at high rates, so the console runs it off the interface thread */
  static std::vector<std::unique_ptr<Clip>> renderBuiltIns(double rate) {
    std::vector<std::unique_ptr<Clip>> v; v.reserve(kSamples);
    for (int n = 1; n <= kSamples; ++n) { auto c = std::make_unique<Clip>(); std::string e; c->source = "sample:" + std::to_string(n); if (loadClip(c->source, rate, c->L, c->R, e)) v.push_back(std::move(c)); }
    return v; }
  /** keep the rendered built-ins (control thread); ignored when the rate changed meanwhile, a slot already loaded keeps its sound */
  void install(double rate, std::vector<std::unique_ptr<Clip>>&& clips) {
    if (rate != rate_) return; collect();
    for (auto& c : clips) { const int s = slotOf(c->source, 0); if (s < 0) continue; Owned& o = owned_[static_cast<std::size_t>(s)]; if (o.clip && o.clip->source == c->source) continue;
      retire(s); bytes_ += c->bytes(); o.clip = std::move(c); o.used = ++tick_; slots_[static_cast<std::size_t>(s)].store(o.clip.get(), std::memory_order_release); } }
  double rate() const { return rate_; }
  bool ready() const { return rate_ > 0; }

  /** make sure [source] is loaded (control thread); returns its slot, -1 with err when it cannot be played */
  int load(const std::string& source, int padN, std::string& err) {
    collect();
    const int s = slotOf(source, padN); if (s < 0) { err = source.empty() ? "empty pad" : "not a pad source"; return -1; }
    if (rate_ <= 0) { err = "no sound card open"; return -1; }
    Owned& o = owned_[static_cast<std::size_t>(s)];
    if (o.clip && o.clip->source == source) { o.used = ++tick_; return s; }
    auto c = std::make_unique<Clip>(); if (!loadClip(source, rate_, c->L, c->R, err)) return -1;
    c->source = source; retire(s); bytes_ += c->bytes(); o.clip = std::move(c); o.used = ++tick_;
    slots_[static_cast<std::size_t>(s)].store(o.clip.get(), std::memory_order_release); trim(s); return s;
  }
  /** play a loaded slot at [padGainDb] on top of the player's gain (control thread); false when the queue is full */
  bool trigger(int slot, double padGainDb = 0) {
    if (slot < 0 || slot >= kSlots || !slots_[static_cast<std::size_t>(slot)].load()) return false;
    const unsigned w = w_.load(); if (w - r_.load() >= kRing) return false;
    ring_[w % kRing] = {slot, static_cast<float>(std::pow(10.0, std::clamp(padGainDb, -40.0, 12.0) / 20.0))}; w_.store(w + 1, std::memory_order_release); return true;
  }
  /** load + trigger in one: what a pad press does */
  bool play(const std::string& source, int padN, double padGainDb, std::string& err) { const int s = load(source, padN, err); if (s < 0) return false; if (!trigger(s, padGainDb)) { err = "too many pads at once"; return false; } return true; }
  void stopAll() { stop_.store(true); }
  std::size_t bytesKept() const { return bytes_; }
  int kept() const { int k = 0; for (const Owned& o : owned_) k += o.clip ? 1 : 0; return k; }

  void render(float* L, float* R, std::size_t frames, double) override {
    for (std::size_t i = 0; i < frames; ++i) { L[i] = 0.f; R[i] = 0.f; }
    if (stop_.exchange(false)) for (Voice& v : voices_) v.clip = nullptr;
    const unsigned w = w_.load(std::memory_order_acquire);
    while (r_.load() != w) { const Trig t = ring_[r_.load() % kRing]; r_.store(r_.load() + 1);
      const Clip* c = slots_[static_cast<std::size_t>(t.slot)].load(std::memory_order_acquire); if (!c) continue;
      int k = -1; for (int j = 0; j < kVoices; ++j) if (!voices_[j].clip) { k = j; break; }
      if (k < 0) k = static_cast<int>(next_++ % kVoices);   // steal round-robin
      voices_[k] = {c, 0, t.gain}; }
    const float g = static_cast<float>(std::pow(10.0, gainDb.load() / 20.0)); int act = 0; float pk = 0;
    for (int k = 0; k < kVoices; ++k) { Voice& v = voices_[k]; playing_[k].store(v.clip, std::memory_order_release); if (!v.clip) continue; ++act;
      const std::size_t n = v.clip->L.size(); const float gg = g * v.gain;
      for (std::size_t i = 0; i < frames && v.pos < n; ++i, ++v.pos) { L[i] += v.clip->L[v.pos] * gg; R[i] += v.clip->R[v.pos] * gg; }
      if (v.pos >= n) v.clip = nullptr; }
    for (std::size_t i = 0; i < frames; ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
    peak.store(pk); active.store(act); blocks_.fetch_add(1, std::memory_order_release);
  }

 private:
  struct Trig { int slot = 0; float gain = 1.f; };
  struct Voice { const Clip* clip = nullptr; std::size_t pos = 0; float gain = 1.f; };
  struct Owned { std::unique_ptr<Clip> clip; std::uint64_t used = 0; };
  struct Grave { std::unique_ptr<Clip> clip; std::uint64_t block; };

  void retire(int s) {
    Owned& o = owned_[static_cast<std::size_t>(s)]; if (!o.clip) return;
    slots_[static_cast<std::size_t>(s)].store(nullptr, std::memory_order_release); bytes_ -= o.clip->bytes();
    grave_.push_back({std::move(o.clip), blocks_.load(std::memory_order_acquire)}); o.used = 0;
  }
  // free what the audio thread can no longer reach: two blocks later (any queued trigger has turned into a voice) and on no voice
  void collect() {
    const std::uint64_t now = blocks_.load(std::memory_order_acquire);
    grave_.erase(std::remove_if(grave_.begin(), grave_.end(), [&](const Grave& g) {
      if (now < g.block + 2) return false;
      for (const auto& p : playing_) if (p.load(std::memory_order_acquire) == g.clip.get()) return false;
      return true; }), grave_.end());
  }
  // over budget: let go of the least recently played effects / files nobody is playing (the 100 built-in samples always stay)
  void trim(int keep) {
    while (bytes_ > kBudgetBytes) {
      int victim = -1; std::uint64_t oldest = ~0ull;
      for (int s = kSamples; s < kSlots; ++s) { const Owned& o = owned_[static_cast<std::size_t>(s)]; if (!o.clip || s == keep || o.used >= oldest) continue;
        bool busy = false; for (const auto& p : playing_) if (p.load() == o.clip.get()) { busy = true; break; } if (!busy) { oldest = o.used; victim = s; } }
      if (victim < 0) break; retire(victim);
    }
  }

  static constexpr unsigned kRing = 64;
  std::array<std::atomic<const Clip*>, kSlots> slots_{};
  std::array<Owned, kSlots> owned_{};
  std::vector<Grave> grave_;
  std::size_t bytes_ = 0; std::uint64_t tick_ = 0; double rate_ = 0;
  std::array<Trig, kRing> ring_{}; std::atomic<unsigned> w_{0}, r_{0}; std::atomic<bool> stop_{false};
  std::array<Voice, kVoices> voices_{}; std::array<std::atomic<const Clip*>, kVoices> playing_{}; unsigned next_ = 0;
  std::atomic<std::uint64_t> blocks_{0};
};

}  // namespace am::pads
