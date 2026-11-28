// AmF X1 IoT SDK: host test. The portable core (builders, JSON scanner, HTTP / WebSocket readers, scenes) on its own, then the
// socket client (sdk/iot/esp-idf/amfx1/amfx1.c, the code the ESP-IDF component runs) against the real AmF X1 Network service
// (am::Daemon on the null backend, in this process): pairing with the minute's PIN, faders / mute / solo / pan / master over HTTP
// and over the WebSocket, the state and meter frames back, scenes, a wrong token and a wrong PIN.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

#include "daemon/daemon.hpp"
extern "C" {
#include "amfx1.h"
}

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

namespace {
bool near(double a, double b, double e = 0.011) { return std::fabs(a - b) <= e; }
template <class F> bool waitFor(amfx1_client_t* c, F ok, int ms = 3000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) { if (ok()) return true; amfx1_loop(c, 20); }
  return ok();
}
int states = 0, meterFrames = 0;
void onState(amfx1_client_t*, void*) { ++states; }
void onMeter(amfx1_client_t*, void*) { ++meterFrames; }
}  // namespace

static int unit() {
  char b[256];
  CHECK(amfx1_msg_fader(b, sizeof b, 1, -6.5f, -1) > 0 && std::string(b) == "{\"op\":\"gain\",\"strip\":0,\"db\":-6.5}");
  CHECK(amfx1_msg_fader(b, sizeof b, 3, 99, 7) > 0 && std::string(b) == "{\"op\":\"gain\",\"strip\":2,\"db\":10,\"seq\":7}");   // clamped to +10
  CHECK(amfx1_msg_fader(b, sizeof b, 2, -1000, -1) > 0 && std::string(b) == "{\"op\":\"gain\",\"strip\":1,\"db\":-120}");
  CHECK(amfx1_msg_fader(b, sizeof b, 0, 0, -1) == -1 && amfx1_msg_fader(b, sizeof b, AMFX1_MAX_STRIPS + 1, 0, -1) == -1);   // channels are 1..N
  CHECK(amfx1_msg_pan(b, sizeof b, 2, -3, -1) > 0 && std::string(b) == "{\"op\":\"pan\",\"strip\":1,\"pan\":-1}");
  CHECK(amfx1_msg_pan(b, sizeof b, 2, 0.25f, -1) > 0 && std::string(b) == "{\"op\":\"pan\",\"strip\":1,\"pan\":0.25}");
  CHECK(amfx1_msg_mute(b, sizeof b, 4, 1, -1) > 0 && std::string(b) == "{\"op\":\"mute\",\"strip\":3,\"on\":true}");
  CHECK(amfx1_msg_solo(b, sizeof b, 4, 0, -1) > 0 && std::string(b) == "{\"op\":\"solo\",\"strip\":3,\"on\":false}");
  CHECK(amfx1_msg_master(b, sizeof b, -3, -1) > 0 && std::string(b) == "{\"op\":\"master\",\"db\":-3}");
  CHECK(amfx1_msg_master_mute(b, sizeof b, 1, -1) > 0 && std::string(b) == "{\"op\":\"master\",\"mute\":true}");
  CHECK(amfx1_msg_fader(b, 10, 1, 0, -1) == -1 && std::strlen(b) == 9);   // too small: -1, still NUL-terminated
  CHECK(amfx1_msg_pair(b, sizeof b, "12345", "x") == -1 && amfx1_msg_pair(b, sizeof b, "12a456", "x") == -1);
  CHECK(amfx1_msg_pair(b, sizeof b, "012345", "Desk \"A\"") > 0 && std::string(b) == "{\"pin\":\"012345\",\"name\":\"Desk \\\"A\\\"\"}");
  CHECK(amfx1_http_head(b, sizeof b, "POST", "10.0.0.5", 8765, "/api/control", "tok\r\nX: y", 2) == -1);   // no header injection
  CHECK(amfx1_http_head(b, sizeof b, "POST", "10.0.0.5", 8765, "/api/control", "amx_ab", 2) > 0 && std::strstr(b, "\r\nX-Amix-Token: amx_ab\r\n") && std::strstr(b, "Host: 10.0.0.5:8765\r\n") && std::strstr(b, "Content-Length: 2\r\n"));
  { const uint8_t k[16] = {0}; CHECK(amfx1_ws_handshake(b, sizeof b, "10.0.0.5", 8765, "amx_ab", k) > 0 && std::strstr(b, "GET /ws/meters HTTP/1.1") && std::strstr(b, "Sec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==") && !std::strstr(b, "token=")); }
  { char o[16]; const uint8_t in[] = "Man"; CHECK(amfx1_base64(o, sizeof o, in, 3) == 4 && std::string(o) == "TWFu"); CHECK(amfx1_base64(o, sizeof o, in, 1) == 4 && std::string(o) == "TQ=="); }
  CHECK(near(amfx1_lin_to_db(1.0f), 0, 0.01) && near(amfx1_lin_to_db(0.5f), -6.0206, 0.01) && near(amfx1_lin_to_db(0.001f), -60, 0.02) && amfx1_lin_to_db(0) == AMFX1_SILENCE_DB);

  // the scanner and the model, fed one byte at a time (as a slow board would read them)
  amfx1_model m; amfx1_model_init(&m);
  const std::string st = "{\"state\":{\"strips\":[{\"id\":0,\"name\":\"Vocal \\\"lead\\\" \\u00e9\",\"db\":-4.5,\"pan\":-0.5,\"mute\":false,\"solo\":true,\"eq\":[[1,100,0.7,0],[0,0,0,0]],\"tone\":{\"low\":0}},"
                         "{\"id\":1,\"name\":\"Gtr\",\"db\":-10,\"pan\":0.3,\"mute\":true,\"solo\":false}],\"master\":{\"db\":-2,\"mute\":false,\"dim\":false},\"v\":42,\"apps\":[]},\"wsControl\":true}";
  amfx1_model_begin(&m); for (char ch : st) amfx1_model_feed(&m, &ch, 1); uint16_t f = amfx1_model_end(&m);
  CHECK((f & AMFX1_F_STATE) && !m.sax.error && m.haveState && m.strips == 2 && near(m.strip[0].db, -4.5) && near(m.strip[0].pan, -0.5) && m.strip[0].solo && !m.strip[0].mute);
  CHECK(m.strip[1].mute && near(m.strip[1].db, -10) && near(m.masterDb, -2) && m.version == 42 && std::string(m.strip[1].name) == "Gtr" && std::string(m.strip[0].name).rfind("Vocal \"lead", 0) == 0);
  const std::string mt = "{\"strips\":[{\"pk\":0.5,\"rms\":0.25,\"hold\":0.5},{\"pk\":1e-3,\"rms\":0}],\"master\":{\"l\":0.25,\"r\":0.125,\"rms\":0.1},\"running\":true}";
  amfx1_model_begin(&m); amfx1_model_feed(&m, mt.data(), mt.size()); f = amfx1_model_end(&m);
  CHECK(f == AMFX1_F_METERS && near(m.strip[0].pk, 0.5) && near(m.strip[1].pk, 0.001, 1e-6) && near(m.masterL, 0.25) && near(m.strip[0].db, -4.5));   // meters leave the faders alone
  amfx1_model_begin(&m); { const std::string a = "{\"ack\":9,\"controlError\":\"strip out of range\"}"; amfx1_model_feed(&m, a.data(), a.size()); } f = amfx1_model_end(&m);
  CHECK((f & AMFX1_F_ACK) && (f & AMFX1_F_ERROR) && m.ack == 9 && std::string(m.error) == "strip out of range");
  amfx1_model_begin(&m); { const std::string a = "{\"ok\":true,\"strips\":[{\"db\":"; amfx1_model_feed(&m, a.data(), a.size()); } amfx1_model_feed(&m, "}}]", 3); f = amfx1_model_end(&m);
  CHECK((f & AMFX1_F_ERROR) && m.sax.error);   // malformed: reported, not crashed
  { amfx1_model big; amfx1_model_init(&big); std::string s = "{\"strips\":["; for (int i = 0; i < AMFX1_MAX_STRIPS + 8; ++i) s += std::string(i ? "," : "") + "{\"db\":-" + std::to_string(i) + "}"; s += "]}";
    amfx1_model_begin(&big); amfx1_model_feed(&big, s.data(), s.size()); amfx1_model_end(&big); CHECK(big.strips == AMFX1_MAX_STRIPS && near(big.strip[AMFX1_MAX_STRIPS - 1].db, -(AMFX1_MAX_STRIPS - 1))); }
  { amfx1_model d; amfx1_model_init(&d); std::string s = "{\"a\":"; for (int i = 0; i < 12; ++i) s += "[{\"k\":"; s += "1"; for (int i = 0; i < 12; ++i) s += "}]"; s += ",\"ack\":3}";   // deeper than the scanner keeps
    amfx1_model_begin(&d); amfx1_model_feed(&d, s.data(), s.size()); f = amfx1_model_end(&d); CHECK(!d.sax.error && (f & AMFX1_F_ACK) && d.ack == 3); }

  // HTTP reader: headers in pieces, the body by Content-Length
  { amfx1_model h; amfx1_model_init(&h); amfx1_http_rx rx; amfx1_http_rx_init(&rx, 0);
    const std::string r = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\ncontent-length: 50\r\nConnection: close\r\n\r\n{\"ok\":true,\"id\":\"p1\",\"token\":\"amx_0123\",\"days\":30}TRAILING";
    size_t used = 0; for (size_t i = 0; i < r.size() && !rx.done; i += 7) used += amfx1_http_rx_feed(&rx, r.data() + i, std::min<size_t>(7, r.size() - i), &h);
    f = amfx1_model_end(&h); CHECK(rx.done && rx.status == 200 && (f & AMFX1_F_TOKEN) && std::string(h.token) == "amx_0123" && h.ok && used == r.size() - 8); }

  // WebSocket reader: a 16-bit length text frame split anywhere, a ping, a close, a masked server frame refused
  { amfx1_model w; amfx1_model_init(&w); amfx1_ws_rx rx; amfx1_ws_rx_init(&rx); std::string payload = "{\"ack\":5,\"pad\":\"" + std::string(300, 'x') + "\"}";
    std::string fr; fr += char(0x81); fr += char(126); fr += char(payload.size() >> 8); fr += char(payload.size() & 255); fr += payload; fr += char(0x89); fr += char(2); fr += "hi"; fr += char(0x88); fr += char(0);
    int msgs = 0, pings = 0, closes = 0; size_t off = 0;
    while (off < fr.size()) { int ev; const size_t step = std::min<size_t>(5, fr.size() - off); const size_t k = amfx1_ws_rx_feed(&rx, (const uint8_t*)fr.data() + off, step, &w, &ev); off += k;
      if (ev == AMFX1_WS_MESSAGE) { ++msgs; f = amfx1_model_end(&w); CHECK((f & AMFX1_F_ACK) && w.ack == 5); } if (ev == AMFX1_WS_PING) { ++pings; CHECK(rx.ctln == 2 && rx.ctl[0] == 'h'); } if (ev == AMFX1_WS_CLOSE) ++closes; }
    CHECK(msgs == 1 && pings == 1 && closes == 1);
    amfx1_ws_rx_init(&rx); const uint8_t bad[] = {0x81, 0x85, 1, 2, 3, 4}; int ev; amfx1_ws_rx_feed(&rx, bad, sizeof bad, &w, &ev); CHECK(ev == AMFX1_WS_ERROR); }
  { uint8_t fr[64]; const uint8_t mask[4] = {1, 2, 3, 4}; const char* p = "{\"op\":\"x\"}"; const int n = amfx1_ws_frame(fr, sizeof fr, 1, (const uint8_t*)p, std::strlen(p), mask);
    CHECK(n == 6 + 10 && fr[0] == 0x81 && fr[1] == (0x80 | 10) && (fr[6] ^ 1) == '{' && (fr[7] ^ 2) == '"'); }

  // scenes: only what differs is sent
  { amfx1_scene sc; CHECK(amfx1_scene_capture(&m, &sc)); amfx1_model now = m; now.strip[1].db = 0; now.strip[0].mute = 1; int cur = 0, n = 0; std::string all;
    while (amfx1_scene_next(&now, &sc, &cur, b, sizeof b) > 0) { ++n; all += b; all += "\n"; }
    CHECK(n == 2 && all == "{\"op\":\"mute\",\"strip\":0,\"on\":false}\n{\"op\":\"gain\",\"strip\":1,\"db\":-10}\n"); }
  return 0;
}

static int live() {
  am::Daemon::Options o; o.port = 0; o.token = "iot-sdk-test-master-0123456789abcdef"; o.strips = 8; o.tokenStore = "iot_sdk_test.tokens.json"; std::remove(o.tokenStore.c_str());
  am::Daemon d(o); std::string err; CHECK(d.start(err)); const unsigned short port = d.port(); CHECK(port);
  auto ctl = [&](const char* j) { bool ok = false; std::string e; d.control(am::json::Value::parse(j, ok), e); };
  ctl("{\"op\":\"open\",\"backend\":\"null\",\"inChannels\":2,\"outChannels\":2}");   // a running mixer: meter frames
  auto stripDb = [&](int i) { return d.state()["strips"].at(i)["db"].asNumber(); };
  auto strip = [&](int i, const char* k) { return d.state()["strips"].at(i)[k]; };

  // a wrong PIN, then the right one: a control token of the device's own
  amfx1_client_t* c = amfx1_create("127.0.0.1", port); CHECK(c);
  const std::string pin = am::Daemon::pinFor(o.token, static_cast<long long>(std::time(nullptr)));
  const std::string wrong = pin == "000000" ? "111111" : "000000";
  CHECK(amfx1_pair(c, "12x", "ESP32 desk") == AMFX1_ERR_ARG);
  CHECK(amfx1_pair(c, wrong.c_str(), "ESP32 desk") == AMFX1_ERR_AUTH && amfx1_token(c)[0] == 0);
  int pr = amfx1_pair(c, pin.c_str(), "ESP32 desk");
  if (pr != AMFX1_OK) { const std::string p2 = am::Daemon::pinFor(o.token, static_cast<long long>(std::time(nullptr))); pr = amfx1_pair(c, p2.c_str(), "ESP32 desk"); }   // the minute turned
  CHECK(pr == AMFX1_OK && std::strncmp(amfx1_token(c), "amx_", 4) == 0);
  const std::string paired = amfx1_token(c);

  // HTTP polling transport with the paired token
  amfx1_set_transport(c, AMFX1_TRANSPORT_HTTP); amfx1_set_poll(c, 100, 100); amfx1_on_state(c, onState, nullptr); amfx1_on_meter(c, onMeter, nullptr);
  CHECK(amfx1_begin(c, nullptr) == AMFX1_OK && !amfx1_using_websocket(c) && amfx1_model_of(c)->strips == 8);
  CHECK(amfx1_set_fader(c, 1, -6) == AMFX1_OK && near(stripDb(0), -6));
  CHECK(amfx1_set_mute(c, 2, 1) == AMFX1_OK && strip(1, "mute").asBool());
  CHECK(amfx1_set_solo(c, 3, 1) == AMFX1_OK && strip(2, "solo").asBool());
  CHECK(amfx1_set_pan(c, 4, -0.75f) == AMFX1_OK && near(strip(3, "pan").asNumber(), -0.75));
  CHECK(amfx1_set_master(c, -3.5f) == AMFX1_OK && near(d.state()["master"]["db"].asNumber(), -3.5));
  CHECK(amfx1_set_fader(c, 99, 0) == AMFX1_ERR_ARG && amfx1_set_fader(c, 0, 0) == AMFX1_ERR_ARG);
  ctl("{\"op\":\"gain\",\"strip\":5,\"db\":-20}");   // the console moves a fader: the poll sees it
  CHECK(waitFor(c, [&] { return near(amfx1_get_fader(c, 6), -20); }) && states > 0);
  CHECK(waitFor(c, [&] { return meterFrames > 0; }) && amfx1_get_meter(c, 1) <= 0.5f);
  amfx1_destroy(c);

  // WebSocket transport (AUTO picks it): moves on the open socket, state frames back
  states = 0; meterFrames = 0;
  c = amfx1_create("127.0.0.1", port); amfx1_on_state(c, onState, nullptr); amfx1_on_meter(c, onMeter, nullptr);
  CHECK(amfx1_begin(c, paired.c_str()) == AMFX1_OK && amfx1_using_websocket(c) && amfx1_connected(c));
  CHECK(waitFor(c, [&] { return states > 0 && amfx1_model_of(c)->haveState; }) && near(amfx1_get_fader(c, 1), -6) && amfx1_get_mute(c, 2) && amfx1_get_solo(c, 3));
  CHECK(waitFor(c, [&] { return meterFrames > 2; }));
  CHECK(amfx1_set_fader(c, 3, -12.25f) == AMFX1_OK && waitFor(c, [&] { return near(stripDb(2), -12.25); }));
  CHECK(amfx1_set_mute(c, 2, 0) == AMFX1_OK && waitFor(c, [&] { return !strip(1, "mute").asBool(); }));
  CHECK(amfx1_set_master_mute(c, 1) == AMFX1_OK && waitFor(c, [&] { return d.state()["master"]["mute"].asBool(); }));
  CHECK(amfx1_set_master_mute(c, 0) == AMFX1_OK);
  ctl("{\"op\":\"pan\",\"strip\":7,\"pan\":0.5}");
  CHECK(waitFor(c, [&] { return near(amfx1_get_pan(c, 8), 0.5); }));
  // a scene: save, change, recall
  CHECK(waitFor(c, [&] { return !amfx1_get_mute(c, 2) && near(amfx1_get_fader(c, 3), -12.25); }));
  CHECK(amfx1_save_scene(c, 1) == AMFX1_OK);
  CHECK(amfx1_set_fader(c, 1, 0) == AMFX1_OK && amfx1_set_mute(c, 4, 1) == AMFX1_OK && waitFor(c, [&] { return strip(3, "mute").asBool() && near(stripDb(0), 0); }));
  CHECK(waitFor(c, [&] { return amfx1_get_mute(c, 4) && near(amfx1_get_fader(c, 1), 0); }));
  CHECK(amfx1_recall_scene(c, 1) == AMFX1_OK && waitFor(c, [&] { return !strip(3, "mute").asBool() && near(stripDb(0), -6) && near(stripDb(2), -12.25); }));
  CHECK(amfx1_recall_scene(c, 2) == AMFX1_ERR_ARG);
  amfx1_destroy(c);

  // a wrong token: refused on both transports, nothing moves
  c = amfx1_create("127.0.0.1", port);
  { const int br = amfx1_begin(c, "amx_not-a-real-token-000000"); if (br != AMFX1_ERR_AUTH) std::fprintf(stderr, "begin %d: %s\n", br, amfx1_last_error(c)); CHECK(br == AMFX1_ERR_AUTH); }
  amfx1_set_transport(c, AMFX1_TRANSPORT_HTTP); amfx1_begin(c, "amx_not-a-real-token-000000");
  CHECK(amfx1_set_fader(c, 1, 5) == AMFX1_ERR_AUTH && near(stripDb(0), -6));
  amfx1_destroy(c);
  d.stop(); std::remove(o.tokenStore.c_str());
  return 0;
}

int main() {
  if (int r = unit()) return r;
  std::printf("iot sdk: core ok\n");
  if (int r = live()) return r;
  std::printf("iot sdk: live service ok\n");
  return 0;
}
