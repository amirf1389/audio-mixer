// see app_capture.hpp
#include "audiomixer/app_capture.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

#include "audiomixer/app_sound.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <objbase.h>
namespace am { long wasapiAppCaptureClient(unsigned long pid, double rate, void** audioClient, void** captureClient, std::string& err);
long wasapiDeviceCaptureClient(const std::string& endpointId, double rate, void** audioClient, void** captureClient, std::string& err);
bool wasapiCaptureProperties(const std::string& endpointId, int& channels, double& rate, int& bits, float& level, bool& muted, int& enhancements, std::string& err); }
#endif

namespace am::app {
namespace {
// one producer (the slot's capture thread), one consumer (the audio thread); neither waits for the other
class Ring {
 public:
  explicit Ring(std::size_t capacity) : buf_(capacity + 1) {}
  std::size_t size() const { const std::size_t w = w_.load(std::memory_order_acquire), r = r_.load(std::memory_order_acquire); return w >= r ? w - r : buf_.size() - r + w; }
  std::size_t capacity() const { return buf_.size() - 1; }
  std::size_t push(const float* src, std::size_t n) {
    std::size_t w = w_.load(std::memory_order_relaxed), done = 0; const std::size_t r = r_.load(std::memory_order_acquire), cap = buf_.size();
    while (done < n) { const std::size_t next = w + 1 == cap ? 0 : w + 1; if (next == r) break; buf_[w] = src ? src[done] : 0.0f; ++done; w = next; }
    w_.store(w, std::memory_order_release); return done; }
  std::size_t pop(float* dst, std::size_t n) {   // dst nullptr: throw the samples away
    std::size_t r = r_.load(std::memory_order_relaxed), done = 0; const std::size_t w = w_.load(std::memory_order_acquire), cap = buf_.size();
    while (done < n && r != w) { if (dst) dst[done] = buf_[r]; ++done; r = r + 1 == cap ? 0 : r + 1; }
    r_.store(r, std::memory_order_release); return done; }
  void clearFromConsumer() { r_.store(w_.load(std::memory_order_acquire), std::memory_order_release); }
 private:
  std::vector<float> buf_; std::atomic<std::size_t> w_{0}, r_{0};
};
constexpr std::size_t kRingFloats = 2 * 96000;   // stereo: one second at 96 kHz, half a second at 192 kHz
}  // namespace

// state: 0 free, 1 opening, 2 live, 3 failed. The audio thread reads a slot only while state >= 1; a slot's ring lives as long as
// the Capture, so a slot that is closed while a block is being pulled is still safe to read.
struct Capture::Slot {
  Ring ring{kRingFloats};
  std::atomic<int> state{0}; std::atomic<bool> stop{false}, test{false}, primed{false}, flush{false};   // flush: the audio thread empties the ring before it reads (a slot taken again)
  std::atomic<double> rate{48000}, hz{440}; double phase = 0;                        // phase: audio thread only
  std::atomic<float> peak{0}; std::atomic<std::uint64_t> gaps{0}, silentFrames{0};
  std::thread thread; mutable std::mutex mu; std::uint32_t pid = 0; std::string label, error, device; bool isDevice = false;   // mu guards pid / label / error / device
  std::vector<float> scratch;                                                       // audio thread only: one block, interleaved
};

Capture::Capture() { for (auto& s : s_) { s = std::make_unique<Slot>(); s->scratch.assign(2 * 8192, 0.0f); } }
Capture::~Capture() { closeAll(); }

bool Capture::available() {
#ifdef _WIN32
  using Fn = LONG(WINAPI*)(OSVERSIONINFOW*); HMODULE nt = GetModuleHandleW(L"ntdll.dll"); if (!nt) return false;
  const auto f = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion"))); if (!f) return false;
  OSVERSIONINFOW v{}; v.dwOSVersionInfoSize = sizeof v; if (f(&v) != 0) return false;
  return v.dwMajorVersion > 10 || (v.dwMajorVersion == 10 && v.dwBuildNumber >= 19041);
#else
  return false;
#endif
}
int Capture::freeSlot() const { for (int k = 0; k < kSlots; ++k) if (s_[k]->state.load() == 0) return k; return -1; }
int Capture::used() const { int n = 0; for (const auto& s : s_) if (s->state.load() != 0) ++n; return n; }

int Capture::open(std::uint32_t pid, const std::string& label, double sampleRate, std::string& err, int wantSlot) {
  if (pid == 0) { err = "no program picked"; return -1; }
  for (int k = 0; k < kSlots; ++k) { Slot& s = *s_[k]; if (s.state.load() == 0 || s.test.load()) continue; std::lock_guard<std::mutex> g(s.mu); if (s.pid == pid) { err = label + " is already captured"; return -1; } }
  if (!available()) { err = "capturing a program needs Windows 10 version 2004 or later"; return -1; }
  const int k = wantSlot >= 0 && wantSlot < kSlots && s_[wantSlot]->state.load() == 0 ? wantSlot : freeSlot(); if (k < 0) { err = "eight programs are captured already: release one first"; return -1; }
  Slot& s = *s_[k]; if (s.thread.joinable()) s.thread.join();
  { std::lock_guard<std::mutex> g(s.mu); s.pid = pid; s.label = label; s.error.clear(); s.device.clear(); s.isDevice = false; }
  s.rate = sampleRate > 0 ? sampleRate : 48000; s.test = false; s.stop = false; s.flush = true; s.peak = 0; s.gaps = 0; s.silentFrames = 0;
  s.state = 1; s.thread = std::thread([this, k] { run(k); });
  return k;
}
int Capture::openFeed(const std::string& label, double sampleRate, std::string& err) {
  const int k = freeSlot(); if (k < 0) { err = "eight programs are captured already: release one first"; return -1; }
  Slot& s = *s_[k]; if (s.thread.joinable()) s.thread.join();
  { std::lock_guard<std::mutex> g(s.mu); s.pid = 0; s.label = label; s.error.clear(); s.device.clear(); s.isDevice = false; }
  s.rate = sampleRate > 0 ? sampleRate : 48000; s.test = false; s.stop = false; s.flush = true; s.peak = 0; s.gaps = 0; s.silentFrames = 0;
  s.state = 2; return k;
}
bool Capture::deviceAvailable() {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}
Capture::DeviceProps Capture::deviceProps(const std::string& endpointId) {
  DeviceProps p;
#ifdef _WIN32
  float level = -1; p.ok = wasapiCaptureProperties(endpointId, p.channels, p.rate, p.bits, level, p.muted, p.enhancements, p.error); p.level = level;
#else
  (void)endpointId; p.error = "Windows' sound properties are read on Windows";
#endif
  return p;
}
int Capture::openDevice(const std::string& endpointId, const std::string& label, double sampleRate, std::string& err, int wantSlot) {
  for (int k = 0; k < kSlots; ++k) { Slot& s = *s_[k]; if (s.state.load() == 0) continue; std::lock_guard<std::mutex> g(s.mu); if (s.isDevice && s.device == endpointId) { err = label + " is already captured"; return -1; } }
  if (!deviceAvailable()) { err = "Windows' recording devices are captured on Windows"; return -1; }
  const int k = wantSlot >= 0 && wantSlot < kSlots && s_[wantSlot]->state.load() == 0 ? wantSlot : freeSlot(); if (k < 0) { err = "eight sources are captured already: release one first"; return -1; }
  Slot& s = *s_[k]; if (s.thread.joinable()) s.thread.join();
  { std::lock_guard<std::mutex> g(s.mu); s.pid = 0; s.label = label; s.error.clear(); s.device = endpointId; s.isDevice = true; }
  s.rate = sampleRate > 0 ? sampleRate : 48000; s.test = false; s.stop = false; s.flush = true; s.peak = 0; s.gaps = 0; s.silentFrames = 0;
  s.state = 1; s.thread = std::thread([this, k] { run(k); });
  return k;
}
int Capture::openTest(const std::string& label, double hz, double sampleRate, std::string& err) {
  const int k = freeSlot(); if (k < 0) { err = "eight programs are captured already: release one first"; return -1; }
  Slot& s = *s_[k]; if (s.thread.joinable()) s.thread.join();
  { std::lock_guard<std::mutex> g(s.mu); s.pid = 0; s.label = label; s.error.clear(); s.device.clear(); s.isDevice = false; }
  s.rate = sampleRate > 0 ? sampleRate : 48000; s.hz = hz; s.test = true; s.stop = false; s.primed = false; s.peak = 0; s.gaps = 0; s.silentFrames = 0;
  s.state = 2; return k;
}
void Capture::close(int slot) {
  if (slot < 0 || slot >= kSlots) return; Slot& s = *s_[slot];
  s.stop = true; if (s.thread.joinable()) s.thread.join();
  s.state = 0; s.test = false; s.peak = 0;
  { std::lock_guard<std::mutex> g(s.mu); s.pid = 0; s.label.clear(); s.error.clear(); s.device.clear(); s.isDevice = false; }
}
void Capture::closeAll() { for (int k = 0; k < kSlots; ++k) close(k); }
void Capture::setSampleRate(double sampleRate) {
  if (sampleRate <= 0) return;
  for (int k = 0; k < kSlots; ++k) { Slot& s = *s_[k]; if (s.state.load() == 0 || s.rate.load() == sampleRate) continue;
    if (s.test.load()) { s.rate = sampleRate; continue; }
    std::uint32_t pid; std::string label; { std::lock_guard<std::mutex> g(s.mu); pid = s.pid; label = s.label; }
    s.stop = true; if (s.thread.joinable()) s.thread.join();   // the same slot (the strips keep their inputs), a new stream at the new rate
    s.rate = sampleRate; s.stop = false; s.flush = true; { std::lock_guard<std::mutex> g(s.mu); s.error.clear(); }
    s.state = 1; s.thread = std::thread([this, k] { run(k); }); }
}
Capture::Info Capture::info(int slot) const {
  Info i; if (slot < 0 || slot >= kSlots) return i; const Slot& s = *s_[slot]; const int st = s.state.load(); if (st == 0) return i;
  i.used = true; i.test = s.test.load(); i.peak = s.peak.load(); i.gaps = s.gaps.load();
  { std::lock_guard<std::mutex> g(s.mu); i.pid = s.pid; i.label = s.label; i.error = s.error; i.device = s.device; i.isDevice = s.isDevice; }
  i.state = st == 1 ? "opening" : st == 3 ? "failed" : (!i.test && s.silentFrames.load() > static_cast<std::uint64_t>(s.rate.load() * 3) ? "silent" : "live");
  return i;
}
std::size_t Capture::feed(int slot, const float* interleaved, std::size_t frames) {
  if (slot < 0 || slot >= kSlots) return 0; Slot& s = *s_[slot]; if (s.state.load() == 0) return 0;
  return s.ring.push(interleaved, frames * 2) / 2;
}

void Capture::pull(float* const* ch, int channels, std::size_t frames, double sampleRate) {
  for (int k = 0; k < kSlots; ++k) {
    float* L = 2 * k < channels ? ch[2 * k] : nullptr; float* R = 2 * k + 1 < channels ? ch[2 * k + 1] : nullptr; float* M = 2 * kSlots + k < channels ? ch[2 * kSlots + k] : nullptr;
    Slot& s = *s_[k]; const int st = s.state.load(std::memory_order_acquire);
    auto silence = [&] { for (std::size_t i = 0; i < frames; ++i) { if (L) L[i] = 0; if (R) R[i] = 0; if (M) M[i] = 0; } };
    if (st < 2 || frames * 2 > s.scratch.size()) { silence(); if (st == 0) s.peak.store(0, std::memory_order_relaxed); continue; }
    float pk = 0;
    if (s.test.load(std::memory_order_relaxed)) {   // a steady tone at -12 dBFS, made here
      const double step = 6.283185307179586 * s.hz.load(std::memory_order_relaxed) / (sampleRate > 0 ? sampleRate : 48000.0);
      for (std::size_t i = 0; i < frames; ++i) { const float v = 0.25f * static_cast<float>(std::sin(s.phase)); s.phase += step; if (s.phase > 6.283185307179586) s.phase -= 6.283185307179586; if (L) L[i] = v; if (R) R[i] = v; if (M) M[i] = v; }
      pk = 0.25f;
    } else {
      if (s.flush.exchange(false, std::memory_order_relaxed)) { s.ring.clearFromConsumer(); s.primed.store(false, std::memory_order_relaxed); }
      // the cushion: about 30 ms (at least two blocks). Not there yet: silence; run empty: a gap, wait for it again; far too much: drop the oldest
      const std::size_t target = std::max<std::size_t>(2 * frames, static_cast<std::size_t>(0.030 * s.rate.load(std::memory_order_relaxed)));
      std::size_t have = s.ring.size() / 2;
      if (!s.primed.load(std::memory_order_relaxed)) { if (have >= target) s.primed.store(true, std::memory_order_relaxed); }
      if (s.primed.load(std::memory_order_relaxed) && have > 4 * target) { s.ring.pop(nullptr, (have - target) * 2); have = s.ring.size() / 2; }
      if (!s.primed.load(std::memory_order_relaxed)) { silence(); }
      else if (have < frames) { s.ring.pop(nullptr, have * 2); silence(); s.primed.store(false, std::memory_order_relaxed); s.gaps.fetch_add(1, std::memory_order_relaxed); }
      else { float* b = s.scratch.data(); s.ring.pop(b, frames * 2);
        for (std::size_t i = 0; i < frames; ++i) { const float l = b[2 * i], r = b[2 * i + 1]; if (L) L[i] = l; if (R) R[i] = r; if (M) M[i] = 0.5f * (l + r); const float a = std::max(std::fabs(l), std::fabs(r)); if (a > pk) pk = a; } }
    }
    const float old = s.peak.load(std::memory_order_relaxed); s.peak.store(pk >= old ? pk : old * 0.9f + pk * 0.1f, std::memory_order_relaxed);
    if (pk > 1e-6f) s.silentFrames.store(0, std::memory_order_relaxed); else s.silentFrames.fetch_add(frames, std::memory_order_relaxed);
  }
}

// the slot's thread: open the program's stream, then move what Windows delivers into the ring until asked to stop
void Capture::run(int slot) {
  Slot& s = *s_[slot];
#ifdef _WIN32
  std::uint32_t pid; std::string device; bool isDevice; { std::lock_guard<std::mutex> g(s.mu); pid = s.pid; device = s.device; isDevice = s.isDevice; }
  auto fail = [&](const std::string& why) { { std::lock_guard<std::mutex> g(s.mu); s.error = why; } s.state = 3; };
  const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  void* acv = nullptr; void* ccv = nullptr; std::string err;
  if ((isDevice ? wasapiDeviceCaptureClient(device, s.rate.load(), &acv, &ccv, err) : wasapiAppCaptureClient(pid, s.rate.load(), &acv, &ccv, err)) != 0) { fail(err.empty() ? "Windows could not open that sound" : err); if (SUCCEEDED(co)) CoUninitialize(); return; }
  IAudioClient* ac = static_cast<IAudioClient*>(acv); IAudioCaptureClient* cc = static_cast<IAudioCaptureClient*>(ccv);
  if (FAILED(ac->Start())) { fail("cannot start that program's stream"); cc->Release(); ac->Release(); if (SUCCEEDED(co)) CoUninitialize(); return; }
  s.state = 2;
  std::vector<float> zeros(2 * 4096, 0.0f); int idle = 0;
  while (!s.stop.load()) {
    Sleep(5); UINT32 n = 0; HRESULT r = cc->GetNextPacketSize(&n);
    if (FAILED(r)) { fail(isDevice ? "the recording device was unplugged or switched off" : "the program closed or its sound device changed"); break; }
    // a program that plays nothing delivers no packets: the console still gets a steady (silent) stream, so the cushion stays
    if (n == 0) { if (++idle >= 4) { const std::size_t want = static_cast<std::size_t>(s.rate.load() * 0.005) * 2; if (s.ring.size() < static_cast<std::size_t>(s.rate.load() * 0.06) * 2) s.ring.push(zeros.data(), std::min(want, zeros.size())); } continue; }
    idle = 0;
    while (n > 0) {
      BYTE* data = nullptr; UINT32 got = 0; DWORD fl = 0;
      if (FAILED(cc->GetBuffer(&data, &got, &fl, nullptr, nullptr))) { n = 0; break; }
      if (fl & AUDCLNT_BUFFERFLAGS_SILENT) { std::size_t left = static_cast<std::size_t>(got) * 2; while (left > 0) { const std::size_t c = std::min(left, zeros.size()); s.ring.push(zeros.data(), c); left -= c; } }
      else s.ring.push(reinterpret_cast<const float*>(data), static_cast<std::size_t>(got) * 2);   // a full ring drops the newest: the audio thread trims it
      cc->ReleaseBuffer(got);
      if (FAILED(cc->GetNextPacketSize(&n))) n = 0;
    }
  }
  ac->Stop(); cc->Release(); ac->Release(); if (SUCCEEDED(co)) CoUninitialize();
#else
  { std::lock_guard<std::mutex> g(s.mu); s.error = "capturing a program needs Windows"; } s.state = 3;
#endif
}

}  // namespace am::app
