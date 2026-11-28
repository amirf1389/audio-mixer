/*
  AmFX1.cpp - control the AmF X1 console from a microcontroller. See AmFX1.h.
  MIT License, AMFB Audio.
*/
#include "AmFX1.h"
#include <string.h>
#include <stdio.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_random.h>
#endif

static void copyStr(char* dst, size_t cap, const char* src) {
  size_t i = 0;
  if (src) for (; src[i] && i + 1 < cap; ++i) dst[i] = src[i];
  dst[i] = 0;
}

#if defined(AMFX1_OWN_CLIENT)
AmFX1::AmFX1(const char* host, uint16_t port) : AmFX1(_own, host, port) {}
#endif

AmFX1::AmFX1(Client& client, const char* host, uint16_t port)
    : _client(&client), _port(port ? port : AMFX1_DEFAULT_PORT), _transport(AMFX1_AUTO), _ws(false), _authFail(false), _timeout(3000),
      _stateMs(500), _meterMs(150), _nextRetry(0), _lastState(0), _lastMeter(0), _heldUntil(0), _seq(0), _onState(0), _onMeter(0), _onError(0) {
  copyStr(_host, sizeof _host, host);
  copyStr(_hostHeader, sizeof _hostHeader, host);
  _token[0] = 0;
  _error[0] = 0;
  amfx1_model_init(&_m);
  amfx1_ws_rx_init(&_wsrx);
  memset(_scenes, 0, sizeof _scenes);
}

void AmFX1::randomBytes(uint8_t* out, size_t n) {
#if defined(ARDUINO_ARCH_ESP32)
  esp_fill_random(out, n);
#else
  for (size_t i = 0; i < n; ++i) out[i] = (uint8_t)random(256);
#endif
}

void AmFX1::fail(const char* e) {
  copyStr(_error, sizeof _error, e);
  if (_onError) _onError(*this);
}

// the service answers only to an IPv4 address in the Host header (its local-network policy): a name is resolved first where we can
bool AmFX1::resolveHost() {
#if defined(AMFX1_OWN_CLIENT)
  IPAddress ip;
  if (ip.fromString(_host)) return true;
  if (WiFi.hostByName(_host, ip) != 1) { fail("the console's name could not be resolved: use its address"); return false; }
  copyStr(_hostHeader, sizeof _hostHeader, ip.toString().c_str());
#endif
  return true;
}

void AmFX1::setPollInterval(uint16_t stateMs, uint16_t meterMs) {
  _stateMs = stateMs < 50 ? 50 : stateMs;
  _meterMs = meterMs == 0 ? 0 : meterMs < 50 ? 50 : meterMs;
}

void AmFX1::stopClient() {
  if (_ws && _client->connected()) writeFrame(8, 0, 0);   // a polite close
  _client->stop();
}

void AmFX1::end() {
  stopClient();
  _ws = false;
}

bool AmFX1::connected() const {
  return _ws ? _client->connected() : (_token[0] != 0 && _m.haveState);
}

void AmFX1::fire(uint16_t f) {
  if (f & AMFX1_F_KICKED) { _heldUntil = millis() + 120000UL; stopClient(); fail("disconnected by the console: waiting two minutes"); return; }
  if ((f & AMFX1_F_ERROR) && _m.error[0]) {
    if (_ws && strcmp(_m.error, "unauthorised") == 0) { _authFail = true; _client->stop(); fail("token refused: pair again"); return; }
    if (_ws && strcmp(_m.error, "held") == 0) { _heldUntil = millis() + 120000UL; _client->stop(); fail("disconnected by the console: waiting two minutes"); return; }
    fail(_m.error);
  }
  if ((f & AMFX1_F_STATE) && _onState) _onState(*this);
  if ((f & AMFX1_F_METERS) && _onMeter) _onMeter(*this);
}

// ---------------------------------------------------------------- HTTP
int AmFX1::http(const char* method, const char* path, const char* body) {
  char head[300];
  long blen = body ? (long)strlen(body) : -1;
  if (amfx1_http_head(head, sizeof head, method, _hostHeader, _port, path, _token, blen) < 0) { fail("request too long or bad token"); return 0; }
  if (_ws) { _client->stop(); _ws = false; }
  if (!_client->connect(_host, _port)) { fail("the console did not answer (address, port, LAN access on?)"); return 0; }
  _client->write((const uint8_t*)head, strlen(head));
  if (body) _client->write((const uint8_t*)body, (size_t)blen);
  amfx1_http_rx rx;
  amfx1_http_rx_init(&rx, 0);
  uint8_t buf[128];
  unsigned long start = millis();
  while (!rx.done && millis() - start < _timeout) {
    int n = _client->available();
    if (n <= 0) { if (!_client->connected()) break; delay(1); continue; }
    if (n > (int)sizeof buf) n = sizeof buf;
    n = _client->read(buf, (size_t)n);
    if (n > 0) amfx1_http_rx_feed(&rx, (const char*)buf, (size_t)n, &_m);
  }
  _client->stop();
  if (rx.st >= 2) amfx1_model_end(&_m);
  if (!rx.status) fail("no answer from the console");
  return rx.status;
}

static const char* statusText(int st) {
  switch (st) {
    case 401: return "token or PIN refused";
    case 403: return "refused: a view-only token, a held link, or a host name instead of the address";
    case 429: return "too many tries: wait a minute";
    default: return "unexpected answer from the console";
  }
}

bool AmFX1::pair(const char* pin, const char* deviceName) {
  char body[96];
  if (amfx1_msg_pair(body, sizeof body, pin, deviceName) < 0) { fail("the PIN is six digits"); return false; }
  if (!resolveHost()) return false;
  char saved[AMFX1_TOKEN_MAX + 1];
  memcpy(saved, _token, sizeof saved);
  _token[0] = 0;
  _m.token[0] = 0;
  int st = http("POST", "/api/pair", body);
  if (st == 200 && _m.token[0]) {
    memcpy(_token, _m.token, sizeof _token);
    memset(_m.token, 0, sizeof _m.token);
    return begin(_token);
  }
  memcpy(_token, saved, sizeof saved);
  if (st == 401) fail("wrong PIN (it changes every minute)");
  else if (st) fail(statusText(st));
  return false;
}

// ---------------------------------------------------------------- WebSocket
void AmFX1::writeFrame(int opcode, const uint8_t* p, size_t n) {
  uint8_t f[160], mask[4];
  randomBytes(mask, 4);
  int k = amfx1_ws_frame(f, sizeof f, opcode, p, n, mask);
  if (k > 0) _client->write(f, (size_t)k);
}

void AmFX1::feedWs(const uint8_t* d, size_t n) {
  size_t off = 0;
  while (off < n && _ws) {
    int ev = 0;
    off += amfx1_ws_rx_feed(&_wsrx, d + off, n - off, &_m, &ev);
    if (ev == AMFX1_WS_MESSAGE) fire(amfx1_model_end(&_m));
    else if (ev == AMFX1_WS_PING) writeFrame(10, _wsrx.ctl, _wsrx.ctln);
    else if (ev == AMFX1_WS_CLOSE || ev == AMFX1_WS_ERROR) { _client->stop(); _nextRetry = millis() + 1000; return; }
    if (!_client->connected()) return;
  }
}

bool AmFX1::openWs() {
  char req[300];
  uint8_t key[16];
  randomBytes(key, sizeof key);
  if (amfx1_ws_handshake(req, sizeof req, _hostHeader, _port, _token, key) < 0) { fail("bad token"); return false; }
  if (!_client->connect(_host, _port)) { fail("the console did not answer (address, port, LAN access on?)"); return false; }
  _client->write((const uint8_t*)req, strlen(req));
  amfx1_http_rx rx;
  amfx1_http_rx_init(&rx, 1);
  uint8_t buf[128];
  unsigned long start = millis();
  while (!rx.done && millis() - start < _timeout) {
    if (!_client->available()) { if (!_client->connected()) break; delay(1); continue; }
    int c = _client->read();   // byte by byte up to the end of the headers: what follows is WebSocket frames
    if (c < 0) continue;
    buf[0] = (uint8_t)c;
    amfx1_http_rx_feed(&rx, (const char*)buf, 1, 0);
  }
  if (rx.status != 101) {
    _client->stop();
    fail(rx.status == 401 ? "token refused: pair again" : rx.status ? statusText(rx.status) : "no answer from the console");
    if (rx.status == 401) _authFail = true;
    return false;
  }
  amfx1_ws_rx_init(&_wsrx);
  _ws = true;
  return true;
}

// the service upgrades first and judges the token after: the first frame says which
bool AmFX1::waitFirstFrame() {
  unsigned long start = millis();
  while (_ws && _client->connected() && !_m.haveState && !_authFail && millis() - start < _timeout) loop();
  return _ws && _client->connected() && _m.haveState;
}

bool AmFX1::begin(const char* token) {
  if (!token || !*token || strlen(token) > AMFX1_TOKEN_MAX) { fail("no token: pair first"); return false; }
  if (token != _token) copyStr(_token, sizeof _token, token);
  end();
  _error[0] = 0;
  _authFail = false;
  if (!resolveHost()) return false;
  bool tryWs = _transport == AMFX1_WEBSOCKET;
#if !defined(__AVR__)
  if (_transport == AMFX1_AUTO) tryWs = true;
#endif
  if (tryWs) {
    if (openWs() && waitFirstFrame()) return true;
    _ws = false;
    if (_authFail) return false;
    if (_transport == AMFX1_WEBSOCKET) { _nextRetry = millis() + 2000; return false; }
  }
  _lastState = _lastMeter = 0;
  int st = http("GET", "/api/state", 0);
  if (st != 200) return false;
  fire(_m.flags);
  return true;
}

// ---------------------------------------------------------------- commands
bool AmFX1::command(const char* json) {
  if (!_token[0]) { fail("no token: pair or begin first"); return false; }
  if (_ws) {
    if (!_client->connected()) { fail("not connected: reconnecting"); return false; }
    char withSeq[140];
    size_t l = strlen(json);
    if (l < 2 || l + 24 > sizeof withSeq) return false;
    memcpy(withSeq, json, l - 1);
    snprintf(withSeq + l - 1, sizeof withSeq - (l - 1), ",\"seq\":%ld}", ++_seq);
    writeFrame(1, (const uint8_t*)withSeq, strlen(withSeq));
    return true;
  }
  _m.error[0] = 0;
  int st = http("POST", "/api/control", json);
  if (st == 200) return true;
  if (st) fail(_m.error[0] ? _m.error : statusText(st));
  return false;
}

bool AmFX1::send(const char* json) {
  if (!json || json[0] != '{' || strlen(json) > 120) { fail("a JSON object of up to 120 characters"); return false; }
  return command(json);
}

#define AMFX1_CH_OR_FAIL(ch) if (!amfx1_valid_ch(ch)) { fail("channel out of range"); return false; }

bool AmFX1::setFader(int ch, float db) {
  char b[96]; AMFX1_CH_OR_FAIL(ch);
  amfx1_msg_fader(b, sizeof b, ch, db, -1);
  if (!command(b)) return false;
  _m.strip[ch - 1].db = amfx1_clamp_db(db);
  return true;
}
bool AmFX1::setPan(int ch, float pan) {
  char b[96]; AMFX1_CH_OR_FAIL(ch);
  amfx1_msg_pan(b, sizeof b, ch, pan, -1);
  if (!command(b)) return false;
  _m.strip[ch - 1].pan = amfx1_clamp_pan(pan);
  return true;
}
bool AmFX1::setMute(int ch, bool on) {
  char b[96]; AMFX1_CH_OR_FAIL(ch);
  amfx1_msg_mute(b, sizeof b, ch, on, -1);
  if (!command(b)) return false;
  _m.strip[ch - 1].mute = on;
  return true;
}
bool AmFX1::setSolo(int ch, bool on) {
  char b[96]; AMFX1_CH_OR_FAIL(ch);
  amfx1_msg_solo(b, sizeof b, ch, on, -1);
  if (!command(b)) return false;
  _m.strip[ch - 1].solo = on;
  return true;
}
bool AmFX1::setMaster(float db) {
  char b[96];
  amfx1_msg_master(b, sizeof b, db, -1);
  if (!command(b)) return false;
  _m.masterDb = amfx1_clamp_db(db);
  return true;
}
bool AmFX1::setMasterMute(bool on) {
  char b[96];
  amfx1_msg_master_mute(b, sizeof b, on, -1);
  if (!command(b)) return false;
  _m.masterMute = on;
  return true;
}

// ---------------------------------------------------------------- scenes
amfx1_scene* AmFX1::sceneData(int n) { return n >= 1 && n <= AMFX1_MAX_SCENES ? &_scenes[n - 1] : 0; }
bool AmFX1::saveScene(int n) {
  amfx1_scene* s = sceneData(n);
  if (!s) { fail("scene out of range"); return false; }
  if (!amfx1_scene_capture(&_m, s)) { fail("no state from the console yet"); return false; }
  return true;
}
bool AmFX1::recallScene(int n) {
  amfx1_scene* s = sceneData(n);
  if (!s || !s->used) { fail("empty scene"); return false; }
  char b[96];
  int cursor = 0;
  // the cursor walks the scene against the console as it is now (command() leaves the model alone until the walk ends)
  while (amfx1_scene_next(&_m, s, &cursor, b, sizeof b) > 0) {
    if (!command(b)) return false;
  }
  for (int i = 0; i < s->strips; ++i) {
    _m.strip[i].db = s->s[i].db; _m.strip[i].pan = s->s[i].pan; _m.strip[i].mute = s->s[i].mute; _m.strip[i].solo = s->s[i].solo;
  }
  _m.masterDb = s->masterDb;
  return true;
}

// ---------------------------------------------------------------- getters
float AmFX1::getFader(int ch) const { return amfx1_valid_ch(ch) ? _m.strip[ch - 1].db : AMFX1_DB_MIN; }
bool AmFX1::getMute(int ch) const { return amfx1_valid_ch(ch) && _m.strip[ch - 1].mute; }
bool AmFX1::getSolo(int ch) const { return amfx1_valid_ch(ch) && _m.strip[ch - 1].solo; }
float AmFX1::getPan(int ch) const { return amfx1_valid_ch(ch) ? _m.strip[ch - 1].pan : 0; }
float AmFX1::getMeter(int ch) const { return amfx1_valid_ch(ch) ? amfx1_lin_to_db(_m.strip[ch - 1].pk) : AMFX1_SILENCE_DB; }
float AmFX1::getMeterRms(int ch) const { return amfx1_valid_ch(ch) ? amfx1_lin_to_db(_m.strip[ch - 1].rms) : AMFX1_SILENCE_DB; }
float AmFX1::getMasterMeter() const { return amfx1_lin_to_db(_m.masterL > _m.masterR ? _m.masterL : _m.masterR); }
const char* AmFX1::channelName(int ch) const { return amfx1_valid_ch(ch) ? _m.strip[ch - 1].name : ""; }

// ---------------------------------------------------------------- the pump
void AmFX1::loop() {
  if (!_token[0] || _authFail) return;
  unsigned long t = millis();
  if (_heldUntil && (long)(t - _heldUntil) < 0) return;
  _heldUntil = 0;
  if (_ws) {
    if (!_client->connected()) {
      if ((long)(t - _nextRetry) < 0) return;
      _nextRetry = t + 2000;
      _ws = false;
      if (!openWs()) return;
    }
    uint8_t buf[128];
    int guard = 32;   // at most 4 KB a call: the sketch's own work keeps running while a big state frame arrives
    while (_ws && _client->available() > 0 && guard--) {
      int n = _client->read(buf, sizeof buf);
      if (n <= 0) break;
      feedWs(buf, (size_t)n);
    }
    return;
  }
  if (t - _lastState >= _stateMs || _lastState == 0) {
    char path[40];
    _lastState = t;
    snprintf(path, sizeof path, "/api/state?since=%lu", _m.version);
    if (http("GET", path, 0) == 200) fire(_m.flags & (uint16_t)~AMFX1_F_SAME);
  }
  if (_meterMs && t - _lastMeter >= _meterMs) {
    _lastMeter = t;
    if (http("GET", "/api/meters", 0) == 200) fire(_m.flags);
  }
}
