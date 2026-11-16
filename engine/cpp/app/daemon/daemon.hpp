// amixos-daemon: the A mix OS engine as a background service with a local JSON API (for the React Native remote, scripts, other programs).
//   GET  /api/status              name, version, consoleId (stable, for phones that look for it), engine state, blocks, xruns, uptime (no token)
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
//   POST /api/pair                {"pin":"123456","name":..} the minute's PIN for a control token of its own (no token needed)
//   GET  /api/links               the phones and browsers connected now: [{id, tokenId, name, role, ip, since, seconds}] (master token)
//   POST /api/links/kick          {"id":N} disconnect one (a named pairing is then refused for 2 minutes) (master token)
//   WS   /ws/meters?token=...     a JSON frame 20 times a second: strips [{pk,rms,hold,clip,gr,gate,in}], master {l,r,rms,clip}, loudness, correlation, blocks, xruns
// Token: <data>/daemon.token (am::paths: %APPDATA%\AMFB Audio\AmF X1 on Windows, the program's data folder on Linux, ...) (created on first start, 0600, 32 random bytes as hex); --token-file / --token override it.
// Binds 127.0.0.1 by default. --allow-lan binds 0.0.0.0 for a phone on the same network: the token still protects control, but nothing is
// encrypted on the wire; put the nginx TLS front (deploy/nginx) in front of it for anything beyond a trusted home / studio network.
#pragma once
#include <functional>
#include <map>
#include <thread>
#include <memory>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "plugin/plugin.hpp"
#include "rta.hpp"
#include "pads.hpp"
#include "audiomixer/master_chain.hpp"
#include "httpd.hpp"
#include "json.hpp"
#include "tokens.hpp"
#include "record/recorder.hpp"

namespace am {

class Daemon {
 public:
  // PLUG-INS: makes one plug-in from a rack slot's kind ("vst3:<module>|<class>", "vst2:<module>", "fx:<effect>") at the engine's rate;
  // the network extension gives the real loaders, tests a stand-in. Empty: racks are listed but not played.
  using PluginFactory = std::function<std::unique_ptr<host::Plugin>(const std::string& kind, double rate, int maxBlock, std::string& err)>;
  struct Options { PluginFactory pluginFactory; std::string bind = "127.0.0.1"; unsigned short port = 8765; std::string token; int strips = 32; std::string version = "0.1.0"; bool localOnly = true; std::string tokenStore; std::string consoleInfo; std::string plan; std::string seed; };   // seed: the console's mixer (remote-seed.json) loaded at start, so the phones begin where the console was   // plan: the console's licence plan name (BASIC / PRO / ...), shown to the phone   // consoleInfo: the console's plug-in / rack snapshot (remote-console.json), served read-only   // tokenStore: the file of the named tokens ("" = only the master token)   // localOnly: the local policy (private-network peers only)
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
  json::Value control(const json::Value& cmd, std::string& err);   // a change that worked moves stateVersion() on
  // LIVE SYNC: every change from any remote (phone, browser, the console) moves this counter; /ws/meters sends {"state":{...}} to every
  // watcher as soon as it moves (and once on connect), and GET /api/state?since=N answers {"v":N,"same":true} while nothing changed
  unsigned long long stateVersion() const { return version_.load(); }
  int remotes() const { return watchers_.load(); }   // phones and browsers watching now (open /ws/meters streams)
  // CONNECTED PHONES: every open live link (/ws/meters) with the pairing it uses (tokenId, "master" for the master token), the device's
  // name (the pairing's name: the phone's model when it paired with the PIN), its address and since when. GET /api/links (master token).
  json::Value links() const;
  // DISCONNECT (POST /api/links/kick {"id":N}, master token): the link closes within 50 ms with {"kicked":...}; a named pairing is then
  // held for kHoldSeconds (its token is refused) so the phone does not come straight back. Revoking a pairing closes its links too.
  bool kick(int id); int kickToken(const std::string& tokenId, bool hold);
  static constexpr int kHoldSeconds = 120;
  // THE CONSOLE'S PLUG-INS while the service plays (the console hands its racks over in the seed: kind, name, bypass, state):
  //   GET /api/racks                every rack: [{strip, name, slots:[{slot, kind, name, vendor, format, bypass, loaded, error}]}], "v"
  //   GET /api/racks?strip=N        that strip's slots with their parameters [{id, title, value 0..1, default, text, steps}]
  //   GET /api/racks?states=1       with each plug-in's state (master token: the console takes the phones' changes back)
  //   POST /api/control {"op":"plugin","strip":N,"slot":K, "bypass":true|false | "param":ID,"value":0..1}
  json::Value racks(int strip = -1, bool params = false, bool states = false) const;
  unsigned long long racksVersion() const { return racksV_.load(); }
  // RTA (the spectrum for the remotes' EQ pages): POST {"op":"rta","src":-1 (master) | strip | "off"}; while on (a remote renews it every
  // few seconds, else it stops by itself after 20 s) /ws/meters also carries {"rta":{"src","db":[..]}} ten times a second, and GET /api/rta
  // answers {"src","f":[band centres],"db":[..]}.
  // RTA EQ (the console's program EQ on the master, 10 bands at the ISO octaves, and its mastering chain, handed over in the seed):
  //   POST {"op":"programEq","gains":[10 dB] (-24..24),"on":true|false}; the state has "programEq":{"on","gains"} and "masterV".
  json::Value rtaJson(bool withCentres) const;
  static constexpr double kIso[10] = {31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
  // CONSOLE SETTINGS over the remote (POST /api/control {"op":"setting","key":K,"value":V}), applied to the sound while the service runs
  // it, reported in the state as "settings" (and kept in step with the console): width 0..2, outputBits 0 (float) / 16 / 24 (-1 auto =
  // float here), dcBlock, analog (DSP MODE ANALOG on every channel), autoLevel / autoTarget (-30..-6 LUFS) / autoMax (0..24 dB), clipDb
  // (-12..0), peakHoldMs (0..10000), winGain (GAIN MODE: the Windows volume trims the master), winMuteSync (the console's rule: fader at
  // -inf mutes Windows), winVolume (0..100: the Windows output slider of this computer).
  static double trimForLevel(double level01);   // GAIN MODE: 30*log10(level), 0 -> -120 (off); the console uses the same curve
  void setVolumeBackend(std::function<bool(float&)> get, std::function<bool(float)> set) { volGet_ = std::move(get); volSet_ = std::move(set); }   // tests
  void pollVolume();                            // reads the Windows slider once (the watcher does it twice a second)
  bool loadSeed(const std::string& file, std::string& err);   // {"strips":[{name,db,pan,mute,solo}],"master":{"db"}}
  json::Value meters() const;
  // SECURITY: which web pages may read the replies (only pages served from this computer), and which Host a request may name while the
  // local policy is on (an address or localhost: a made-up domain pointed at 127.0.0.1 by a hostile page is refused)
  // PAIRING PIN: six digits that change every minute, made from the master token and the minute (so the console can show the same PIN
  // without asking the service). A phone sends it once to POST /api/pair {"pin":..,"name":..} and gets its own control token (30 days,
  // tied to its address). The PIN of the minute before is still taken; five wrong PINs close pairing until the next minute.
  static std::string pinFor(const std::string& masterToken, long long unixSeconds);
  static bool originAllowed(const std::string& origin);
  static bool hostAllowed(const std::string& host);
  static std::string randomToken();
  static std::string computerName();   // this computer's name (GetComputerName / gethostname), printable, at most 40 characters
  static std::string loadOrCreateToken(const std::string& file, std::string& err);   // 0600; created when missing

 private:
  // a strip: fader, pan, mute, solo; its input GAIN (trim, dB) and the console's analog 3-band channel EQ (LOW shelf 100 Hz, MID bell
  // 100 Hz .. 8 kHz, HIGH shelf 10 kHz, ±15 dB each, the engine's bands 10..12), kept here so every remote shows them
  struct Strip { std::string name; double db = 0, pan = 0; bool mute = false, solo = false; double trim = 0, low = 0, mid = 0, midHz = 1000, high = 0; bool toneOn = true;
    struct Band { int type = 0; double freq = 1000, q = 0.707, gain = 0; };   // the channel's parametric EQ, bands 0..7 (type: dsp::FilterType)
    Band eq[8]; };
  void pushTone(Runtime& rt, int i) const;
  struct Bus { double db = 0, returnDb = -60; bool mute = false; };
  struct Monitor { int source = 0; double db = 0; bool dim = false, mono = false, mute = false; } mon_;
  httpd::Response handle(const httpd::Request& r);
  json::Value apply(const json::Value& cmd, std::string& err);
  std::atomic<unsigned long long> version_{1}; std::atomic<int> watchers_{0};
  struct Link { int id = 0; std::string tokenId, name, role, ip; long long since = 0; std::atomic<bool> kick{false}; };
  mutable std::mutex linksM_; std::map<int, std::shared_ptr<Link>> links_; int nextLink_ = 1; std::map<std::string, long long> held_;   // tokenId -> until (unix s)
  bool held(const std::string& tokenId) const;
  Pads pads_;   // the console's SAMPLE PADS while the service plays (op sample)
  DeviceConfig lastCfg_;   // the device opened last (ENGINE CONTROL on the phones reopens it at another rate / buffer)
  // ROUTING, kept here so the phones show it and a fresh device gets it: per strip the device input it reads (-1 none), per output its source
  int inRoute_[kMaxStrips] = {}; int outRoute_[Runtime::kMaxOutputs] = {}; bool routesInit_ = false; void initRoutes();
  json::Value routingJson() const;
  Rta rta_; std::atomic<long long> rtaUntil_{0}; MasterChain master_; MasterChain::Params mp_; std::atomic<unsigned long long> masterV_{1};
  json::Value programEqJson() const;
  struct PlugSlot { int slot = 0; std::string kind, name, state, error; bool bypass = false; std::shared_ptr<host::Plugin> pl; };
  std::map<int, std::vector<PlugSlot>> racks_; std::map<int, std::string> rackNames_; std::atomic<unsigned long long> racksV_{1};
  void loadRacks(Runtime& rt);   // every slot made at the runtime's rate, the chains handed to it (m_ held)
  void loadRack(Runtime& rt, int strip);   // the same for one strip
  bool racksFromConsole_ = false;          // the last rack change came from the console itself (op rack): it does not take it back
  struct Prefs { double width = 1.0, autoTarget = -16, autoMax = 12, clipDb = -0.1, peakHoldMs = 1500; int outputBits = 0; bool dcBlock = true, analog = false, autoLevel = false, winGain = false, winMuteSync = true; } set_;
  std::atomic<int> winVol_{-1}; std::function<bool(float&)> volGet_; std::function<bool(float)> volSet_; std::thread volWatch_;
  double outTrim() const;    // GAIN MODE's listening trim, dB (0 = none, -120 = silent)
  void applyMaster(Runtime& rt) const;   // the master fader and the trim to the engine (m_ held)
  bool windowsSetsLevel() const;
  void pushSettings(Runtime& rt) const;
  json::Value settingsJson() const;
  bool authorised(const httpd::Request& r) const;
  Options opt_; mutable TokenStore tokens_; httpd::Server http_;
  bool noteRefused();   // a wrong token: counted and slowed down; true when there were too many in the last minute
  std::atomic<int> refused_{0}, refusedNow_{0}; std::atomic<long long> refusedSince_{0};
  std::atomic<int> pairFails_{0}; std::atomic<long long> pairMinute_{0};
  rec::Recorder rec_; double rate_ = 48000;   // RECORD from the remote: the master to <data>/recordings (the folder is fixed: a remote never names a path)
  mutable std::mutex m_; std::unique_ptr<Runtime> rt_; std::string backend_, lastError_; std::vector<Strip> strips_; Bus buses_[kMaxBuses]; double masterDb_ = 0; bool limiter_ = true; double ceiling_ = -0.5;
  std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now(); std::atomic<bool> running_{false};
};

}  // namespace am
