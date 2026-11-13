// amfb-hybrid: one audio device (ASIO, or WASAPI / ALSA / null through the same engine backends),
// its input pairs on the first strips, captured programs on the others, sixteen strips into a stereo
// bus. Controlled and metered through the shared memory block only.
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include "audiomixer/runtime.hpp"
#include "../core/mixer.hpp"
#include "app_capture.hpp"
#include "ext/ext_host.hpp"
#include "audiomixer/dynamics.hpp"

namespace {

std::string usage() {
  return (
      "amfb-hybrid [options]\n"
      "  --backend NAME   asio | wasapi | alsa | null | auto (default auto)\n"
      "  --device ID      the device to open (default: the system's)\n"
      "  --rate HZ        sample rate (default 48000)\n"
      "  --frames N       block size (default 480 = 10 ms at 48 kHz)\n"
      "  --in N           input channels of the device used as strips 1.. (default 2)\n"
      "  --app S=EXE      strip S (1-16) captures that program, e.g. --app 3=Spotify.exe (Windows)\n"
      "  --tone S         a 440 Hz test tone at -12 dBFS on strip S\n"
      "  --seconds N      stop after N seconds (default: run until Ctrl+C)");
}

int run(int argc, char** argv, std::atomic<bool>& g_quit, std::function<void(const std::string&)> say) {
  using amfb_ext::fmt;
  am::DeviceConfig cfg; cfg.frames = 480; cfg.inChannels = 2; cfg.outChannels = 2;
  std::map<int, std::string> apps; int tone = 0; double seconds = 0; bool defaults = true;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const char* v = i + 1 < argc ? argv[i + 1] : nullptr;
    if (a == "--help" || a == "-h") { say(usage()); return 0; }
    if (!v) { say(fmt("%s needs a value", a.c_str())); return 2; }
    ++i;
    if (a == "--backend") cfg.backend = v;
    else if (a == "--device") cfg.outDevice = cfg.inDevice = v;
    else if (a == "--rate") cfg.sampleRate = std::atof(v);
    else if (a == "--frames") cfg.frames = unsigned(std::atoi(v));
    else if (a == "--in") cfg.inChannels = std::atoi(v);
    else if (a == "--tone") tone = std::atoi(v);
    else if (a == "--seconds") seconds = std::atof(v);
    else if (a == "--app") {
      const char* eq = std::strchr(v, '=');
      const int s = std::atoi(v);
      if (!eq || s < 1 || s > amfb::kStrips || !eq[1]) { say(fmt("--app wants STRIP=PROGRAM, strip 1-%d", amfb::kStrips)); return 2; }
      apps[s] = eq + 1; defaults = false;
    } else { say(fmt("unknown option %s", a.c_str())); say(usage()); return 2; }
  }
  if (cfg.sampleRate < 32000 || cfg.sampleRate > 192000 || cfg.frames < 16 || cfg.frames > 8192 || cfg.inChannels < 0 || cfg.inChannels > 32 ||
      tone < 0 || tone > amfb::kStrips) { say(fmt("a value is out of range")); return 2; }

  if (cfg.backend == "auto") {
    cfg.backend = "null";
    for (const auto& n : am::availableBackends()) if (n != "null") { cfg.backend = n; break; }
  }
  if (cfg.backend == "null") cfg.inChannels = 0;
  const int hwStrips = cfg.inChannels / 2 + cfg.inChannels % 2;
#ifdef _WIN32
  if (defaults) {                                  // the usual players on the strips after the device's inputs
    const char* usual[] = {"Spotify.exe", "AppleMusic.exe", "Roon.exe", "Deezer.exe", "chrome.exe", "msedge.exe", "firefox.exe"};
    int s = hwStrips + 1;
    for (const char* u : usual) if (s <= amfb::kStrips) apps[s++] = u;
  }
#else
  (void)defaults;
  if (!apps.empty()) say(fmt("note: capturing a program is a Windows function; --app is ignored here"));
#endif

  amfb::SharedMem shm; std::string err;
  amfb::SharedBlock* block = shm.open(true, &err);
  if (!block) { say(fmt("error: %s", err.c_str())); return 1; }
  for (int s = 0; s < amfb::kStrips; ++s) {
    std::string label = s < hwStrips ? "IN " + std::to_string(2 * s + 1) + "-" + std::to_string(2 * s + 2) : "";
    if (apps.count(s + 1) && s >= hwStrips) label = apps[s + 1];
    if (tone == s + 1) label = "TONE 440";
    std::snprintf(block->m[s].source, sizeof block->m[s].source, "%s", label.c_str());
  }
  std::snprintf(block->m[amfb::kStrips].source, sizeof block->m[0].source, "MASTER");

  amfb::HybridMixer mixer(block, cfg.sampleRate, hwStrips);
  auto backend = am::createBackend(cfg.backend);
  if (!backend) { say(fmt("error: this build has no %s backend", cfg.backend.c_str())); return 1; }
  if (!backend->open(cfg, [&](const float* const* in, float* const* out, std::size_t frames) {
        mixer.process(in, cfg.inChannels, out, cfg.outChannels, frames);
      }, err)) { say(fmt("error: %s", err.c_str())); return 1; }

#ifdef _WIN32
  std::vector<std::unique_ptr<amfb::AppCapture>> captures;
  for (const auto& [s, exe] : apps) {
    if (s <= hwStrips || s == tone) continue;
    captures.push_back(std::make_unique<amfb::AppCapture>(std::wstring(exe.begin(), exe.end()), mixer.ring(s - 1), cfg.sampleRate));
    captures.back()->start();
  }
#endif
  std::thread toneThread;
  if (tone > hwStrips) toneThread = std::thread([&] {                 // a paced producer, like a capture thread
    const double w = 2 * amfb::kPi * 440.0 / cfg.sampleRate, amp = amfb::dbToLin(-12.0);
    std::vector<float> buf(960); double ph = 0;
    auto next = std::chrono::steady_clock::now();
    while (!g_quit) {
      for (std::size_t i = 0; i < 480; ++i) { buf[2 * i] = buf[2 * i + 1] = float(amp * std::sin(ph)); ph += w; if (ph > 2 * amfb::kPi) ph -= 2 * amfb::kPi; }
      mixer.ring(tone - 1).write(buf.data(), buf.size());
      next += std::chrono::microseconds(long(480.0 / cfg.sampleRate * 1e6));
      std::this_thread::sleep_until(next);
    }
  });

  if (!backend->start(err)) { say(fmt("error: %s", err.c_str())); g_quit = true; if (toneThread.joinable()) toneThread.join(); return 1; }
  say(fmt("amfb-hybrid: %s, %.0f Hz, %u frames, %d device strips, shared memory %s", backend->name(), cfg.sampleRate, cfg.frames,
              hwStrips, amfb::sharedName().c_str()));
  const auto t0 = std::chrono::steady_clock::now();
  am::DynamicsTracker dyn[amfb::kStrips]; int tick = 0;
  while (!g_quit) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (++tick % 2 == 0) for (int i = 0; i < amfb::kStrips; ++i) {   // ten a second, off the audio thread: COMPRESSION TARGET suggestions (never applied here)
      const float pk = block->m[i].inPeak.exchange(0.f, std::memory_order_relaxed);
      dyn[i].push(pk > 1e-6f ? 20.0 * std::log10(static_cast<double>(pk)) : -120.0);
      const am::CompSuggestion c = dyn[i].suggest(block->compTargetDb.load(std::memory_order_relaxed));
      block->m[i].spreadDb.store(c.ready ? static_cast<float>(c.spreadDb) : 0.f, std::memory_order_relaxed); block->m[i].suggestLevel.store(c.ready ? c.level : -1, std::memory_order_relaxed);
    }
    if (seconds > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= seconds) g_quit = true;
  }
  backend->stop();
  if (toneThread.joinable()) toneThread.join();
#ifdef _WIN32
  captures.clear();
#endif
  say(fmt("amfb-hybrid: stopped, device overruns %llu, source underruns %u", static_cast<unsigned long long>(backend->xruns()),
              block->underruns.load()));
  return 0;
}
}  // namespace

AMFB_EXTENSION("AmF X1 Hybrid", run)
