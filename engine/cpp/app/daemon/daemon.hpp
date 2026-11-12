// amixos-daemon: the A mix OS engine as a background service with a local JSON API (for the React Native remote, scripts, other programs).
//   GET  /api/status              name, version, engine state, blocks, xruns, uptime (no token)
//   GET  /api/devices             every device of every backend built in (no token)
//   GET  /api/state               the console: strips (name, gain, pan, mute, solo), buses, master, monitor (no token)
//   POST /api/control             {"op":..., ...} one change, X-Amix-Token required:
//        open {backend,in,out,rate,frames,inChannels,outChannels} | stop | gain {strip,db} | pan {strip,pan} | mute {strip,on} | solo {strip,on}
//        name {strip,name} | master {db} | limiter {on,ceilingDb} | send {strip,bus,db,pre} | busLevel {bus,db} | busMute {bus,on} | busReturn {bus,db}
//        eq {strip,band,type,freq,q,gainDb} | delay {strip,ms} | polarity {strip,on} | compressor {strip,on,thresholdDb,ratio,kneeDb,attackMs,releaseMs,makeupDb}
//        gate {strip,on,thresholdDb,rangeDb,attackMs,holdMs,releaseMs} | monitor {source,db,dim,mono,mute} | inRoute {strip,input} | outRoute {out,source} | trim {strip,db} | clearClips
//   GET  /api/tokens              the named tokens, without their secrets (master token)
//   POST /api/tokens              {"name":..,"role":"control"|"view","days":N} makes one and returns it once (master token)
//   POST /api/tokens/revoke       {"id":..} (master token)
//   WS   /ws/meters?token=...     a JSON frame 20 times a second: strips [{pk,rms,hold,clip,gr,gate,in}], master {l,r,rms,clip}, loudness, correlation, blocks, xruns
// Token: <data>/daemon.token (am::paths: %APPDATA%\AMFB Audio\AmF X1 on Windows, the program's data folder on Linux, ...) (created on first start, 0600, 32 random bytes as hex); --token-file / --token override it.
// Binds 127.0.0.1 by default. --allow-lan binds 0.0.0.0 for a phone on the same network: the token still protects control, but nothing is
// encrypted on the wire; put the nginx TLS front (deploy/nginx) in front of it for anything beyond a trusted home / studio network.
#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "httpd.hpp"
#include "json.hpp"
#include "tokens.hpp"

namespace am {

class Daemon {
 public:
  struct Options { std::string bind = "127.0.0.1"; unsigned short port = 8765; std::string token; int strips = 32; std::string version = "0.1.0"; bool localOnly = true; std::string tokenStore; };   // tokenStore: the file of the named tokens ("" = only the master token)   // localOnly: the local policy (private-network peers only)
  explicit Daemon(Options o);
  ~Daemon();
  bool start(std::string& err);
  void stop();
  unsigned short port() const { return http_.port(); }
  const std::string& token() const { return opt_.token; }
  TokenStore& tokens() { return tokens_; }
  // "admin" (the master token), "control", "view" or "" for what a request carries
  std::string roleOf(const httpd::Request& r, bool allowQuery = true) const;   // allowQuery false: the header only (a token in a URL ends up in logs and histories)
  int refusedTokens() const { return refused_.load(); }
  // the API itself, also callable without HTTP (tests)
  json::Value status() const;
  json::Value devices() const;
  json::Value state() const;
  json::Value control(const json::Value& cmd, std::string& err);
  json::Value meters() const;
  static std::string randomToken();
  static std::string loadOrCreateToken(const std::string& file, std::string& err);   // 0600; created when missing

 private:
  struct Strip { std::string name; double db = 0, pan = 0; bool mute = false, solo = false; };
  struct Bus { double db = 0, returnDb = -60; bool mute = false; };
  struct Monitor { int source = 0; double db = 0; bool dim = false, mono = false, mute = false; } mon_;
  httpd::Response handle(const httpd::Request& r);
  bool authorised(const httpd::Request& r) const;
  Options opt_; TokenStore tokens_; httpd::Server http_;
  bool noteRefused();   // a wrong token: counted and slowed down; true when there were too many in the last minute
  std::atomic<int> refused_{0}, refusedNow_{0}; std::atomic<long long> refusedSince_{0};
  mutable std::mutex m_; std::unique_ptr<Runtime> rt_; std::string backend_, lastError_; std::vector<Strip> strips_; Bus buses_[kMaxBuses]; double masterDb_ = 0; bool limiter_ = true; double ceiling_ = -0.5;
  std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now(); std::atomic<bool> running_{false};
};

}  // namespace am
