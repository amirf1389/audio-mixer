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
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/devices", "", {}, status, body) && status == 200);
  Value dv = Value::parse(body, ok); CHECK(ok && dv.isArray());

  // control without the token: 401 and nothing changes
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"gain\",\"strip\":0,\"db\":-6}", {{"Content-Type", "application/json"}}, status, body) && status == 401);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"gain\",\"strip\":0,\"db\":-6}", {{"X-Amix-Token", "wrong"}}, status, body) && status == 401);
  CHECK(d.state()["strips"].items()[0]["db"].asNumber() == 0);
  // with the token: open the null backend, change the console, read it back
  const std::vector<std::pair<std::string, std::string>> auth = {{"X-Amix-Token", o.token}};
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"open\",\"backend\":\"null\",\"rate\":48000,\"frames\":128}", auth, status, body) && status == 200);
  Value r = Value::parse(body, ok); CHECK(ok && r["ok"].asBool() && r["backend"].asString() == "null");
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"gain\",\"strip\":2,\"db\":-6.5}", auth, status, body) && status == 200);
  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"name\",\"strip\":2,\"name\":\"Vox\"}", auth, status, body) && status == 200);
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
    CHECK(am::TokenStore::same("abc", "abc") && !am::TokenStore::same("abc", "abd") && !am::TokenStore::same("abc", "abcd"));
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
    for (int i = 0; i < 3; ++i) { CHECK(ws.read(t, op) && op == 1); Value f = Value::parse(t, ok); CHECK(ok && f["strips"].size() == 8); ++frames; }
    CHECK(frames == 3); ws.close(); }

  CHECK(am::httpd::Client::request("127.0.0.1", port, "POST", "/api/control", "{\"op\":\"stop\"}", auth, status, body) && status == 200);
  CHECK(!d.state()["running"].asBool());
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/nothing", "", {}, status, body) && status == 404);
  d.stop();
  { using S = am::httpd::Server; auto ip = [](unsigned a, unsigned b, unsigned c, unsigned d) { return static_cast<unsigned long>((a << 24) | (b << 16) | (c << 8) | d); };   // the local policy
    CHECK(S::localPeer(ip(127, 0, 0, 1)) && S::localPeer(ip(192, 168, 1, 20)) && S::localPeer(ip(10, 4, 5, 6)) && S::localPeer(ip(172, 16, 0, 9)) && S::localPeer(ip(172, 31, 255, 1)) && S::localPeer(ip(169, 254, 3, 3)));
    CHECK(!S::localPeer(ip(8, 8, 8, 8)) && !S::localPeer(ip(172, 32, 0, 1)) && !S::localPeer(ip(172, 15, 0, 1)) && !S::localPeer(ip(192, 169, 1, 1)) && !S::localPeer(ip(100, 64, 0, 1)) && !S::localPeer(ip(203, 0, 113, 7))); }
  std::printf("daemon_test: json, local policy, token gate, control/state, HTTP meters, WebSocket meters OK on port %u\n", port);
  return 0;
}
