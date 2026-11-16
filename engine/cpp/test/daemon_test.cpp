#include <ctime>
// amixos-daemon on the null backend: the JSON parser, the token gate, control -> state, the meters frame over HTTP and over WebSocket.
#include <fstream>
#include <iterator>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "daemon/daemon.hpp"

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
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"monitor\",\"source\":1,\"db\":-6,\"dim\":true}", auth, status, body) && status == 200);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/state", "", {}, status, body) && status == 200);
  Value s = Value::parse(body, ok); CHECK(ok);
  CHECK(s["monitor"]["source"].asInt() == 1 && s["monitor"]["db"].asNumber() == -6 && s["monitor"]["dim"].asBool() && !s["monitor"]["mute"].asBool());
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
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"stop\"}", auth, status, body) && status == 200);
  CHECK(!d.state()["running"].asBool());
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/nothing", "", {}, status, body) && status == 404);
  d.stop();
  { using S = am::httpd::Server; auto ip = [](unsigned a, unsigned b, unsigned c, unsigned d) { return static_cast<unsigned long>((a << 24) | (b << 16) | (c << 8) | d); };   // the local policy
    CHECK(S::localPeer(ip(127, 0, 0, 1)) && S::localPeer(ip(192, 168, 1, 20)) && S::localPeer(ip(10, 4, 5, 6)) && S::localPeer(ip(172, 16, 0, 9)) && S::localPeer(ip(172, 31, 255, 1)) && S::localPeer(ip(169, 254, 3, 3)));
    CHECK(!S::localPeer(ip(8, 8, 8, 8)) && !S::localPeer(ip(172, 32, 0, 1)) && !S::localPeer(ip(172, 15, 0, 1)) && !S::localPeer(ip(192, 169, 1, 1)) && !S::localPeer(ip(100, 64, 0, 1)) && !S::localPeer(ip(203, 0, 113, 7))); }
  { // SEED: the console's mixer, loaded when the service starts
    const std::string sf = "daemon_test_seed.json"; { std::FILE* f = std::fopen(sf.c_str(), "wb"); std::fputs("{\"strips\":[{\"name\":\"Vocal\",\"db\":-4,\"pan\":0.25,\"mute\":false,\"solo\":true},{\"name\":\"Gtr\",\"db\":-12,\"mute\":true}],\"master\":{\"db\":-2}}", f); std::fclose(f); }
    am::Daemon::Options so = o; so.port = 0; so.seed = sf; so.tokenStore.clear(); am::Daemon sd(so); std::string e; CHECK(sd.start(e));
    const Value st = sd.state(); CHECK(st["strips"].at(0)["name"].asString() == "Vocal" && st["strips"].at(0)["db"].asNumber() == -4 && st["strips"].at(0)["solo"].asBool());
    CHECK(st["strips"].at(1)["mute"].asBool() && st["strips"].at(1)["db"].asNumber() == -12 && st["master"]["db"].asNumber() == -2);
    sd.stop(); std::remove(sf.c_str()); }
  std::printf("daemon_test: live state push, seed, json, local policy, token gate, control/state, HTTP meters, WebSocket meters, connected phones (list, disconnect, hold, forget) OK on port %u\n", port);
  return 0;
}
