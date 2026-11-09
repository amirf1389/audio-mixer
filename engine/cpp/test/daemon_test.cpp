// amixos-daemon on the null backend: the JSON parser, the token gate, control -> state, the meters frame over HTTP and over WebSocket.
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

  am::Daemon::Options o; o.port = 0; o.token = "testtoken-0123456789abcdef0123456789"; o.strips = 8;
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
  CHECK(am::httpd::Client::request("127.0.0.1", port, "GET", "/api/state", "", {}, status, body) && status == 200);
  Value s = Value::parse(body, ok); CHECK(ok);
  CHECK(s["strips"].items()[2]["db"].asNumber() == -6.5 && s["strips"].items()[2]["name"].asString() == "Vox" && s["strips"].items()[3]["mute"].asBool() && s["buses"].items()[1]["db"].asNumber() == -3 && s["running"].asBool());

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
  std::printf("daemon_test: json, token gate, control/state, HTTP meters, WebSocket meters OK on port %u\n", port);
  return 0;
}
