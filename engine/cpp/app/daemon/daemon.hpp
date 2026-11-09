// amixos-daemon: the A mix OS engine as a background service with a local JSON API (for the React Native remote, scripts, other programs).
//   GET  /api/status              name, version, engine state, blocks, xruns, uptime (no token)
//   GET  /api/devices             every device of every backend built in (no token)
//   GET  /api/state               the console: strips (name, gain, pan, mute, solo), buses, master, monitor (no token)
//   POST /api/control             {"op":..., ...} one change, X-Amix-Token required:
//        open {backend,in,out,rate,frames,inChannels,outChannels} | stop | gain {strip,db} | pan {strip,pan} | mute {strip,on} | solo {strip,on}
//        name {strip,name} | master {db} | limiter {on,ceilingDb} | send {strip,bus,db,pre} | busLevel {bus,db} | busMute {bus,on} | busReturn {bus,db}
//        eq {strip,band,type,freq,q,gainDb} | delay {strip,ms} | polarity {strip,on} | compressor {strip,on,thresholdDb,ratio,kneeDb,attackMs,releaseMs,makeupDb}
//        gate {strip,on,thresholdDb,rangeDb,attackMs,holdMs,releaseMs} | monitor {source,db,dim,mono,mute} | clearClips
//   WS   /ws/meters?token=...     a JSON frame 20 times a second: strips [{pk,rms,hold,clip,gr,gate,in}], master {l,r,rms,clip}, loudness, correlation, blocks, xruns
// Token: ~/.amixos/daemon.token (created on first start, 0600, 32 random bytes as hex); --token-file / --token override it.
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

namespace am {

class Daemon {
 public:
  struct Options { std::string bind = "127.0.0.1"; unsigned short port = 8765; std::string token; int strips = 32; std::string version = "0.1.0"; };
  explicit Daemon(Options o);
  ~Daemon();
  bool start(std::string& err);
  void stop();
  unsigned short port() const { return http_.port(); }
  const std::string& token() const { return opt_.token; }
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
  Options opt_; httpd::Server http_;
  mutable std::mutex m_; std::unique_ptr<Runtime> rt_; std::string backend_, lastError_; std::vector<Strip> strips_; Bus buses_[kMaxBuses]; double masterDb_ = 0; bool limiter_ = true; double ceiling_ = -0.5;
  std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now(); std::atomic<bool> running_{false};
};

}  // namespace am
