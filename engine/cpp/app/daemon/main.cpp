// amixos-daemon: the A mix OS engine as a background service with a local JSON API. See daemon.hpp for the routes.
//   amixos-daemon [--port 8765] [--allow-lan] [--token-file PATH | --token T] [--strips 32] [--open BACKEND]
// --allow-lan binds 0.0.0.0: the token still protects control, but nothing is encrypted on the wire. Use deploy/nginx for TLS beyond a trusted LAN.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "daemon.hpp"
#include "paths/paths.hpp"

namespace {
std::atomic<bool> g_quit{false};
void onSignal(int) { g_quit = true; }
}  // namespace

int main(int argc, char** argv) {
  am::Daemon::Options o; std::string tokenFile = am::paths::file("daemon.token"), openBackend; bool lan = false, tokenArg = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i]; auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--port") o.port = static_cast<unsigned short>(std::atoi(next().c_str()));
    else if (a == "--allow-lan") lan = true;
    else if (a == "--bind") o.bind = next();
    else if (a == "--token") { o.token = next(); tokenArg = true; }
    else if (a == "--token-file") tokenFile = next();
    else if (a == "--strips") o.strips = std::atoi(next().c_str());
    else if (a == "--open") openBackend = next();
    else if (a == "--version") o.version = next();
    else if (a == "--help" || a == "-h") { std::printf("amixos-daemon [--port 8765] [--allow-lan] [--bind ADDR] [--token-file PATH | --token T] [--strips 32] [--open BACKEND]\n"); return 0; }
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
  }
  if (lan) o.bind = "0.0.0.0";
  std::string err;
  if (o.token.empty()) { o.token = am::Daemon::loadOrCreateToken(tokenFile, err); if (o.token.empty()) { std::fprintf(stderr, "amixos-daemon: %s\n", err.c_str()); return 1; } }
  am::Daemon d(o);
  if (!d.start(err)) { std::fprintf(stderr, "amixos-daemon: %s\n", err.c_str()); return 1; }
  if (!openBackend.empty()) { am::json::Value c = am::json::Value::object(); c.set("op", "open"); c.set("backend", openBackend); std::string e; d.control(c, e); if (!e.empty()) std::fprintf(stderr, "amixos-daemon: open %s: %s\n", openBackend.c_str(), e.c_str()); }
  std::signal(SIGINT, onSignal); std::signal(SIGTERM, onSignal);
  std::printf("amixos-daemon listening on %s:%u (token in %s)%s\n", o.bind.c_str(), d.port(), tokenArg ? "--token" : tokenFile.c_str(), lan ? " — LAN mode: unencrypted, put deploy/nginx in front for TLS" : "");
  std::fflush(stdout);
  while (!g_quit) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  d.stop();
  return 0;
}
