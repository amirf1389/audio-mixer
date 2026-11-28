/* AmF X1 IoT SDK for ESP-IDF / POSIX: the socket client around the portable core (amfx1_core.c). See include/amfx1.h. */
#include "amfx1.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

struct amfx1_client {
  char host[64], ip[48]; uint16_t port; struct sockaddr_storage addr; socklen_t addrLen; int resolved;
  char token[AMFX1_TOKEN_MAX + 1];
  amfx1_transport_t transport; int useWs, fd, timeoutMs, stateMs, meterMs;
  long long nextRetry, lastState, lastMeter, heldUntil;
  long seq; int authFail;
  amfx1_model model; amfx1_ws_rx wsrx;
  amfx1_scene scenes[AMFX1_MAX_SCENES];
  char error[96];
  amfx1_event_cb onState, onMeter; void *stateUser, *meterUser;
};

static long long now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static void set_err(amfx1_client_t* c, const char* e) { snprintf(c->error, sizeof c->error, "%s", e); }
static void rnd(uint8_t* out, size_t n) {
#ifdef ESP_PLATFORM
  esp_fill_random(out, n);
#else
  size_t i; FILE* f = fopen("/dev/urandom", "rb"); size_t got = f ? fread(out, 1, n, f) : 0; if (f) fclose(f);
  for (i = got; i < n; ++i) out[i] = (uint8_t)rand();
#endif
}

amfx1_client_t* amfx1_create(const char* host, uint16_t port) {
  amfx1_client_t* c; if (!host || strlen(host) >= sizeof c->host) return NULL;
  c = (amfx1_client_t*)calloc(1, sizeof *c); if (!c) return NULL;
  snprintf(c->host, sizeof c->host, "%s", host); c->port = port ? port : AMFX1_DEFAULT_PORT; c->fd = -1; c->timeoutMs = 3000; c->stateMs = 500; c->meterMs = 100;
  amfx1_model_init(&c->model); return c;
}
void amfx1_destroy(amfx1_client_t* c) { if (!c) return; amfx1_stop(c); memset(c->token, 0, sizeof c->token); free(c); }
void amfx1_set_transport(amfx1_client_t* c, amfx1_transport_t t) { c->transport = t; }
void amfx1_set_timeout(amfx1_client_t* c, int ms) { c->timeoutMs = ms < 200 ? 200 : ms; }
void amfx1_set_poll(amfx1_client_t* c, int s, int m) { c->stateMs = s < 50 ? 50 : s; c->meterMs = m <= 0 ? 0 : m < 50 ? 50 : m; }
const char* amfx1_token(const amfx1_client_t* c) { return c->token; }
const char* amfx1_last_error(const amfx1_client_t* c) { return c->error; }
const amfx1_model* amfx1_model_of(const amfx1_client_t* c) { return &c->model; }
int amfx1_connected(const amfx1_client_t* c) { return c->useWs ? c->fd >= 0 : c->token[0] != 0 && c->model.haveState; }
int amfx1_using_websocket(const amfx1_client_t* c) { return c->useWs; }
void amfx1_on_state(amfx1_client_t* c, amfx1_event_cb cb, void* u) { c->onState = cb; c->stateUser = u; }
void amfx1_on_meter(amfx1_client_t* c, amfx1_event_cb cb, void* u) { c->onMeter = cb; c->meterUser = u; }

static int resolve(amfx1_client_t* c) {
  struct addrinfo hints, *res = NULL; char port[8];
  if (c->resolved) return 0;
  memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM; snprintf(port, sizeof port, "%u", c->port);
  if (getaddrinfo(c->host, port, &hints, &res) != 0 || !res) { set_err(c, "the console's address could not be resolved"); return -1; }
  memcpy(&c->addr, res->ai_addr, res->ai_addrlen); c->addrLen = (socklen_t)res->ai_addrlen;
  inet_ntop(AF_INET, &((struct sockaddr_in*)res->ai_addr)->sin_addr, c->ip, sizeof c->ip);   /* the service serves IPv4 literals as Host only */
  freeaddrinfo(res); c->resolved = 1; return 0;
}
static int tcp_open(amfx1_client_t* c) {
  int fd, fl, one = 1, e = 0; socklen_t el = sizeof e; fd_set w; struct timeval tv;
  if (resolve(c) < 0) return -1;
  fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) { set_err(c, "no socket"); return -1; }
  fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  if (connect(fd, (struct sockaddr*)&c->addr, c->addrLen) < 0 && errno != EINPROGRESS) { close(fd); set_err(c, "the console refused the connection"); return -1; }
  FD_ZERO(&w); FD_SET(fd, &w); tv.tv_sec = c->timeoutMs / 1000; tv.tv_usec = (c->timeoutMs % 1000) * 1000;
  if (select(fd + 1, NULL, &w, NULL, &tv) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el) < 0 || e) { close(fd); set_err(c, "the console did not answer (address, port, --allow-lan?)"); return -1; }
  fcntl(fd, F_SETFL, fl);
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  return fd;
}
static int send_all(int fd, const void* p, size_t n) {
  const char* b = (const char*)p; while (n) { ssize_t k = send(fd, b, n, 0); if (k <= 0) { if (k < 0 && errno == EINTR) continue; return -1; } b += k; n -= (size_t)k; } return 0;
}

/* one HTTP exchange; the body goes into the model; returns the status (0: no answer) */
static int http(amfx1_client_t* c, const char* method, const char* path, const char* body) {
  char head[384], buf[512]; amfx1_http_rx rx; int fd, n; long blen = body ? (long)strlen(body) : -1;
  if (amfx1_http_head(head, sizeof head, method, c->ip[0] ? c->ip : c->host, c->port, path, c->token, blen) < 0) { set_err(c, "request too long or bad token"); return 0; }
  fd = tcp_open(c); if (fd < 0) return 0;
  if (send_all(fd, head, strlen(head)) < 0 || (body && send_all(fd, body, (size_t)blen) < 0)) { close(fd); set_err(c, "send failed"); return 0; }
  amfx1_http_rx_init(&rx, 0);
  while (!rx.done && (n = (int)recv(fd, buf, sizeof buf, 0)) > 0) amfx1_http_rx_feed(&rx, buf, (size_t)n, &c->model);
  close(fd);
  if (rx.st >= 2) amfx1_model_end(&c->model);   /* a body was read (to its length or to the close) */
  if (!rx.status) set_err(c, "no answer from the console");
  return rx.status;
}
static void fire(amfx1_client_t* c, uint16_t f) {
  if ((f & AMFX1_F_KICKED)) { set_err(c, "disconnected by the console: waiting two minutes"); c->heldUntil = now_ms() + 120000; amfx1_stop(c); }
  if ((f & AMFX1_F_ERROR) && c->model.error[0]) {
    if (c->fd >= 0 && !strcmp(c->model.error, "unauthorised")) { set_err(c, "token refused: pair again"); c->authFail = 1; amfx1_stop(c); return; }
    if (c->fd >= 0 && !strcmp(c->model.error, "held")) { set_err(c, "disconnected by the console: waiting two minutes"); c->heldUntil = now_ms() + 120000; amfx1_stop(c); return; }
    set_err(c, c->model.error); }
  if ((f & AMFX1_F_STATE) && c->onState) c->onState(c, c->stateUser);
  if ((f & AMFX1_F_METERS) && c->onMeter) c->onMeter(c, c->meterUser);
}
static int status_err(amfx1_client_t* c, int st) {
  if (st == 200) return AMFX1_OK;
  if (st == 401) { set_err(c, "token refused: pair again"); return AMFX1_ERR_AUTH; }
  if (st == 403) { if (!c->model.error[0]) set_err(c, "refused (a view token, a held link, or a host name instead of the address)"); return AMFX1_ERR_REFUSED; }
  if (st == 429) { set_err(c, "too many tries: wait a minute"); return AMFX1_ERR_REFUSED; }
  return st ? AMFX1_ERR_PROTOCOL : AMFX1_ERR_CONNECT;
}

int amfx1_pair(amfx1_client_t* c, const char* pin, const char* name) {
  char body[96]; int st; char saved[AMFX1_TOKEN_MAX + 1];
  if (amfx1_msg_pair(body, sizeof body, pin, name) < 0) { set_err(c, "the PIN is six digits"); return AMFX1_ERR_ARG; }
  memcpy(saved, c->token, sizeof saved); c->token[0] = 0; c->model.token[0] = 0; c->model.error[0] = 0;
  st = http(c, "POST", "/api/pair", body);
  if (st == 200 && c->model.token[0]) { memcpy(c->token, c->model.token, sizeof c->token); memset(c->model.token, 0, sizeof c->model.token); return AMFX1_OK; }
  memcpy(c->token, saved, sizeof saved);
  if (st == 401) { set_err(c, "wrong PIN"); return AMFX1_ERR_AUTH; }
  return st == 200 ? AMFX1_ERR_PROTOCOL : status_err(c, st);
}

static int ws_open(amfx1_client_t* c) {
  char req[320], buf[512]; uint8_t key[16]; amfx1_http_rx rx; int fd, n = 0; size_t used = 0;
  rnd(key, sizeof key);
  if (amfx1_ws_handshake(req, sizeof req, c->ip[0] ? c->ip : c->host, c->port, c->token, key) < 0) { set_err(c, "bad token"); return AMFX1_ERR_ARG; }
  fd = tcp_open(c); if (fd < 0) return AMFX1_ERR_CONNECT;
  if (send_all(fd, req, strlen(req)) < 0) { close(fd); return AMFX1_ERR_CONNECT; }
  amfx1_http_rx_init(&rx, 1);
  while (!rx.done && (n = (int)recv(fd, buf, sizeof buf, 0)) > 0) used = amfx1_http_rx_feed(&rx, buf, (size_t)n, NULL);
  if (rx.status != 101) { close(fd); if (rx.status == 404 || rx.status == 400) set_err(c, "this console has no WebSocket control"); if (!rx.status) return AMFX1_ERR_CONNECT; if (rx.status == 404 || rx.status == 400) return AMFX1_ERR_PROTOCOL; return status_err(c, rx.status); }
  c->fd = fd; amfx1_ws_rx_init(&c->wsrx);
  if (n > 0 && used < (size_t)n) {   /* the first frames came with the answer */
    size_t off = used; while (off < (size_t)n) { int ev; off += amfx1_ws_rx_feed(&c->wsrx, (uint8_t*)buf + off, (size_t)n - off, &c->model, &ev); if (ev == AMFX1_WS_MESSAGE) fire(c, amfx1_model_end(&c->model)); }
  }
  return AMFX1_OK;
}

void amfx1_stop(amfx1_client_t* c) {
  if (c->fd >= 0) { uint8_t f[8], m[4]; int n; rnd(m, 4); n = amfx1_ws_frame(f, sizeof f, 8, NULL, 0, m); if (n > 0) send(c->fd, f, (size_t)n, 0); close(c->fd); }
  c->fd = -1;
}

int amfx1_begin(amfx1_client_t* c, const char* token) {
  int r;
  if (token && token != c->token) { if (strlen(token) > AMFX1_TOKEN_MAX) return AMFX1_ERR_ARG; snprintf(c->token, sizeof c->token, "%s", token); }
  if (!c->token[0]) { set_err(c, "no token: pair first"); return AMFX1_ERR_ARG; }
  amfx1_stop(c); c->error[0] = 0;
  if (c->transport != AMFX1_TRANSPORT_HTTP) {
    c->authFail = 0; r = ws_open(c);
    if (r == AMFX1_OK) {   /* the service upgrades first and judges the token after: wait for its first frame */
      long long end = now_ms() + c->timeoutMs; c->useWs = 1;
      while (c->fd >= 0 && !c->model.haveState && !c->authFail && now_ms() < end) amfx1_loop(c, 50);
      if (c->authFail) { c->useWs = 0; c->token[0] = 0; return AMFX1_ERR_AUTH; }
      if (c->fd < 0) { c->nextRetry = now_ms() + 2000; return c->heldUntil ? AMFX1_ERR_REFUSED : AMFX1_ERR_CONNECT; }
      return AMFX1_OK; }
    if (c->transport == AMFX1_TRANSPORT_WEBSOCKET || r == AMFX1_ERR_AUTH || r == AMFX1_ERR_REFUSED || r == AMFX1_ERR_CONNECT) { c->nextRetry = now_ms() + 2000; return r; }
  }
  c->useWs = 0; c->lastState = c->lastMeter = 0;
  r = status_err(c, http(c, "GET", "/api/state", NULL));   /* the first state; /api/state is readable without a token, so check it can control too */
  if (r == AMFX1_OK) fire(c, c->model.flags);
  return r;
}

static int send_cmd(amfx1_client_t* c, const char* json) {
  if (!c->token[0]) { set_err(c, "no token"); return AMFX1_ERR_ARG; }
  if (c->useWs) {
    uint8_t f[160], m[4]; char withSeq[140]; size_t l = strlen(json); int n;
    if (l < 2 || l + 24 > sizeof withSeq) return AMFX1_ERR_ARG;
    memcpy(withSeq, json, l - 1); n = snprintf(withSeq + l - 1, sizeof withSeq - (l - 1), ",\"seq\":%ld}", ++c->seq); (void)n;
    rnd(m, 4); n = amfx1_ws_frame(f, sizeof f, 1, (const uint8_t*)withSeq, strlen(withSeq), m);
    if (c->fd < 0 || n < 0 || send_all(c->fd, f, (size_t)n) < 0) { amfx1_stop(c); set_err(c, "not connected"); c->nextRetry = now_ms() + 500; return AMFX1_ERR_CONNECT; }
    return AMFX1_OK;
  }
  c->model.error[0] = 0;
  { int st = http(c, "POST", "/api/control", json); return status_err(c, st); }
}
int amfx1_send_raw(amfx1_client_t* c, const char* json) { if (!json || strlen(json) > 120 || json[0] != '{') return AMFX1_ERR_ARG; return send_cmd(c, json); }

#define STRIP_OR_ARG(ch) if (!amfx1_valid_ch(ch)) { set_err(c, "channel out of range"); return AMFX1_ERR_ARG; }
int amfx1_set_fader(amfx1_client_t* c, int ch, float db) { char b[96]; int r; STRIP_OR_ARG(ch); amfx1_msg_fader(b, sizeof b, ch, db, -1); r = send_cmd(c, b); if (!r) c->model.strip[ch - 1].db = amfx1_clamp_db(db); return r; }
int amfx1_set_pan(amfx1_client_t* c, int ch, float p) { char b[96]; int r; STRIP_OR_ARG(ch); amfx1_msg_pan(b, sizeof b, ch, p, -1); r = send_cmd(c, b); if (!r) c->model.strip[ch - 1].pan = amfx1_clamp_pan(p); return r; }
int amfx1_set_mute(amfx1_client_t* c, int ch, int on) { char b[96]; int r; STRIP_OR_ARG(ch); amfx1_msg_mute(b, sizeof b, ch, on, -1); r = send_cmd(c, b); if (!r) c->model.strip[ch - 1].mute = (uint8_t)!!on; return r; }
int amfx1_set_solo(amfx1_client_t* c, int ch, int on) { char b[96]; int r; STRIP_OR_ARG(ch); amfx1_msg_solo(b, sizeof b, ch, on, -1); r = send_cmd(c, b); if (!r) c->model.strip[ch - 1].solo = (uint8_t)!!on; return r; }
int amfx1_set_master(amfx1_client_t* c, float db) { char b[96]; int r; amfx1_msg_master(b, sizeof b, db, -1); r = send_cmd(c, b); if (!r) c->model.masterDb = amfx1_clamp_db(db); return r; }
int amfx1_set_master_mute(amfx1_client_t* c, int on) { char b[96]; int r; amfx1_msg_master_mute(b, sizeof b, on, -1); r = send_cmd(c, b); if (!r) c->model.masterMute = (uint8_t)!!on; return r; }

amfx1_scene* amfx1_scene_data(amfx1_client_t* c, int n) { return n >= 1 && n <= AMFX1_MAX_SCENES ? &c->scenes[n - 1] : NULL; }
int amfx1_save_scene(amfx1_client_t* c, int n) { amfx1_scene* s = amfx1_scene_data(c, n); if (!s) return AMFX1_ERR_ARG; if (!amfx1_scene_capture(&c->model, s)) { set_err(c, "no state from the console yet"); return AMFX1_ERR_PROTOCOL; } return AMFX1_OK; }
int amfx1_recall_scene(amfx1_client_t* c, int n) {
  amfx1_scene* s = amfx1_scene_data(c, n); amfx1_model snap; int cur = 0, r = AMFX1_OK; char b[96];
  if (!s || !s->used) { set_err(c, "empty scene"); return AMFX1_ERR_ARG; }
  memcpy(&snap, &c->model, sizeof snap);   /* what the console is now: the scene is sent as differences from it */
  while (r == AMFX1_OK && amfx1_scene_next(&snap, s, &cur, b, sizeof b) > 0) r = send_cmd(c, b);
  if (r == AMFX1_OK) { int i; for (i = 0; i < s->strips; ++i) { c->model.strip[i].db = s->s[i].db; c->model.strip[i].pan = s->s[i].pan; c->model.strip[i].mute = s->s[i].mute; c->model.strip[i].solo = s->s[i].solo; } c->model.masterDb = s->masterDb; }
  return r;
}

float amfx1_get_fader(const amfx1_client_t* c, int ch) { return amfx1_valid_ch(ch) ? c->model.strip[ch - 1].db : AMFX1_DB_MIN; }
float amfx1_get_pan(const amfx1_client_t* c, int ch) { return amfx1_valid_ch(ch) ? c->model.strip[ch - 1].pan : 0; }
int amfx1_get_mute(const amfx1_client_t* c, int ch) { return amfx1_valid_ch(ch) ? c->model.strip[ch - 1].mute : 0; }
int amfx1_get_solo(const amfx1_client_t* c, int ch) { return amfx1_valid_ch(ch) ? c->model.strip[ch - 1].solo : 0; }
float amfx1_get_meter(const amfx1_client_t* c, int ch) { return amfx1_valid_ch(ch) ? amfx1_lin_to_db(c->model.strip[ch - 1].pk) : AMFX1_SILENCE_DB; }
float amfx1_get_master_meter(const amfx1_client_t* c) { return amfx1_lin_to_db(c->model.masterL > c->model.masterR ? c->model.masterL : c->model.masterR); }

int amfx1_loop(amfx1_client_t* c, int waitMs) {
  long long t = now_ms();
  if (!c->token[0] || t < c->heldUntil) return AMFX1_OK;
  if (c->useWs) {
    fd_set r; struct timeval tv; uint8_t buf[768]; int n;
    if (c->fd < 0) { if (t < c->nextRetry) return AMFX1_ERR_CONNECT; if (ws_open(c) != AMFX1_OK) { c->nextRetry = t + 2000; return AMFX1_ERR_CONNECT; } }
    FD_ZERO(&r); FD_SET(c->fd, &r); tv.tv_sec = waitMs / 1000; tv.tv_usec = (waitMs % 1000) * 1000;
    if (select(c->fd + 1, &r, NULL, NULL, &tv) <= 0) return AMFX1_OK;
    n = (int)recv(c->fd, buf, sizeof buf, 0);
    if (n <= 0) { close(c->fd); c->fd = -1; c->nextRetry = t + 1000; set_err(c, "connection lost: reconnecting"); return AMFX1_ERR_CONNECT; }
    { size_t off = 0;
      while (off < (size_t)n && c->fd >= 0) { int ev = 0; off += amfx1_ws_rx_feed(&c->wsrx, buf + off, (size_t)n - off, &c->model, &ev);
        if (ev == AMFX1_WS_MESSAGE) fire(c, amfx1_model_end(&c->model));
        else if (ev == AMFX1_WS_PING) { uint8_t f[140], m[4]; int k; rnd(m, 4); k = amfx1_ws_frame(f, sizeof f, 10, c->wsrx.ctl, c->wsrx.ctln, m); if (k > 0) send_all(c->fd, f, (size_t)k); }
        else if (ev == AMFX1_WS_CLOSE || ev == AMFX1_WS_ERROR) { close(c->fd); c->fd = -1; c->nextRetry = t + 1000; return AMFX1_ERR_PROTOCOL; } } }
    return AMFX1_OK;
  }
  if (t - c->lastState >= c->stateMs) {
    char path[48]; c->lastState = t; snprintf(path, sizeof path, "/api/state?since=%lu", c->model.version);
    if (http(c, "GET", path, NULL) == 200) fire(c, c->model.flags & (uint16_t)~AMFX1_F_SAME);
  }
  if (c->meterMs && t - c->lastMeter >= c->meterMs) { c->lastMeter = t; if (http(c, "GET", "/api/meters", NULL) == 200) fire(c, c->model.flags); }
  if (waitMs > 0) { struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = (long)(waitMs > 20 ? 20 : waitMs) * 1000000L; nanosleep(&ts, NULL); }
  return AMFX1_OK;
}
