#include <ctime>
// amixos-daemon on the null backend: the JSON parser, the token gate, control -> state, the meters frame over HTTP and over WebSocket.
#include <fstream>
#include <iterator>
#include <cassert>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "daemon/daemon.hpp"
#include <atomic>

namespace {
// a stand-in plug-in: GAIN and TONE, counts its audio calls, keeps its state as "gain,tone"
struct FakePlugin final : am::host::Plugin {
  am::host::Info inf; std::string path = "fake"; double gain = 0.5, tone = 0.25; bool byp = false; std::atomic<int> calls{0};
  FakePlugin() { inf.format = "VST3"; inf.name = "Fake Gain"; inf.vendor = "Test Labs"; }
  const am::host::Info& info() const override { return inf; } const std::string& modulePath() const override { return path; }
  int inputChannels() const override { return 1; } int outputChannels() const override { return 1; } std::uint32_t latencySamples() const override { return 0; }
  std::vector<am::host::ParamInfo> params() const override { am::host::ParamInfo a; a.id = 7; a.title = "GAIN"; a.units = "dB"; a.normalized = gain; a.defaultNormalized = 0.5;
    am::host::ParamInfo b; b.id = 9; b.title = "TONE"; b.normalized = tone; b.defaultNormalized = 0.25; am::host::ParamInfo h; h.id = 11; h.title = "HIDDEN"; h.hidden = true; return {a, b, h}; }
  double param(std::uint32_t id) const override { return id == 7 ? gain : tone; }
  void setParam(std::uint32_t id, double v) override { if (id == 7) gain = v; else tone = v; }
  std::string paramText(std::uint32_t id, double v) const override { return id == 7 ? std::to_string(static_cast<int>(v * 24 - 12)) : std::to_string(static_cast<int>(v * 100)) + " %"; }
  bool hasBypass() const override { return true; } void setBypass(bool on) override { byp = on; } bool bypass() const override { return byp; }
  void process(const float* const* in, float* const* out, int frames) override { ++calls; for (int i = 0; i < frames; ++i) { out[0][i] = in ? in[0][i] : 0.f; out[1][i] = out[0][i]; } }
  std::string state() const override { return std::to_string(gain) + "," + std::to_string(tone); }
  bool setState(const std::string& s) override { const auto c = s.find(','); if (c == std::string::npos) return false; gain = std::atof(s.substr(0, c).c_str()); tone = std::atof(s.substr(c + 1).c_str()); return true; }
};
FakePlugin* g_fake = nullptr;
}  // namespace

using am::json::Value;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main() {
  // JSON round trip
  bool ok = false; Value v = Value::parse("{\"a\":[1,2.5,\"x\\u00e9\\ud83c\\udfb5\"],\"b\":{\"c\":true,\"d\":null},\"e\":-3e2}", ok);
  CHECK(ok && v.isObject() && v["a"].size() == 3 && v["a"].items()[1].asNumber() == 2.5 && v["b"]["c"].asBool() && v["e"].asNumber() == -300);
  CHECK(v["a"].items()[2].asString() == "x\xC3\xA9\xF0\x9F\x8E\xB5");
  CHECK(Value::parse(v.dump(), ok).dump() == v.dump() && ok);
  Value::parse("{\"a\":}", ok); CHECK(!ok); Value::parse("[1,]", ok); CHECK(!ok); Value::parse("{} x", ok); CHECK(!ok);
  CHECK(am::httpd::Server::sha1Base64("dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");   // RFC 6455 example

  am::Daemon::Options o; o.port = 0; o.token = "testtoken-0123456789abcdef0123456789"; o.strips = 8; o.tokenStore = "daemon_test.tokens.json"; std::remove(o.tokenStore.c_str());
  am::Daemon d(o); std::string err;
  CHECK(d.start(err));
  const unsigned short port = d.port(); CHECK(port != 0);

  int status = 0; std::string body;
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/status", "", {}, status, body) && status == 200);
  Value st = Value::parse(body, ok); CHECK(ok && st["name"].asString() == "A mix OS" && st["strips"].asInt() == 8 && !st["running"].asBool());
  // consoleId: 12 hex characters, stable, never the token itself
  CHECK(st["consoleId"].asString().size() == 12 && st["consoleId"].asString() == am::TokenStore::hashOf("amfx1-console-id:" + o.token).substr(0, 12) && body.find(o.token) == std::string::npos);
  CHECK(st["computer"].asString() == am::Daemon::computerName() && !st["computer"].asString().empty());
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/devices", "", {}, status, body) && status == 200);
  Value dv = Value::parse(body, ok); CHECK(ok && dv.isArray());
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/bluetooth", "", {}, status, body) && status == 401);   // no token: no Bluetooth list

  // control without the token: 401 and nothing changes
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"gain\",\"strip\":0,\"db\":-6}", {{"Content-Type", "application/json"}}, status, body) && status == 401);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"gain\",\"strip\":0,\"db\":-6}", {{"X-Amix-Token", "wrong"}}, status, body) && status == 401);
  CHECK(d.state()["strips"].items()[0]["db"].asNumber() == 0);
  // with the token: open the null backend, change the console, read it back
  const std::vector<std::pair<std::string, std::string>> auth = {{"X-Amix-Token", o.token}};
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/bluetooth", "", auth, status, body) && status == 200 && body.find("\"devices\"") != std::string::npos && body.find("\"radio\"") != std::string::npos);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"open\",\"backend\":\"null\",\"rate\":48000,\"frames\":128}", auth, status, body) && status == 200);
  Value r = Value::parse(body, ok); CHECK(ok && r["ok"].asBool() && r["backend"].asString() == "null");
  // the PRESET PACK built in (releases/presets/presets-pack.json) and a MASTERING PRESET picked by name: the console applies its values
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/presets", "", {}, status, body) && status == 200);
  { const Value pk = Value::parse(body, ok); CHECK(ok && pk["mastering"].size() >= 10 && pk["effects"].size() == 618 && pk["tone"].size() >= 1 && !pk["version"].asString().empty()); }
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"mastering\",\"preset\":\"WARM\"}", auth, status, body) && status == 200);
  CHECK(d.state()["mastering"]["preset"].asString() == "WARM");
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"gain\",\"strip\":2,\"db\":-6.5}", auth, status, body) && status == 200);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"name\",\"strip\":2,\"name\":\"Vox\"}", auth, status, body) && status == 200);
  { // RECORD from the remote: the master to FLAC in the data folder, shown in the status, stopped again; a file is left behind
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"record\",\"on\":true,\"format\":\"flac\"}", auth, status, body) && status == 200);
    bool jok = false; const Value rr = Value::parse(body, jok); CHECK(jok); const std::string file = rr["file"].asString(); CHECK(file.find("-master.flac") != std::string::npos);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/status", "", auth, status, body) && status == 200 && body.find("\"recording\":true") != std::string::npos);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"record\",\"on\":true}", auth, status, body) && status != 200);   // a second start is refused
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"record\",\"on\":false}", auth, status, body) && status == 200);
    FILE* f = std::fopen(file.c_str(), "rb"); char m[4] = {}; CHECK(f && std::fread(m, 1, 4, f) == 4 && !std::memcmp(m, "fLaC", 4)); if (f) std::fclose(f); std::remove(file.c_str()); }
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"mute\",\"strip\":3,\"on\":true}", auth, status, body) && status == 200);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"busLevel\",\"bus\":1,\"db\":-3}", auth, status, body) && status == 200);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"compressor\",\"strip\":2,\"on\":true,\"thresholdDb\":-20,\"ratio\":4}", auth, status, body) && status == 200);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"gain\",\"strip\":99,\"db\":0}", auth, status, body) && status == 400);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"nope\"}", auth, status, body) && status == 400);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "not json", auth, status, body) && status == 400);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"monitor\",\"source\":1,\"db\":-6,\"dim\":true,\"dual\":true,\"sourceB\":3,\"blend\":0.25,\"listen\":2}", auth, status, body) && status == 200);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/state", "", {}, status, body) && status == 200);
  Value s = Value::parse(body, ok); CHECK(ok);
  CHECK(s["monitor"]["source"].asInt() == 1 && s["monitor"]["db"].asNumber() == -6 && s["monitor"]["dim"].asBool() && !s["monitor"]["mute"].asBool());
  CHECK(s["monitor"]["dual"].asBool() && s["monitor"]["sourceB"].asInt() == 3 && s["monitor"]["blend"].asNumber() == 0.25 && s["monitor"]["listen"].asInt() == 2);   // DUAL LISTEN mirrored to the remote
  CHECK(s["strips"].items()[2]["db"].asNumber() == -6.5 && s["strips"].items()[2]["name"].asString() == "Vox" && s["strips"].items()[3]["mute"].asBool() && s["buses"].items()[1]["db"].asNumber() == -3 && s["running"].asBool());

  // THE NAMED TOKENS: only the master token makes them; a control token changes the console, a view token does not; a revoked one is refused;
  // the file holds hashes, never the token; the tokens survive a reload
  { const char* T = "/api/tokens";
    CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", T, "", {}, status, body) && status == 401);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", T, "{\"name\":\"Phone\",\"role\":\"control\",\"days\":7}", auth, status, body) && status == 200);
    Value a = Value::parse(body, ok); const std::string ctl = a["token"].asString(), ctlId = a["id"].asString(); CHECK(ok && a["ok"].asBool() && ctl.size() == 68 && ctl.compare(0, 4, "amx_") == 0);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", T, "{\"name\":\"Stage screen\",\"role\":\"view\"}", auth, status, body) && status == 200);
    const std::string view = Value::parse(body, ok)["token"].asString(); CHECK(view.size() == 68 && view != ctl);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", T, "{\"name\":\"x\",\"role\":\"owner\"}", auth, status, body) && status == 400);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", T, "{\"role\":\"view\"}", auth, status, body) && status == 400);
    const char* G = "{\"op\":\"gain\",\"strip\":4,\"db\":-9}";
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", G, {{"X-Amix-Token", ctl}}, status, body) && status == 200);
    CHECK(d.state()["strips"].items()[4]["db"].asNumber() == -9);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"gain\",\"strip\":4,\"db\":-1}", {{"X-Amix-Token", view}}, status, body) && status == 403);
    CHECK(d.state()["strips"].items()[4]["db"].asNumber() == -9);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", T, "{\"name\":\"y\",\"role\":\"control\"}", {{"X-Amix-Token", ctl}}, status, body) && status == 403);   // a control token cannot make tokens
    CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", T, "", auth, status, body) && status == 200);
    CHECK(body.find(ctl) == std::string::npos && body.find(view) == std::string::npos && body.find("hash") == std::string::npos && Value::parse(body, ok)["tokens"].size() == 2);
    { std::ifstream f(o.tokenStore); std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); CHECK(!all.empty() && all.find(ctl) == std::string::npos && all.find(view) == std::string::npos && all.find(am::TokenStore::hashOf(ctl)) != std::string::npos); }
    { am::TokenStore again(o.tokenStore); std::string e; CHECK(again.load(e) && again.roleOf(ctl) == "control" && again.roleOf(view) == "view" && again.roleOf(ctl, 4102444800LL).empty() && again.roleOf(view, 4102444800LL) == "view"); }   // the 7-day token has ended by 2100, the endless one has not
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/tokens/revoke", "{\"id\":\"" + ctlId + "\"}", auth, status, body) && status == 200);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", G, {{"X-Amix-Token", ctl}}, status, body) && status == 401);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/tokens/revoke", "{\"id\":\"nope\"}", auth, status, body) && status == 400);
    // SECURITY: only pages of this computer may read replies; a made-up host name is refused, addresses and localhost are served
    CHECK(am::Daemon::originAllowed("http://127.0.0.1:5173") && am::Daemon::originAllowed("http://localhost") && !am::Daemon::originAllowed("https://evil.example") && !am::Daemon::originAllowed("http://127.0.0.1.evil.example") && !am::Daemon::originAllowed("http://localhost.evil.example"));
    CHECK(am::Daemon::hostAllowed("127.0.0.1:8765") && am::Daemon::hostAllowed("192.168.1.20:8765") && am::Daemon::hostAllowed("localhost:8765") && am::Daemon::hostAllowed("[::1]:8765") && am::Daemon::hostAllowed(""));
    CHECK(!am::Daemon::hostAllowed("evil.example:8765") && !am::Daemon::hostAllowed("127.0.0.1.evil.example") && !am::Daemon::hostAllowed("1234.1.1.1") && !am::Daemon::hostAllowed("1..1.1"));
    CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/state", "", {}, status, body) && status == 200);
    { // SAME ADDRESS ONLY: the first address owns a named token, another address is refused, and it stays so after a reload
      const std::string f = "daemon_test.pin.json"; std::remove(f.c_str()); am::TokenStore ts(f); std::string e, id; CHECK(ts.load(e)); const std::string t = ts.issue("Pinned", "control", 0, id, e);
      CHECK(ts.roleFor(t, "192.168.1.20") == "control" && ts.roleFor(t, "192.168.1.21").empty() && ts.roleFor(t, "192.168.1.20") == "control");
      am::TokenStore again(f); CHECK(again.load(e) && again.roleFor(t, "192.168.1.21").empty() && again.roleFor(t, "192.168.1.20") == "control" && again.list()[0].ip == "192.168.1.20"); std::remove(f.c_str()); }
    { // PAIRING PIN: six digits, another one each minute, the same within a minute; a wrong PIN is refused, the right one gives a control token that works, and the PIN is spent
      const std::string p1 = am::Daemon::pinFor(o.token, 600), p2 = am::Daemon::pinFor(o.token, 659), p3 = am::Daemon::pinFor(o.token, 660);
      CHECK(p1.size() == 6 && p1 == p2 && p1 != p3 && p1.find_first_not_of("0123456789") == std::string::npos && am::Daemon::pinFor("another-master-token-123", 600) != p1);
      const std::string P = "/api/pair"; const std::string pin = am::Daemon::pinFor(o.token, static_cast<long long>(std::time(nullptr)));
      CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", P, "{\"pin\":\"000000x\"}", {}, status, body) && status == 401);
      CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", P, "{\"pin\":\"" + pin + "\",\"name\":\"Paired phone\"}", {}, status, body) && (status == 200 || status == 401));   // 401 only if the minute turned twice in between
      if (status == 200) { const std::string pt = Value::parse(body, ok)["token"].asString(); CHECK(pt.size() == 68);
        CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"gain\",\"strip\":5,\"db\":-3}", {{"X-Amix-Token", pt}}, status, body) && status == 200);
        CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", P, "{\"pin\":\"" + pin + "\"}", {}, status, body) && status == 429); } }   // spent: the next phone needs the next PIN
    CHECK(am::TokenStore::same("abc", "abc") && !am::TokenStore::same("abc", "abd") && !am::TokenStore::same("abc", "abcd"));
    // SECURITY: a token in the URL does not open control (header only); after more than 20 wrong tokens in a minute the wrong ones get 429
    // while the right one still works
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control?token=" + o.token, G, {}, status, body) && status == 401);
    for (int k = 0; k < 24; ++k) am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", G, {{"X-Amix-Token", "wrong"}}, status, body);
    CHECK(status == 429 && d.refusedTokens() > 20);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", G, auth, status, body) && status == 200);
    std::remove(o.tokenStore.c_str()); }
  // the meters run (the null backend clocks blocks)
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/meters", "", {}, status, body) && status == 200);
  Value m = Value::parse(body, ok); CHECK(ok && m["running"].asBool() && m["blocks"].asNumber() > 0 && m["strips"].size() == 8 && m["master"].has("l"));

  // WebSocket: wrong token -> error frame and close; right token -> JSON frames
  int fd = am::httpd::Client::connectWs("127.0.0.1", port, "/ws/meters?token=bad", {}); CHECK(fd >= 0);
  { am::httpd::WebSocket ws(fd); std::string t; int op = 0; CHECK(ws.read(t, op) && op == 1 && t.find("unauthorised") != std::string::npos); ws.close(); }
  fd = am::httpd::Client::connectWs("127.0.0.1", port, "/ws/meters?token=" + o.token, {}); CHECK(fd >= 0);
  { am::httpd::WebSocket ws(fd); std::string t; int op = 0; int frames = 0;
    CHECK(ws.read(t, op) && op == 1); { Value f = Value::parse(t, ok); CHECK(ok && f["state"]["strips"].size() == 8 && f["state"]["v"].asNumber() > 0 && f["state"]["remotes"].asNumber() == 1); }   // LIVE SYNC: the state first; this watcher is counted
    for (int i = 0; i < 3; ++i) { CHECK(ws.read(t, op) && op == 1); Value f = Value::parse(t, ok); CHECK(ok && f["strips"].size() == 8); ++frames; }
    CHECK(frames == 3);
    // another remote moves strip 3: this watcher gets the new state pushed, without asking
    const double v0 = d.state()["v"].asNumber();
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"gain\",\"strip\":3,\"db\":-7.5}", auth, status, body) && status == 200);
    bool pushed = false;
    for (int i = 0; i < 40 && !pushed; ++i) { CHECK(ws.read(t, op)); Value f = Value::parse(t, ok); if (ok && f.has("state")) { pushed = f["state"]["strips"].at(3)["db"].asNumber() == -7.5 && f["state"]["v"].asNumber() > v0; } }
    // CONTROL ON THE OPEN SOCKET: the same command as a text frame on this connection lands at once (no new connection per move),
    // with an ack for its seq; a broken command answers with the error
    { const auto t0 = std::chrono::steady_clock::now();
      CHECK(ws.send("{\"op\":\"gain\",\"strip\":4,\"db\":-11.5,\"seq\":7}"));
      bool acked = false, landed = false;
      for (int i = 0; i < 60 && !(acked && landed); ++i) { CHECK(ws.read(t, op)); Value f = Value::parse(t, ok); if (!ok) continue;
        if (f.has("ack") && f["ack"].asNumber() == 7 && !f.has("controlError") && !f.has("error")) acked = true;
        if (f.has("state")) CHECK(f["wsControl"].asBool());
        if (f.has("state") && f["state"]["strips"].at(4)["db"].asNumber() == -11.5) landed = true; }
      const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
      CHECK(acked && landed && d.state()["strips"].at(4)["db"].asNumber() == -11.5); std::printf("daemon_test: control over the open socket: acked %d, in the state %d, %ld ms\n", acked, landed, ms);
      CHECK(ws.send("{\"op\":\"gain\",\"strip\":99,\"db\":0,\"seq\":8}")); bool refused = false;
      for (int i = 0; i < 60 && !refused; ++i) { CHECK(ws.read(t, op)); Value f = Value::parse(t, ok); if (ok && f.has("ack") && f["ack"].asNumber() == 8 && f.has("controlError") && !f.has("error")) refused = true; }
      CHECK(refused); }
    CHECK(pushed);
    ws.close(); }
  { // GET /api/state?since=N: a short answer while nothing changed, the full state after a change
    const std::string v = std::to_string(static_cast<unsigned long long>(d.stateVersion()));
    CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/state?since=" + v, "", {}, status, body) && status == 200 && Value::parse(body, ok)["same"].asBool());
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"mute\",\"strip\":2,\"on\":true}", auth, status, body) && status == 200);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/state?since=" + v, "", {}, status, body) && status == 200);
    Value st = Value::parse(body, ok); CHECK(ok && !st["same"].asBool() && st["strips"].at(2)["mute"].asBool()); }

  { // CONNECTED PHONES: a paired phone's live link is listed under its pairing's name; DISCONNECT closes it and holds the pairing off a while;
    // FORGET (revoke) closes the links of that pairing; nobody but the master token sees or drops links
    { bool idle = false; for (int i = 0; i < 40 && !idle; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(25)); idle = d.links().size() == 0; } CHECK(idle); }   // the earlier stream has ended
    std::string pid, e; const std::string pt = d.tokens().issue("Pixel 9 Pro", "control", 0, pid, e); CHECK(!pt.empty());
    int pf = am::httpd::Client::connectWs("127.0.0.1", port, "/ws/meters?token=" + pt, {}); CHECK(pf >= 0);
    am::httpd::WebSocket pws(pf); std::string t; int op = 0; CHECK(pws.read(t, op) && op == 1);   // the state on connect: the link is up
    int mf = am::httpd::Client::connectWs("127.0.0.1", port, "/ws/meters?token=" + o.token + "&client=Browser%20remote", {}); CHECK(mf >= 0);
    am::httpd::WebSocket mws(mf); CHECK(mws.read(t, op) && op == 1);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/links", "", {}, status, body) && (status == 401 || status == 429));   // no token (429 once the wrong tokens above passed 20 a minute)
    CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/links", "", {{"X-Amix-Token", pt}}, status, body) && status == 403);   // a phone may not
    CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/links", "", auth, status, body) && status == 200);
    Value ls = Value::parse(body, ok)["links"]; CHECK(ok && ls.size() == 2 && d.remotes() == 2);
    int phoneLink = 0; for (std::size_t i = 0; i < ls.size(); ++i) { const Value l = ls.at(i);
      if (l["tokenId"].asString() == pid) { phoneLink = l["id"].asInt(); CHECK(l["name"].asString() == "Pixel 9 Pro" && l["ip"].asString() == "127.0.0.1" && l["role"].asString() == "control" && l["seconds"].asNumber() >= 0); }
      else CHECK(l["tokenId"].asString() == "master" && l["name"].asString() == "Browser remote"); }
    CHECK(phoneLink > 0);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/links/kick", "{\"id\":" + std::to_string(phoneLink) + "}", {{"X-Amix-Token", pt}}, status, body) && status == 403);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/links/kick", "{\"id\":" + std::to_string(phoneLink) + "}", auth, status, body) && status == 200);
    bool kicked = false; for (int i = 0; i < 60 && !kicked; ++i) { if (!pws.read(t, op)) break; kicked = t.find("\"kicked\":true") != std::string::npos && t.find("\"holdSeconds\":120") != std::string::npos; }
    CHECK(kicked); pws.close();
    bool gone = false; for (int i = 0; i < 40 && !gone; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(25)); gone = d.links().size() == 1; } CHECK(gone);
    // held: the phone cannot come straight back (control and the live link are refused) until the hold ends
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"mute\",\"strip\":0,\"on\":false}", {{"X-Amix-Token", pt}}, status, body) && status == 403 && body.find("\"held\":true") != std::string::npos);
    { int hf = am::httpd::Client::connectWs("127.0.0.1", port, "/ws/meters?token=" + pt, {}); CHECK(hf >= 0); am::httpd::WebSocket hws(hf); CHECK(hws.read(t, op) && t.find("\"held\"") != std::string::npos && t.find("\"kicked\":true") != std::string::npos); hws.close(); }
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/links/kick", "{\"id\":99999}", auth, status, body) && status == 404);
    // FORGET: a second phone's pairing revoked while it is connected: its link closes at once (no restart of the service)
    std::string qid; const std::string qt = d.tokens().issue("Galaxy Tab", "view", 0, qid, e); CHECK(!qt.empty());
    int qf = am::httpd::Client::connectWs("127.0.0.1", port, "/ws/meters?token=" + qt, {}); CHECK(qf >= 0); am::httpd::WebSocket qws(qf); CHECK(qws.read(t, op) && op == 1);
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/tokens/revoke", "{\"id\":\"" + qid + "\"}", auth, status, body) && status == 200 && Value::parse(body, ok)["closed"].asInt() == 1);
    bool closed = false; for (int i = 0; i < 60 && !closed; ++i) { if (!qws.read(t, op)) { closed = true; break; } closed = t.find("\"kicked\":true") != std::string::npos; } CHECK(closed); qws.close();
    mws.close(); bool none = false; for (int i = 0; i < 40 && !none; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(25)); none = d.links().size() == 0 && d.remotes() == 0; } CHECK(none); }
  { // CHANNEL GAIN and the analog 3-BAND channel EQ: kept in the state (every remote shows them), clamped, partial updates keep the rest
    const auto ctl = [&](const std::string& j) { return am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", j, auth, status, body) ? status : -1; };
    CHECK(ctl("{\"op\":\"trim\",\"strip\":2,\"db\":30}") == 200 && d.state()["strips"].at(2)["trim"].asNumber() == 24);
    CHECK(ctl("{\"op\":\"tone\",\"strip\":2,\"low\":4,\"mid\":-20,\"midHz\":20000,\"high\":2.5}") == 200);
    { const Value t = d.state()["strips"].at(2)["tone"]; CHECK(t["low"].asNumber() == 4 && t["mid"].asNumber() == -15 && t["midHz"].asNumber() == 8000 && t["high"].asNumber() == 2.5 && t["on"].asBool()); }
    CHECK(ctl("{\"op\":\"tone\",\"strip\":2,\"high\":-3,\"on\":false}") == 200);
    { const Value t = d.state()["strips"].at(2)["tone"]; CHECK(t["low"].asNumber() == 4 && t["high"].asNumber() == -3 && !t["on"].asBool()); }
    CHECK(ctl("{\"op\":\"tone\",\"strip\":99,\"low\":1}") == 400);
    // DCA GROUP CHANNELS: kept and shared with every remote (the console applies them to its mix), clamped, members checked
    CHECK(ctl("{\"op\":\"dca\",\"group\":2,\"db\":30,\"mute\":true,\"name\":\"VOICES\",\"color\":3,\"members\":[0,1,999]}") == 200);
    { const Value g = d.state()["dcas"].at(1); CHECK(g["id"].asInt() == 2 && g["db"].asNumber() == 10 && g["mute"].asBool() && g["name"].asString() == "VOICES" && g["color"].asInt() == 3 && g["members"].size() == 2); }
    CHECK(d.state()["dcas"].size() == 8 && d.state()["dcas"].at(0)["name"].asString() == "DCA 1");
    CHECK(ctl("{\"op\":\"dca\",\"group\":9,\"db\":0}") == 400);
    CHECK(ctl("{\"op\":\"dca\",\"group\":3,\"winVol\":true}") == 200 && d.state()["dcas"].at(2)["winVol"].asBool() && d.state()["dcaLimit"].asInt() == 8);
    { am::Daemon::Options po; po.port = 0; po.token = o.token; po.strips = 8; po.plan = "PRO"; po.tokenStore = "daemon_test.pro.tokens.json"; std::remove(po.tokenStore.c_str()); am::Daemon pd(po);
      std::string e1, e2; bool k1 = false, k2 = false; const Value a = Value::parse("{\"op\":\"dca\",\"group\":4,\"db\":-3}", k1), b = Value::parse("{\"op\":\"dca\",\"group\":5,\"db\":-3}", k2);   // DCA GROUPS BY EDITION: PRO has four
      CHECK(k1 && k2 && !pd.control(a, e1).isNull() && pd.control(b, e2).isNull() && e2.find("not in this edition") != std::string::npos);
      CHECK(pd.state()["dcaLimit"].asInt() == 4 && pd.state()["dcas"].at(4)["locked"].asBool() && !pd.state()["dcas"].at(3)["locked"].asBool()); std::remove(po.tokenStore.c_str()); }
    // MASTERING from a remote: clamped, kept in the state, the master version moves (the console takes it); the SAMPLE PADS list
    { const double mv = d.state()["masterV"].asNumber();
      CHECK(ctl("{\"op\":\"mastering\",\"on\":true,\"thr\":-60,\"low\":3,\"makeup\":40}") == 200);
      const Value m = d.state()["mastering"]; CHECK(m["on"].asBool() && m["thr"].asNumber() == -40 && m["low"].asNumber() == 3 && m["makeup"].asNumber() == 18 && d.state()["masterV"].asNumber() > mv);
      CHECK(ctl("{\"op\":\"mastering\",\"on\":false,\"low\":0,\"makeup\":0,\"thr\":-12}") == 200);
      CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/samples", "", auth, status, body) && status == 200);
      const Value sl = Value::parse(body, ok)["samples"]; CHECK(ok && sl.size() == 100 && sl.at(0)["id"].asInt() == 1 && !sl.at(0)["name"].asString().empty());
      CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/samples", "", {}, status, body) && (status == 401 || status == 429)); }
    // 1000 CUSTOM PADS and 1000 SOUND EFFECTS: listed, changed and played from a remote; a file is never chosen from a phone
    { CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/sfx", "", auth, status, body) && status == 200);
      const Value fx = Value::parse(body, ok); CHECK(ok && fx["effects"].size() == 1000 && fx["families"].size() == 20 && fx["effects"].at(999)["name"].asString() == "Bell 50");
      CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/pads?from=95&to=104", "", auth, status, body) && status == 200);
      const Value pd = Value::parse(body, ok); CHECK(ok && pd["count"].asInt() == 1000 && pd["pads"].size() == 10 && pd["pads"].at(0)["n"].asInt() == 95 && pd["pads"].at(6)["source"].asString() == "sfx:1");
      CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/pads", "", {}, status, body) && (status == 401 || status == 429));
      const double pv = d.state()["padsVersion"].asNumber();
      CHECK(ctl("{\"op\":\"padSet\",\"n\":500,\"source\":\"sfx:999\",\"gainDb\":-3,\"color\":5}") == 200 && d.state()["padsVersion"].asNumber() > pv);
      CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/pads?from=500&to=500", "", auth, status, body) && status == 200);
      { const Value one = Value::parse(body, ok)["pads"].at(0); CHECK(one["source"].asString() == "sfx:999" && one["name"].asString() == "Bell 49" && one["gainDb"].asNumber() == -3 && one["custom"].asBool()); }
      CHECK(ctl("{\"op\":\"padSet\",\"n\":500,\"source\":\"file:C:/Windows/win.ini\"}") == 400 && ctl("{\"op\":\"padSet\",\"n\":1001,\"source\":\"sfx:1\"}") == 400 && ctl("{\"op\":\"padSet\",\"n\":3,\"source\":\"sfx:5000\"}") == 400);
      CHECK(ctl("{\"op\":\"padReset\",\"n\":500}") == 200);
      const int pc = ctl("{\"op\":\"pad\",\"n\":500}"); CHECK(pc == 200 || pc == 400); const int sc = ctl("{\"op\":\"sfx\",\"id\":42}"); CHECK(sc == 200 || sc == 400);
      CHECK(ctl("{\"op\":\"pad\",\"n\":0}") == 400 && ctl("{\"op\":\"sfx\",\"id\":1001}") == 400); }
    // INPUT while phones are linked: the service reopens its sound card with the microphone; with no sound card open it says so
    { const int code = ctl("{\"op\":\"input\",\"in\":\"\"}"); CHECK(code == 200 || code == 400);
      if (code == 200) { CHECK(d.status()["inChannels"].asInt() == 2 && d.status()["running"].asBool()); } }
    // ROUTING: kept without a device, in the state and on /api/routing with the sources by name; out of range refused
    { CHECK(ctl("{\"op\":\"inRoute\",\"strip\":3,\"input\":7}") == 200 && ctl("{\"op\":\"outRoute\",\"out\":2,\"source\":1}") == 200);
      CHECK(ctl("{\"op\":\"outRoute\",\"out\":2,\"source\":9999}") == 400 && ctl("{\"op\":\"outRoute\",\"out\":-1,\"source\":1}") == 400);
      const Value rt = d.state()["routing"]; CHECK(rt["in"].at(3).asInt() == 7 && rt["in"].at(4).asInt() == 4 && rt["out"].at(2).asInt() == 1);
      CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/routing", "", auth, status, body) && status == 200);
      const Value r2 = Value::parse(body, ok); CHECK(ok && r2["sources"].size() > 6 && r2["sources"].at(1)["name"].asString() == "MASTER L");
      CHECK(ctl("{\"op\":\"inRoute\",\"strip\":3,\"input\":3}") == 200 && ctl("{\"op\":\"outRoute\",\"out\":2,\"source\":5}") == 200); }
    CHECK(ctl("{\"op\":\"trim\",\"strip\":2,\"db\":0}") == 200 && ctl("{\"op\":\"tone\",\"strip\":2,\"low\":0,\"mid\":0,\"midHz\":1000,\"high\":0,\"on\":true}") == 200); }
  { // CONSOLE SETTINGS over the remote, and the WINDOWS VOLUME with GAIN MODE (a stand-in for the Windows slider)
    const auto set = [&](const std::string& k, const std::string& v) { return am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"setting\",\"key\":\"" + k + "\",\"value\":" + v + "}", auth, status, body) ? status : -1; };
    CHECK(set("width", "3") == 200 && d.state()["settings"]["width"].asNumber() == 2);                       // clamped to 0..2
    CHECK(set("outputBits", "7") == 400 && set("outputBits", "24") == 200 && d.state()["settings"]["outputBits"].asInt() == 24);
    CHECK(set("dcBlock", "false") == 200 && !d.state()["settings"]["dcBlock"].asBool());
    CHECK(set("analog", "true") == 200 && set("autoLevel", "true") == 200 && set("autoTarget", "-40") == 200 && d.state()["settings"]["autoTarget"].asNumber() == -30);
    CHECK(set("clipDb", "-3") == 200 && set("peakHoldMs", "99999") == 200 && d.state()["settings"]["peakHoldMs"].asNumber() == 10000);
    CHECK(set("nonsense", "1") == 400);
    CHECK(set("loudPreset", "\"BROADCAST\"") == 200 && d.state()["settings"]["loudTarget"].asNumber() == -23 && set("loudPreset", "\"LOUD\"") == 400);   // LOUDNESS MANAGER from a remote
    CHECK(set("loudRide", "true") == 200 && d.state()["settings"]["loudRide"].asBool() && set("loudTarget", "-50") == 200 && d.state()["settings"]["loudTarget"].asNumber() == -30 && d.state()["settings"]["loudPreset"].asString() == "CUSTOM");
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"setting\",\"key\":\"width\",\"value\":1}", {}, status, body) && (status == 401 || status == 429));   // no token, no settings
    float winLevel = 0.5f; d.setVolumeBackend([&](float& f) { f = winLevel; return true; }, [&](float f) { winLevel = f; return true; });
    d.pollVolume(); CHECK(d.state()["settings"]["winVolume"].asInt() == 50 && d.state()["settings"]["winTrimDb"].asNumber() == 0);   // GAIN MODE off: no trim
    CHECK(set("winGain", "true") == 200 && std::abs(d.state()["settings"]["winTrimDb"].asNumber() + 9.03) < 0.01);
    CHECK(set("winVolume", "25") == 200 && std::abs(winLevel - 0.25f) < 1e-6 && std::abs(d.state()["settings"]["winTrimDb"].asNumber() + 18.06) < 0.01);   // a remote moves the Windows slider
    const auto v0 = d.stateVersion(); winLevel = 1.0f; d.pollVolume();                                       // the keyboard's volume-up in Windows
    CHECK(d.stateVersion() > v0 && d.state()["settings"]["winVolume"].asInt() == 100 && d.state()["settings"]["winTrimDb"].asNumber() == 0);
    winLevel = 0.0f; d.pollVolume(); CHECK(d.state()["settings"]["winTrimDb"].asNumber() <= -119);
    CHECK(std::abs(am::Daemon::trimForLevel(0.1) + 30) < 1e-9 && am::Daemon::trimForLevel(0) <= -119);
    d.setVolumeBackend([](float&) { return false; }, [](float) { return false; }); CHECK(set("winVolume", "40") == 400);   // no Windows slider here: said so
    CHECK(set("winGain", "false") == 200 && set("winMuteSync", "false") == 200 && !d.state()["settings"]["winMuteSync"].asBool()); }
  { // PER-APP VOLUME over the remote: the list in the state, a checked change, the rate limit, a Windows-mixer move as a new state, no echo
    auto mem = std::make_unique<am::vol::MemoryLayer>(); am::vol::MemoryLayer* L = mem.get();
    L->add({"spotify#1", 42, "Spotify", "", 0.8f, false, false}); L->add({"sys", 0, "System sounds", "", 1.f, false, true});
    d.setAppLayer(std::move(mem));
    Value st2 = d.state(); CHECK(st2["appsAvailable"].asBool() && st2["apps"].size() == 2 && st2["apps"].items()[0]["system"].asBool() && st2["apps"].items()[1]["name"].asString() == "Spotify" && st2["apps"].items()[1]["volume"].asNumber() == 80);
    auto post = [&](const std::string& j) { std::string b2; int s2 = 0; am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", j, auth, s2, b2); return s2; };
    CHECK(post("{\"op\":\"appVolume\",\"id\":\"spotify#1\",\"volume\":25}") == 200 && d.state()["apps"].items()[1]["volume"].asNumber() == 25);
    const int listsBefore = L->lists; const auto v1 = d.stateVersion(); d.state(); CHECK(L->lists == listsBefore && d.stateVersion() == v1);   // its echo moved nothing, and the list was not read again
    CHECK(post("{\"op\":\"appVolume\",\"id\":\"nobody\",\"volume\":25}") == 400);       // not listed
    CHECK(post("{\"op\":\"appVolume\",\"id\":\"spotify#1\",\"volume\":140}") == 400);   // out of range
    CHECK(post("{\"op\":\"appVolume\",\"id\":\"spotify#1\",\"volume\":\"9\"}") == 400); // not a number
    CHECK(post("{\"op\":\"appVolume\",\"id\":7,\"volume\":9}") == 400);                   // the id is text
    CHECK(post("{\"op\":\"appMute\",\"id\":\"spotify#1\",\"on\":1}") == 400);           // on: a boolean
    CHECK(post("{\"op\":\"appMute\",\"id\":\"spotify#1\",\"on\":true}") == 200 && d.state()["apps"].items()[1]["mute"].asBool());
    CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"appMute\",\"id\":\"spotify#1\",\"on\":false}", {}, status, body) && (status == 401 || status == 429));   // no token
    const auto v2 = d.stateVersion(); L->poke("spotify#1", 0.5f, false); CHECK(d.stateVersion() > v2 && d.state()["apps"].items()[1]["volume"].asNumber() == 50 && !d.state()["apps"].items()[1]["mute"].asBool());   // moved in the Windows mixer
    L->add({"discord", 9, "Discord", "", 1.f, false, false}); CHECK(d.state()["apps"].size() == 3 && d.state()["apps"].items()[1]["name"].asString() == "Discord");
    int refused = 0; for (int i = 0; i < 60; ++i) if (post("{\"op\":\"appVolume\",\"id\":\"discord\",\"volume\":" + std::to_string(i) + "}") == 400) ++refused;
    CHECK(refused > 0 && L->sets < 70);   // the rate limit holds a flood back
  }
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"stop\"}", auth, status, body) && status == 200);
  CHECK(!d.state()["running"].asBool());
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/nothing", "", {}, status, body) && status == 404);
  d.stop();
  { using S = am::httpd::Server; auto ip = [](unsigned a, unsigned b, unsigned c, unsigned d) { return static_cast<unsigned long>((a << 24) | (b << 16) | (c << 8) | d); };   // the local policy
    CHECK(S::localPeer(ip(127, 0, 0, 1)) && S::localPeer(ip(192, 168, 1, 20)) && S::localPeer(ip(10, 4, 5, 6)) && S::localPeer(ip(172, 16, 0, 9)) && S::localPeer(ip(172, 31, 255, 1)) && S::localPeer(ip(169, 254, 3, 3)));
    CHECK(!S::localPeer(ip(8, 8, 8, 8)) && !S::localPeer(ip(172, 32, 0, 1)) && !S::localPeer(ip(172, 15, 0, 1)) && !S::localPeer(ip(192, 169, 1, 1)) && !S::localPeer(ip(100, 64, 0, 1)) && !S::localPeer(ip(203, 0, 113, 7))); }
  { // SEED: the console's mixer, loaded when the service starts
    const std::string sf = "daemon_test_seed.json"; { std::FILE* f = std::fopen(sf.c_str(), "wb"); std::fputs("{\"settings\":{\"width\":1.5,\"winGain\":true,\"outputBits\":16},\"strips\":[{\"name\":\"Vocal\",\"db\":-4,\"pan\":0.25,\"mute\":false,\"solo\":true},{\"name\":\"Gtr\",\"db\":-12,\"mute\":true}],\"master\":{\"db\":-2}}", f); std::fclose(f); }
    am::Daemon::Options so = o; so.port = 0; so.seed = sf; so.tokenStore.clear(); am::Daemon sd(so); std::string e; CHECK(sd.start(e));
    const Value st = sd.state(); CHECK(st["strips"].at(0)["name"].asString() == "Vocal" && st["strips"].at(0)["db"].asNumber() == -4 && st["strips"].at(0)["solo"].asBool());
    CHECK(st["strips"].at(1)["mute"].asBool() && st["strips"].at(1)["db"].asNumber() == -12 && st["master"]["db"].asNumber() == -2);
    CHECK(st["settings"]["width"].asNumber() == 1.5 && st["settings"]["winGain"].asBool() && st["settings"]["outputBits"].asInt() == 16);   // the console's settings came with the seed
    sd.stop(); std::remove(sf.c_str()); }
  { // THE CONSOLE'S PLUG-INS played by the service: racks from the seed, a plug-in that cannot load says why, parameters, bypass, states
    const std::string sf = "daemon_test_racks.json"; { std::FILE* f = std::fopen(sf.c_str(), "wb");
      std::fputs("{\"racks\":[{\"strip\":0,\"name\":\"VOCAL\",\"plugins\":[{\"slot\":2,\"kind\":\"vst3:C:/VST3/FakeGain.vst3|ABC\",\"name\":\"Fake Gain\",\"bypass\":false,\"state\":\"0.75,0.5\"}]},"
                 "{\"strip\":1,\"name\":\"GTR\",\"plugins\":[{\"slot\":0,\"kind\":\"vst2:C:/missing.dll\",\"name\":\"Gone\"}]}]}", f); std::fclose(f); }
    am::Daemon::Options po = o; po.port = 0; po.seed = sf; po.tokenStore.clear();
    const std::string cif = "daemon_test_console.json"; { std::FILE* f = std::fopen(cif.c_str(), "wb"); std::fputs("{\"vst\":[{\"name\":\"Fake Gain\",\"vendor\":\"Test Labs\",\"format\":\"VST3\",\"kind\":\"vst3:C:/VST3/FakeGain.vst3|ABC\"},{\"name\":\"Odd\",\"kind\":\"C:/evil.dll\"}]}", f); std::fclose(f); }
    po.consoleInfo = cif;
    po.pluginFactory = [](const std::string& kind, double, int, std::string& err) -> std::unique_ptr<am::host::Plugin> {
      if (kind.find("FakeGain") == std::string::npos) { err = "the module is not on this computer"; return nullptr; } auto p = std::make_unique<FakePlugin>(); g_fake = p.get(); return p; };
    am::Daemon pd(po); std::string e; CHECK(pd.start(e)); const unsigned short pp = pd.port(); const std::vector<std::pair<std::string, std::string>> pa = {{"X-Amix-Token", po.token}};
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"open\",\"backend\":\"null\",\"inChannels\":2,\"outChannels\":2}", pa, status, body) && status == 200);
    CHECK(g_fake && std::abs(g_fake->gain - 0.75) < 1e-9);   // loaded with the console's state
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "GET", "/api/racks", "", {}, status, body) && (status == 401 || status == 429));
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "GET", "/api/racks", "", pa, status, body) && status == 200);
    Value rk = Value::parse(body, ok)["racks"]; CHECK(ok && rk.size() == 2);
    CHECK(rk.at(0)["name"].asString() == "VOCAL" && rk.at(0)["slots"].at(0)["slot"].asInt() == 2 && rk.at(0)["slots"].at(0)["loaded"].asBool() && rk.at(0)["slots"].at(0)["vendor"].asString() == "Test Labs");
    CHECK(!rk.at(1)["slots"].at(0)["loaded"].asBool() && rk.at(1)["slots"].at(0)["error"].asString() == "the module is not on this computer");
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "GET", "/api/racks?strip=0", "", pa, status, body) && status == 200);
    Value ps = Value::parse(body, ok)["racks"].at(0)["slots"].at(0)["params"]; CHECK(ok && ps.size() == 2 && ps.at(0)["title"].asString() == "GAIN" && ps.at(0)["text"].asString() == "6 dB");   // the hidden one is not shown
    const double rv = pd.state()["racksV"].asNumber();
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"plugin\",\"strip\":0,\"slot\":2,\"param\":7,\"value\":0.25}", pa, status, body) && status == 200 && Value::parse(body, ok)["text"].asString() == "-6");
    CHECK(std::abs(g_fake->gain - 0.25) < 1e-9 && pd.state()["racksV"].asNumber() > rv);
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"plugin\",\"strip\":0,\"slot\":2,\"bypass\":true}", pa, status, body) && status == 200 && g_fake->byp);
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"plugin\",\"strip\":0,\"slot\":2,\"param\":99,\"value\":0.5}", pa, status, body) && status == 400);   // no such parameter
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"plugin\",\"strip\":0,\"slot\":2,\"param\":7,\"value\":3}", pa, status, body) && status == 400);    // out of 0..1
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"plugin\",\"strip\":1,\"slot\":0,\"param\":7,\"value\":0.5}", pa, status, body) && status == 400);    // not loaded here
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "GET", "/api/racks?states=1", "", pa, status, body) && status == 200 && Value::parse(body, ok)["racks"].at(0)["slots"].at(0)["state"].asString().rfind("0.25", 0) == 0);
    { const int c0 = g_fake->calls.load(); std::this_thread::sleep_for(std::chrono::milliseconds(300)); CHECK(g_fake->calls.load() > c0); }   // the sound goes through it
    // THE CONSOLE'S RACK as it changes (op rack): the same plug-in in the same slot keeps its instance and takes the new state; the
    // version moves but says it came from the console (so the console does not take it back); a new slot is made; an empty list clears
    { FakePlugin* same = g_fake; const double rv2 = pd.state()["racksV"].asNumber();
      CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"rack\",\"strip\":0,\"plugins\":[{\"slot\":2,\"kind\":\"vst3:C:/VST3/FakeGain.vst3|ABC\",\"name\":\"Fake Gain\",\"bypass\":false,\"state\":\"0.9,0.1\"}]}", pa, status, body) && status == 200);
      CHECK(g_fake == same && std::abs(same->gain - 0.9) < 1e-9 && !same->byp && pd.state()["racksV"].asNumber() > rv2 && pd.state()["racksBy"].asString() == "console");
      CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"rack\",\"strip\":0,\"plugins\":[{\"slot\":2,\"kind\":\"vst3:C:/VST3/FakeGain.vst3|ABC\",\"state\":\"0.9,0.1\"},{\"slot\":3,\"kind\":\"vst3:C:/VST3/FakeGain.vst3|ABC\",\"state\":\"0.2,0.2\"}]}", pa, status, body) && status == 200);
      CHECK(g_fake != same && std::abs(g_fake->gain - 0.2) < 1e-9);   // slot 3 made fresh
      CHECK(am::httpd::Client::request("127.0.0.1", pp, "GET", "/api/racks?strip=0", "", pa, status, body) && Value::parse(body, ok)["racks"].at(0)["slots"].size() == 2);
      CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"plugin\",\"strip\":0,\"slot\":2,\"bypass\":true}", pa, status, body) && pd.state()["racksBy"].asString() == "remote");
      CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"rack\",\"strip\":0,\"plugins\":[]}", pa, status, body) && status == 200);
      CHECK(am::httpd::Client::request("127.0.0.1", pp, "GET", "/api/racks", "", pa, status, body) && Value::parse(body, ok)["racks"].size() == 1); }
    { // CHANNEL INSERTS FROM A REMOTE: the catalog has ids (no paths), add / move / bypass / param / preset / remove, bad input refused
      CHECK(am::httpd::Client::request("127.0.0.1", pp, "GET", "/api/inserts", "", {}, status, body) && (status == 401 || status == 429));
      CHECK(am::httpd::Client::request("127.0.0.1", pp, "GET", "/api/inserts", "", pa, status, body) && status == 200 && body.find("C:/") == std::string::npos && body.find("evil") == std::string::npos);
      const Value cat = Value::parse(body, ok)["plugins"]; std::string fakeId, revId;
      for (const Value& x : cat.items()) { if (x["name"].asString() == "Fake Gain") fakeId = x["id"].asString(); if (x["fx"].asBool() && revId.empty()) revId = x["id"].asString(); }
      CHECK(ok && fakeId.size() == 16 && revId.size() == 16 && Value::parse(body, ok)["maxSlots"].asInt() == 10);
      auto ctl = [&](const std::string& j) { std::this_thread::sleep_for(std::chrono::milliseconds(110)); return am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", j, pa, status, body) ? status : 0; };
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"add\",\"id\":\"" + fakeId + "\"}") == 200 && Value::parse(body, ok)["slot"].asInt() == 0);
      CHECK(pd.state()["racksBy"].asString() == "remote");
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"add\",\"id\":\"" + revId + "\",\"slot\":3}") == 200);
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"add\",\"id\":\"" + revId + "\",\"slot\":3}") == 400);   // taken
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"add\",\"id\":\"" + revId + "\",\"slot\":10}") == 400);  // slot range
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"add\",\"id\":\"0123456789abcdef\"}") == 400);           // not in the list
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"add\",\"id\":\"vst3:C:/VST3/FakeGain.vst3|ABC\"}") == 400);   // a path is never taken
      CHECK(ctl("{\"op\":\"insert\",\"strip\":99,\"action\":\"add\",\"id\":\"" + fakeId + "\"}") == 400);
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"move\",\"slot\":0,\"to\":5}") == 200);
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"move\",\"slot\":0,\"to\":1}") == 400);   // nothing there now
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"move\",\"slot\":5,\"to\":-1}") == 400);
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"explode\"}") == 400);
      { const Value r = pd.racks(4, true); CHECK(r.size() == 1 && r.at(0)["slots"].size() == 2 && r.at(0)["slots"].at(0)["slot"].asInt() == 3 && r.at(0)["slots"].at(1)["slot"].asInt() == 5 && r.at(0)["slots"].at(1)["loaded"].asBool());
        CHECK(r.at(0)["slots"].at(0)["presets"].size() > 0 || r.at(0)["slots"].at(0)["kind"].asString().rfind("fx:", 0) == 0); }
      CHECK(ctl("{\"op\":\"plugin\",\"strip\":4,\"slot\":5,\"bypass\":true}") == 200 && g_fake->byp);
      CHECK(ctl("{\"op\":\"plugin\",\"strip\":4,\"slot\":5,\"param\":7,\"value\":0.125}") == 200 && std::abs(g_fake->gain - 0.125) < 1e-12);
      CHECK(ctl("{\"op\":\"plugin\",\"strip\":4,\"slot\":5,\"param\":7,\"value\":-0.1}") == 400);
      CHECK(ctl("{\"op\":\"plugin\",\"strip\":4,\"slot\":5,\"param\":12345,\"value\":0.5}") == 400);
      CHECK(ctl("{\"op\":\"plugin\",\"strip\":4,\"slot\":5,\"preset\":1}") == 400);   // a built-in effect's preset is not for this plug-in
      CHECK(ctl("{\"op\":\"plugin\",\"strip\":4,\"slot\":5,\"defaults\":true}") == 200 && std::abs(g_fake->gain - 0.5) < 1e-12);
      { int pid = 0; const Value r = pd.racks(4, true); const Value& pr = r.at(0)["slots"].at(0)["presets"]; if (pr.size()) pid = pr.at(0)["id"].asInt();
        if (r.at(0)["slots"].at(0)["loaded"].asBool() && pid) CHECK(ctl("{\"op\":\"plugin\",\"strip\":4,\"slot\":3,\"preset\":" + std::to_string(pid) + "}") == 200); }
      CHECK(ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"remove\",\"slot\":5}") == 200 && ctl("{\"op\":\"insert\",\"strip\":4,\"action\":\"remove\",\"slot\":5}") == 400);
      { int refused = 0; for (int i = 0; i < 40; ++i) if (am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"insert\",\"strip\":4,\"action\":\"move\",\"slot\":3,\"to\":2}", pa, status, body) && status == 400 && body.find("slow down") != std::string::npos) ++refused;
        CHECK(refused > 0); }   // rate limited
      std::this_thread::sleep_for(std::chrono::milliseconds(2100)); }
    // MASTER from a remote: the value comes back exactly; mute / dim alone leave the fader where it is (no echo moves it)
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"master\",\"db\":-7.3}", pa, status, body) && status == 200);
    CHECK(pd.state()["master"]["db"].asNumber() == -7.3);
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"master\",\"mute\":true}", pa, status, body) && status == 200);
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"master\",\"dim\":true}", pa, status, body) && status == 200);
    { const Value m = pd.state()["master"]; CHECK(m["db"].asNumber() == -7.3 && m["mute"].asBool() && m["dim"].asBool()); }
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"master\",\"db\":" + Value(pd.state()["master"]["db"].asNumber()).dump() + "}", pa, status, body) && pd.state()["master"]["db"].asNumber() == -7.3);   // its own echo: the same value
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"master\",\"mute\":false,\"dim\":false}", pa, status, body) && !pd.state()["master"]["mute"].asBool() && pd.state()["master"]["db"].asNumber() == -7.3);
    // THE CHANNEL EQ (bands 0..7) is kept in the state, so the console and every remote follow it
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"eq\",\"strip\":1,\"band\":3,\"type\":\"highshelf\",\"freq\":8000,\"q\":0.7,\"gainDb\":4.5}", pa, status, body) && status == 200);
    { const Value e = pd.state()["strips"].at(1)["eq"].at(3); CHECK(e.at(0).asInt() == 3 && e.at(1).asNumber() == 8000 && e.at(3).asNumber() == 4.5); }
    pd.stop(); std::remove(sf.c_str()); std::remove(cif.c_str()); g_fake = nullptr; }
  { // RTA: a 1 kHz sine at -6 dBFS reads about -6 dB in its band and nothing far from it; the master's RTA EQ from the seed and from a remote
    am::Rta rta; rta.source.store(am::Rta::kMaster); std::vector<float> L(256), R(256); double ph = 0;
    for (int b = 0; b < 40; ++b) { for (std::size_t i = 0; i < L.size(); ++i) { L[i] = R[i] = 0.5f * static_cast<float>(std::sin(ph)); ph += 2 * 3.14159265358979 * 1000.0 / 48000.0; } rta.onBlock(nullptr, 0, L.data(), R.data(), L.size(), 48000); }
    std::vector<double> db; CHECK(rta.bands(db)); const auto& c = am::Rta::centres(); std::size_t k1 = 0; for (std::size_t i = 0; i < c.size(); ++i) if (std::abs(std::log2(c[i] / 1000.0)) < std::abs(std::log2(c[k1] / 1000.0))) k1 = i;
    double far = -200; for (std::size_t i = 0; i < c.size(); ++i) if (c[i] < 250 || c[i] > 4000) far = std::max(far, db[i]);
    std::printf("daemon_test: RTA %zu bands, 1 kHz sine at -6 dBFS reads %.1f dB at %.0f Hz, at most %.1f dB away from it\n", c.size(), db[k1], c[k1], far);
    CHECK(std::abs(db[k1] + 6.0) < 1.5 && far < -60 && c.size() >= 59 && c.size() <= 61);
    am::Rta off; std::vector<double> d2; CHECK(!off.bands(d2));   // off: nothing collected
    // the master chain from the seed, the RTA EQ op, and the RTA on the live stream
    const std::string sf = "daemon_test_master.json"; { std::FILE* f = std::fopen(sf.c_str(), "wb"); std::fputs("{\"masterChain\":{\"eqOn\":true,\"eq\":[{\"type\":1,\"freq\":31.5,\"q\":1.41,\"gainDb\":3}],\"on\":false}}", f); std::fclose(f); }
    am::Daemon::Options mo = o; mo.port = 0; mo.seed = sf; mo.tokenStore.clear(); am::Daemon md(mo); std::string e; CHECK(md.start(e)); const unsigned short mp = md.port(); const std::vector<std::pair<std::string, std::string>> ma = {{"X-Amix-Token", mo.token}};
    CHECK(md.state()["programEq"]["on"].asBool() && md.state()["programEq"]["gains"].at(0).asNumber() == 3);
    CHECK(am::httpd::Client::request("127.0.0.1", mp, "POST", "/api/control", "{\"op\":\"open\",\"backend\":\"null\",\"inChannels\":2,\"outChannels\":2}", ma, status, body) && status == 200);
    const double mv = md.state()["masterV"].asNumber();
    CHECK(am::httpd::Client::request("127.0.0.1", mp, "POST", "/api/control", "{\"op\":\"programEq\",\"gains\":[0,0,0,0,-4,0,2.5,0,0,30],\"on\":true}", ma, status, body) && status == 200);
    { const Value pe = md.state()["programEq"]; CHECK(pe["gains"].at(4).asNumber() == -4 && pe["gains"].at(6).asNumber() == 2.5 && pe["gains"].at(9).asNumber() == 24 && md.state()["masterV"].asNumber() > mv); }   // clamped to +24
    CHECK(am::httpd::Client::request("127.0.0.1", mp, "POST", "/api/control", "{\"op\":\"programEq\",\"gains\":[1,2]}", ma, status, body) && status == 400);
    CHECK(am::httpd::Client::request("127.0.0.1", mp, "POST", "/api/control", "{\"op\":\"rta\",\"src\":-1}", ma, status, body) && status == 200);
    int wf = am::httpd::Client::connectWs("127.0.0.1", mp, "/ws/meters?token=" + mo.token, {}); CHECK(wf >= 0);
    { am::httpd::WebSocket ws(wf); std::string t; int op = 0; bool got = false, centres = false;
      for (int i = 0; i < 80 && !got; ++i) { if (!ws.read(t, op)) break; Value f = Value::parse(t, ok); if (ok && f.has("rta")) { centres = centres || f["rta"].has("f"); got = f["rta"]["db"].size() > 50 && f["rta"]["src"].asInt() == -1; } }
      CHECK(got && centres); ws.close(); }
    CHECK(am::httpd::Client::request("127.0.0.1", mp, "GET", "/api/rta", "", ma, status, body) && status == 200 && Value::parse(body, ok)["f"].size() > 50);
    CHECK(am::httpd::Client::request("127.0.0.1", mp, "POST", "/api/control", "{\"op\":\"rta\",\"src\":\"off\"}", ma, status, body) && status == 200 && md.rtaJson(false)["src"].asInt() == am::Rta::kOff);
    md.stop(); std::remove(sf.c_str()); }
  { // PHONE MIC: a phone's 1 kHz tone at -6 dBFS posted as s16le PCM plays on the strip it picked; off again gives the strip its input back
    am::Daemon::Options po = o; po.port = 0; po.tokenStore.clear(); am::Daemon pm(po); std::string e; CHECK(pm.start(e)); const unsigned short pp = pm.port(); const std::vector<std::pair<std::string, std::string>> pa = {{"X-Amix-Token", po.token}};
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"open\",\"backend\":\"null\",\"rate\":48000,\"frames\":128,\"inChannels\":2,\"outChannels\":2}", pa, status, body) && status == 200);
    std::string chunk(2 * 960, '\0'); double ph = 0;   // 20 ms at 48 kHz
    auto fill = [&] { for (int i = 0; i < 960; ++i) { const auto x = static_cast<std::int16_t>(16384 * std::sin(ph)); ph += 2 * 3.14159265358979 * 1000.0 / 48000.0; chunk[2 * i] = static_cast<char>(x & 0xff); chunk[2 * i + 1] = static_cast<char>((x >> 8) & 0xff); } };
    fill(); CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/mic?rate=48000", chunk, pa, status, body) && status == 409);   // off: refused
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/mic", chunk, {}, status, body) && status == 401);                       // no token
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"phoneMic\",\"strip\":3,\"on\":true}", pa, status, body) && status == 200);
    CHECK(pm.state()["phoneMic"]["on"].asBool() && pm.state()["phoneMic"]["strip"].asInt() == 3);
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/mic?rate=1000", chunk, pa, status, body) && status == 400);
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/mic", chunk.substr(1), pa, status, body) && status == 400);   // odd length
    double best = 0;
    for (int k = 0; k < 40; ++k) { fill(); CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/mic?rate=48000", chunk, pa, status, body) && status == 200);
      std::this_thread::sleep_for(std::chrono::milliseconds(18)); best = std::max(best, pm.meters()["strips"].at(3)["in"].asNumber(-200)); }
    const Value pmj = pm.state()["phoneMic"];
    std::printf("daemon_test: PHONE MIC strip 3 input peak %.3f (level %.2f, underruns %d)\n", best, pmj["level"].asNumber(), pmj["underruns"].asInt());
    CHECK(pmj["live"].asBool() && pmj["level"].asNumber() > 0.4);
    CHECK(best > 0.05);   // the tone reached the strip (linear peak or dB, either is above silence)
    CHECK(pm.meters()["strips"].at(2)["in"].asNumber(0) < 0.01 || pm.meters()["strips"].at(2)["in"].asNumber(0) < -60);   // a neighbour stays silent
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/control", "{\"op\":\"phoneMic\",\"on\":false}", pa, status, body) && status == 200 && !pm.state()["phoneMic"]["on"].asBool());
    CHECK(am::httpd::Client::request("127.0.0.1", pp, "POST", "/api/mic", chunk, pa, status, body) && status == 409);
    pm.stop(); }
  std::printf("daemon_test: live state push, seed, json, local policy, token gate, control/state, HTTP meters, WebSocket meters, connected phones (list, disconnect, hold, forget), console settings, Windows volume / GAIN MODE, console plug-ins, RTA and RTA EQ OK on port %u\n", port);
  return 0;
}
