#include "recorder.hpp"
#include "audiomixer/sample_format.hpp"

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

static int fmtOf(const am::rec::Options& o) { return o.format ? o.format : (o.float32 ? 1 : 0); }
static int bytesOf(int f) { return f == 2 ? 8 : f == 0 || f == 4 ? 3 : 4; }
// the WAV header: 80 bytes, a 28-byte JUNK chunk reserved after WAVE. Up to 4 GB it stays a plain RIFF WAV (readers skip JUNK); past 4 GB
// (11 minutes of 32-bit stereo at 768 kHz) the file is promoted on close to RF64 (EBU Tech 3306): RIFF -> RF64, JUNK -> ds64 with 64-bit sizes
void Recorder::writeHeader(File& x, bool close) {
  if (x.flac) { if (close) { FLAC__stream_encoder_finish(x.flac); FLAC__stream_encoder_delete(x.flac); x.flac = nullptr; } return; }
  const int fmt = fmtOf(opt_); const std::uint32_t bps = static_cast<std::uint32_t>(bytesOf(fmt)), sr = static_cast<std::uint32_t>(sampleRate_);
  const std::uint64_t data64 = x.frames * static_cast<std::uint64_t>(x.channels) * bps; const bool big = data64 > 0xFFFFFFFFull - 80;
  unsigned char h[80] = {}; auto w32 = [&](int at, std::uint32_t v) { h[at] = v & 255; h[at + 1] = (v >> 8) & 255; h[at + 2] = (v >> 16) & 255; h[at + 3] = (v >> 24) & 255; }; auto w16 = [&](int at, std::uint16_t v) { h[at] = v & 255; h[at + 1] = (v >> 8) & 255; };
  auto w64 = [&](int at, std::uint64_t v) { w32(at, static_cast<std::uint32_t>(v & 0xFFFFFFFFu)); w32(at + 4, static_cast<std::uint32_t>(v >> 32)); };
  std::memcpy(h, big ? "RF64" : "RIFF", 4); w32(4, big ? 0xFFFFFFFFu : static_cast<std::uint32_t>(72 + data64)); std::memcpy(h + 8, "WAVE", 4);
  std::memcpy(h + 12, big ? "ds64" : "JUNK", 4); w32(16, 28); if (big) { w64(20, 72 + data64); w64(28, data64); w64(36, x.frames); w32(44, 0); }
  std::memcpy(h + 48, "fmt ", 4); w32(52, 16); w16(56, fmt == 1 || fmt == 2 ? 3 : 1); w16(58, static_cast<std::uint16_t>(x.channels)); w32(60, sr); w32(64, sr * x.channels * bps); w16(68, static_cast<std::uint16_t>(x.channels * bps)); w16(70, static_cast<std::uint16_t>(bps * 8));
  std::memcpy(h + 72, "data", 4); w32(76, big ? 0xFFFFFFFFu : static_cast<std::uint32_t>(data64));
  std::fseek(x.f, 0, SEEK_SET); std::fwrite(h, 1, 80, x.f); if (close) { std::fflush(x.f); std::fclose(x.f); x.f = nullptr; } else std::fseek(x.f, 0, SEEK_END);
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
  const bool flac = fmtOf(opt_) == 4;
  if (flac && sampleRate_ > 1048575) { err = "FLAC takes rates up to 1,048,575 Hz: record this rate as WAV"; return false; }
  auto open = [&](const std::string& name, int ch) { File x; x.path = opt_.folder + "/" + opt_.session + "-" + name + (flac ? ".flac" : ".wav"); x.channels = ch;
    if (flac) {   // FLAC 24-bit (libFLAC, level 5): lossless, about half the size of the WAV
      x.flac = FLAC__stream_encoder_new(); if (!x.flac) { err = "FLAC: out of memory"; return false; }
      FLAC__stream_encoder_set_channels(x.flac, static_cast<unsigned>(ch)); FLAC__stream_encoder_set_bits_per_sample(x.flac, 24); FLAC__stream_encoder_set_sample_rate(x.flac, static_cast<unsigned>(std::lround(sampleRate_))); FLAC__stream_encoder_set_compression_level(x.flac, 5);
      if (FLAC__stream_encoder_init_file(x.flac, x.path.c_str(), nullptr, nullptr) != FLAC__STREAM_ENCODER_INIT_STATUS_OK) { FLAC__stream_encoder_delete(x.flac); err = "cannot write " + x.path; return false; }
      out_.push_back(x); files_.push_back(x.path); return true; }
    x.f = std::fopen(x.path.c_str(), "wb"); if (!x.f) { err = "cannot write " + x.path; return false; } writeHeader(x, false); out_.push_back(x); files_.push_back(x.path); return true; };
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
  const std::size_t ch = static_cast<std::size_t>(chans_); const int fmt = fmtOf(opt_), bps = bytesOf(fmt);
  auto flush = [&]() {
    const std::size_t h = head_.load(std::memory_order_acquire), t = tail_.load(std::memory_order_relaxed); const std::size_t n = std::min<std::size_t>(h - t, scratch_.size() / (ch * static_cast<std::size_t>(bps)) / 2);
    if (!n) return false;
    std::size_t fileCol = 0;
    for (File& x : out_) {
      unsigned char* o = scratch_.data(); const std::size_t cols = static_cast<std::size_t>(x.channels);
      for (std::size_t i = 0; i < n; ++i) { const float* src = ring_.data() + ((t + i) % ringFrames_) * ch + fileCol;
        for (std::size_t c = 0; c < cols; ++c) { const float v = src[c]; if (fmt == 1) { std::memcpy(o, &v, 4); o += 4; } else if (fmt == 2) { const double d = v; std::memcpy(o, &d, 8); o += 8; } else if (fmt == 3) { const std::int32_t q = am::fmt::toI32(v); std::memcpy(o, &q, 4); o += 4; }   /* FLOAT files keep everything above 0 dBFS (that is what float is for); integer formats round and clamp (sample_format.hpp) */ else { const std::int32_t s = am::fmt::toI24(v); *o++ = static_cast<unsigned char>(s & 255); *o++ = static_cast<unsigned char>((s >> 8) & 255); *o++ = static_cast<unsigned char>((s >> 16) & 255); } } }
      if (x.flac) { const std::size_t m = static_cast<std::size_t>(o - scratch_.data()) / 3; flacBuf_.resize(m);   // the 24-bit samples just packed, widened for libFLAC
        for (std::size_t k = 0; k < m; ++k) { const unsigned char* b = scratch_.data() + 3 * k; flacBuf_[k] = static_cast<FLAC__int32>((static_cast<std::uint32_t>(b[0]) << 8 | static_cast<std::uint32_t>(b[1]) << 16 | static_cast<std::uint32_t>(b[2]) << 24)) >> 8; }
        FLAC__stream_encoder_process_interleaved(x.flac, flacBuf_.data(), static_cast<unsigned>(n)); }
      if (x.f) std::fwrite(scratch_.data(), 1, static_cast<std::size_t>(o - scratch_.data()), x.f);
      x.frames += n; fileCol += cols;
    }
    tail_.store(t + n, std::memory_order_release); return true;
  };
  while (!quit_.load(std::memory_order_acquire)) { if (!flush()) std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
  while (flush()) {}
  for (File& x : out_) if (x.f || x.flac) writeHeader(x, true);
}

void Recorder::stop() {
  if (!recording_.exchange(false)) { if (writer_.joinable()) { quit_ = true; writer_.join(); } return; }
  quit_ = true; if (writer_.joinable()) writer_.join();
  out_.clear();
}

}  // namespace am::rec
