#include "recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace am::rec {

Recorder::Recorder() = default;
Recorder::~Recorder() { stop(); }

std::string Recorder::stampNow() { const std::time_t t = std::time(nullptr); std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char b[32]; std::strftime(b, sizeof b, "%Y-%m-%d_%H-%M-%S", &tmv); return b; }

void Recorder::writeHeader(File& x, bool close) {
  const std::uint32_t bps = opt_.float32 ? 4 : 3, dataLen = static_cast<std::uint32_t>(std::min<std::uint64_t>(x.frames * x.channels * bps, 0xFFFFFFFFu - 44)), sr = static_cast<std::uint32_t>(sampleRate_);
  unsigned char h[44]; auto w32 = [&](int at, std::uint32_t v) { h[at] = v & 255; h[at + 1] = (v >> 8) & 255; h[at + 2] = (v >> 16) & 255; h[at + 3] = (v >> 24) & 255; }; auto w16 = [&](int at, std::uint16_t v) { h[at] = v & 255; h[at + 1] = (v >> 8) & 255; };
  std::memcpy(h, "RIFF", 4); w32(4, 36 + dataLen); std::memcpy(h + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, opt_.float32 ? 3 : 1); w16(22, static_cast<std::uint16_t>(x.channels)); w32(24, sr); w32(28, sr * x.channels * bps); w16(32, static_cast<std::uint16_t>(x.channels * bps)); w16(34, static_cast<std::uint16_t>(bps * 8)); std::memcpy(h + 36, "data", 4); w32(40, dataLen);
  std::fseek(x.f, 0, SEEK_SET); std::fwrite(h, 1, 44, x.f); if (close) { std::fflush(x.f); std::fclose(x.f); x.f = nullptr; } else std::fseek(x.f, 0, SEEK_END);
}

bool Recorder::start(const Options& o, double sampleRate, std::string& err) {
  if (recording_.load()) { err = "already recording"; return false; }
  opt_ = o; sampleRate_ = sampleRate > 0 ? sampleRate : 48000; if (opt_.session.empty()) opt_.session = stampNow();
#ifdef _WIN32
  CreateDirectoryA(opt_.folder.c_str(), nullptr);
#else
  ::mkdir(opt_.folder.c_str(), 0755);
#endif
  out_.clear(); files_.clear();
  auto open = [&](const std::string& name, int ch) { File x; x.path = opt_.folder + "/" + opt_.session + "-" + name + ".wav"; x.channels = ch; x.f = std::fopen(x.path.c_str(), "wb"); if (!x.f) { err = "cannot write " + x.path; return false; } writeHeader(x, false); out_.push_back(x); files_.push_back(x.path); return true; };
  if (opt_.master && !open("master", 2)) return false;
  for (int s : opt_.strips) { char n[16]; std::snprintf(n, sizeof n, "ch%02d", s + 1); if (!open(n, 1)) { stop(); return false; } }
  if (out_.empty()) { err = "nothing to record: arm the master or a strip"; return false; }
  chans_ = (opt_.master ? 2 : 0) + static_cast<int>(opt_.strips.size());
  ringFrames_ = static_cast<std::size_t>(sampleRate_ * 4);   // 4 s of audio between the threads
  ring_.assign(ringFrames_ * static_cast<std::size_t>(chans_), 0.f); head_ = 0; tail_ = 0; framesTaken_ = 0; dropped_ = 0;
  scratch_.resize(static_cast<std::size_t>(sampleRate_) * static_cast<std::size_t>(chans_) * 4);
  quit_ = false; recording_ = true; writer_ = std::thread([this] { writerLoop(); });
  return true;
}

void Recorder::onBlock(const float* const* strips, int nStrips, const float* L, const float* R, std::size_t frames, double) {
  if (!recording_.load(std::memory_order_acquire)) return;
  const std::size_t h = head_.load(std::memory_order_relaxed), t = tail_.load(std::memory_order_acquire), used = h - t;
  if (used + frames > ringFrames_) { dropped_.fetch_add(1); return; }
  const std::size_t ch = static_cast<std::size_t>(chans_);
  for (std::size_t i = 0; i < frames; ++i) {
    float* dst = ring_.data() + ((h + i) % ringFrames_) * ch; std::size_t c = 0;
    if (opt_.master) { dst[c++] = L[i]; dst[c++] = R[i]; }
    for (int s : opt_.strips) dst[c++] = (s < nStrips && strips && strips[s]) ? strips[s][i] : 0.f;
  }
  head_.store(h + frames, std::memory_order_release); framesTaken_.fetch_add(frames);
}

void Recorder::writerLoop() {
  const std::size_t ch = static_cast<std::size_t>(chans_); const int bps = opt_.float32 ? 4 : 3;
  auto flush = [&]() {
    const std::size_t h = head_.load(std::memory_order_acquire), t = tail_.load(std::memory_order_relaxed); const std::size_t n = std::min<std::size_t>(h - t, scratch_.size() / (ch * static_cast<std::size_t>(bps)) / 2);
    if (!n) return false;
    std::size_t fileCol = 0;
    for (File& x : out_) {
      unsigned char* o = scratch_.data(); const std::size_t cols = static_cast<std::size_t>(x.channels);
      for (std::size_t i = 0; i < n; ++i) { const float* src = ring_.data() + ((t + i) % ringFrames_) * ch + fileCol;
        for (std::size_t c = 0; c < cols; ++c) { const float v = std::max(-1.f, std::min(1.f, src[c])); if (opt_.float32) { std::memcpy(o, &v, 4); o += 4; } else { const std::int32_t s = static_cast<std::int32_t>(std::lrint(v * 8388607.0)); *o++ = static_cast<unsigned char>(s & 255); *o++ = static_cast<unsigned char>((s >> 8) & 255); *o++ = static_cast<unsigned char>((s >> 16) & 255); } } }
      if (x.f) std::fwrite(scratch_.data(), 1, static_cast<std::size_t>(o - scratch_.data()), x.f);
      x.frames += n; fileCol += cols;
    }
    tail_.store(t + n, std::memory_order_release); return true;
  };
  while (!quit_.load(std::memory_order_acquire)) { if (!flush()) std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
  while (flush()) {}
  for (File& x : out_) if (x.f) writeHeader(x, true);
}

void Recorder::stop() {
  if (!recording_.exchange(false)) { if (writer_.joinable()) { quit_ = true; writer_.join(); } return; }
  quit_ = true; if (writer_.joinable()) writer_.join();
  out_.clear();
}

}  // namespace am::rec
