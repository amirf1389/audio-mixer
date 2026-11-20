// amixos-daemon: see daemon.hpp. The console model (names, gains, sends ...) lives here so that /api/state can answer without a round trip
// to the audio thread; every change goes to the Runtime's lock-free control ring and is mirrored in the model under m_.
#include "daemon.hpp"
#include "audiomixer/latency.hpp"
#include "samples/samples.hpp"
#include "samples/sfx.hpp"
#include "audiomixer/bluetooth.hpp"
#include "paths/paths.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <ctime>
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
  bank_ = std::make_unique<pads::Bank>(opt_.padsFile);
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
  if (!opt_.seed.empty()) { std::string e; if (!loadSeed(opt_.seed, e)) std::fprintf(stderr, "amixos-daemon: %s (the mixer starts flat)\n", e.c_str()); }
  http_.route([this](const httpd::Request& r) { return handle(r); });
  http_.websocket("/ws/meters", [this](const httpd::Request& r, httpd::WebSocket& ws) {
    const std::string role = roleOf(r); if (role == "held") { ws.send("{\"error\":\"held\",\"kicked\":true,\"holdSeconds\":" + std::to_string(kHoldSeconds) + "}"); ws.close(); return; }
    if (role.empty()) { ws.send("{\"error\":\"unauthorised\"}"); ws.close(); return; }   // any valid token may watch the meters
    struct Count { std::atomic<int>& n; explicit Count(std::atomic<int>& c) : n(c) { ++n; } ~Count() { --n; } } watching(watchers_);   // how many remotes are live
    // CONNECTED PHONES: this link in the list (the pairing's name; a master-token client may say what it is with ?client=)
    auto link = std::make_shared<Link>(); { std::string t = r.header("x-amix-token"); if (t.empty()) t = r.param("token"); TokenStore::Entry e;
      if (role == "admin") { link->tokenId = "master"; std::string c; const std::string raw = r.param("client");
        for (std::size_t i = 0; i < raw.size() && c.size() < 40; ++i) { char ch = raw[i];   // %XX and + decoded; printable characters only
          if (ch == '+') ch = ' '; else if (ch == '%' && i + 2 < raw.size() && std::isxdigit(static_cast<unsigned char>(raw[i + 1])) && std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) { ch = static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16)); i += 2; }
          if (static_cast<unsigned char>(ch) >= 32 && ch != '"' && ch != 127) c += ch; } link->name = c.empty() ? "master token" : c; }
      else if (tokens_.find(t, e)) { link->tokenId = e.id; link->name = e.name; }
      link->role = role; link->ip = r.peer; link->since = static_cast<long long>(std::time(nullptr));
      std::lock_guard<std::mutex> g(linksM_); link->id = nextLink_++; links_[link->id] = link; }
    struct Drop { Daemon* d; int id; ~Drop() { std::lock_guard<std::mutex> g(d->linksM_); d->links_.erase(id); } } drop{this, link->id};
    // CONTROL ON THE OPEN SOCKET: a phone sends its moves as text frames ({"op":"gain",...}, the same commands as POST /api/control) on
    // this connection, so a fader move costs no new TCP connection over the Wi-Fi (each HTTP request opens one: the delay the user felt).
    // Only a token that may control (master / control) is obeyed; a view token's frame gets an error back. A frame may carry "seq": the
    // answer {"ack":seq} (or {"ack":seq,"controlError":...}: not "error", which ends a stream) lets the phone know it landed. The state
    // frames say "wsControl":true, so a phone uses this path only with a service that has it (an older one would ignore the frames). Pings and close are answered as before.
    const bool mayControl = role == "admin" || role == "control";
    std::thread reader([this, &ws, link, mayControl] { std::string t; int op = 0;
      while (ws.open() && ws.read(t, op)) {
        if (op != 1 || t.empty() || t.size() > 256 * 1024) continue;
        if (link->kick) break;
        bool ok = false; const json::Value cmd = json::Value::parse(t, ok); if (!ok || !cmd.isObject() || !cmd.has("op")) continue;   // not a command: ignored
        const bool hasSeq = cmd.has("seq"); json::Value ack = json::Value::object(); if (hasSeq) ack.set("ack", cmd["seq"]);
        if (!mayControl) { ack.set("controlError", "this token may only watch"); ws.send(ack.dump()); continue; }
        std::string err; control(cmd, err);
        if (!err.empty()) { ack.set("controlError", err); ws.send(ack.dump()); } else if (hasSeq) ws.send(ack.dump());
      }
      ws.close(); });
    unsigned long long sent = 0; unsigned rtaTick = 0, padsTick = 0;   // LIVE SYNC: the console state goes out on connect and whenever any remote changed it (at most 20 times a second)
    while (running_ && ws.open()) {
      if (link->kick) { ws.send("{\"kicked\":true,\"holdSeconds\":" + std::to_string(link->tokenId == "master" ? 0 : kHoldSeconds) + "}"); break; }   // DISCONNECT from the console
      if (++padsTick % 20 == 0 && bank_->reloadIfChanged()) ++version_;   // the console changed a CUSTOM PAD (it saves the shared file): the phones hear of it within a second
      const unsigned long long v = version_.load();
      if (v != sent) { json::Value m = json::Value::object(); m.set("state", state()); m.set("wsControl", true); if (!ws.send(m.dump())) break; sent = v; }
      if (!ws.send(meters().dump())) break;
      if (rta_.source.load() != Rta::kOff) {   // RTA: ten times a second while a remote has it open; stops by itself when nobody renews it
        if (static_cast<long long>(std::time(nullptr)) > rtaUntil_.load()) rta_.source.store(Rta::kOff);
        else if (++rtaTick % 2 == 0) { json::Value m = json::Value::object(); m.set("rta", rtaJson(rtaTick == 2)); if (!ws.send(m.dump())) break; } }
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    ws.close(); reader.join();
  });
  http_.setLocalOnly(opt_.localOnly);
  if (!http_.listen(opt_.bind, opt_.port, err)) return false;
  running_ = true; http_.start();
  // WINDOWS VOLUME: this computer's output slider, watched twice a second (only where there is one: the first read must work)
  if (!volGet_) volGet_ = [](float& f) { return systemVolumeGet(f); }; if (!volSet_) volSet_ = [](float f) { return systemVolumeSet(f); };
  { float f = 0; if (volGet_(f)) { pollVolume(); volWatch_ = std::thread([this] { while (running_) { for (int i = 0; i < 10 && running_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50)); if (running_) pollVolume(); } }); } }
  return true;
}

void Daemon::stop() {
  if (!running_.exchange(false)) return;
  if (volWatch_.joinable()) volWatch_.join();
  http_.stop();
  std::lock_guard<std::mutex> g(m_);
  if (rt_) { rt_->setTap(nullptr); rta_.next.store(nullptr); rec_.stop(); rt_->stop(); rt_.reset(); }
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
  std::string role = tokens_.roleFor(t, r.peer);   // a named token works from the address that used it first, and from no other
  if (!role.empty()) { TokenStore::Entry e; if (tokens_.find(t, e) && held(e.id)) return "held"; }   // disconnected by the console a moment ago: refused for a while
  return role;
}
bool Daemon::held(const std::string& tokenId) const {
  std::lock_guard<std::mutex> g(linksM_); const auto it = held_.find(tokenId);
  return it != held_.end() && static_cast<long long>(std::time(nullptr)) < it->second;
}
json::Value Daemon::links() const {
  const long long now = static_cast<long long>(std::time(nullptr)); json::Value a = json::Value::array(); std::lock_guard<std::mutex> g(linksM_);
  for (const auto& [id, l] : links_) { json::Value o = json::Value::object(); o.set("id", id); o.set("tokenId", l->tokenId); o.set("name", l->name); o.set("role", l->role);
    o.set("ip", l->ip); o.set("since", static_cast<double>(l->since)); o.set("seconds", static_cast<double>(now - l->since)); a.push(o); }
  return a;
}
bool Daemon::kick(int id) {
  std::lock_guard<std::mutex> g(linksM_); const auto it = links_.find(id); if (it == links_.end()) return false;
  it->second->kick = true; if (it->second->tokenId != "master" && !it->second->tokenId.empty()) held_[it->second->tokenId] = static_cast<long long>(std::time(nullptr)) + kHoldSeconds;
  return true;
}
int Daemon::kickToken(const std::string& tokenId, bool hold) {
  std::lock_guard<std::mutex> g(linksM_); int n = 0;
  for (auto& [id, l] : links_) if (l->tokenId == tokenId) { l->kick = true; ++n; }
  if (hold && tokenId != "master") held_[tokenId] = static_cast<long long>(std::time(nullptr)) + kHoldSeconds;
  return n;
}
bool Daemon::authorised(const httpd::Request& r) const { const std::string role = roleOf(r, false); return role == "admin" || role == "control"; }

std::string Daemon::pinFor(const std::string& masterToken, long long unixSeconds) {
  const std::string h = TokenStore::hashOf(masterToken + "|pair|" + std::to_string(unixSeconds / 60));
  unsigned long v = 0; for (int i = 0; i < 8; ++i) { const char c = h[static_cast<std::size_t>(i)]; v = v * 16 + static_cast<unsigned long>(c <= '9' ? c - '0' : c - 'a' + 10); }
  char b[8]; std::snprintf(b, sizeof b, "%06lu", v % 1000000UL); return b;
}
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
  if (r.method == "GET" && r.path == "/api/presets") {   // the PRESET PACK built in (releases/presets/presets-pack.json): the owner's factory presets, nothing private
    static const char kPack[] =
#include "presets_pack.inc"
      ;
    res.body = kPack; return res; }
  if (r.method == "GET" && (r.path == "/api/bluetooth" || r.path.rfind("/api/bluetooth?", 0) == 0)) {   // ?scan=SECONDS also listens for devices in range
    if (!authorised(r)) { const std::string rr = roleOf(r, false); res.status = (rr == "view" || rr == "held") ? 403 : (noteRefused() ? 429 : 401); res.body = rr == "held" ? "{\"ok\":false,\"error\":\"disconnected by the console: try again in two minutes\",\"held\":true}" : "{\"ok\":false,\"error\":\"X-Amix-Token missing or wrong\"}"; return res; }   // device names and a radio scan: control only
    int scan = 0; const auto q = r.path.find("scan="); if (q != std::string::npos) scan = std::clamp(std::atoi(r.path.c_str() + q + 5), 0, 20);
    std::string note; json::Value o = json::Value::object(), a = json::Value::array();
    const BluetoothRadio radio = bluetoothRadio();
    json::Value rj = json::Value::object(); rj.set("present", radio.present); rj.set("name", radio.name); rj.set("address", radio.address); o.set("radio", rj);
    for (const auto& d : bluetoothDevices(scan, &note)) {
      json::Value x = json::Value::object(); x.set("address", d.address); x.set("name", d.name); x.set("kind", d.kind); x.set("audio", d.audio);
      x.set("connected", d.connected); x.set("paired", d.paired); x.set("remembered", d.remembered); x.set("classOfDevice", double(d.classOfDevice)); x.set("rssi", double(d.rssi)); a.push(x);
    }
    o.set("devices", a); o.set("note", note); res.body = o.dump(); return res;
  }
  if (r.method == "GET" && r.path == "/api/state") {
    const std::string since = r.param("since");
    if (!since.empty() && std::strtoull(since.c_str(), nullptr, 10) == version_.load()) { json::Value o = json::Value::object(); o.set("v", static_cast<double>(version_.load())); o.set("same", true); o.set("remotes", watchers_.load()); res.body = o.dump(); return res; }
    res.body = state().dump(); return res; }
  if (r.method == "GET" && r.path == "/api/meters") { res.body = meters().dump(); return res; }
  if (r.method == "POST" && r.path == "/api/pair") {   // a phone trades the minute's PIN for a token of its own
    if (opt_.tokenStore.empty()) { res.status = 400; res.body = "{\"ok\":false,\"error\":\"this service was started without a token file\"}"; return res; }
    const long long now = static_cast<long long>(std::time(nullptr)), minute = now / 60;
    if (pairMinute_.load() != minute) { pairMinute_ = minute; pairFails_ = 0; }
    if (pairFails_.load() >= 5) { res.status = 429; res.body = "{\"ok\":false,\"error\":\"too many wrong PINs: wait for the next PIN\"}"; return res; }
    bool ok = false; const json::Value b = json::Value::parse(r.body, ok); const std::string pin = ok ? b["pin"].asString() : std::string();
    if (pin.size() != 6 || !(TokenStore::same(pin, pinFor(opt_.token, now)) || TokenStore::same(pin, pinFor(opt_.token, now - 60)))) {
      ++pairFails_; std::this_thread::sleep_for(std::chrono::milliseconds(300)); res.status = 401; res.body = "{\"ok\":false,\"error\":\"wrong PIN\"}"; return res; }
    std::string id, terr, name = b["name"].asString(); if (name.empty()) name = "Phone " + r.peer;
    const std::string t = tokens_.issue(name, "control", 30, id, terr);
    if (t.empty()) { res.status = 400; json::Value e = json::Value::object(); e.set("ok", false); e.set("error", terr); res.body = e.dump(); return res; }
    tokens_.roleFor(t, r.peer);   // it belongs to this phone's address from the start
    pairFails_ = 5;               // one pairing per PIN: the next phone waits for the next PIN
    json::Value out = json::Value::object(); out.set("ok", true); out.set("id", id); out.set("token", t); out.set("role", "control"); out.set("days", 30); res.body = out.dump(); return res; }
  if (r.method == "GET" && r.path == "/api/routing") {   // ROUTING (any valid token)
    const std::string role = roleOf(r, false); if (role.empty() || role == "held") { res.status = role == "held" ? 403 : (noteRefused() ? 429 : 401); res.body = "{\"ok\":false,\"error\":\"X-Amix-Token missing or wrong\"}"; return res; }
    std::lock_guard<std::mutex> g(m_); res.body = routingJson().dump(); return res; }
  if (r.method == "GET" && r.path == "/api/samples") {   // the SAMPLE PADS: id, name, category, seconds (any valid token)
    const std::string role = roleOf(r, false); if (role.empty() || role == "held") { res.status = role == "held" ? 403 : (noteRefused() ? 429 : 401); res.body = "{\"ok\":false,\"error\":\"X-Amix-Token missing or wrong\"}"; return res; }
    json::Value a = json::Value::array(); for (const auto& s : samples::list()) { json::Value v = json::Value::object(); v.set("id", s.id); v.set("name", s.name); v.set("category", s.category); v.set("seconds", s.seconds); a.push(v); }
    json::Value o = json::Value::object(); o.set("samples", a); o.set("gainDb", pads_.gainDb.load()); o.set("voices", pads_.active.load()); res.body = o.dump(); return res; }
  if (r.method == "GET" && (r.path == "/api/pads" || r.path == "/api/sfx")) {   // the 1000 CUSTOM PADS (?from=&to=) and the 1000 SOUND EFFECTS (any valid token)
    const std::string role = roleOf(r, false); if (role.empty() || role == "held") { res.status = role == "held" ? 403 : (noteRefused() ? 429 : 401); res.body = "{\"ok\":false,\"error\":\"X-Amix-Token missing or wrong\"}"; return res; }
    if (r.path == "/api/sfx") { json::Value fam = json::Value::array(); for (const auto& f : sfx::families()) fam.push(f);
      json::Value a = json::Value::array(); for (int id = 1; id <= sfx::kCount; ++id) { const sfx::Info in = sfx::info(id); json::Value v = json::Value::object(); v.set("id", id); v.set("name", in.name); v.set("family", in.family); v.set("seconds", in.seconds); a.push(v); }
      json::Value o = json::Value::object(); o.set("families", fam); o.set("perFamily", sfx::kPerFamily); o.set("effects", a); res.body = o.dump(); return res; }
    res.body = padsJson(r).dump(); return res; }
  if (r.method == "GET" && r.path == "/api/rta") {   // the analyzer now (any valid token)
    const std::string role = roleOf(r, false); if (role.empty() || role == "held") { res.status = role == "held" ? 403 : (noteRefused() ? 429 : 401); res.body = "{\"ok\":false,\"error\":\"X-Amix-Token missing or wrong\"}"; return res; }
    res.body = rtaJson(true).dump(); return res; }
  if (r.method == "GET" && r.path == "/api/racks") {   // the console's plug-ins (any valid token; their states: the master token only)
    const std::string role = roleOf(r, false);
    if (role.empty() || role == "held") { res.status = role == "held" ? 403 : (noteRefused() ? 429 : 401); res.body = "{\"ok\":false,\"error\":\"X-Amix-Token missing or wrong\"}"; return res; }
    const std::string sp = r.param("strip"); const bool states = r.param("states") == "1" && role == "admin";
    json::Value out = json::Value::object(); out.set("ok", true); out.set("v", static_cast<double>(racksV_.load())); out.set("loader", static_cast<bool>(opt_.pluginFactory));
    out.set("racks", racks(sp.empty() ? -1 : std::atoi(sp.c_str()), !sp.empty(), states)); res.body = out.dump(); return res; }
  if (r.path == "/api/links" || r.path == "/api/links/kick") {   // CONNECTED PHONES: only the master token (the console) sees and drops them
    if (roleOf(r, false) != "admin") { const bool known = !roleOf(r, false).empty(); res.status = known ? 403 : (noteRefused() ? 429 : 401); res.body = "{\"ok\":false,\"error\":\"the master token is needed\"}"; return res; }
    json::Value out = json::Value::object();
    if (r.method == "GET" && r.path == "/api/links") { out.set("ok", true); out.set("links", links()); out.set("holdSeconds", kHoldSeconds); res.body = out.dump(); return res; }
    bool ok = false; const json::Value b = json::Value::parse(r.body, ok);
    if (r.method != "POST" || !ok || !b.isObject()) { res.status = 400; res.body = "{\"ok\":false,\"error\":\"the body is not a JSON object\"}"; return res; }
    if (!kick(b["id"].asInt())) { res.status = 404; res.body = "{\"ok\":false,\"error\":\"no such link (it may have closed already)\"}"; return res; }
    out.set("ok", true); res.body = out.dump(); return res; }
  if (r.path == "/api/tokens" || r.path == "/api/tokens/revoke") {   // the named tokens: only the master token manages them
    if (roleOf(r, false) != "admin") { const bool known = !roleOf(r, false).empty(); res.status = known ? 403 : (noteRefused() ? 429 : 401); res.body = "{\"ok\":false,\"error\":\"the master token is needed to manage tokens\"}"; return res; }
    if (opt_.tokenStore.empty()) { res.status = 400; res.body = "{\"ok\":false,\"error\":\"this daemon was started without a token file\"}"; return res; }
    json::Value out = json::Value::object(); std::string terr;
    if (r.method == "GET" && r.path == "/api/tokens") {
      json::Value arr = json::Value::array();
      for (const auto& e : tokens_.list()) { json::Value t = json::Value::object(); t.set("id", e.id); t.set("name", e.name); t.set("role", e.role); t.set("created", e.created); t.set("expires", e.expires); t.set("revoked", e.revoked); t.set("ip", e.ip); arr.push(t); }
      out.set("ok", true); out.set("tokens", arr); res.body = out.dump(); return res; }
    bool ok = false; const json::Value b = json::Value::parse(r.body, ok);
    if (r.method != "POST" || !ok || !b.isObject()) { res.status = 400; res.body = "{\"ok\":false,\"error\":\"the body is not a JSON object\"}"; return res; }
    if (r.path == "/api/tokens") { std::string id; const std::string t = tokens_.issue(b["name"].asString(), b["role"].asString("control"), b["days"].asInt(0), id, terr); if (!t.empty()) { out.set("ok", true); out.set("id", id); out.set("token", t); } }
    else if (tokens_.revoke(b["id"].asString(), terr)) { out.set("ok", true); out.set("closed", kickToken(b["id"].asString(), false)); }   // FORGET: its live links close too
    if (!terr.empty()) { res.status = 400; out.set("ok", false); out.set("error", terr); }
    res.body = out.dump(); return res; }
  if (r.method == "POST" && r.path == "/api/control") {
    if (!authorised(r)) { const std::string rr = roleOf(r, false); res.status = (rr == "view" || rr == "held") ? 403 : (noteRefused() ? 429 : 401); res.body = rr == "held" ? "{\"ok\":false,\"error\":\"disconnected by the console: try again in two minutes\",\"held\":true}" : "{\"ok\":false,\"error\":\"X-Amix-Token missing or wrong\"}"; return res; }
    bool ok = false; const json::Value cmd = json::Value::parse(r.body, ok);
    if (!ok || !cmd.isObject()) { res.status = 400; res.body = "{\"ok\":false,\"error\":\"the body is not a JSON object\"}"; return res; }
    std::string err; json::Value out = control(cmd, err);
    if (!err.empty()) { res.status = 400; json::Value e = json::Value::object(); e.set("ok", false); e.set("error", err); res.body = e.dump(); return res; }
    res.body = out.dump(); return res;
  }
  if (r.method == "GET" && r.path == "/api/console-info") {   // the console's plug-ins, racks and extensions as it saved them when the service started (read-only)
    { const std::string rr = roleOf(r); if (rr == "held") { res.status = 403; res.body = "{\"ok\":false,\"error\":\"disconnected by the console: try again in two minutes\",\"held\":true}"; return res; }
      if (rr.empty()) { res.status = noteRefused() ? 429 : 401; res.body = "{\"ok\":false,\"error\":\"X-Amix-Token missing or wrong\"}"; return res; } }
    std::ifstream f(opt_.consoleInfo, std::ios::binary); std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    bool ok = false; json::Value::parse(s, ok);   // only well-formed JSON goes out
    res.body = ok && !s.empty() && s.size() < 4 * 1024 * 1024 ? s : std::string("{\"available\":false}"); return res;
  }
  if (r.method == "GET" && (r.path == "/favicon.svg" || r.path == "/favicon.ico")) {   // the remote's tab icon: the AmF X1 app icon (three faders on violet to ember)
    res.type = "image/svg+xml"; res.headers.push_back({"Cache-Control", "public, max-age=86400"});
    res.body = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#5b21b6"/><stop offset=".45" stop-color="#2a1356"/><stop offset=".65" stop-color="#140d22"/><stop offset="1" stop-color="#c2410c"/></linearGradient><linearGradient id="a" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#c4b5fd"/><stop offset="1" stop-color="#7c3aed"/></linearGradient><linearGradient id="b" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#f9a8d4"/><stop offset="1" stop-color="#db2777"/></linearGradient><linearGradient id="c" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fcd34d"/><stop offset="1" stop-color="#f97316"/></linearGradient></defs><rect width="64" height="64" rx="14" fill="url(#g)"/><g fill="#fff" fill-opacity=".22"><rect x="18" y="10" width="4" height="44" rx="2"/><rect x="30" y="10" width="4" height="44" rx="2"/><rect x="42" y="10" width="4" height="44" rx="2"/></g><rect x="18" y="40" width="4" height="14" rx="2" fill="url(#a)"/><rect x="30" y="29" width="4" height="25" rx="2" fill="url(#b)"/><rect x="42" y="19" width="4" height="35" rx="2" fill="url(#c)"/><path d="M20 38L32 27L44 17" stroke="#fff" stroke-opacity=".4" stroke-width="1.6" fill="none" stroke-linecap="round"/><rect x="12" y="35" width="16" height="6" rx="2" fill="#f1f5f9"/><rect x="24" y="24" width="16" height="6" rx="2" fill="#f1f5f9"/><rect x="36" y="14" width="16" height="6" rx="2" fill="#fbbf24"/><g fill="#1e293b"><rect x="15" y="37.5" width="10" height="1"/><rect x="27" y="26.5" width="10" height="1"/><rect x="39" y="16.5" width="10" height="1"/></g></svg>)SVG";
    return res;
  }
  if (r.method == "GET" && (r.path == "/remote" || r.path == "/remote/")) {   // THE BROWSER REMOTE: any phone, tablet or computer on the network pairs with the minute's PIN
    static const char kPage[] =
#include "remote_page.inc"
      ;
    res.type = "text/html; charset=utf-8";
    res.headers.push_back({"Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self' ws: wss:; img-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"});
    res.body = kPage; return res;
  }
  if (r.method == "GET" && (r.path == "/" || r.path == "/api")) {
    res.body = "{\"name\":\"amixos-daemon\",\"api\":[\"GET /api/status\",\"GET /api/devices\",\"GET /api/state\",\"GET /api/meters\",\"POST /api/control (X-Amix-Token)\",\"POST /api/pair\",\"GET /api/tokens\",\"POST /api/tokens\",\"POST /api/tokens/revoke\",\"GET /api/links\",\"POST /api/links/kick\",\"WS /ws/meters?token=\"]}"; return res;
  }
  res.status = 404; res.body = "{\"ok\":false,\"error\":\"not found\"}"; return res;
}

std::string Daemon::computerName() {
  char buf[256] = {0};
#ifdef _WIN32
  DWORD n = sizeof(buf); if (!GetComputerNameA(buf, &n)) buf[0] = 0;
#else
  if (gethostname(buf, sizeof(buf) - 1) != 0) buf[0] = 0;
#endif
  std::string s; for (const char* p = buf; *p && s.size() < 40; ++p) if (static_cast<unsigned char>(*p) >= 32 && *p != '"' && *p != 127) s += *p;   // printable, short
  return s;
}
json::Value Daemon::status() const {
  std::lock_guard<std::mutex> g(m_);
  json::Value o = json::Value::object();
  o.set("name", "A mix OS"); o.set("version", opt_.version); o.set("daemon", "amixos-daemon");
  // consoleId: a stable name for this console that phones use to find it again after its address changes; a one-way hash of the
  // master token with its own prefix (12 hex characters): it says nothing about the token and changes only if the token does
  o.set("consoleId", TokenStore::hashOf("amfx1-console-id:" + opt_.token).substr(0, 12));
  o.set("computer", computerName());   // this PC's name: phones tell several consoles apart by it (STUDIO-PC, STAGE-PC)
  o.set("backend", rt_ ? backend_ : ""); o.set("running", rt_ && rt_->running());
  if (rt_) { const Snapshot s = rt_->snapshot(); o.set("blocks", static_cast<double>(s.blocks)); o.set("frames", static_cast<double>(s.frames)); o.set("xruns", static_cast<double>(s.xruns));
    // CONSOLE STATUS on the remotes: the device's rate, the DSP load of the last block and its worst, the connected remotes
    o.set("inDevice", lastCfg_.inDevice); o.set("outDevice", lastCfg_.outDevice); o.set("bufferFrames", static_cast<int>(lastCfg_.frames)); { long li = 0, lo = 0; if (rt_) rt_->latencyFrames(li, lo); am::latency::Input L; L.backend = rt_ ? rt_->backendName() : std::string(); L.exclusive = rt_ && rt_->exclusive(); L.rate = rt_ ? rt_->sampleRate() : 48000; L.blockFrames = static_cast<int>(lastCfg_.frames); L.inFrames = li; L.outFrames = lo; L.hasInput = lastCfg_.inChannels > 0;
      const am::latency::Report lr = am::latency::assess(L); o.set("latencyMs", std::round(lr.totalMs * 10) / 10); o.set("latencyLevel", lr.levelName); o.set("latencyReported", lr.reported); }   // LOW LATENCY: what the phone shows
    o.set("inChannels", lastCfg_.inChannels); o.set("outChannels", lastCfg_.outChannels);
    o.set("rate", rt_->sampleRate()); o.set("loadPct", std::round(s.loadPct * 10) / 10); o.set("maxBlockUs", std::round(s.maxBlockUs)); o.set("blockUs", std::round(s.blockUs)); }
  else { o.set("blocks", 0); o.set("frames", 0); o.set("xruns", 0); }
  o.set("remotes", watchers_.load());
  o.set("strips", opt_.strips); o.set("buses", kMaxBuses); o.set("plan", opt_.plan);
  o.set("recording", rec_.recording()); o.set("recordSeconds", rec_.seconds()); o.set("recordFile", rec_.recording() && !rec_.files().empty() ? rec_.files()[0] : std::string());
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

json::Value Daemon::rtaJson(bool withCentres) const {
  json::Value o = json::Value::object(); o.set("src", rta_.source.load()); std::vector<double> db; json::Value a = json::Value::array();
  if (rta_.bands(db)) for (double v : db) a.push(std::round(v * 10) / 10); o.set("db", a);
  if (withCentres) { json::Value f = json::Value::array(); for (double c : Rta::centres()) f.push(std::round(c * 10) / 10); o.set("f", f); }
  return o;
}
json::Value Daemon::programEqJson() const {
  json::Value o = json::Value::object(); o.set("on", mp_.eqOn); json::Value g = json::Value::array();
  for (int b = 0; b < 10; ++b) g.push(mp_.eq[b].type == dsp::FilterType::Peaking && std::fabs(mp_.eq[b].freq - kIso[b]) < 1 ? mp_.eq[b].gainDb : 0.0);
  o.set("gains", g); o.set("mastering", mp_.on); return o;
}
void Daemon::loadRacks(Runtime& rt) { for (auto& r : racks_) loadRack(rt, r.first); }
void Daemon::loadRack(Runtime& rt, int strip) {
  auto rit = racks_.find(strip); if (rit == racks_.end()) { rt.setInserts(strip, nullptr); return; }
  std::vector<PlugSlot>& slots = rit->second;
  {
    auto chain = std::make_shared<Runtime::InsertChain>();
    for (PlugSlot& q : slots) {
      if (!q.pl) { q.error.clear(); std::string e;
        if (!opt_.pluginFactory) q.error = "no plug-in loader in this service";
        else if (auto made = opt_.pluginFactory(q.kind, rt.sampleRate(), 1024, e)) { if (!q.state.empty()) made->setState(q.state); made->setBypass(q.bypass); q.pl = std::move(made); }
        else q.error = e.empty() ? std::string("could not be loaded") : e; }
      if (q.pl) chain->push_back(std::make_shared<host::MonoInsert>(q.pl, 1024)); }
    rt.setInserts(strip, chain->empty() ? nullptr : chain); }
}
json::Value Daemon::racks(int onlyStrip, bool params, bool states) const {
  std::lock_guard<std::mutex> g(m_); json::Value a = json::Value::array();
  for (const auto& [strip, slots] : racks_) { if (onlyStrip >= 0 && strip != onlyStrip) continue;
    json::Value r = json::Value::object(); r.set("strip", strip); auto n = rackNames_.find(strip); r.set("name", n != rackNames_.end() ? n->second : std::string());
    json::Value ss = json::Value::array();
    for (const PlugSlot& q : slots) { json::Value o = json::Value::object(); o.set("slot", q.slot); o.set("kind", q.kind); o.set("loaded", static_cast<bool>(q.pl)); o.set("error", !q.pl && q.error.empty() && !rt_ ? std::string("waiting for a sound card: the service loads plug-ins when it opens one") : q.error);
      o.set("name", q.pl && !q.pl->info().name.empty() ? q.pl->info().name : q.name); o.set("bypass", q.pl ? q.pl->bypass() : q.bypass);
      if (q.pl) { o.set("vendor", q.pl->info().vendor); o.set("format", q.pl->info().format); }
      if (params && q.pl) { json::Value ps = json::Value::array(); int n2 = 0;
        for (const auto& pi : q.pl->params()) { if (pi.hidden || pi.readOnly || pi.bypass || n2 >= 64) continue; ++n2; const double v = q.pl->param(pi.id);
          json::Value x = json::Value::object(); x.set("id", static_cast<double>(pi.id)); x.set("title", pi.title.empty() ? std::string("Param ") + std::to_string(pi.id) : pi.title); x.set("value", v);
          x.set("default", pi.defaultNormalized); x.set("steps", pi.stepCount); { std::string t = q.pl->paramText(pi.id, v); const bool hasUnit = pi.units.empty() || (t.size() >= pi.units.size() && t.compare(t.size() - pi.units.size(), pi.units.size(), pi.units) == 0);
            // the plug-in's own text, its unit added only when it left it out
            x.set("text", hasUnit ? t : t + " " + pi.units); }
          ps.push(x); }
        o.set("params", ps); }
      if (states) o.set("state", q.pl ? q.pl->state() : q.state);
      ss.push(o); }
    r.set("slots", ss); a.push(r); }
  return a;
}
double Daemon::trimForLevel(double l) { return l <= 0.0005 ? -120.0 : std::max(-120.0, std::min(0.0, 30.0 * std::log10(l))); }
bool Daemon::windowsSetsLevel() const { return rt_ && rt_->backendName() == "wasapi" && !rt_->exclusive() && (rt_->outDevice().empty() || rt_->outDevice() == "-"); }
void Daemon::initRoutes() { if (routesInit_) return; routesInit_ = true; for (int i = 0; i < kMaxStrips; ++i) inRoute_[i] = i; for (int o = 0; o < Runtime::kMaxOutputs; ++o) outRoute_[o] = Runtime::defaultOutputSource(o); }
json::Value Daemon::routingJson() const {   // ROUTING for the phones: what feeds each strip, what each output carries, and the sources by name
  const_cast<Daemon*>(this)->initRoutes(); json::Value o = json::Value::object();
  json::Value in = json::Value::array(); for (int i = 0; i < opt_.strips; ++i) in.push(inRoute_[i]); o.set("in", in);
  json::Value out = json::Value::array(); for (int k = 0; k < 32; ++k) out.push(outRoute_[k]); o.set("out", out);
  json::Value src = json::Value::array(); auto add = [&](int id, const std::string& name, const char* group) { json::Value v = json::Value::object(); v.set("id", id); v.set("name", name); v.set("group", group); src.push(v); };
  add(Runtime::kSrcOff, "OFF", "off"); add(Runtime::kSrcMasterL, "MASTER L", "master"); add(Runtime::kSrcMasterR, "MASTER R", "master"); add(Runtime::kSrcMasterMono, "MASTER MONO", "master");
  add(Runtime::kSrcMonL, "MONITOR L", "monitor"); add(Runtime::kSrcMonR, "MONITOR R", "monitor");
  for (int k = 0; k < kMaxBuses; ++k) { const std::string b = k < 8 ? "AUX " + std::to_string(k + 1) : "FX " + std::to_string(k - 7); add(Runtime::kSrcBus0L + 2 * k, b + " L", "bus"); add(Runtime::kSrcBus0L + 2 * k + 1, b + " R", "bus"); }
  o.set("sources", src); o.set("inputs", rt_ ? rt_->inputChannels() : 0); o.set("outputs", rt_ ? rt_->outputChannels() : 0); return o;
}
void Daemon::pushTone(Runtime& rt, int i) const {   // the same bands as the console's Engine::setChannelEq
  const Strip& st = strips_[static_cast<std::size_t>(i)]; const int f = dsp::kChannelEqFirst;
  auto put = [&](int b, dsp::FilterType t, double hz, double q, double g) { rt.setEqBand(i, b, st.toneOn && std::fabs(g) > 0.01 ? t : dsp::FilterType::Off, hz, q, g); };
  put(f, dsp::FilterType::LowShelf, 100, 0.71, st.low); put(f + 1, dsp::FilterType::Peaking, st.midHz, 0.9, st.mid); put(f + 2, dsp::FilterType::HighShelf, 10000, 0.71, st.high);
}
double Daemon::outTrim() const {   // GAIN MODE's trim (the same rule as the console's Engine::windowsTrimDb)
  const int v = winVol_.load(); return (set_.winGain && v >= 0 && !windowsSetsLevel()) ? trimForLevel(v / 100.0) : 0.0;
}
// the master fader and, separately, GAIN MODE's listening trim: the trim comes after the meters, the loudness meter and AUTO LEVEL
// (Mixer::setOutputTrimDb), so turning Windows down does not make AUTO LEVEL turn the mix back up
void Daemon::applyMaster(Runtime& rt) const { rt.setMasterDb(masterDb_); rt.setOutputTrimDb(outTrim()); }
void Daemon::pushSettings(Runtime& rt) const {
  rt.setWidth(set_.width); rt.setOutputBits(set_.outputBits < 0 ? 0 : set_.outputBits); rt.setDcBlock(set_.dcBlock); rt.setAnalogChannels(set_.analog);
  rt.setAutoLevel(set_.autoLevel, set_.autoTarget, set_.autoMax); rt.setClipDb(set_.clipDb); rt.setPeakHoldMs(set_.peakHoldMs); applyMaster(rt);
}
json::Value Daemon::settingsJson() const {
  json::Value s = json::Value::object(); s.set("width", set_.width); s.set("outputBits", set_.outputBits); s.set("dcBlock", set_.dcBlock); s.set("analog", set_.analog);
  s.set("autoLevel", set_.autoLevel); s.set("autoTarget", set_.autoTarget); s.set("autoMax", set_.autoMax); s.set("clipDb", set_.clipDb); s.set("peakHoldMs", set_.peakHoldMs);
  s.set("winGain", set_.winGain); s.set("winMuteSync", set_.winMuteSync); s.set("winVolume", winVol_.load());
  const int v = winVol_.load(); s.set("winTrimDb", (set_.winGain && v >= 0 && !windowsSetsLevel()) ? trimForLevel(v / 100.0) : 0.0); s.set("winSetsLevel", windowsSetsLevel());
  return s;
}
void Daemon::pollVolume() {
  if (!volGet_) return; float f = 0; const int v = volGet_(f) ? static_cast<int>(std::lround(std::max(0.0f, std::min(1.0f, f)) * 100.0f)) : -1;
  if (v == winVol_.load()) return;
  std::lock_guard<std::mutex> g(m_); winVol_ = v; if (rt_) applyMaster(*rt_); ++version_;   // a move of the Windows slider reaches every remote
}
json::Value Daemon::padsJson(const httpd::Request& r) {
  bank_->reloadIfChanged();
  auto q = [&](const char* k, int d) { const std::string v = r.param(k); return v.empty() ? d : std::atoi(v.c_str()); };
  json::Value o = bank_->toJson(std::max(1, q("from", 1)), std::min(pads::kPads, q("to", pads::kPads))); o.set("gainDb", pads_.gainDb.load()); o.set("voices", pads_.active.load()); return o;
}

json::Value Daemon::state() const {
  std::lock_guard<std::mutex> g(m_);
  json::Value o = json::Value::object();
  json::Value strips = json::Value::array();
  for (std::size_t i = 0; i < strips_.size(); ++i) {
    const Strip& s = strips_[i]; json::Value v = json::Value::object();
    v.set("id", static_cast<int>(i)); v.set("name", s.name); v.set("db", s.db); v.set("pan", s.pan); v.set("mute", s.mute); v.set("solo", s.solo);
    v.set("trim", s.trim); json::Value t = json::Value::object(); t.set("low", s.low); t.set("mid", s.mid); t.set("midHz", s.midHz); t.set("high", s.high); t.set("on", s.toneOn); v.set("tone", t);
    json::Value eq = json::Value::array(); for (const Strip::Band& e : s.eq) { json::Value b = json::Value::array(); b.push(e.type); b.push(e.freq); b.push(e.q); b.push(e.gain); eq.push(b); } v.set("eq", eq);   // [type, freq, q, gainDb]
    strips.push(v);
  }
  o.set("strips", strips);
  json::Value buses = json::Value::array();
  for (int k = 0; k < kMaxBuses; ++k) { json::Value v = json::Value::object(); v.set("id", k); v.set("db", buses_[k].db); v.set("returnDb", buses_[k].returnDb); v.set("mute", buses_[k].mute); buses.push(v); }
  o.set("buses", buses);
  json::Value m = json::Value::object(); m.set("db", masterDb_); m.set("limiter", limiter_); m.set("ceilingDb", ceiling_); o.set("master", m);
  o.set("settings", settingsJson());
  json::Value mo = json::Value::object(); mo.set("source", mon_.source); mo.set("db", mon_.db); mo.set("dim", mon_.dim); mo.set("mono", mon_.mono); mo.set("mute", mon_.mute); o.set("monitor", mo);
  o.set("backend", rt_ ? backend_ : ""); o.set("running", rt_ && rt_->running()); o.set("v", static_cast<double>(version_.load())); o.set("remotes", watchers_.load()); o.set("racksV", static_cast<double>(racksV_.load())); o.set("racksBy", racksFromConsole_ ? "console" : "remote");
  o.set("programEq", programEqJson()); o.set("masterV", static_cast<double>(masterV_.load())); o.set("padsVersion", bank_->version()); o.set("oneVoiceNote", ovNote_); o.set("routing", routingJson());
  { json::Value m = json::Value::object();   // MASTERING (the master chain), in the console's own words (Engine::mastering)
    m.set("on", mp_.on); m.set("low", mp_.lowDb); m.set("air", mp_.airDb); m.set("thr", mp_.thrDb); m.set("ratio", mp_.ratio); m.set("attack", mp_.attackMs); m.set("release", mp_.releaseMs);
    m.set("makeup", mp_.makeupDb); m.set("drive", mp_.drive); m.set("gain", mp_.gainDb); m.set("reductionDb", master_.reductionDb()); m.set("preset", masterPreset_); o.set("mastering", m); }
  return o;
}

json::Value Daemon::meters() const {
  std::lock_guard<std::mutex> g(m_);
  json::Value o = json::Value::object();
  if (!rt_) { o.set("running", false); return o; }
  const Snapshot s = rt_->snapshot();
  if (!ovActed_ && backend_ != "null" && inRoute_[0] == 0 && inRoute_[1] == 1 && ov_.update(s)) {   // ONE VOICE (the phones see the routing change and the note)
    ovActed_ = true; const_cast<Daemon*>(this)->inRoute_[0] = -1; rt_->setInputRoute(0, -1); ++const_cast<Daemon*>(this)->version_;
    ovNote_ = "Inputs 1 and 2 carry the same microphone: CH 1 is set to no input so the voice is heard once, on CH 2."; }
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
  if (cmd["op"].asString() == "input") {   // the console's INPUT button while phones are linked: the same device reopened with the microphone ("") or COMPUTER SOUND ("loopback:system")
    DeviceConfig keep; { std::lock_guard<std::mutex> g(m_); if (!rt_) { err = "no sound card open"; return {}; } keep = lastCfg_; }
    const std::string in = cmd["in"].asString("");
    auto openCmd = [](const DeviceConfig& c, const std::string& inDev, int inCh) { json::Value o = json::Value::object(); o.set("op", "open"); o.set("backend", c.backend); o.set("in", inDev); o.set("out", c.outDevice);
      o.set("rate", c.sampleRate); o.set("frames", static_cast<int>(c.frames)); o.set("inChannels", inCh); o.set("outChannels", c.outChannels); return o; };
    json::Value r = apply(openCmd(keep, in, in == "-" ? 0 : 2), err);   // "-": outputs only
    if (!err.empty()) { const std::string why = err; std::string e2; apply(openCmd(keep, keep.inDevice, keep.inChannels), e2); err = why; return {}; }   // could not open it: the sound comes back as it was
    ++version_; return r; }
  json::Value out = apply(cmd, err);
  if (err.empty() && cmd["op"].asString() != "clearClips") ++version_;
  return out;
}

bool Daemon::loadSeed(const std::string& file, std::string& err) {
  std::ifstream f(file, std::ios::binary); if (!f) { err = "no seed " + file; return false; }
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  bool ok = false; const json::Value o = json::Value::parse(text, ok); if (!ok || !o.isObject()) { err = "the seed is not a JSON object"; return false; }
  std::lock_guard<std::mutex> g(m_);
  const json::Value& l = o["strips"];
  for (std::size_t i = 0; i < strips_.size() && i < l.size(); ++i) { const json::Value& v = l.at(i); Strip& s = strips_[i];
    s.name = v["name"].asString(s.name).substr(0, 24); s.db = std::max(-120.0, std::min(10.0, v["db"].asNumber(s.db))); s.pan = std::max(-1.0, std::min(1.0, v["pan"].asNumber(s.pan)));
    s.mute = v["mute"].asBool(s.mute); s.solo = v["solo"].asBool(s.solo); s.trim = std::max(-24.0, std::min(24.0, v["trim"].asNumber(s.trim)));
    if (v["tone"].isObject()) { const json::Value& t = v["tone"]; auto g = [](double x) { return std::max(-15.0, std::min(15.0, x)); };
      s.low = g(t["low"].asNumber(s.low)); s.mid = g(t["mid"].asNumber(s.mid)); s.high = g(t["high"].asNumber(s.high)); s.midHz = std::max(100.0, std::min(8000.0, t["midHz"].asNumber(s.midHz))); s.toneOn = t["on"].asBool(s.toneOn); }
    const json::Value& eq = v["eq"]; for (std::size_t b = 0; b < eq.size() && b < 8; ++b) { const json::Value& e = eq.at(b); Strip::Band& d = s.eq[b];
      d.type = std::max(0, std::min(8, e.at(0).asInt(0))); d.freq = std::max(10.0, std::min(24000.0, e.at(1).asNumber(1000))); d.q = std::max(0.05, std::min(36.0, e.at(2).asNumber(0.707))); d.gain = std::max(-24.0, std::min(24.0, e.at(3).asNumber(0))); } }
  if (o["master"].isObject()) masterDb_ = std::max(-120.0, std::min(10.0, o["master"]["db"].asNumber(masterDb_)));
  if (o["routes"].isObject()) {   // the console's ROUTING: what feeds each strip, what each output carries
    initRoutes(); const json::Value& ri = o["routes"]["in"]; const json::Value& ro = o["routes"]["out"];
    for (std::size_t i = 0; i < ri.size() && i < static_cast<std::size_t>(kMaxStrips); ++i) inRoute_[i] = std::max(-1, std::min(Runtime::kNetInBase - 1, ri.at(i).asInt(static_cast<int>(i))));
    for (std::size_t k = 0; k < ro.size() && k < static_cast<std::size_t>(Runtime::kMaxOutputs); ++k) outRoute_[k] = std::max(Runtime::kSrcOff, std::min(Runtime::kSrcMasterMono, ro.at(k).asInt(0))); }
  if (o["masterChain"].isObject()) {   // the console's RTA EQ and mastering: they keep sounding while the service plays
    const json::Value& m = o["masterChain"]; mp_.eqOn = m["eqOn"].asBool(mp_.eqOn);
    const json::Value& eq = m["eq"]; for (std::size_t b = 0; b < eq.size() && b < static_cast<std::size_t>(MasterChain::kBands); ++b) { const json::Value& e = eq.at(b);
      mp_.eq[b] = dsp::EqBand{static_cast<dsp::FilterType>(std::max(0, std::min(6, e["type"].asInt(0)))), std::max(10.0, std::min(24000.0, e["freq"].asNumber(1000))), std::max(0.1, std::min(36.0, e["q"].asNumber(1))), std::max(-24.0, std::min(24.0, e["gainDb"].asNumber(0)))}; }
    auto num = [&](const char* k, double& v, double lo, double hi) { if (m.has(k)) v = std::max(lo, std::min(hi, m[k].asNumber(v))); };
    auto flag = [&](const char* k, bool& v) { if (m.has(k)) v = m[k].asBool(v); };
    flag("on", mp_.on); flag("analog", mp_.analog); flag("toneOn", mp_.toneOn); flag("compOn", mp_.compOn); flag("satOn", mp_.satOn); flag("deessOn", mp_.deessOn); flag("exciterOn", mp_.exciterOn); flag("bassMonoOn", mp_.bassMonoOn);
    num("lowDb", mp_.lowDb, -24, 24); num("midDb", mp_.midDb, -24, 24); num("airDb", mp_.airDb, -24, 24); num("thrDb", mp_.thrDb, -60, 0); num("ratio", mp_.ratio, 1, 20); num("attackMs", mp_.attackMs, 0.1, 500);
    num("releaseMs", mp_.releaseMs, 5, 5000); num("makeupDb", mp_.makeupDb, -24, 24); num("drive", mp_.drive, 0, 1); num("gainDb", mp_.gainDb, -24, 24); num("compMix", mp_.compMix, 0, 1);
    num("deessThrDb", mp_.deessThrDb, -60, 0); num("deessMaxDb", mp_.deessMaxDb, 0, 24); num("exciterAmt", mp_.exciterAmt, 0, 1); num("bassMonoHz", mp_.bassMonoHz, 20, 500); ++masterV_; }
  if (o["racks"].isArray()) {   // the console's plug-ins: played by the service while it runs the sound
    racks_.clear(); rackNames_.clear(); const json::Value& rs = o["racks"];
    for (std::size_t i = 0; i < rs.size(); ++i) { const json::Value& r = rs.at(i); const int strip = r["strip"].asInt(-1); if (strip < 0 || strip >= opt_.strips) continue;
      rackNames_[strip] = r["name"].asString(); const json::Value& ps = r["plugins"];
      for (std::size_t k = 0; k < ps.size() && k < 64; ++k) { const json::Value& q = ps.at(k); PlugSlot sl; sl.slot = q["slot"].asInt(static_cast<int>(k)); sl.kind = q["kind"].asString(); if (sl.kind.empty()) continue;
        sl.name = q["name"].asString(sl.kind); sl.bypass = q["bypass"].asBool(false); sl.state = q["state"].asString(); racks_[strip].push_back(sl); } }
    ++racksV_; }
  if (o["settings"].isObject()) { const json::Value& c = o["settings"];   // the console's settings: the service starts sounding like the console
    set_.width = std::max(0.0, std::min(2.0, c["width"].asNumber(set_.width))); { const int b = c["outputBits"].asInt(set_.outputBits); if (b == 0 || b == 16 || b == 24 || b == -1) set_.outputBits = b; }
    set_.dcBlock = c["dcBlock"].asBool(set_.dcBlock); set_.analog = c["analog"].asBool(set_.analog); set_.autoLevel = c["autoLevel"].asBool(set_.autoLevel);
    set_.autoTarget = std::max(-30.0, std::min(-6.0, c["autoTarget"].asNumber(set_.autoTarget))); set_.autoMax = std::max(0.0, std::min(24.0, c["autoMax"].asNumber(set_.autoMax)));
    set_.clipDb = std::max(-12.0, std::min(0.0, c["clipDb"].asNumber(set_.clipDb))); set_.peakHoldMs = std::max(0.0, std::min(10000.0, c["peakHoldMs"].asNumber(set_.peakHoldMs)));
    set_.winGain = c["winGain"].asBool(set_.winGain); set_.winMuteSync = c["winMuteSync"].asBool(set_.winMuteSync); }
  ++version_; return true;
}

json::Value Daemon::apply(const json::Value& cmd, std::string& err) {
  std::lock_guard<std::mutex> g(m_);
  json::Value ok = json::Value::object(); ok.set("ok", true);
  const std::string op = cmd["op"].asString();
  auto strip = [&](int& s) { s = cmd["strip"].asInt(-1); if (s < 0 || s >= opt_.strips) { err = "strip out of range"; return false; } return true; };
  auto bus = [&](int& b) { b = cmd["bus"].asInt(-1); if (b < 0 || b >= kMaxBuses) { err = "bus out of range"; return false; } return true; };
  auto need = [&]() { if (!rt_) { err = "no device is open: send {\"op\":\"open\"} first"; return false; } return true; };
  int s = 0, b = 0;
  // RECORD: {"op":"record","on":true,"format":"flac"|"wav24"|"wavf32"} writes the master to <data>/recordings/<time>-master.*; on:false stops
  if (op == "record") {
    if (!cmd["on"].asBool()) { rta_.next.store(nullptr); rec_.stop(); return ok; }
    if (!need()) return {}; if (rec_.recording()) { err = "already recording"; return {}; }
    const std::string f = cmd["format"].asString("flac"); rec::Options o; o.folder = paths::file("recordings"); o.master = true; o.format = f == "flac" ? 4 : f == "wavf32" ? 1 : 0;
    if (!rec_.start(o, rate_, err)) return {}; rta_.next.store(&rec_);   // the recorder rides behind the RTA's tap
    ok.set("file", rec_.files().empty() ? std::string() : rec_.files()[0]); return ok;
  }
  if (op == "open") {
    if (rt_) { rt_->setTap(nullptr); rta_.next.store(nullptr); rec_.stop(); rt_->stop(); rt_.reset(); }
    DeviceConfig cfg; cfg.backend = cmd["backend"].asString("auto"); cfg.inDevice = cmd["in"].asString(""); cfg.outDevice = cmd["out"].asString("");
    cfg.sampleRate = cmd["rate"].asNumber(48000); cfg.frames = static_cast<unsigned>(std::max(16, cmd["frames"].asInt(256)));
    cfg.inChannels = std::max(0, std::min(kMaxStrips, cmd["inChannels"].asInt(2))); cfg.outChannels = std::max(0, std::min(32, cmd["outChannels"].asInt(2))); cfg.exclusive = cmd["exclusive"].asBool(false);
    auto rt = std::make_unique<Runtime>(opt_.strips);
    if (!rt->open(cfg, err)) { lastError_ = err; return {}; }
    if (!rt->start(err)) { lastError_ = err; return {}; }
    // replay the console onto the fresh runtime so that what the phone shows is what sounds
    for (int i = 0; i < opt_.strips; ++i) { const Strip& st = strips_[static_cast<std::size_t>(i)]; rt->setGainDb(i, st.db); rt->setPan(i, st.pan); rt->setMute(i, st.mute); rt->setSolo(i, st.solo);
      rt->setTrimDb(i, st.trim); pushTone(*rt, i);
      for (int b = 0; b < 8; ++b) { const Strip::Band& e = st.eq[b]; if (e.type) rt->setEqBand(i, b, static_cast<dsp::FilterType>(e.type), e.freq, e.q, e.gain); } }
    for (int k = 0; k < kMaxBuses; ++k) { rt->setBusLevel(k, buses_[k].db); rt->setBusMute(k, buses_[k].mute); rt->setBusReturn(k, buses_[k].returnDb); }
    pushSettings(*rt); loadRacks(*rt); rt->setTap(&rta_);
    mp_.fs = rt->sampleRate(); master_.set(mp_); rt->setMasterProcessor(&master_);   // the console's RTA EQ and mastering, as handed over
    rt->setLimiter(limiter_, ceiling_); rt->setMonitor(mon_.source, mon_.db, mon_.dim, mon_.mono, mon_.mute);
    pads_.prepare(rt->sampleRate()); rt->setExtraSource(&pads_);   // the SAMPLE PADS, at this device's rate
    initRoutes(); for (int i = 0; i < opt_.strips; ++i) rt->setInputRoute(i, inRoute_[i]); for (int o = 0; o < Runtime::kMaxOutputs; ++o) rt->setOutputRoute(o, outRoute_[o]);   // the routing as kept
    ovActed_ = false; ov_.reset(); ovNote_.clear();   // ONE VOICE looks again on every device
    lastCfg_ = cfg; rt_ = std::move(rt); applyMaster(*rt_); backend_ = rt_->backendName(); lastError_.clear(); rate_ = cfg.sampleRate;   // the trim knows the new output
    ok.set("backend", backend_); ok.set("rate", cfg.sampleRate); ok.set("frames", static_cast<int>(cfg.frames)); return ok;
  }
  if (op == "stop") { if (rt_) { rt_->setTap(nullptr); rta_.next.store(nullptr); rec_.stop(); rt_->stop(); rt_.reset(); } backend_.clear(); return ok; }
  if (op == "gain") { if (!strip(s)) return {}; strips_[static_cast<std::size_t>(s)].db = clampDb(cmd["db"].asNumber(0)); if (rt_) rt_->setGainDb(s, strips_[static_cast<std::size_t>(s)].db); return ok; }
  if (op == "pan") { if (!strip(s)) return {}; double p = cmd["pan"].asNumber(0); p = p < -1 ? -1 : p > 1 ? 1 : p; strips_[static_cast<std::size_t>(s)].pan = p; if (rt_) rt_->setPan(s, p); return ok; }
  if (op == "mute") { if (!strip(s)) return {}; const bool on = cmd["on"].asBool(); strips_[static_cast<std::size_t>(s)].mute = on; if (rt_) rt_->setMute(s, on); return ok; }
  if (op == "solo") { if (!strip(s)) return {}; const bool on = cmd["on"].asBool(); strips_[static_cast<std::size_t>(s)].solo = on; if (rt_) rt_->setSolo(s, on); return ok; }
  if (op == "name") { if (!strip(s)) return {}; std::string n = cmd["name"].asString(); if (n.size() > 24) n.resize(24); if (n.empty()) { err = "empty name"; return {}; } strips_[static_cast<std::size_t>(s)].name = n; return ok; }
  if (op == "master") { masterDb_ = clampDb(cmd["db"].asNumber(0)); if (rt_) applyMaster(*rt_); return ok; }
  if (op == "rta") {   // the analyzer's source; a remote renews it while its page is open
    const json::Value& v = cmd["src"]; const int src = v.type() == json::Value::String ? Rta::kOff : std::max(-1, std::min(opt_.strips - 1, v.asInt(Rta::kOff)));
    rta_.source.store(src); rtaUntil_ = src == Rta::kOff ? 0 : static_cast<long long>(std::time(nullptr)) + 20; ok.set("src", src); return ok; }
  if (op == "programEq") {   // the RTA EQ on the master: ten gains at the ISO octaves (a bell each, Q 1.41), on / off
    const json::Value& g = cmd["gains"]; const bool on = cmd["on"].asBool(mp_.eqOn);
    if (g.isArray()) { if (g.size() != 10) { err = "gains: 10 values"; return {}; }
      for (int b = 0; b < 10; ++b) { const double db = std::max(-24.0, std::min(24.0, g.at(static_cast<std::size_t>(b)).asNumber(0)));
        mp_.eq[b] = dsp::EqBand{std::fabs(db) > 0.01 ? dsp::FilterType::Peaking : dsp::FilterType::Off, kIso[b], 1.41, db}; } }
    mp_.eqOn = on; master_.set(mp_); ++masterV_; return ok; }
  if (op == "sample") {   // a SAMPLE PAD (the console's 100 one-shots) into the master: {id 1..100} or {stop: true}; gainDb sets the pads' level
    if (cmd.has("gainDb")) pads_.gainDb.store(std::max(-40.0, std::min(12.0, cmd["gainDb"].asNumber(-6))));
    if (cmd["stop"].asBool(false)) { pads_.stopAll(); return ok; }
    if (cmd.has("id")) { if (!rt_) { err = "no sound card open"; return {}; } const int id = cmd["id"].asInt(0); if (id < 1 || id > 100) { err = "id: 1..100"; return {}; }
      if (!pads_.play("sample:" + std::to_string(id), 0, 0, err)) return {}; }
    ok.set("voices", pads_.active.load()); return ok; }
  if (op == "pad") {   // a CUSTOM PAD {n 1..1000}: plays what the pad holds at the pad's gain (on top of the pads' level)
    if (!rt_) { err = "no sound card open"; return {}; }
    bank_->reloadIfChanged(); const int n = cmd["n"].asInt(0); if (n < 1 || n > pads::kPads) { err = "n: 1..1000"; return {}; }
    const pads::Pad p = bank_->at(n); if (!pads_.play(p.source, n, p.gainDb, err)) return {};
    ok.set("n", n); ok.set("voices", pads_.active.load()); return ok; }
  if (op == "sfx") {   // a SOUND EFFECT straight from the list {id 1..1000} (to try one before putting it on a pad)
    if (!rt_) { err = "no sound card open"; return {}; } const int id = cmd["id"].asInt(0); if (id < 1 || id > sfx::kCount) { err = "id: 1..1000"; return {}; }
    if (!pads_.play("sfx:" + std::to_string(id), 0, 0, err)) return {}; ok.set("voices", pads_.active.load()); return ok; }
  if (op == "padSet" || op == "padReset") {   // change a CUSTOM PAD from a remote: {n, source "sample:N" | "sfx:N" | "", name, gainDb, color} or back to its default; a file is chosen on the computer only
    bank_->reloadIfChanged(); const int n = cmd["n"].asInt(0); if (n < 1 || n > pads::kPads) { err = "n: 1..1000"; return {}; }
    if (op == "padReset") bank_->resetPad(n);
    else { pads::Pad p = bank_->at(n); if (cmd.has("source")) { p.source = cmd["source"].asString(); if (!cmd.has("name")) p.name.clear(); }
      if (cmd.has("name")) p.name = cmd["name"].asString(); if (cmd.has("gainDb")) p.gainDb = cmd["gainDb"].asNumber(p.gainDb); if (cmd.has("color")) p.color = cmd["color"].asInt(p.color);
      if (!bank_->set(n, p, p.source == bank_->at(n).source, err)) return {}; }   // an unchanged file pad can be renamed / levelled from a phone
    if (!bank_->save(err)) return {};
    const pads::Pad now = bank_->at(n); ok.set("n", n); ok.set("source", now.source); ok.set("name", now.name); ok.set("gainDb", now.gainDb); ok.set("color", now.color); ok.set("padsVersion", bank_->version()); return ok; }
  if (op == "mastering") {   // the master chain from a remote: any of on, low, air (dB), thr (dB), ratio, attack, release (ms), makeup, drive (0..1), gain (dB)
    auto num = [&](const char* k, double& v, double lo, double hi) { if (cmd.has(k)) v = std::max(lo, std::min(hi, cmd[k].asNumber(v))); };
    if (cmd.has("on")) mp_.on = cmd["on"].asBool(mp_.on);
    num("low", mp_.lowDb, -12, 12); num("air", mp_.airDb, -12, 12); num("thr", mp_.thrDb, -40, 0); num("ratio", mp_.ratio, 1, 20); num("attack", mp_.attackMs, 0.1, 200);
    num("release", mp_.releaseMs, 10, 2000); num("makeup", mp_.makeupDb, 0, 18); num("drive", mp_.drive, 0, 1); num("gain", mp_.gainDb, -12, 12);
    if (cmd.has("preset")) { masterPreset_ = cmd["preset"].asString(); if (masterPreset_.size() > 40) masterPreset_.resize(40); }   // a preset by NAME: the console applies its values and sends them here
    master_.set(mp_); ++masterV_; ++version_; return ok; }
  if (op == "rack") {   // the console's rack of one strip, as it is now (a plug-in added, removed, a knob turned in its window, bypass):
    // slots that keep their plug-in keep their instance and take the new state; the rest are made; the chain is handed over at once
    if (!strip(s)) return {}; const json::Value& ps = cmd["plugins"]; if (!ps.isArray()) { err = "plugins: a list"; return {}; }
    std::vector<PlugSlot> old = std::move(racks_[s]); std::vector<PlugSlot> now;
    for (std::size_t k = 0; k < ps.size() && k < 64; ++k) { const json::Value& q = ps.at(k); PlugSlot sl; sl.slot = q["slot"].asInt(static_cast<int>(k)); sl.kind = q["kind"].asString(); if (sl.kind.empty()) continue;
      sl.name = q["name"].asString(sl.kind); sl.bypass = q["bypass"].asBool(false); sl.state = q["state"].asString();
      for (PlugSlot& o : old) if (o.slot == sl.slot && o.kind == sl.kind && o.pl) { sl.pl = o.pl; if (!sl.state.empty() && sl.state != o.state) sl.pl->setState(sl.state); sl.pl->setBypass(sl.bypass); }
      now.push_back(std::move(sl)); }
    if (now.empty()) racks_.erase(s); else racks_[s] = std::move(now);
    if (cmd.has("name")) rackNames_[s] = cmd["name"].asString();
    if (rt_) loadRack(*rt_, s); ++racksV_; racksFromConsole_ = true; return ok; }
  if (op == "plugin") {   // a plug-in of the console's racks: bypass or one parameter (0..1)
    const int st = cmd["strip"].asInt(-1), sl = cmd["slot"].asInt(-1); auto it = racks_.find(st); PlugSlot* p = nullptr;
    if (it != racks_.end()) for (PlugSlot& q : it->second) if (q.slot == sl) p = &q;
    if (!p) { err = "no plug-in in that slot"; return {}; }
    if (cmd.has("bypass")) { p->bypass = cmd["bypass"].asBool(false); if (p->pl) p->pl->setBypass(p->bypass); }
    else if (cmd.has("param")) { if (!p->pl) { err = "that plug-in is not loaded here: " + (p->error.empty() ? std::string("no loader") : p->error); return {}; }
      const double v = cmd["value"].asNumber(-1); if (!(v >= 0 && v <= 1)) { err = "value: 0..1"; return {}; }
      const auto id = static_cast<std::uint32_t>(cmd["param"].asNumber(-1)); bool known = false; for (const auto& q : p->pl->params()) if (q.id == id && !q.readOnly) known = true;
      if (!known) { err = "no such parameter"; return {}; } p->pl->setParam(id, v); ok.set("text", p->pl->paramText(id, v)); }
    else { err = "bypass or param"; return {}; }
    ++racksV_; racksFromConsole_ = false; return ok; }
  if (op == "setting") {   // a console setting from a remote (see daemon.hpp); every value is checked here
    const std::string k = cmd["key"].asString(); const json::Value& v = cmd["value"];
    if (k == "width") { set_.width = std::max(0.0, std::min(2.0, v.asNumber(1))); if (rt_) rt_->setWidth(set_.width); }
    else if (k == "outputBits") { const int b = v.asInt(0); if (b != 0 && b != 16 && b != 24 && b != -1) { err = "outputBits: 0, 16, 24 or -1"; return {}; } set_.outputBits = b; if (rt_) rt_->setOutputBits(b < 0 ? 0 : b); }
    else if (k == "dcBlock") { set_.dcBlock = v.asBool(true); if (rt_) rt_->setDcBlock(set_.dcBlock); }
    else if (k == "analog") { set_.analog = v.asBool(false); if (rt_) rt_->setAnalogChannels(set_.analog); }
    else if (k == "autoLevel" || k == "autoTarget" || k == "autoMax") {
      if (k == "autoLevel") set_.autoLevel = v.asBool(false); else if (k == "autoTarget") set_.autoTarget = std::max(-30.0, std::min(-6.0, v.asNumber(-16))); else set_.autoMax = std::max(0.0, std::min(24.0, v.asNumber(12)));
      if (rt_) rt_->setAutoLevel(set_.autoLevel, set_.autoTarget, set_.autoMax); }
    else if (k == "clipDb") { set_.clipDb = std::max(-12.0, std::min(0.0, v.asNumber(-0.1))); if (rt_) rt_->setClipDb(set_.clipDb); }
    else if (k == "peakHoldMs") { set_.peakHoldMs = std::max(0.0, std::min(10000.0, v.asNumber(1500))); if (rt_) rt_->setPeakHoldMs(set_.peakHoldMs); }
    else if (k == "winGain") { set_.winGain = v.asBool(false); if (rt_) applyMaster(*rt_); }
    else if (k == "winMuteSync") set_.winMuteSync = v.asBool(true);   // the console applies this rule (it watches the master)
    else if (k == "winVolume") { const int lv = std::max(0, std::min(100, v.asInt(-1)));
      if (!volSet_ || v.asInt(-1) < 0 || !volSet_(static_cast<float>(lv / 100.0))) { err = "the Windows volume is not available on this computer"; return {}; }
      winVol_ = lv; if (rt_) applyMaster(*rt_); }
    else { err = "unknown setting " + k; return {}; }
    return ok; }
  if (op == "limiter") { limiter_ = cmd["on"].asBool(true); ceiling_ = clampDb(cmd["ceilingDb"].asNumber(-0.5), -20, 0); if (rt_) rt_->setLimiter(limiter_, ceiling_); return ok; }
  if (op == "send") { if (!strip(s) || !bus(b)) return {}; if (!need()) return {}; rt_->setSend(s, b, clampDb(cmd["db"].asNumber(-120)), cmd["pre"].asBool(false)); return ok; }
  if (op == "busLevel") { if (!bus(b)) return {}; buses_[b].db = clampDb(cmd["db"].asNumber(0)); if (rt_) rt_->setBusLevel(b, buses_[b].db); return ok; }
  if (op == "busMute") { if (!bus(b)) return {}; buses_[b].mute = cmd["on"].asBool(); if (rt_) rt_->setBusMute(b, buses_[b].mute); return ok; }
  if (op == "busReturn") { if (!bus(b)) return {}; buses_[b].returnDb = clampDb(cmd["db"].asNumber(-120)); if (rt_) rt_->setBusReturn(b, buses_[b].returnDb); return ok; }
  if (op == "eq") {
    if (!strip(s)) return {};
    const int band = cmd["band"].asInt(-1); if (band < 0 || band > 7) { err = "band out of range"; return {}; }
    static const char* names[] = {"off", "peaking", "lowshelf", "highshelf", "lowpass", "highpass", "notch", "allpass1", "allpass2"};
    const std::string t = cmd["type"].asString("peaking"); int ti = -1; for (int i = 0; i < 9; ++i) if (t == names[i]) ti = i;
    if (ti < 0) { err = "unknown filter type"; return {}; }
    // kept here (every remote and the console see it, LIVE SYNC), sounding at once when a device is open
    Strip::Band& e = strips_[static_cast<std::size_t>(s)].eq[band]; e.type = ti; e.freq = std::max(10.0, std::min(24000.0, cmd["freq"].asNumber(1000)));
    e.q = std::max(0.05, std::min(36.0, cmd["q"].asNumber(0.707))); e.gain = clampDb(cmd["gainDb"].asNumber(0), -24, 24);
    if (rt_) rt_->setEqBand(s, band, static_cast<dsp::FilterType>(e.type), e.freq, e.q, e.gain); ++version_; return ok;
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
  if (op == "inRoute") { if (!strip(s)) return {}; initRoutes(); inRoute_[s] = std::max(-1, std::min(Runtime::kNetInBase - 1, cmd["input"].asInt(-1))); if (rt_) rt_->setInputRoute(s, inRoute_[s]); ++version_; return ok; }
  if (op == "outRoute") { const int o = cmd["out"].asInt(-1); if (o < 0 || o >= Runtime::kMaxOutputs) { err = "out of range"; return {}; }
    const int src = cmd["source"].asInt(0); if (src < Runtime::kSrcOff || src > Runtime::kSrcMasterMono) { err = "source out of range"; return {}; }
    initRoutes(); outRoute_[o] = src; if (rt_) rt_->setOutputRoute(o, src); ++version_; return ok; }
  if (op == "trim") { if (!strip(s)) return {}; Strip& st = strips_[static_cast<std::size_t>(s)]; st.trim = clampDb(cmd["db"].asNumber(0), -24, 24); if (rt_) rt_->setTrimDb(s, st.trim); ++version_; return ok; }
  if (op == "tone") {   // the analog 3-band channel EQ: any of low / mid / midHz / high / on; the rest stays
    if (!strip(s)) return {}; Strip& st = strips_[static_cast<std::size_t>(s)]; auto g = [](double v) { return std::max(-15.0, std::min(15.0, v)); };
    if (cmd.has("low")) st.low = g(cmd["low"].asNumber(st.low)); if (cmd.has("mid")) st.mid = g(cmd["mid"].asNumber(st.mid)); if (cmd.has("high")) st.high = g(cmd["high"].asNumber(st.high));
    if (cmd.has("midHz")) st.midHz = std::max(100.0, std::min(8000.0, cmd["midHz"].asNumber(st.midHz))); if (cmd.has("on")) st.toneOn = cmd["on"].asBool(st.toneOn);
    if (rt_) pushTone(*rt_, s); ++version_; return ok; }
  if (op == "clearClips") { if (rt_) rt_->clearClips(); return ok; }
  err = op.empty() ? "no op" : "unknown op \"" + op + "\""; return {};
}

}  // namespace am
