/* am_link: see am_link.h. A WebSocket client (RFC 6455, text + binary, no extensions) for the virtual device protocol of bridge/virtual.js. */
#include "am_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET am_sock;
#define AM_BAD_SOCK INVALID_SOCKET
#define am_close_sock closesocket
typedef CRITICAL_SECTION am_mutex;
typedef HANDLE am_thread;
#define am_mutex_init(m) InitializeCriticalSection(m)
#define am_mutex_lock(m) EnterCriticalSection(m)
#define am_mutex_unlock(m) LeaveCriticalSection(m)
#define am_mutex_destroy(m) DeleteCriticalSection(m)
static void am_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int am_sock;
#define AM_BAD_SOCK (-1)
#define am_close_sock close
typedef pthread_mutex_t am_mutex;
typedef pthread_t am_thread;
#define am_mutex_init(m) pthread_mutex_init(m, NULL)
#define am_mutex_lock(m) pthread_mutex_lock(m)
#define am_mutex_unlock(m) pthread_mutex_unlock(m)
#define am_mutex_destroy(m) pthread_mutex_destroy(m)
static void am_sleep_ms(int ms) { struct timespec t; t.tv_sec = ms / 1000; t.tv_nsec = (long)(ms % 1000) * 1000000L; nanosleep(&t, NULL); }
#endif

#define AM_MAX_FRAME (1u << 20)

struct am_link {
  am_sock sock;
  am_mutex send_lock;
  am_thread thread;
  int thread_started;
  volatile int alive;
  volatile int ready;
  volatile int refused;
  int device_id;
  int channels;
  am_link_record_cb on_record;
  void *user;
  char error[160];
  uint32_t mask_state;
};

/* ───────────── small helpers ───────────── */
static void set_err(char *err, size_t n, const char *m) { if (err && n) { strncpy(err, m, n - 1); err[n - 1] = 0; } }

static int send_all(am_sock s, const uint8_t *p, size_t n) {
  while (n) {
#ifdef _WIN32
    int w = send(s, (const char *)p, (int)(n > 65536 ? 65536 : n), 0);
#else
    ssize_t w = send(s, p, n, MSG_NOSIGNAL);
#endif
    if (w <= 0) return -1;
    p += w; n -= (size_t)w;
  }
  return 0;
}

static int recv_all(am_sock s, uint8_t *p, size_t n) {
  while (n) {
#ifdef _WIN32
    int r = recv(s, (char *)p, (int)(n > 65536 ? 65536 : n), 0);
#else
    ssize_t r = recv(s, p, n, 0);
#endif
    if (r <= 0) return -1;
    p += r; n -= (size_t)r;
  }
  return 0;
}

static uint32_t next_mask(am_link *l) {   /* the mask only has to differ from frame to frame (RFC 6455 5.3); xorshift32 */
  uint32_t x = l->mask_state;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return l->mask_state = x ? x : 0x9e3779b9u;
}

static int ws_send(am_link *l, int opcode, const uint8_t *data, size_t n) {
  uint8_t head[14]; size_t h = 0;
  uint32_t m;
  uint8_t mask[4];
  uint8_t *buf; int rc;
  head[h++] = (uint8_t)(0x80 | opcode);
  if (n < 126) head[h++] = (uint8_t)(0x80 | n);
  else if (n < 65536) { head[h++] = 0x80 | 126; head[h++] = (uint8_t)(n >> 8); head[h++] = (uint8_t)n; }
  else { head[h++] = 0x80 | 127; for (int i = 7; i >= 0; i--) head[h++] = (uint8_t)(((uint64_t)n >> (8 * i)) & 0xff); }
  buf = (uint8_t *)malloc(h + 4 + n);
  if (!buf) return -1;
  am_mutex_lock(&l->send_lock);
  m = next_mask(l);
  mask[0] = (uint8_t)m; mask[1] = (uint8_t)(m >> 8); mask[2] = (uint8_t)(m >> 16); mask[3] = (uint8_t)(m >> 24);
  memcpy(buf, head, h); memcpy(buf + h, mask, 4);
  for (size_t i = 0; i < n; i++) buf[h + 4 + i] = data[i] ^ mask[i & 3];
  rc = send_all(l->sock, buf, h + 4 + n);
  am_mutex_unlock(&l->send_lock);
  free(buf);
  return rc;
}

/* reads one message (continuation frames joined); returns opcode (1 text, 2 binary, 8 close) or -1; *out is malloc'ed */
static int ws_read(am_link *l, uint8_t **out, size_t *outlen) {
  uint8_t *msg = NULL; size_t len = 0; int op0 = 0;
  for (;;) {
    uint8_t h[2], ext[8]; uint64_t n; int fin, op; uint8_t *p;
    if (recv_all(l->sock, h, 2) < 0) goto fail;
    fin = (h[0] & 0x80) != 0; op = h[0] & 0x0f;
    n = h[1] & 0x7f;
    if (h[1] & 0x80) goto fail;                                   /* a server must not mask */
    if (n == 126) { if (recv_all(l->sock, ext, 2) < 0) goto fail; n = ((uint64_t)ext[0] << 8) | ext[1]; }
    else if (n == 127) { if (recv_all(l->sock, ext, 8) < 0) goto fail; n = 0; for (int i = 0; i < 8; i++) n = (n << 8) | ext[i]; }
    if (n > AM_MAX_FRAME || len + n > AM_MAX_FRAME) goto fail;
    p = (uint8_t *)realloc(msg, len + (size_t)n + 1);
    if (!p) goto fail;
    msg = p;
    if (n && recv_all(l->sock, msg + len, (size_t)n) < 0) goto fail;
    if (op == 9) { ws_send(l, 10, msg + len, (size_t)n); continue; }   /* ping -> pong */
    if (op == 10) continue;
    if (op == 8) { free(msg); return 8; }
    if (op != 0) op0 = op;
    len += (size_t)n;
    if (fin) { msg[len] = 0; *out = msg; *outlen = len; return op0; }
  }
fail:
  free(msg);
  return -1;
}

/* ───────────── reader thread ───────────── */
static void handle_text(am_link *l, const char *t) {
  const char *id;
  if (strstr(t, "\"ready\"")) {
    id = strstr(t, "\"id\":");
    l->device_id = id ? atoi(id + 5) : 0;
    l->ready = 1;
  } else if (strstr(t, "\"error\"")) {
    const char *m = strstr(t, "\"message\":\"");
    size_t i = 0;
    if (m) { m += 11; while (*m && *m != '"' && i < sizeof l->error - 1) l->error[i++] = *m++; }
    l->error[i] = 0;
    l->refused = 1;
  }
}

#ifdef _WIN32
static DWORD WINAPI reader(LPVOID arg)
#else
static void *reader(void *arg)
#endif
{
  am_link *l = (am_link *)arg;
  for (;;) {
    uint8_t *msg = NULL; size_t len = 0;
    int op = ws_read(l, &msg, &len);
    if (op < 0 || op == 8) { free(msg); break; }
    if (op == 1) handle_text(l, (const char *)msg);
    else if (op == 2 && l->on_record && l->ready) {
      size_t frame = 2u * (size_t)l->channels;
      if (len % frame == 0) {
        /* msg comes from malloc: 8-byte aligned, so the samples can be read in place */
        l->on_record(l->user, (const int16_t *)msg, len / frame);
      }
    }
    free(msg);
  }
  l->alive = 0;
#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

/* ───────────── connect ───────────── */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static am_sock connect_port(int port) {
  struct sockaddr_in a; am_sock s;
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == AM_BAD_SOCK) return AM_BAD_SOCK;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* only this computer */
  if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { am_close_sock(s); return AM_BAD_SOCK; }
  { int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one); }
#ifdef _WIN32
  { DWORD ms = 2000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms); }
#else
  { struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); }
#endif
  return s;
}

static void clear_timeout(am_sock s) {   /* the handshake times out after 2 s; the reader thread then waits for audio as long as it takes */
#ifdef _WIN32
  DWORD ms = 0; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
#else
  struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 0; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
}

static int upgrade(am_link *l, int port) {
  uint8_t raw[16]; char key[32]; char req[320]; char resp[512]; size_t got = 0;
  for (int i = 0; i < 16; i++) raw[i] = (uint8_t)(next_mask(l) >> 8);
  for (int i = 0, o = 0; i < 15; i += 3, o += 4) {            /* base64 of 16 bytes */
    uint32_t v = ((uint32_t)raw[i] << 16) | ((uint32_t)raw[i + 1] << 8) | raw[i + 2];
    key[o] = B64[v >> 18]; key[o + 1] = B64[(v >> 12) & 63]; key[o + 2] = B64[(v >> 6) & 63]; key[o + 3] = B64[v & 63];
  }
  key[20] = B64[raw[15] >> 2]; key[21] = B64[(raw[15] & 3) << 4]; key[22] = '='; key[23] = '='; key[24] = 0;
  snprintf(req, sizeof req, "GET /ws/virtual HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n", port, key);
  if (send_all(l->sock, (const uint8_t *)req, strlen(req)) < 0) return -1;
  while (got < sizeof resp - 1) {                              /* read the response head byte by byte: the first WebSocket frame may follow it */
    uint8_t c;
    if (recv_all(l->sock, &c, 1) < 0) return -1;
    resp[got++] = (char)c;
    if (got >= 4 && memcmp(resp + got - 4, "\r\n\r\n", 4) == 0) break;
  }
  resp[got] = 0;
  return strstr(resp, " 101 ") ? 0 : -1;
}

am_link *am_link_open(const am_link_config *cfg, char *err, size_t errlen) {
  am_link *l; char hello[256]; const char *name; int channels, rate, first, last, port;
#ifdef _WIN32
  WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
  name = cfg && cfg->name && cfg->name[0] ? cfg->name : "Audio Mixer";
  channels = cfg && cfg->channels > 0 ? cfg->channels : 2;
  rate = cfg && cfg->rate > 0 ? cfg->rate : 48000;
  if (channels > 32) { set_err(err, errlen, "channels must be 1 to 32"); return NULL; }
  if (strpbrk(name, "\"\\\r\n")) { set_err(err, errlen, "the device name must not contain quotes, backslashes or line breaks"); return NULL; }
  first = 8765; last = 8774;
  if (cfg && cfg->port > 0) first = last = cfg->port;
  else if (getenv("AUDIO_MIXER_PORT") && atoi(getenv("AUDIO_MIXER_PORT")) > 0) first = last = atoi(getenv("AUDIO_MIXER_PORT"));
  l = (am_link *)calloc(1, sizeof *l);
  if (!l) { set_err(err, errlen, "out of memory"); return NULL; }
  l->channels = channels; l->on_record = cfg ? cfg->on_record : NULL; l->user = cfg ? cfg->user : NULL;
  l->mask_state = (uint32_t)time(NULL) ^ 0x5bd1e995u ^ (uint32_t)(size_t)l;
  am_mutex_init(&l->send_lock);
  l->sock = AM_BAD_SOCK;
  for (port = first; port <= last; port++) {
    l->sock = connect_port(port);
    if (l->sock == AM_BAD_SOCK) continue;
    if (upgrade(l, port) == 0) { clear_timeout(l->sock); break; }   /* a program on that port that is not the mixer fails the handshake */
    am_close_sock(l->sock); l->sock = AM_BAD_SOCK;
  }
  if (l->sock == AM_BAD_SOCK) { set_err(err, errlen, "the Audio Mixer program is not running (start it first, then open the device again)"); goto fail; }
  snprintf(hello, sizeof hello, "{\"type\":\"hello\",\"name\":\"%s\",\"channels\":%d,\"rate\":%d}", name, channels, rate);
  if (ws_send(l, 1, (const uint8_t *)hello, strlen(hello)) < 0) { set_err(err, errlen, "the mixer closed the connection"); goto fail2; }
  l->alive = 1;
#ifdef _WIN32
  l->thread = CreateThread(NULL, 0, reader, l, 0, NULL); l->thread_started = l->thread != NULL;
#else
  l->thread_started = pthread_create(&l->thread, NULL, reader, l) == 0;
#endif
  if (!l->thread_started) { set_err(err, errlen, "cannot start the link thread"); goto fail2; }
  for (int waited = 0; waited < 2000 && !l->ready && !l->refused && l->alive; waited += 5) am_sleep_ms(5);
  if (!l->ready) { set_err(err, errlen, l->refused && l->error[0] ? l->error : "the mixer did not accept the device"); am_link_close(l); return NULL; }
  return l;
fail2:
  am_close_sock(l->sock);
fail:
  am_mutex_destroy(&l->send_lock);
  free(l);
  return NULL;
}

int am_link_play(am_link *l, const int16_t *frames, size_t nframes) {
  if (!l || !l->alive || !l->ready) return -1;
  return ws_send(l, 2, (const uint8_t *)frames, nframes * 2u * (size_t)l->channels);
}

int am_link_alive(const am_link *l) { return l && l->alive && l->ready; }
int am_link_device_id(const am_link *l) { return l ? l->device_id : 0; }

void am_link_close(am_link *l) {
  if (!l) return;
  if (l->sock != AM_BAD_SOCK) {
    uint8_t none[1] = { 0 };
    if (l->alive) ws_send(l, 8, none, 0);
#ifdef _WIN32
    shutdown(l->sock, SD_BOTH);
#else
    shutdown(l->sock, SHUT_RDWR);
#endif
  }
  if (l->thread_started) {
#ifdef _WIN32
    WaitForSingleObject(l->thread, 3000); CloseHandle(l->thread);
#else
    pthread_join(l->thread, NULL);
#endif
  }
  if (l->sock != AM_BAD_SOCK) am_close_sock(l->sock);
  am_mutex_destroy(&l->send_lock);
  free(l);
}

void am_s16_from_s32(int16_t *dst, const int32_t *src, size_t n) { for (size_t i = 0; i < n; i++) dst[i] = (int16_t)(src[i] >> 16); }
void am_s32_from_s16(int32_t *dst, const int16_t *src, size_t n) { for (size_t i = 0; i < n; i++) dst[i] = (int32_t)((uint32_t)(uint16_t)src[i] << 16); }
