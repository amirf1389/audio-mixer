/* AmF X1 IoT SDK: the portable protocol core. See amfx1_core.h. */
#include "amfx1_core.h"
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- small writer */
typedef struct { char* p; size_t n, len; int ovf; } wbuf;
static void w_c(wbuf* b, char c) { if (b->len + 1 < b->n) b->p[b->len++] = c; else b->ovf = 1; }
static void w_s(wbuf* b, const char* s) { while (s && *s) w_c(b, *s++); }
static void w_u(wbuf* b, unsigned long v) { char t[12]; int i = 0; do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v && i < 11); while (i) w_c(b, t[--i]); }
static void w_l(wbuf* b, long v) { if (v < 0) { w_c(b, '-'); w_u(b, (unsigned long)(-(v + 1)) + 1); } else w_u(b, (unsigned long)v); }
static void w_f(wbuf* b, float f) {   /* two decimals, rounded; enough for dB and pan */
  long c; if (f != f) f = 0; c = (long)(f * 100.0f + (f < 0 ? -0.5f : 0.5f));
  if (c < 0) { w_c(b, '-'); c = -c; }
  w_u(b, (unsigned long)(c / 100)); if (c % 100) { w_c(b, '.'); w_c(b, (char)('0' + (c % 100) / 10)); if (c % 10) w_c(b, (char)('0' + c % 10)); }
}
static void w_js(wbuf* b, const char* s) {   /* a JSON string, printable ASCII only */
  w_c(b, '"'); for (; s && *s; ++s) { unsigned char c = (unsigned char)*s; if (c < 32 || c > 126) continue; if (c == '"' || c == '\\') w_c(b, '\\'); w_c(b, (char)c); } w_c(b, '"');
}
static int w_end(wbuf* b) { if (b->n) b->p[b->len < b->n ? b->len : b->n - 1] = 0; return b->ovf ? -1 : (int)b->len; }
static wbuf w_new(char* out, size_t n) { wbuf b; b.p = out; b.n = n; b.len = 0; b.ovf = n == 0; return b; }

int amfx1_valid_ch(int ch) { return ch >= 1 && ch <= AMFX1_MAX_STRIPS; }
float amfx1_clamp_db(float db) { if (db != db) return AMFX1_DB_MIN; return db < AMFX1_DB_MIN ? AMFX1_DB_MIN : db > AMFX1_DB_MAX ? AMFX1_DB_MAX : db; }
float amfx1_clamp_pan(float p) { if (p != p) return 0; return p < -1 ? -1 : p > 1 ? 1 : p; }
int amfx1_valid_pin(const char* pin) { int i; if (!pin) return 0; for (i = 0; i < 6; ++i) if (pin[i] < '0' || pin[i] > '9') return 0; return pin[6] == 0; }

/* log10 without libm: the board's libm may be absent from small builds; 0.01 dB is plenty for a meter */
float amfx1_lin_to_db(float v) {
  int e = 0; float y, z, ln;
  if (!(v > 1e-6f)) return AMFX1_SILENCE_DB;
  while (v >= 2.0f) { v *= 0.5f; ++e; } while (v < 1.0f) { v *= 2.0f; --e; }
  y = (v - 1.0f) / (v + 1.0f); z = y * y;   /* ln(v) = 2 atanh(y) */
  ln = 2.0f * y * (1.0f + z * (1.0f / 3 + z * (1.0f / 5 + z * (1.0f / 7 + z / 9))));
  ln += (float)e * 0.69314718f;
  return 8.6858896f * ln;   /* 20 / ln(10) */
}

static int msg_head(wbuf* b, const char* op) { w_s(b, "{\"op\":\""); w_s(b, op); w_c(b, '"'); return 0; }
static int msg_tail(wbuf* b, long seq) { if (seq >= 0) { w_s(b, ",\"seq\":"); w_l(b, seq); } w_c(b, '}'); return w_end(b); }
static int msg_strip(char* out, size_t n, const char* op, int ch) {
  (void)out; (void)n; (void)op; return amfx1_valid_ch(ch) ? ch - 1 : -1;
}
int amfx1_msg_fader(char* out, size_t n, int ch, float db, long seq) {
  wbuf b = w_new(out, n); int s = msg_strip(out, n, "gain", ch); if (s < 0) return w_end(&b), -1;
  msg_head(&b, "gain"); w_s(&b, ",\"strip\":"); w_l(&b, s); w_s(&b, ",\"db\":"); w_f(&b, amfx1_clamp_db(db)); return msg_tail(&b, seq);
}
int amfx1_msg_pan(char* out, size_t n, int ch, float pan, long seq) {
  wbuf b = w_new(out, n); int s = msg_strip(out, n, "pan", ch); if (s < 0) return w_end(&b), -1;
  msg_head(&b, "pan"); w_s(&b, ",\"strip\":"); w_l(&b, s); w_s(&b, ",\"pan\":"); w_f(&b, amfx1_clamp_pan(pan)); return msg_tail(&b, seq);
}
static int msg_onoff(char* out, size_t n, const char* op, int ch, int on, long seq) {
  wbuf b = w_new(out, n); int s = msg_strip(out, n, op, ch); if (s < 0) return w_end(&b), -1;
  msg_head(&b, op); w_s(&b, ",\"strip\":"); w_l(&b, s); w_s(&b, on ? ",\"on\":true" : ",\"on\":false"); return msg_tail(&b, seq);
}
int amfx1_msg_mute(char* out, size_t n, int ch, int on, long seq) { return msg_onoff(out, n, "mute", ch, on, seq); }
int amfx1_msg_solo(char* out, size_t n, int ch, int on, long seq) { return msg_onoff(out, n, "solo", ch, on, seq); }
int amfx1_msg_master(char* out, size_t n, float db, long seq) { wbuf b = w_new(out, n); msg_head(&b, "master"); w_s(&b, ",\"db\":"); w_f(&b, amfx1_clamp_db(db)); return msg_tail(&b, seq); }
int amfx1_msg_master_mute(char* out, size_t n, int on, long seq) { wbuf b = w_new(out, n); msg_head(&b, "master"); w_s(&b, on ? ",\"mute\":true" : ",\"mute\":false"); return msg_tail(&b, seq); }
int amfx1_msg_pair(char* out, size_t n, const char* pin, const char* name) {
  wbuf b = w_new(out, n); if (!amfx1_valid_pin(pin)) return w_end(&b), -1;
  w_s(&b, "{\"pin\":"); w_js(&b, pin); if (name && *name) { char t[41]; size_t i = 0; for (; name[i] && i < 40; ++i) t[i] = name[i]; t[i] = 0; w_s(&b, ",\"name\":"); w_js(&b, t); }
  w_c(&b, '}'); return w_end(&b);
}

static int token_ok(const char* t) { for (; t && *t; ++t) if ((unsigned char)*t <= 32 || (unsigned char)*t > 126) return 0; return 1; }   /* no header injection */
static int host_ok(const char* h) { if (!h || !*h) return 0; for (; *h; ++h) if ((unsigned char)*h <= 32 || (unsigned char)*h > 126 || *h == '/') return 0; return 1; }
int amfx1_http_head(char* out, size_t n, const char* method, const char* host, uint16_t port, const char* path, const char* token, long contentLength) {
  wbuf b = w_new(out, n); if (!host_ok(host) || !token_ok(token) || !path || path[0] != '/' || !token_ok(path)) return w_end(&b), -1;
  w_s(&b, method); w_c(&b, ' '); w_s(&b, path); w_s(&b, " HTTP/1.1\r\nHost: "); w_s(&b, host); w_c(&b, ':'); w_u(&b, port);
  w_s(&b, "\r\nUser-Agent: AmFX1-IoT\r\nAccept: application/json\r\nConnection: close\r\n");
  if (token && *token) { w_s(&b, AMFX1_TOKEN_HEADER ": "); w_s(&b, token); w_s(&b, "\r\n"); }
  if (contentLength >= 0) { w_s(&b, "Content-Type: application/json\r\nContent-Length: "); w_l(&b, contentLength); w_s(&b, "\r\n"); }
  w_s(&b, "\r\n"); return w_end(&b);
}
int amfx1_base64(char* out, size_t n, const uint8_t* in, size_t len) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  wbuf b = w_new(out, n); size_t i;
  for (i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16; if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8; if (i + 2 < len) v |= in[i + 2];
    w_c(&b, T[(v >> 18) & 63]); w_c(&b, T[(v >> 12) & 63]); w_c(&b, i + 1 < len ? T[(v >> 6) & 63] : '='); w_c(&b, i + 2 < len ? T[v & 63] : '=');
  }
  return w_end(&b);
}
int amfx1_ws_handshake(char* out, size_t n, const char* host, uint16_t port, const char* token, const uint8_t key16[16]) {
  char key[32]; wbuf b = w_new(out, n);
  if (!host_ok(host) || !token || !*token || !token_ok(token) || amfx1_base64(key, sizeof key, key16, 16) < 0) return w_end(&b), -1;
  w_s(&b, "GET /ws/meters HTTP/1.1\r\nHost: "); w_s(&b, host); w_c(&b, ':'); w_u(&b, port);
  w_s(&b, "\r\nUser-Agent: AmFX1-IoT\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "); w_s(&b, key);
  w_s(&b, "\r\nSec-WebSocket-Version: 13\r\n" AMFX1_TOKEN_HEADER ": "); w_s(&b, token); w_s(&b, "\r\n\r\n");
  return w_end(&b);
}
int amfx1_ws_frame(uint8_t* out, size_t n, int opcode, const uint8_t* payload, size_t len, const uint8_t mask[4]) {
  size_t h = len < 126 ? 6 : 8, i;
#if SIZE_MAX > 0xFFFFu
  if (len > 65535u) return -1;
#endif
  if (n < h + len) return -1;
  out[0] = (uint8_t)(0x80 | (opcode & 15));
  if (len < 126) out[1] = (uint8_t)(0x80 | len); else { out[1] = 0x80 | 126; out[2] = (uint8_t)(len >> 8); out[3] = (uint8_t)len; }
  memcpy(out + h - 4, mask, 4);
  for (i = 0; i < len; ++i) out[h + i] = (uint8_t)(payload[i] ^ mask[i & 3]);
  return (int)(h + len);
}

/* ---------------------------------------------------------------- JSON scanner */
enum { S_VALUE, S_AFTER, S_KEY, S_COLON, S_STR, S_ESC, S_U, S_LIT, S_DONE };
void amfx1_sax_reset(amfx1_sax* s) { amfx1_sax_cb cb = s->cb; void* u = s->user; memset(s, 0, sizeof *s); s->cb = cb; s->user = u; s->st = S_VALUE; }
void amfx1_sax_init(amfx1_sax* s, amfx1_sax_cb cb, void* user) { s->cb = cb; s->user = user; amfx1_sax_reset(s); }
int amfx1_sax_is_array(const amfx1_sax* s, int l) { return l >= 0 && l < 32 && ((s->arrays >> l) & 1u); }
int amfx1_sax_key_is(const amfx1_sax* s, int l, const char* k) { return l >= 0 && l < AMFX1_SAX_DEPTH && l < s->depth && !amfx1_sax_is_array(s, l) && strcmp(s->key[l], k) == 0; }
static void sax_emit(amfx1_sax* s, int type) { s->val[s->vlen] = 0; if (s->cb && s->depth <= AMFX1_SAX_DEPTH) s->cb(s, type, s->user); }
static void sax_after(amfx1_sax* s) { s->st = s->depth == 0 ? S_DONE : S_AFTER; if (s->depth == 0) s->done = 1; }
static void sax_push(amfx1_sax* s, int arr) {
  if (s->depth >= 31) { s->error = 1; return; }
  if (arr) s->arrays |= 1u << s->depth; else s->arrays &= ~(1u << s->depth);
  if (s->depth < AMFX1_SAX_DEPTH) { s->key[s->depth][0] = 0; s->index[s->depth] = 0; }
  s->depth++; s->st = arr ? S_VALUE : S_KEY;
}
static void sax_pop(amfx1_sax* s) { if (s->depth) s->depth--; sax_after(s); }
static void sax_lit(amfx1_sax* s) {
  int t = AMFX1_J_NUMBER; s->val[s->vlen] = 0;
  if (!strcmp(s->val, "true")) t = AMFX1_J_TRUE; else if (!strcmp(s->val, "false")) t = AMFX1_J_FALSE; else if (!strcmp(s->val, "null")) t = AMFX1_J_NULL;
  sax_emit(s, t); sax_after(s);
}
static int ws_char(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
void amfx1_sax_feed(amfx1_sax* s, const char* d, size_t len) {
  size_t i;
  for (i = 0; i < len && !s->error; ++i) {
    char c = d[i];
  again:
    switch (s->st) {
      case S_DONE: if (!ws_char(c)) s->error = 1; break;
      case S_VALUE:
        if (ws_char(c)) break;
        if (c == '{') sax_push(s, 0);
        else if (c == '[') sax_push(s, 1);
        else if (c == ']' && amfx1_sax_is_array(s, s->depth - 1)) sax_pop(s);
        else if (c == '"') { s->st = S_STR; s->inKey = 0; s->vlen = 0; }
        else if (c == '-' || (c >= '0' && c <= '9') || c == 't' || c == 'f' || c == 'n') { s->st = S_LIT; s->vlen = 0; s->val[s->vlen++] = c; }
        else s->error = 1;
        break;
      case S_LIT:
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '+' || c == '-') { if (s->vlen < AMFX1_SAX_VAL) s->val[s->vlen++] = c; break; }
        sax_lit(s); goto again;
      case S_STR:
        if (c == '\\') { s->st = S_ESC; break; }
        if (c == '"') {
          s->val[s->vlen] = 0;
          if (s->inKey) { if (s->depth - 1 < AMFX1_SAX_DEPTH) { size_t k = s->vlen > AMFX1_SAX_KEY ? AMFX1_SAX_KEY : s->vlen; memcpy(s->key[s->depth - 1], s->val, k); s->key[s->depth - 1][k] = 0; } s->st = S_COLON; }
          else { sax_emit(s, AMFX1_J_STRING); sax_after(s); }
          break;
        }
        if (s->vlen < AMFX1_SAX_VAL) s->val[s->vlen++] = c;
        break;
      case S_ESC: {
        char o = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == 'b' ? '\b' : c == 'f' ? '\f' : c;
        if (c == 'u') { o = '?'; s->ucount = 0; s->st = S_U; } else s->st = S_STR;
        if (s->vlen < AMFX1_SAX_VAL) s->val[s->vlen++] = o;
        break; }
      case S_U: if (++s->ucount >= 4) s->st = S_STR; break;
      case S_COLON: if (ws_char(c)) break; if (c == ':') s->st = S_VALUE; else s->error = 1; break;
      case S_KEY:
        if (ws_char(c)) break;
        if (c == '"') { s->st = S_STR; s->inKey = 1; s->vlen = 0; } else if (c == '}') sax_pop(s); else s->error = 1;
        break;
      case S_AFTER:
        if (ws_char(c)) break;
        if (c == ',') { if (amfx1_sax_is_array(s, s->depth - 1)) { if (s->depth - 1 < AMFX1_SAX_DEPTH) s->index[s->depth - 1]++; s->st = S_VALUE; } else s->st = S_KEY; }
        else if ((c == '}' && !amfx1_sax_is_array(s, s->depth - 1)) || (c == ']' && amfx1_sax_is_array(s, s->depth - 1))) sax_pop(s);
        else s->error = 1;
        break;
    }
  }
}
void amfx1_sax_end(amfx1_sax* s) { if (s->st == S_LIT && !s->error) sax_lit(s); }

/* ---------------------------------------------------------------- the model */
static void copy_str(char* dst, size_t cap, const char* src) { size_t i = 0; for (; src[i] && i + 1 < cap; ++i) dst[i] = src[i]; dst[i] = 0; }
static void model_cb(const amfx1_sax* s, int type, void* user) {
  amfx1_model* m = (amfx1_model*)user; int base = 0, d = s->depth; float num = 0; int on = type == AMFX1_J_TRUE;
  if (type == AMFX1_J_NUMBER) num = (float)strtod(s->val, 0);
  if (d >= 2 && amfx1_sax_key_is(s, 0, "state")) base = 1;   /* a WebSocket state frame: {"state":{...},"wsControl":true} */
  /* strips[i].k */
  if (d == base + 3 && amfx1_sax_key_is(s, base, "strips") && amfx1_sax_is_array(s, base + 1) && !amfx1_sax_is_array(s, base + 2)) {
    int i = s->index[base + 1]; const char* k = s->key[base + 2]; amfx1_strip* st;
    if (i >= AMFX1_MAX_STRIPS) return;
    st = &m->strip[i]; if (i + 1 > m->strips) m->strips = i + 1;
    if (!strcmp(k, "db")) { st->db = num; m->flags |= AMFX1_F_STATE; }
    else if (!strcmp(k, "pan")) { st->pan = num; m->flags |= AMFX1_F_STATE; }
    else if (!strcmp(k, "mute")) { st->mute = (uint8_t)on; m->flags |= AMFX1_F_STATE; }
    else if (!strcmp(k, "solo")) { st->solo = (uint8_t)on; m->flags |= AMFX1_F_STATE; }
    else if (!strcmp(k, "name") && type == AMFX1_J_STRING) { copy_str(st->name, sizeof st->name, s->val); m->flags |= AMFX1_F_STATE; }
    else if (!strcmp(k, "pk")) { st->pk = num; m->flags |= AMFX1_F_METERS; }
    else if (!strcmp(k, "rms")) { st->rms = num; m->flags |= AMFX1_F_METERS; }
    return;
  }
  if (d == base + 2 && amfx1_sax_key_is(s, base, "master")) {
    const char* k = s->key[base + 1];
    if (!strcmp(k, "db")) { m->masterDb = num; m->flags |= AMFX1_F_STATE; }
    else if (!strcmp(k, "mute")) { m->masterMute = (uint8_t)on; m->flags |= AMFX1_F_STATE; }
    else if (!strcmp(k, "l")) { m->masterL = num; m->flags |= AMFX1_F_METERS; }
    else if (!strcmp(k, "r")) { m->masterR = num; m->flags |= AMFX1_F_METERS; }
    return;
  }
  if (d == base + 1 && amfx1_sax_key_is(s, base, "v") && type == AMFX1_J_NUMBER) { m->version = (unsigned long)strtod(s->val, 0); return; }
  if (d != 1) return;
  if (amfx1_sax_key_is(s, 0, "ack")) { m->ack = (long)num; m->flags |= AMFX1_F_ACK; }
  else if ((amfx1_sax_key_is(s, 0, "controlError") || amfx1_sax_key_is(s, 0, "error")) && type == AMFX1_J_STRING) { copy_str(m->error, sizeof m->error, s->val); m->flags |= AMFX1_F_ERROR; }
  else if (amfx1_sax_key_is(s, 0, "kicked") && on) m->flags |= AMFX1_F_KICKED;
  else if (amfx1_sax_key_is(s, 0, "token") && type == AMFX1_J_STRING && s->vlen <= AMFX1_TOKEN_MAX) { copy_str(m->token, sizeof m->token, s->val); m->flags |= AMFX1_F_TOKEN; }
  else if (amfx1_sax_key_is(s, 0, "ok")) m->ok = (uint8_t)on;
  else if (amfx1_sax_key_is(s, 0, "same") && on) m->flags |= AMFX1_F_SAME;
}
void amfx1_model_init(amfx1_model* m) { memset(m, 0, sizeof *m); m->masterDb = 0; amfx1_sax_init(&m->sax, model_cb, m); }
void amfx1_model_begin(amfx1_model* m) { m->flags = 0; m->ok = 0; amfx1_sax_reset(&m->sax); }
void amfx1_model_feed(amfx1_model* m, const char* d, size_t len) { amfx1_sax_feed(&m->sax, d, len); }
uint16_t amfx1_model_end(amfx1_model* m) {
  amfx1_sax_end(&m->sax);
  if ((m->flags & AMFX1_F_STATE) && !m->sax.error) m->haveState = 1;
  if (m->sax.error && !(m->flags & AMFX1_F_ERROR)) { copy_str(m->error, sizeof m->error, "malformed JSON from the console"); m->flags |= AMFX1_F_ERROR; }
  return m->flags;
}

/* ---------------------------------------------------------------- HTTP response reader */
enum { H_STATUS, H_HEADER, H_BODY, H_DONE };
void amfx1_http_rx_init(amfx1_http_rx* r, int headersOnly) { memset(r, 0, sizeof *r); r->headersOnly = (uint8_t)headersOnly; r->contentLength = -1; }
static int lower_prefix(const char* line, const char* p) { for (; *p; ++p, ++line) { char c = *line; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != *p) return 0; } return 1; }
size_t amfx1_http_rx_feed(amfx1_http_rx* r, const char* d, size_t len, amfx1_model* m) {
  size_t i = 0;
  while (i < len && !r->done) {
    if (r->st == H_BODY) {
      size_t take = len - i;
      if (r->contentLength >= 0 && (long)take > r->contentLength - r->bodyRead) take = (size_t)(r->contentLength - r->bodyRead);
      if (m) amfx1_model_feed(m, d + i, take);
      r->bodyRead += (long)take; i += take;
      if (r->contentLength >= 0 && r->bodyRead >= r->contentLength) { r->done = 1; r->st = H_DONE; }
      continue;
    }
    {
      char c = d[i++];
      if (c == '\r') continue;
      if (c != '\n') { if (r->ll < sizeof r->line - 1) r->line[r->ll++] = c; continue; }
      r->line[r->ll] = 0;
      if (r->st == H_STATUS) {   /* HTTP/1.1 200 OK */
        const char* sp = strchr(r->line, ' '); r->status = sp ? atoi(sp + 1) : 0; r->st = H_HEADER;
        if (!lower_prefix(r->line, "http/1.")) { r->status = 0; r->done = 1; }
      } else if (r->ll == 0) {
        if (r->headersOnly) { r->done = 1; r->st = H_DONE; }
        else { r->st = H_BODY; if (m) amfx1_model_begin(m); if (r->contentLength == 0) { r->done = 1; r->st = H_DONE; } }
      } else if (lower_prefix(r->line, "content-length:")) { r->contentLength = atol(r->line + 15); if (r->contentLength < 0) r->contentLength = 0; }
      r->ll = 0;
    }
  }
  return i;
}

/* ---------------------------------------------------------------- WebSocket frame reader */
enum { W_B0, W_B1, W_EXT, W_MASK, W_PAY };
void amfx1_ws_rx_init(amfx1_ws_rx* r) { memset(r, 0, sizeof *r); }
static int ws_payload_done(amfx1_ws_rx* r, amfx1_model* m) {
  r->st = W_B0;
  if (r->op >= 8) return r->op == 9 ? AMFX1_WS_PING : r->op == 8 ? AMFX1_WS_CLOSE : AMFX1_WS_NONE;
  if (r->fin && r->msgOp == 1) { (void)m; return AMFX1_WS_MESSAGE; }
  return AMFX1_WS_NONE;
}
static void ws_header_done(amfx1_ws_rx* r, amfx1_model* m) {
  if (r->op == 1) { r->msgOp = 1; if (m) amfx1_model_begin(m); }
  else if (r->op == 2) r->msgOp = 2;   /* binary: skipped */
  if (r->op >= 8) r->ctln = 0;
  r->st = W_PAY;
}
size_t amfx1_ws_rx_feed(amfx1_ws_rx* r, const uint8_t* d, size_t len, amfx1_model* m, int* event) {
  size_t i = 0; *event = AMFX1_WS_NONE;
  while (i < len) {
    uint8_t c;
    if (r->st == W_PAY) {
      size_t take = len - i; if (take > r->remain) take = r->remain;
      if (r->op >= 8) { size_t k; for (k = 0; k < take; ++k) if (r->ctln < sizeof r->ctl) r->ctl[r->ctln++] = d[i + k]; }
      else if (r->msgOp == 1 && m) amfx1_model_feed(m, (const char*)d + i, take);
      i += take; r->remain -= (uint32_t)take;
      if (r->remain == 0) { *event = ws_payload_done(r, m); if (*event) return i; }
      continue;
    }
    c = d[i++];
    switch (r->st) {
      case W_B0: r->fin = (uint8_t)(c >> 7); r->op = (uint8_t)(c & 15); r->st = W_B1;
        if (r->op != 0 && r->op != 1 && r->op != 2 && r->op != 8 && r->op != 9 && r->op != 10) { *event = AMFX1_WS_ERROR; return i; }
        break;
      case W_B1: r->masked = (uint8_t)(c >> 7); r->remain = c & 127u; r->hn = 0;
        if (r->masked) { *event = AMFX1_WS_ERROR; return i; }   /* a server never masks (RFC 6455 5.1) */
        if (r->remain == 126) { r->need = 2; r->remain = 0; r->st = W_EXT; }
        else if (r->remain == 127) { r->need = 8; r->remain = 0; r->st = W_EXT; }
        else { ws_header_done(r, m); if (r->remain == 0) { *event = ws_payload_done(r, m); if (*event) return i; } }
        break;
      case W_EXT:
        if (r->need == 8 && r->hn < 4 && c) { *event = AMFX1_WS_ERROR; return i; }   /* more than 4 GB: refused */
        r->remain = (r->remain << 8) | c; if (++r->hn == r->need) { ws_header_done(r, m); if (r->remain == 0) { *event = ws_payload_done(r, m); if (*event) return i; } }
        break;
      default: break;
    }
  }
  return i;
}

/* ---------------------------------------------------------------- scenes */
int amfx1_scene_capture(const amfx1_model* m, amfx1_scene* sc) {
  int i; if (!m->haveState) return 0;
  memset(sc, 0, sizeof *sc); sc->used = 1; sc->strips = (uint8_t)(m->strips < AMFX1_MAX_STRIPS ? m->strips : AMFX1_MAX_STRIPS); sc->masterDb = m->masterDb;
  for (i = 0; i < sc->strips; ++i) { sc->s[i].db = m->strip[i].db; sc->s[i].pan = m->strip[i].pan; sc->s[i].mute = m->strip[i].mute; sc->s[i].solo = m->strip[i].solo; }
  return 1;
}
static float fabsf_(float v) { return v < 0 ? -v : v; }
int amfx1_scene_next(const amfx1_model* m, const amfx1_scene* sc, int* cursor, char* out, size_t n) {
  /* cursor = strip * 4 + field; the master last. Only what differs from the console is sent. */
  if (!sc->used) return 0;
  while (*cursor < sc->strips * 4 + 1) {
    int c = (*cursor)++, i = c / 4, f = c % 4;
    if (i >= sc->strips) { if (!m->haveState || fabsf_(m->masterDb - sc->masterDb) > 0.01f) return amfx1_msg_master(out, n, sc->masterDb, -1); continue; }
    {
      const amfx1_strip* now = &m->strip[i]; const amfx1_scene_strip* want = &sc->s[i]; int known = m->haveState && i < m->strips;
      if (f == 0 && (!known || fabsf_(now->db - want->db) > 0.01f)) return amfx1_msg_fader(out, n, i + 1, want->db, -1);
      if (f == 1 && (!known || fabsf_(now->pan - want->pan) > 0.005f)) return amfx1_msg_pan(out, n, i + 1, want->pan, -1);
      if (f == 2 && (!known || now->mute != want->mute)) return amfx1_msg_mute(out, n, i + 1, want->mute, -1);
      if (f == 3 && (!known || now->solo != want->solo)) return amfx1_msg_solo(out, n, i + 1, want->solo, -1);
    }
  }
  return 0;
}
