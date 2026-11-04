// --stream: the engine as the bridge's audio I/O (in place of the Audify / RtAudio binding). Raw PCM over the standard streams:
//   stdin  -> the output device: interleaved int16, little-endian, outChannels per frame
//   stdout <- the input device:  interleaved int16, little-endian, inChannels per frame
//   stderr    status, one JSON object per line: {"type":"started",...} once the device runs, {"type":"error","message":...} when it cannot,
//             {"type":"stopped",...} at the end (with the samples dropped because nobody read stdout, and the output underruns)
// The audio thread only touches the two lock-free rings (pcm_ring.hpp) and preallocated scratch buffers: it never allocates, locks or waits.
// The main thread copies stdin into the play ring; a second thread copies the capture ring to stdout. The stream ends when stdin ends.
#include "stream.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "pcm_ring.hpp"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace am {

namespace {

std::string jsonText(const std::string& s) {
  std::string o = "\"";
  for (const char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
    else o += c;
  }
  return o + "\"";
}

void status(const std::string& json) { std::fprintf(stderr, "%s\n", json.c_str()); std::fflush(stderr); }

std::int16_t toPcm(float x) {
  const float c = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
  return static_cast<std::int16_t>(c * 32767.0f);
}

constexpr std::size_t kChunkFrames = 1024;   // the callback is processed in chunks of this many frames, so any block size fits the scratch buffers

}  // namespace

int runStream(const DeviceConfig& requested) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  DeviceConfig cfg = requested;
  const std::string name = cfg.backend == "auto" ? availableBackends().front() : cfg.backend;
  auto backend = createBackend(name);
  if (!backend) { status("{\"type\":\"error\",\"message\":" + jsonText("the backend \"" + name + "\" is not built into this binary") + "}"); return 1; }
  const int nIn = std::max(0, cfg.inChannels), nOut = std::max(0, cfg.outChannels);
  cfg.inChannels = nIn; cfg.outChannels = nOut;

  // about half a second of audio each way: enough to ride out a slow pipe, short enough to keep the delay down
  const std::size_t ringFrames = std::max<std::size_t>(cfg.frames * 8u, static_cast<std::size_t>(cfg.sampleRate / 2));
  PcmRing capture(ringFrames * static_cast<std::size_t>(std::max(1, nIn))), play(ringFrames * static_cast<std::size_t>(std::max(1, nOut)));
  std::vector<std::int16_t> scratchIn(kChunkFrames * static_cast<std::size_t>(std::max(1, nIn)));
  std::vector<std::int16_t> scratchOut(kChunkFrames * static_cast<std::size_t>(std::max(1, nOut)));
  std::atomic<std::uint64_t> dropped{0}, underruns{0};
  std::atomic<bool> playing{false};   // underruns are only counted once the bridge has sent audio

  AudioCallback cb = [&](const float* const* in, float* const* out, std::size_t frames) {
    for (std::size_t f0 = 0; f0 < frames; f0 += kChunkFrames) {
      const std::size_t n = std::min(kChunkFrames, frames - f0);
      if (nIn > 0 && in) {
        for (std::size_t f = 0; f < n; ++f)
          for (int c = 0; c < nIn; ++c) scratchIn[f * nIn + c] = in[c] ? toPcm(in[c][f0 + f]) : 0;
        const std::size_t want = n * static_cast<std::size_t>(nIn), put = capture.push(scratchIn.data(), want);
        if (put < want) dropped.fetch_add(want - put, std::memory_order_relaxed);
      }
      if (nOut > 0 && out) {
        const std::size_t want = n * static_cast<std::size_t>(nOut), got = play.pop(scratchOut.data(), want);
        if (got < want) {
          std::fill(scratchOut.begin() + static_cast<std::ptrdiff_t>(got), scratchOut.begin() + static_cast<std::ptrdiff_t>(want), std::int16_t(0));
          if (playing.load(std::memory_order_relaxed)) underruns.fetch_add(1, std::memory_order_relaxed);
        }
        for (std::size_t f = 0; f < n; ++f)
          for (int c = 0; c < nOut; ++c) if (out[c]) out[c][f0 + f] = scratchOut[f * nOut + c] / 32768.0f;
      }
    }
  };

  std::string err;
  if (!backend->open(cfg, cb, err) || !backend->start(err)) {
    status("{\"type\":\"error\",\"message\":" + jsonText(err.empty() ? "cannot open the device" : err) + "}");
    return 1;
  }
  char head[256];
  std::snprintf(head, sizeof head, "{\"type\":\"started\",\"backend\":%s,\"sampleRate\":%.0f,\"frames\":%u,\"inChannels\":%d,\"outChannels\":%d}",
                jsonText(name).c_str(), cfg.sampleRate, cfg.frames, nIn, nOut);
  status(head);

  std::atomic<bool> done{false};
  std::thread writer([&] {
    std::vector<std::int16_t> buf(8192);
    while (true) {
      const std::size_t n = capture.pop(buf.data(), buf.size());
      if (n) { std::fwrite(buf.data(), sizeof(std::int16_t), n, stdout); std::fflush(stdout); continue; }
      if (done.load()) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  });

  // stdin -> play ring. A full ring waits, which holds the bridge back through the pipe instead of piling up delay.
  std::vector<unsigned char> raw(16384 + 1);
  std::size_t carry = 0;   // a trailing odd byte waits for the next read
  while (true) {
    const std::size_t got = std::fread(raw.data() + carry, 1, raw.size() - 1 - carry, stdin);
    if (got == 0) break;
    const std::size_t bytes = carry + got, samples = bytes / 2;
    if (nOut > 0) {
      playing.store(true, std::memory_order_relaxed);
      const std::int16_t* s = reinterpret_cast<const std::int16_t*>(raw.data());
      std::size_t sent = 0;
      while (sent < samples) {
        sent += play.push(s + sent, samples - sent);
        if (sent < samples) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    carry = bytes % 2;
    if (carry) raw[0] = raw[bytes - 1];
  }

  backend->stop();
  done.store(true);
  writer.join();
  char tail[256];
  std::snprintf(tail, sizeof tail, "{\"type\":\"stopped\",\"dropped\":%llu,\"underruns\":%llu,\"xruns\":%llu}",
                static_cast<unsigned long long>(dropped.load()), static_cast<unsigned long long>(underruns.load()), static_cast<unsigned long long>(backend->xruns()));
  status(tail);
  return 0;
}

}  // namespace am
