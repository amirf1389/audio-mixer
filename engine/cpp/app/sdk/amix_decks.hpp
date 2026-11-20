// THE STANDALONE PHONE MIXER'S SOURCES (used by android/amix-sdk/src/main/cpp/amix_jni.cpp; tested on the host by test/decks_test.cpp):
// two DECKS (a song decoded elsewhere, resampled here with r8brain to the mixer's rate, played on a pair of strips) and the MASTER
// RECORDER (the stereo master copied out for a writer thread). Each is a single-producer / single-consumer ring of stereo frames: the
// audio callback only reads or writes it, never allocates, never locks.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <thread>
#include <vector>

#include "amix_hires.h"

namespace amix_decks {
struct Ring {
  std::vector<float> buf; std::size_t cap = 0; std::atomic<std::size_t> w{0}, r{0};
  explicit Ring(std::size_t frames) : buf(2 * frames, 0.f), cap(frames) {}
  std::size_t readable() const { return w.load(std::memory_order_acquire) - r.load(std::memory_order_relaxed); }
  std::size_t writable() const { return cap - (w.load(std::memory_order_relaxed) - r.load(std::memory_order_acquire)); }
  std::size_t push(const float* lr, std::size_t n) {   // interleaved L R
    n = std::min(n, writable()); std::size_t at = w.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; ++i, ++at) { const std::size_t k = 2 * (at % cap); buf[k] = lr[2 * i]; buf[k + 1] = lr[2 * i + 1]; }
    w.store(at, std::memory_order_release); return n; }
  std::size_t pop(float* L, float* R, std::size_t n) {   // planar out
    n = std::min(n, readable()); std::size_t at = r.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; ++i, ++at) { const std::size_t k = 2 * (at % cap); L[i] = buf[k]; R[i] = buf[k + 1]; }
    r.store(at, std::memory_order_release); return n; }
  std::size_t popInter(float* lr, std::size_t n) {
    n = std::min(n, readable()); std::size_t at = r.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; ++i, ++at) { const std::size_t k = 2 * (at % cap); lr[2 * i] = buf[k]; lr[2 * i + 1] = buf[k + 1]; }
    r.store(at, std::memory_order_release); return n; }
};
struct Deck {
  Ring ring{65536};                         // about 1.4 s at 48 kHz
  std::atomic<int> stripL{-1};              // its left strip; the right one is stripL + 1; -1 = not routed
  std::atomic<bool> flushReq{false};
  std::atomic<unsigned long long> played{0};   // frames the mixer has taken (at the mixer's rate)
  // the feeding side (one Kotlin thread)
  amix_hr_resampler* rs = nullptr; double rsFrom = 0, rsTo = 0; std::vector<float> pending;   // resampled frames that did not fit yet (interleaved)
  ~Deck() { if (rs) amix_hr_resampler_destroy(rs); }
};
// MULTITRACK on the phone: every strip's own input (before its fader and effects), interleaved, for the writer thread to split into files
struct MultiRing {
  static constexpr int kCh = 16;                                // room for every strip of the phone mixer
  std::vector<float> buf; std::size_t cap = 0; std::atomic<std::size_t> w{0}, r{0};
  explicit MultiRing(std::size_t frames) : buf(static_cast<std::size_t>(kCh) * frames, 0.f), cap(frames) {}
  std::size_t readable() const { return w.load(std::memory_order_acquire) - r.load(std::memory_order_relaxed); }
  std::size_t writable() const { return cap - (w.load(std::memory_order_relaxed) - r.load(std::memory_order_acquire)); }
  std::size_t push(const float* const* ptrs, int strips, std::size_t n) {   // nothing allocated: called on the audio thread
    n = std::min(n, writable()); std::size_t at = w.load(std::memory_order_relaxed); const int c = std::min(strips, kCh);
    for (std::size_t i = 0; i < n; ++i, ++at) { float* d = buf.data() + static_cast<std::size_t>(kCh) * (at % cap); for (int s = 0; s < c; ++s) d[s] = ptrs[s] ? ptrs[s][i] : 0.f; for (int s = c; s < kCh; ++s) d[s] = 0.f; }
    w.store(at, std::memory_order_release); return n; }
  std::size_t pop(float* out, int strips, std::size_t n) {   // interleaved, `strips` channels a frame
    n = std::min(n, readable()); std::size_t at = r.load(std::memory_order_relaxed); const int c = std::min(strips, kCh);
    for (std::size_t i = 0; i < n; ++i, ++at) { const float* d = buf.data() + static_cast<std::size_t>(kCh) * (at % cap); for (int s = 0; s < c; ++s) out[i * static_cast<std::size_t>(c) + static_cast<std::size_t>(s)] = d[s]; }
    r.store(at, std::memory_order_release); return n; }
};
constexpr int kDecks = 4;                  // DECK A .. D: four stereo file players (a 10-channel phone mixer: 2 inputs + 4 stereo decks)
struct Side {
  Deck deck[kDecks];
  Ring rec{262144};                         // about 5 s of master for the writer thread
  std::atomic<bool> recording{false}; std::atomic<unsigned long long> recDropped{0};
  MultiRing multi{131072};                  // about 2.7 s of every strip at 48 kHz
  std::atomic<bool> multiOn{false}; std::atomic<unsigned long long> multiDropped{0};
  std::atomic<long long> lastRead{0};       // when a mixer block last ran (ns): nobody reading = a flush can be done at once
  std::vector<float> dL, dR;                // deck blocks for the Java path (the native loop has its own)
};
inline long long nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
// inside a mixer block: each routed deck's frames onto its strips (silence where it ran short), a requested flush done here
inline void readDecks(Side* sd, const float** ptrs, int strips, int frames, float* const* bufL, float* const* bufR) {
  sd->lastRead.store(nowNs(), std::memory_order_relaxed);
  for (int d = 0; d < kDecks; ++d) { Deck& k = sd->deck[d]; const int sl = k.stripL.load(std::memory_order_relaxed); if (sl < 0 || sl + 1 >= strips) continue;
    if (k.flushReq.load(std::memory_order_acquire)) { k.ring.r.store(k.ring.w.load(std::memory_order_acquire), std::memory_order_release); k.flushReq.store(false, std::memory_order_release); }
    const std::size_t got = k.ring.pop(bufL[d], bufR[d], static_cast<std::size_t>(frames));
    for (int i = static_cast<int>(got); i < frames; ++i) { bufL[d][i] = 0.f; bufR[d][i] = 0.f; }
    k.played.fetch_add(got, std::memory_order_relaxed);
    ptrs[sl] = bufL[d]; ptrs[sl + 1] = bufR[d]; }
}
inline void recordStrips(Side* sd, const float* const* ptrs, int strips, int frames) {
  if (!sd->multiOn.load(std::memory_order_relaxed) || frames <= 0) return;
  const std::size_t put = sd->multi.push(ptrs, strips, static_cast<std::size_t>(frames));
  if (put < static_cast<std::size_t>(frames)) sd->multiDropped.fetch_add(static_cast<unsigned long long>(frames) - put);
}
inline void recordMaster(Side* sd, const float* L, const float* R, int frames) {
  if (!sd->recording.load(std::memory_order_relaxed)) return;
  float tmp[512]; int done = 0;
  while (done < frames) { const int n = std::min(256, frames - done); for (int i = 0; i < n; ++i) { tmp[2 * i] = L[done + i]; tmp[2 * i + 1] = R[done + i]; }
    const std::size_t put = sd->rec.push(tmp, static_cast<std::size_t>(n)); if (put < static_cast<std::size_t>(n)) sd->recDropped.fetch_add(static_cast<unsigned long long>(n) - put); done += n; }
}

// the feeding side (one thread): `frames` interleaved stereo at srcRate (none: only move what waits), resampled to dstRate; returns the frames waiting
inline std::size_t deckPushFrames(Deck& k, const float* in, int frames, double srcRate, double dstRate) {
  if (in && frames > 0) {
    if (std::abs(srcRate - dstRate) < 0.5 || srcRate <= 0 || dstRate <= 0) k.pending.insert(k.pending.end(), in, in + 2 * static_cast<std::size_t>(frames));
    else {
      if (!k.rs || k.rsFrom != srcRate || k.rsTo != dstRate) { if (k.rs) amix_hr_resampler_destroy(k.rs); char e[128] = {0}; k.rs = amix_hr_resampler_create(srcRate, dstRate, 2, "r8brain", 1, e, sizeof e); k.rsFrom = srcRate; k.rsTo = dstRate; }
      if (k.rs) {
        std::vector<double> a(static_cast<std::size_t>(frames)), b(static_cast<std::size_t>(frames));
        for (int i = 0; i < frames; ++i) { a[static_cast<std::size_t>(i)] = in[2 * static_cast<std::size_t>(i)]; b[static_cast<std::size_t>(i)] = in[2 * static_cast<std::size_t>(i) + 1]; }
        const int capOut = static_cast<int>(frames * dstRate / srcRate) + 4096; std::vector<double> oa(static_cast<std::size_t>(capOut)), ob(static_cast<std::size_t>(capOut));
        const double* ip[2] = {a.data(), b.data()}; double* op[2] = {oa.data(), ob.data()};
        const int got = amix_hr_resampler_process(k.rs, ip, frames, op, capOut);
        for (int i = 0; i < got; ++i) { k.pending.push_back(static_cast<float>(oa[static_cast<std::size_t>(i)])); k.pending.push_back(static_cast<float>(ob[static_cast<std::size_t>(i)])); } } } }
  if (!k.pending.empty()) { const std::size_t put = k.ring.push(k.pending.data(), k.pending.size() / 2); k.pending.erase(k.pending.begin(), k.pending.begin() + static_cast<std::ptrdiff_t>(2 * put)); }
  return k.ring.readable() + k.pending.size() / 2;
}
// drops what is buffered (seek, stop, a new file): done by the reader in its next block, or here when no block runs
inline void deckFlushNow(Side& sd, int d) {
  Deck& k = sd.deck[d]; k.pending.clear();
  if (k.rs) { amix_hr_resampler_destroy(k.rs); k.rs = nullptr; k.rsFrom = k.rsTo = 0; }
  k.flushReq.store(true, std::memory_order_release);
  for (int i = 0; i < 50 && k.flushReq.load(std::memory_order_acquire); ++i) { if (nowNs() - sd.lastRead.load() > 100000000LL) break; std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
  if (k.flushReq.load(std::memory_order_acquire)) { k.ring.r.store(k.ring.w.load()); k.flushReq.store(false); }   // nobody is reading: done here
}
}  // namespace amix_decks
