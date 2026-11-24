// amixos-daemon: the A mix OS engine as a background service with a local JSON API. See daemon.hpp for the routes.
//   amixos-daemon [--port 8765] [--allow-lan] [--token-file PATH | --token T] [--strips 32] [--open BACKEND [--out ID] [--in ID|default|-]
//                 [--rate HZ] [--frames N] [--in-channels N] [--out-channels N]] [--no-feedback-guard]
// --open takes the sound card the console had: the console passes its exact backend, output, input, rate and buffer (Remote::openArgs).
// Without --in NO input is opened: the system's default input is the microphone, and a microphone opened by itself next to the speakers howls.
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
#include "vst3/vst3_host.hpp"
#include "vst2/vst2_host.hpp"
#include "fx/fx_plugin.hpp"

namespace {
int run(int argc, char** argv, std::atomic<bool>& g_quit, std::function<void(const std::string&)> say) {
  using amfb_ext::fmt;
  am::Daemon::Options o;
  // THE CONSOLE'S PLUG-INS: the same loaders the console uses, so its racks keep sounding while the service plays
  o.pluginFactory = [](const std::string& kind, double rate, int block, std::string& err) -> std::unique_ptr<am::host::Plugin> {
    if (kind.rfind("fx:", 0) == 0) { auto p = am::fx::FxPlugin::make(kind.substr(3), rate); if (!p) err = "unknown effect " + kind.substr(3); return p; }
    const bool v3 = kind.rfind("vst3:", 0) == 0; if (!v3 && kind.rfind("vst2:", 0) != 0) { err = "unknown plug-in kind"; return nullptr; }
    std::string k = kind.substr(5), module = k, cid; const auto bar = k.rfind('|'); if (bar != std::string::npos) { module = k.substr(0, bar); cid = k.substr(bar + 1); }
    if (v3) return am::vst3::Plugin::load(module, cid, rate, block, err);
    return am::vst2::Plugin::load(module, rate, block, err); };
  std::string tokenFile = am::paths::file("daemon.token"), openBackend, openIn = "-", openOut; bool lan = false, tokenArg = false; double openRate = 0; int openFrames = 0, openInCh = -1, openOutCh = -1;
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
    else if (a == "--in") openIn = next();   // the input --open takes: "-" none (the default), "default" the system's default input, "loopback:system" the computer's sound, a device id
    else if (a == "--out") openOut = next();   // the output --open takes ("" the system's default)
    else if (a == "--rate") openRate = std::atof(next().c_str());
    else if (a == "--frames") openFrames = std::atoi(next().c_str());
    else if (a == "--in-channels") openInCh = std::atoi(next().c_str());
    else if (a == "--out-channels") openOutCh = std::atoi(next().c_str());
    else if (a == "--no-feedback-guard") o.feedbackGuard = false;
    else if (a == "--version") o.version = next();
    else if (a == "--seed") o.seed = next();   // the console's mixer to start from (the console writes remote-seed.json)
    else if (a == "--help" || a == "-h") { say(fmt("AmF X1 Network (amixos-daemon) [--port 8765] [--allow-lan] [--allow-any-address] [--bind ADDR] [--token-file PATH | --token T] [--strips 32] [--open BACKEND [--out ID] [--in ID|default|-] [--rate HZ] [--frames N] [--in-channels N] [--out-channels N]] [--no-feedback-guard]\n  --open opens no input unless --in names one.\n  tokens: --new-token NAME [--role control|view] [--days N] | --list-tokens | --revoke-token ID  [--tokens-file PATH]\n  --allow-lan: phones on the same network may connect (local policy: private addresses only; the token is still needed; not encrypted).\n")); return 0; }
    else { say(fmt("unknown option %s\n", a.c_str())); return 2; }
  }
  if (lan) o.bind = "0.0.0.0";
  std::string err;
  o.tokenStore = tokensFile; o.consoleInfo = am::paths::file("remote-console.json"); o.padsFile = am::paths::file("pads.json");
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
  if (!openBackend.empty()) {
    // --in: the input to open ("-" none, "default" the system's default input, "loopback:system" the computer's sound, an id); without it
    // none: the system's default input is the microphone, and opened by itself next to the speakers it howled. An input that does not
    // open falls back to no input: the outputs still start.
    am::Daemon::OpenArgs oa; oa.backend = openBackend; oa.in = openIn; oa.out = openOut; oa.rate = openRate; oa.frames = openFrames; oa.inChannels = openInCh; oa.outChannels = openOutCh;
    am::json::Value c = am::Daemon::openCommand(oa);
    std::string e; d.control(c, e);
    if (!e.empty() && !openIn.empty() && openIn != "-") { say(fmt("amixos-daemon: input %s did not open (%s): outputs only\n", openIn.c_str(), e.c_str())); e.clear(); c.set("in", std::string()); c.set("inChannels", 0); d.control(c, e); }
    if (!e.empty()) say(fmt("amixos-daemon: open %s: %s\n", openBackend.c_str(), e.c_str())); }
  say(fmt("AmF X1 Network listening on %s:%u (token in %s)%s\n", o.bind.c_str(), d.port(), tokenArg ? "--token" : tokenFile.c_str(), lan ? (o.localOnly ? " - LAN mode, local policy: private-network addresses only; unencrypted, put deploy/nginx in front for TLS" : " - LAN mode, ANY address: unencrypted, put deploy/nginx in front for TLS") : ""));
  while (!g_quit) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  d.stop(); say("off");
  return 0;
}
}  // namespace

AMFB_EXTENSION("AmF X1 Network", run)
