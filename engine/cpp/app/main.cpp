// audiomixer-native: the Audio Mixer engine as a native program on the audio layer of the operating system it is built for
// (Linux ALSA, Windows WASAPI / ASIO, macOS Core Audio; "null" everywhere). No browser, no Node.js: the mixer (am::Mixer) runs in the real-time callback of the chosen backend.
//   audiomixer-native --list
//   audiomixer-native [--backend auto|alsa|wasapi|asio|coreaudio|null] [--in DEV] [--out DEV] [--rate 48000] [--frames 256] [--strips 8]
//                     [--in-ch 2] [--out-ch 2] [--seconds N] [--master-db X] [--spatial AZIMUTH_DEG]
// Prints the meters twice a second; stops after --seconds (default: until Ctrl-C).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "audiomixer/asio.hpp"
#include "audiomixer/runtime.hpp"
#include "audiomixer/spatial.hpp"

static std::atomic<bool> g_stop{false};
static void onSignal(int) { g_stop = true; }
static double db(double lin) { return lin > 1e-9 ? 20.0 * std::log10(lin) : -120.0; }

int main(int argc, char** argv) {
  am::DeviceConfig cfg; int strips = 8; double seconds = 0, masterDb = 0, azimuth = 1e9; bool list = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i]; auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--list") list = true;
    else if (a == "--backend") cfg.backend = next();
    else if (a == "--in") cfg.inDevice = next();
    else if (a == "--out") cfg.outDevice = next();
    else if (a == "--rate") cfg.sampleRate = std::atof(next());
    else if (a == "--frames") cfg.frames = static_cast<unsigned>(std::atoi(next()));
    else if (a == "--strips") strips = std::atoi(next());
    else if (a == "--in-ch") cfg.inChannels = std::atoi(next());
    else if (a == "--out-ch") cfg.outChannels = std::atoi(next());
    else if (a == "--seconds") seconds = std::atof(next());
    else if (a == "--master-db") masterDb = std::atof(next());
    else if (a == "--spatial") azimuth = std::atof(next());
    else { std::printf("unknown option %s (see the header of engine/cpp/app/main.cpp)\n", a.c_str()); return 2; }
  }
  if (list) {
    std::printf("backends built in:"); for (const auto& b : am::availableBackends()) std::printf(" %s", b.c_str());
    std::printf("\n360 output (Resonance Audio): %s\n", am::Spatial::available() ? "built in" : "not built in");
    const auto drivers = am::listAsioDrivers(); std::printf("ASIO drivers:"); if (drivers.empty()) std::printf(" none"); for (const auto& d : drivers) std::printf(" [%s]", d.name.c_str()); std::printf("\n");
    return 0;
  }
  strips = std::max(1, std::min(strips, static_cast<int>(am::kMaxStrips)));
  std::signal(SIGINT, onSignal); std::signal(SIGTERM, onSignal);
  am::Runtime rt(strips); std::string err;
  if (!rt.open(cfg, err)) { std::printf("cannot open the audio device: %s\n", err.c_str()); return 1; }
  rt.setMasterDb(masterDb);
  if (azimuth < 1e8) { if (rt.setSpatial(true)) rt.setSpatialPosition(azimuth, 0, 2, 30); else std::printf("note: this build has no 360 output\n"); }
  if (!rt.start(err)) { std::printf("cannot start: %s\n", err.c_str()); return 1; }
  std::printf("audiomixer-native: %s, %.0f Hz, %u frames, %d strips -> stereo master%s\n", rt.backendName().c_str(), cfg.sampleRate, cfg.frames, strips, azimuth < 1e8 ? " (360)" : "");
  const auto t0 = std::chrono::steady_clock::now();
  while (!g_stop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const am::Snapshot s = rt.snapshot();
    std::printf("master L %6.1f R %6.1f dBFS | %6.1f LUFS (momentary) | xruns %llu\n", db(s.master[0].pk), db(s.master[1].pk), s.momentary, static_cast<unsigned long long>(s.xruns)); std::fflush(stdout);
    if (seconds > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= seconds) break;
  }
  rt.stop();
  return 0;
}
