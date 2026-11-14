// amixos-daemon: the A mix OS engine as a background service with a local JSON API. See daemon.hpp for the routes.
//   amixos-daemon [--port 8765] [--allow-lan] [--token-file PATH | --token T] [--strips 32] [--open BACKEND]
// --allow-lan binds 0.0.0.0: the token still protects control, but nothing is encrypted on the wire. Use deploy/nginx for TLS beyond a trusted LAN.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <functional>
#include "daemon.hpp"
#include "paths/paths.hpp"
#include "ext/ext_host.hpp"

namespace {
int run(int argc, char** argv, std::atomic<bool>& g_quit, std::function<void(const std::string&)> say) {
  using amfb_ext::fmt;
  am::Daemon::Options o; std::string tokenFile = am::paths::file("daemon.token"), openBackend; bool lan = false, tokenArg = false;
  std::string tokensFile = am::paths::file("daemon.tokens.json"), newName, newRole = "control", revokeId; int newDays = 0; bool doNew = false, doList = false, doRevoke = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i]; auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--port") o.port = static_cast<unsigned short>(std::atoi(next().c_str()));
    else if (a == "--allow-lan") lan = true;
    else if (a == "--bind") o.bind = next();
    else if (a == "--allow-any-address") o.localOnly = false;   // drops the local policy: only behind a TLS proxy that filters for itself
    else if (a == "--token") { o.token = next(); tokenArg = true; }
    else if (a == "--token-file") tokenFile = next();
    else if (a == "--tokens-file") tokensFile = next();
    else if (a == "--new-token") { doNew = true; newName = next(); }
    else if (a == "--role") newRole = next();
    else if (a == "--days") newDays = std::atoi(next().c_str());
    else if (a == "--list-tokens") doList = true;
    else if (a == "--revoke-token") { doRevoke = true; revokeId = next(); }
    else if (a == "--strips") o.strips = std::atoi(next().c_str());
    else if (a == "--plan") o.plan = next();
    else if (a == "--open") openBackend = next();
    else if (a == "--version") o.version = next();
    else if (a == "--help" || a == "-h") { say(fmt("AmF X1 Network (amixos-daemon) [--port 8765] [--allow-lan] [--allow-any-address] [--bind ADDR] [--token-file PATH | --token T] [--strips 32] [--open BACKEND]\n  tokens: --new-token NAME [--role control|view] [--days N] | --list-tokens | --revoke-token ID  [--tokens-file PATH]\n  --allow-lan: phones on the same network may connect (local policy: private addresses only; the token is still needed; not encrypted).\n")); return 0; }
    else { say(fmt("unknown option %s\n", a.c_str())); return 2; }
  }
  if (lan) o.bind = "0.0.0.0";
  std::string err;
  o.tokenStore = tokensFile; o.consoleInfo = am::paths::file("remote-console.json");
  if (doNew || doList || doRevoke) {   // token management from the command line: no server is started
    am::TokenStore ts(tokensFile); if (!ts.load(err)) { say(fmt("amixos-daemon: %s\n", err.c_str())); return 1; }
    if (doNew) { std::string id; const std::string t = ts.issue(newName, newRole, newDays, id, err); if (t.empty()) { say(fmt("amixos-daemon: %s\n", err.c_str())); return 1; }
      say(fmt("id %s  role %s  %s\n%s\nThis token is shown only now: copy it into the remote. A running daemon reads the new token when it is restarted.\n", id.c_str(), newRole.c_str(), newDays > 0 ? ("ends in " + std::to_string(newDays) + " day(s)").c_str() : "no end", t.c_str())); }
    if (doRevoke) { if (!ts.revoke(revokeId, err)) { say(fmt("amixos-daemon: %s\n", err.c_str())); return 1; } say(fmt("revoked %s (restart a running daemon)\n", revokeId.c_str())); }
    if (doList) for (const auto& e : ts.list()) say(fmt("%s  %-7s  %-9s  %s\n", e.id.c_str(), e.role.c_str(), e.revoked ? "revoked" : (e.expires && e.expires < static_cast<long long>(std::time(nullptr)) ? "ended" : "active"), e.name.c_str()));
    return 0; }
  if (o.token.empty()) { o.token = am::Daemon::loadOrCreateToken(tokenFile, err); if (o.token.empty()) { say(fmt("amixos-daemon: %s\n", err.c_str())); return 1; } }
  am::Daemon d(o);
  if (!d.start(err)) { say(fmt("amixos-daemon: %s\n", err.c_str())); return 1; }
  if (!openBackend.empty()) { am::json::Value c = am::json::Value::object(); c.set("op", "open"); c.set("backend", openBackend); std::string e; d.control(c, e); if (!e.empty()) say(fmt("amixos-daemon: open %s: %s\n", openBackend.c_str(), e.c_str())); }
  say(fmt("AmF X1 Network listening on %s:%u (token in %s)%s\n", o.bind.c_str(), d.port(), tokenArg ? "--token" : tokenFile.c_str(), lan ? (o.localOnly ? " - LAN mode, local policy: private-network addresses only; unencrypted, put deploy/nginx in front for TLS" : " - LAN mode, ANY address: unencrypted, put deploy/nginx in front for TLS") : ""));
  while (!g_quit) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  d.stop(); say("off");
  return 0;
}
}  // namespace

AMFB_EXTENSION("AmF X1 Network", run)
