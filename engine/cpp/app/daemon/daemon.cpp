// amixos-daemon: see daemon.hpp. The console model (names, gains, sends ...) lives here so that /api/state can answer without a round trip
// to the audio thread; every change goes to the Runtime's lock-free control ring and is mirrored in the model under m_.
#include "daemon.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <thread>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace am {

namespace {
double clampDb(double db, double lo = -120, double hi = 24) { return db < lo ? lo : db > hi ? hi : db; }
json::Value num(double v) { return json::Value(v); }
}  // namespace

Daemon::Daemon(Options o) : opt_(std::move(o)) {
  if (opt_.strips < 1) opt_.strips = 1;
  if (opt_.strips > kMaxStrips) opt_.strips = kMaxStrips;
  strips_.resize(static_cast<std::size_t>(opt_.strips));
  for (std::size_t i = 0; i < strips_.size(); ++i) strips_[i].name = "CH " + std::to_string(i + 1);
}
Daemon::~Daemon() { stop(); }

std::string Daemon::randomToken() {
  std::random_device rd; std::string out; static const char* hex = "0123456789abcdef";
  for (int i = 0; i < 8; ++i) { const std::uint32_t v = rd(); for (int k = 0; k < 8; ++k) out += hex[(v >> (4 * k)) & 15]; }
  return out;   // 32 random bytes as 64 hex characters
}

std::string Daemon::loadOrCreateToken(const std::string& file, std::string& err) {
  { std::ifstream in(file); std::string t; if (in && std::getline(in, t)) { while (!t.empty() && (t.back() == '\r' || t.back() == ' ')) t.pop_back(); if (t.size() >= 32) return t; } }
  const std::string t = randomToken();
  const std::size_t slash = file.find_last_of("/\\");
  if (slash != std::string::npos) {
    const std::string dir = file.substr(0, slash);
#ifdef _WIN32
    CreateDirectoryA(dir.c_str(), nullptr);
#else
    ::mkdir(dir.c_str(), 0700);
#endif
  }
#ifdef _WIN32
  FILE* f = std::fopen(file.c_str(), "wb");
  if (!f) { err = "cannot write " + file; return ""; }
  std::fputs(t.c_str(), f); std::fputc('\n', f); std::fclose(f);
#else
  const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);   // owner only: the token is the key to the console
  if (fd < 0) { err = "cannot write " + file; return ""; }
  const std::string line = t + "\n"; const auto w = ::write(fd, line.data(), line.size()); (void)w; ::close(fd); ::chmod(file.c_str(), 0600);
#endif
  return t;
}

bool Daemon::start(std::string& err) {
  if (opt_.token.empty()) { err = "no token: pass --token or --token-file"; return false; }
  if (opt_.token.size() < 12) { err = "the token is too short: 12 characters at least (the daemon makes a 64-character one by itself)"; return false; }
  if (!opt_.tokenStore.empty()) { tokens_.setFile(opt_.tokenStore); if (!tokens_.load(err)) return false; }
  http_.route([this](const httpd::Request& r) { return handle(r); });
  http_.websocket("/ws/meters", [this](const httpd::Request& r, httpd::WebSocket& ws) {
    if (roleOf(r).empty()) { ws.send("{\"error\":\"unauthorised\"}"); ws.close(); return; }   // any valid token may watch the meters
    std::thread reader([&ws] { std::string t; int op = 0; while (ws.open() && ws.read(t, op)) {} });   // drains pings / close from the phone
    while (running_ && ws.open()) { if (!ws.send(meters().dump())) break; std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    ws.close(); reader.join();
  });
  http_.setLocalOnly(opt_.localOnly);
  if (!http_.listen(opt_.bind, opt_.port, err)) return false;
  running_ = true; http_.start();
  return true;
}

void Daemon::stop() {
  if (!running_.exchange(false)) return;
  http_.stop();
  std::lock_guard<std::mutex> g(m_);
  if (rt_) { rt_->stop(); rt_.reset(); }
}

// SECURITY: a wrong token costs 100 ms and is counted; more than 20 in a minute and further wrong ones get 429 (a right token is never
// held back, so a noisy guesser cannot lock the owner out). A token of 256 random bits cannot be guessed; this keeps the log honest and
// a flood cheap to refuse.
bool Daemon::noteRefused() {
  const long long now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if (now - refusedSince_.load() >= 60) { refusedSince_ = now; refusedNow_ = 0; }
  const int n = ++refusedNow_; ++refused_;
  if (n == 21) std::fprintf(stderr, "amixos-daemon: more than 20 wrong tokens in a minute: further wrong ones are refused with 429\n");
  if (n <= 20) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return n > 20;
}
std::string Daemon::roleOf(const httpd::Request& r, bool allowQuery) const {
  std::string t = r.header("x-amix-token"); if (t.empty() && allowQuery) t = r.param("token"); if (t.empty()) return "";
  if (TokenStore::same(t, opt_.token)) return "admin";
  return tokens_.roleOf(t);
}
bool Daemon::authorised(const httpd::Request& r) const { const std::string role = roleOf(r, false); return role == "admin" || role == "control"; }

bool Daemon::originAllowed(const std::string& o) {
  for (const char* p : {"http://127.0.0.1", "http://localhost", "http://[::1]", "https://127.0.0.1", "https://localhost"}) { const std::size_t n = std::strlen(p); if (o.compare(0, n, p) == 0 && (o.size() == n || o[n] == ':')) return true; }
  return false;
}
bool Daemon::hostAllowed(const std::string& hostHeader) {
  std::string h = hostHeader; if (h.empty()) return true;   // HTTP/1.0 clients send none
  if (h[0] == '[') return h.find(']') != std::string::npos;   // an IPv6 literal
  const std::size_t c = h.rfind(':'); if (c != std::string::npos) h.resize(c);
  if (h == "localhost") return true;
  int dots = 0, digits = 0; for (char ch : h) { if (ch == '.') { if (!digits) return false; ++dots; digits = 0; } else if (ch >= '0' && ch <= '9') { if (++digits > 3) return false; } else return false; }
  return dots == 3 && digits > 0;   // an IPv4 address: how a phone on the network names this computer
}
httpd::Response Daemon::handle(const httpd::Request& r) {
  httpd::Response res;
  res.headers.push_back({"Cache-Control", "no-store"});
  res.headers.push_back({"X-Content-Type-Options", "nosniff"}); res.headers.push_back({"X-Frame-Options", "DENY"}); res.headers.push_back({"Referrer-Policy", "no-referrer"});
  if (opt_.localOnly && !hostAllowed(r.header("host"))) { res.status = 403; res.body = "{\"ok\":false,\"error\":\"this host name is not served: use the address\"}"; return res; }
  const std::string origin = r.header("origin"); const bool cors = !origin.empty() && originAllowed(origin);   // the phone app is not a browser and sends no Origin; a page from elsewhere gets no permission to read
  if (cors) { res.headers.push_back({"Access-Control-Allow-Origin", origin}); res.headers.push_back({"Vary", "Origin"}); }
  if (r.method == "OPTIONS") { res.status = cors ? 204 : 403; if (cors) { res.headers.push_back({"Access-Control-Allow-Headers", "Content-Type, X-Amix-Token"}); res.headers.push_back({"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}); } return res; }
  if (r.method == "GET" && r.path == "/api/status") { res.body = status().dump(); return res; }
  if (r.method == "GET" && r.path == "/api/devices") { res.body = devices().dump(); return res; }
  if (r.method == "GET" && r.path == "/api/state") { res.body = state().dump(); return res; }
  if (r.method == "GET" && r.path == "/api/meters") { res.body = meters().dump(); return res; }
  if (r.path == "/api/tokens" || r.path == "/api/tokens/revoke") {   // the named tokens: only the master token manages them
    if (roleOf(r, false) != "admin") { const bool known = !roleOf(r, false).empty(); res.status = known ? 403 : (noteRefused() ? 429 : 401); res.body = "{\"ok\":false,\"error\":\"the master token is needed to manage tokens\"}"; return res; }
    if (opt_.tokenStore.empty()) { res.status = 400; res.body = "{\"ok\":false,\"error\":\"this daemon was started without a token file\"}"; return res; }
    json::Value out = json::Value::object(); std::string terr;
    if (r.method == "GET" && r.path == "/api/tokens") {
      json::Value arr = json::Value::array();
      for (const auto& e : tokens_.list()) { json::Value t = json::Value::object(); t.set("id", e.id); t.set("name", e.name); t.set("role", e.role); t.set("created", e.created); t.set("expires", e.expires); t.set("revoked", e.revoked); arr.push(t); }
      out.set("ok", true); out.set("tokens", arr); res.body = out.dump(); return res; }
    bool ok = false; const json::Value b = json::Value::parse(r.body, ok);
    if (r.method != "POST" || !ok || !b.isObject()) { res.status = 400; res.body = "{\"ok\":false,\"error\":\"the body is not a JSON object\"}"; return res; }
    if (r.path == "/api/tokens") { std::string id; const std::string t = tokens_.issue(b["name"].asString(), b["role"].asString("control"), b["days"].asInt(0), id, terr); if (!t.empty()) { out.set("ok", true); out.set("id", id); out.set("token", t); } }
    else if (tokens_.revoke(b["id"].asString(), terr)) out.set("ok", true);
    if (!terr.empty()) { res.status = 400; out.set("ok", false); out.set("error", terr); }
    res.body = out.dump(); return res; }
  if (r.method == "POST" && r.path == "/api/control") {
    if (!authorised(r)) { res.status = roleOf(r, false) == "view" ? 403 : (noteRefused() ? 429 : 401); res.body = "{\"ok\":false,\"error\":\"X-Amix-Token missing or wrong\"}"; return res; }
    bool ok = false; const json::Value cmd = json::Value::parse(r.body, ok);
    if (!ok || !cmd.isObject()) { res.status = 400; res.body = "{\"ok\":false,\"error\":\"the body is not a JSON object\"}"; return res; }
    std::string err; json::Value out = control(cmd, err);
    if (!err.empty()) { res.status = 400; json::Value e = json::Value::object(); e.set("ok", false); e.set("error", err); res.body = e.dump(); return res; }
    res.body = out.dump(); return res;
  }
  if (r.method == "GET" && (r.path == "/" || r.path == "/api")) {
    res.body = "{\"name\":\"amixos-daemon\",\"api\":[\"GET /api/status\",\"GET /api/devices\",\"GET /api/state\",\"GET /api/meters\",\"POST /api/control (X-Amix-Token)\",\"GET /api/tokens\",\"POST /api/tokens\",\"POST /api/tokens/revoke\",\"WS /ws/meters?token=\"]}"; return res;
  }
  res.status = 404; res.body = "{\"ok\":false,\"error\":\"not found\"}"; return res;
}

json::Value Daemon::status() const {
  std::lock_guard<std::mutex> g(m_);
  json::Value o = json::Value::object();
  o.set("name", "A mix OS"); o.set("version", opt_.version); o.set("daemon", "amixos-daemon");
  o.set("backend", rt_ ? backend_ : ""); o.set("running", rt_ && rt_->running());
  if (rt_) { const Snapshot s = rt_->snapshot(); o.set("blocks", static_cast<double>(s.blocks)); o.set("frames", static_cast<double>(s.frames)); o.set("xruns", static_cast<double>(s.xruns)); }
  else { o.set("blocks", 0); o.set("frames", 0); o.set("xruns", 0); }
  o.set("strips", opt_.strips); o.set("buses", kMaxBuses);
  o.set("uptimeS", std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count());
  if (!lastError_.empty()) o.set("lastError", lastError_);
  json::Value b = json::Value::array(); for (const auto& n : availableBackends()) b.push(n); o.set("backends", b);
  return o;
}

json::Value Daemon::devices() const {
  json::Value a = json::Value::array();
  for (const auto& d : listDevices()) {
    json::Value o = json::Value::object();
    o.set("backend", d.backend); o.set("id", d.id); o.set("name", d.name); o.set("in", d.inChannels); o.set("out", d.outChannels); o.set("defaultRate", d.defaultRate);
    o.set("defaultIn", d.defaultIn); o.set("defaultOut", d.defaultOut);
    json::Value r = json::Value::array(); for (double x : d.sampleRates) r.push(x); o.set("sampleRates", r);
    a.push(o);
  }
  return a;
}

json::Value Daemon::state() const {
  std::lock_guard<std::mutex> g(m_);
  json::Value o = json::Value::object();
  json::Value strips = json::Value::array();
  for (std::size_t i = 0; i < strips_.size(); ++i) {
    const Strip& s = strips_[i]; json::Value v = json::Value::object();
    v.set("id", static_cast<int>(i)); v.set("name", s.name); v.set("db", s.db); v.set("pan", s.pan); v.set("mute", s.mute); v.set("solo", s.solo);
    strips.push(v);
  }
  o.set("strips", strips);
  json::Value buses = json::Value::array();
  for (int k = 0; k < kMaxBuses; ++k) { json::Value v = json::Value::object(); v.set("id", k); v.set("db", buses_[k].db); v.set("returnDb", buses_[k].returnDb); v.set("mute", buses_[k].mute); buses.push(v); }
  o.set("buses", buses);
  json::Value m = json::Value::object(); m.set("db", masterDb_); m.set("limiter", limiter_); m.set("ceilingDb", ceiling_); o.set("master", m);
  json::Value mo = json::Value::object(); mo.set("source", mon_.source); mo.set("db", mon_.db); mo.set("dim", mon_.dim); mo.set("mono", mon_.mono); mo.set("mute", mon_.mute); o.set("monitor", mo);
  o.set("backend", rt_ ? backend_ : ""); o.set("running", rt_ && rt_->running());
  return o;
}

json::Value Daemon::meters() const {
  std::lock_guard<std::mutex> g(m_);
  json::Value o = json::Value::object();
  if (!rt_) { o.set("running", false); return o; }
  const Snapshot s = rt_->snapshot();
  json::Value strips = json::Value::array();
  for (int i = 0; i < opt_.strips; ++i) {
    const auto& m = s.strips[i]; json::Value v = json::Value::object();
    v.set("pk", num(m.pk)); v.set("rms", num(m.rms)); v.set("hold", num(m.hold)); v.set("clip", static_cast<double>(m.clipN)); v.set("gr", num(m.compGrDb)); v.set("gate", num(m.gateDb)); v.set("in", num(m.inPk));
    strips.push(v);
  }
  o.set("strips", strips);
  json::Value master = json::Value::object();
  master.set("l", num(s.master[0].pk)); master.set("r", num(s.master[1].pk)); master.set("rms", num(0.5 * (s.master[0].rms + s.master[1].rms))); master.set("clip", static_cast<double>(s.master[0].clipN + s.master[1].clipN));
  o.set("master", master);
  json::Value buses = json::Value::array(); for (int k = 0; k < kMaxBuses; ++k) buses.push(num(s.buses[k].pk)); o.set("buses", buses);
  json::Value loud = json::Value::object(); loud.set("m", num(s.momentary)); loud.set("s", num(s.shortTerm)); loud.set("i", num(s.integrated)); loud.set("lra", num(s.lra)); loud.set("tp", num(s.truePeak)); o.set("loudness", loud);
  json::Value mon = json::Value::object(); mon.set("l", num(s.monitorPk[0])); mon.set("r", num(s.monitorPk[1])); o.set("monitor", mon);
  o.set("correlation", num(s.correlation)); o.set("blocks", static_cast<double>(s.blocks)); o.set("xruns", static_cast<double>(s.xruns)); o.set("running", rt_->running());
  return o;
}

json::Value Daemon::control(const json::Value& cmd, std::string& err) {
  std::lock_guard<std::mutex> g(m_);
  json::Value ok = json::Value::object(); ok.set("ok", true);
  const std::string op = cmd["op"].asString();
  auto strip = [&](int& s) { s = cmd["strip"].asInt(-1); if (s < 0 || s >= opt_.strips) { err = "strip out of range"; return false; } return true; };
  auto bus = [&](int& b) { b = cmd["bus"].asInt(-1); if (b < 0 || b >= kMaxBuses) { err = "bus out of range"; return false; } return true; };
  auto need = [&]() { if (!rt_) { err = "no device is open: send {\"op\":\"open\"} first"; return false; } return true; };
  int s = 0, b = 0;
  if (op == "open") {
    if (rt_) { rt_->stop(); rt_.reset(); }
    DeviceConfig cfg; cfg.backend = cmd["backend"].asString("auto"); cfg.inDevice = cmd["in"].asString(""); cfg.outDevice = cmd["out"].asString("");
    cfg.sampleRate = cmd["rate"].asNumber(48000); cfg.frames = static_cast<unsigned>(std::max(16, cmd["frames"].asInt(256)));
    cfg.inChannels = std::max(0, std::min(kMaxStrips, cmd["inChannels"].asInt(2))); cfg.outChannels = std::max(0, std::min(32, cmd["outChannels"].asInt(2))); cfg.exclusive = cmd["exclusive"].asBool(false);
    auto rt = std::make_unique<Runtime>(opt_.strips);
    if (!rt->open(cfg, err)) { lastError_ = err; return {}; }
    if (!rt->start(err)) { lastError_ = err; return {}; }
    // replay the console onto the fresh runtime so that what the phone shows is what sounds
    for (int i = 0; i < opt_.strips; ++i) { const Strip& st = strips_[static_cast<std::size_t>(i)]; rt->setGainDb(i, st.db); rt->setPan(i, st.pan); rt->setMute(i, st.mute); rt->setSolo(i, st.solo); }
    for (int k = 0; k < kMaxBuses; ++k) { rt->setBusLevel(k, buses_[k].db); rt->setBusMute(k, buses_[k].mute); rt->setBusReturn(k, buses_[k].returnDb); }
    rt->setMasterDb(masterDb_); rt->setLimiter(limiter_, ceiling_); rt->setMonitor(mon_.source, mon_.db, mon_.dim, mon_.mono, mon_.mute);
    rt_ = std::move(rt); backend_ = rt_->backendName(); lastError_.clear();
    ok.set("backend", backend_); ok.set("rate", cfg.sampleRate); ok.set("frames", static_cast<int>(cfg.frames)); return ok;
  }
  if (op == "stop") { if (rt_) { rt_->stop(); rt_.reset(); } backend_.clear(); return ok; }
  if (op == "gain") { if (!strip(s)) return {}; strips_[static_cast<std::size_t>(s)].db = clampDb(cmd["db"].asNumber(0)); if (rt_) rt_->setGainDb(s, strips_[static_cast<std::size_t>(s)].db); return ok; }
  if (op == "pan") { if (!strip(s)) return {}; double p = cmd["pan"].asNumber(0); p = p < -1 ? -1 : p > 1 ? 1 : p; strips_[static_cast<std::size_t>(s)].pan = p; if (rt_) rt_->setPan(s, p); return ok; }
  if (op == "mute") { if (!strip(s)) return {}; const bool on = cmd["on"].asBool(); strips_[static_cast<std::size_t>(s)].mute = on; if (rt_) rt_->setMute(s, on); return ok; }
  if (op == "solo") { if (!strip(s)) return {}; const bool on = cmd["on"].asBool(); strips_[static_cast<std::size_t>(s)].solo = on; if (rt_) rt_->setSolo(s, on); return ok; }
  if (op == "name") { if (!strip(s)) return {}; std::string n = cmd["name"].asString(); if (n.size() > 24) n.resize(24); if (n.empty()) { err = "empty name"; return {}; } strips_[static_cast<std::size_t>(s)].name = n; return ok; }
  if (op == "master") { masterDb_ = clampDb(cmd["db"].asNumber(0)); if (rt_) rt_->setMasterDb(masterDb_); return ok; }
  if (op == "limiter") { limiter_ = cmd["on"].asBool(true); ceiling_ = clampDb(cmd["ceilingDb"].asNumber(-0.5), -20, 0); if (rt_) rt_->setLimiter(limiter_, ceiling_); return ok; }
  if (op == "send") { if (!strip(s) || !bus(b)) return {}; if (!need()) return {}; rt_->setSend(s, b, clampDb(cmd["db"].asNumber(-120)), cmd["pre"].asBool(false)); return ok; }
  if (op == "busLevel") { if (!bus(b)) return {}; buses_[b].db = clampDb(cmd["db"].asNumber(0)); if (rt_) rt_->setBusLevel(b, buses_[b].db); return ok; }
  if (op == "busMute") { if (!bus(b)) return {}; buses_[b].mute = cmd["on"].asBool(); if (rt_) rt_->setBusMute(b, buses_[b].mute); return ok; }
  if (op == "busReturn") { if (!bus(b)) return {}; buses_[b].returnDb = clampDb(cmd["db"].asNumber(-120)); if (rt_) rt_->setBusReturn(b, buses_[b].returnDb); return ok; }
  if (op == "eq") {
    if (!strip(s) || !need()) return {};
    const int band = cmd["band"].asInt(-1); if (band < 0 || band > 7) { err = "band out of range"; return {}; }
    static const char* names[] = {"off", "peaking", "lowshelf", "highshelf", "lowpass", "highpass", "notch", "allpass1", "allpass2"};
    const std::string t = cmd["type"].asString("peaking"); int ti = -1; for (int i = 0; i < 9; ++i) if (t == names[i]) ti = i;
    if (ti < 0) { err = "unknown filter type"; return {}; }
    rt_->setEqBand(s, band, static_cast<dsp::FilterType>(ti), cmd["freq"].asNumber(1000), std::max(0.05, cmd["q"].asNumber(0.707)), clampDb(cmd["gainDb"].asNumber(0), -24, 24)); return ok;
  }
  if (op == "delay") { if (!strip(s) || !need()) return {}; rt_->setDelayMs(s, std::max(0.0, std::min(500.0, cmd["ms"].asNumber(0)))); return ok; }
  if (op == "polarity") { if (!strip(s) || !need()) return {}; rt_->setPolarity(s, cmd["on"].asBool()); return ok; }
  if (op == "compressor") {
    if (!strip(s) || !need()) return {};
    rt_->setCompressor(s, cmd["on"].asBool(), clampDb(cmd["thresholdDb"].asNumber(-18), -60, 0), std::max(1.0, std::min(20.0, cmd["ratio"].asNumber(3))), std::max(0.0, std::min(24.0, cmd["kneeDb"].asNumber(6))),
                       std::max(0.1, cmd["attackMs"].asNumber(10)), std::max(5.0, cmd["releaseMs"].asNumber(100)), clampDb(cmd["makeupDb"].asNumber(0), 0, 24));
    return ok;
  }
  if (op == "gate") {
    if (!strip(s) || !need()) return {};
    rt_->setGate(s, cmd["on"].asBool(), clampDb(cmd["thresholdDb"].asNumber(-40), -80, 0), clampDb(cmd["rangeDb"].asNumber(-80), -120, 0), std::max(0.1, cmd["attackMs"].asNumber(1)), std::max(0.0, cmd["holdMs"].asNumber(50)), std::max(5.0, cmd["releaseMs"].asNumber(100)));
    return ok;
  }
  if (op == "monitor") {   // control room: source 0 master, 1..8 bus; level, dim, mono, mute (after the master meters)
    mon_.source = std::max(0, std::min(kMaxBuses, cmd["source"].asInt(mon_.source))); if (cmd.has("db")) mon_.db = clampDb(cmd["db"].asNumber(0), -60, 10);
    if (cmd.has("dim")) mon_.dim = cmd["dim"].asBool();
    if (cmd.has("mono")) mon_.mono = cmd["mono"].asBool();
    if (cmd.has("mute")) mon_.mute = cmd["mute"].asBool();
    if (rt_) rt_->setMonitor(mon_.source, mon_.db, mon_.dim, mon_.mono, mon_.mute);
    return ok;
  }
  if (op == "inRoute") { if (!strip(s) || !need()) return {}; rt_->setInputRoute(s, cmd["input"].asInt(-1)); return ok; }
  if (op == "outRoute") { if (!need()) return {}; const int o = cmd["out"].asInt(-1); if (o < 0 || o >= Runtime::kMaxOutputs) { err = "out of range"; return {}; } rt_->setOutputRoute(o, cmd["source"].asInt(0)); return ok; }
  if (op == "trim") { if (!strip(s) || !need()) return {}; rt_->setTrimDb(s, clampDb(cmd["db"].asNumber(0), -24, 24)); return ok; }
  if (op == "clearClips") { if (rt_) rt_->clearClips(); return ok; }
  err = op.empty() ? "no op" : "unknown op \"" + op + "\""; return {};
}

}  // namespace am
