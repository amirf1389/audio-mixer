/* AmF X1 IoT SDK: the portable protocol core (plain C, also compiles as C++).
 *
 * Shared by the Arduino library (AmFX1.h), the ESP-IDF component (amfx1.h) and the host tests. No heap, no libc formatting of
 * floats (AVR printf has none), no third-party code. Everything works on byte streams fed in pieces of any size, so a board with
 * 2 KB of RAM can follow a state document of 30 KB.
 *
 *   builders   control commands ({"op":"gain",...}), the HTTP request head, the WebSocket handshake and client frames (masked)
 *   receivers  an HTTP response reader and a WebSocket frame reader (server frames)
 *   JSON       a streaming scanner (SAX style): one callback per scalar with its path, a few hundred bytes of state
 *   model      the console as the SDK sees it: faders, pan, mute, solo, names, meters, master; fed by the scanner
 *   scenes     snapshots of the model and the commands that bring the console back to one
 *
 * The protocol is the one of the AmF X1 Network service (amixos-daemon): token in the X-Amix-Token header, POST /api/control,
 * POST /api/pair (the console's 6-digit PIN for a token of its own), GET /api/state, GET /api/meters, WS /ws/meters (state frames,
 * meter frames 20 times a second, and control frames from the client). Channels in this API are 1-based (CH 1 is strip 0).
 */
#ifndef AMFX1_CORE_H
#define AMFX1_CORE_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AMFX1_MAX_STRIPS
#if defined(__AVR__)
#define AMFX1_MAX_STRIPS 8
#else
#define AMFX1_MAX_STRIPS 32
#endif
#endif
#ifndef AMFX1_MAX_SCENES
#if defined(__AVR__)
#define AMFX1_MAX_SCENES 2
#else
#define AMFX1_MAX_SCENES 8
#endif
#endif
#define AMFX1_DEFAULT_PORT 8765
#define AMFX1_TOKEN_HEADER "X-Amix-Token"
#define AMFX1_TOKEN_MAX 95      /* the service's tokens are 68 characters */
#define AMFX1_NAME_MAX 12       /* strip names kept (the console allows 24) */
#define AMFX1_DB_MIN (-120.0f)  /* the service clamps faders to -120 .. +24 dB; the SDK clamps to -120 .. +10 like the console */
#define AMFX1_DB_MAX (10.0f)
#define AMFX1_SILENCE_DB (-120.0f)

/* ---- builders: each returns the length written (a NUL always follows) or -1 when the buffer is too small ---- */
int amfx1_msg_fader(char* out, size_t n, int ch, float db, long seq);   /* seq < 0: none */
int amfx1_msg_pan(char* out, size_t n, int ch, float pan, long seq);    /* -1 left .. +1 right */
int amfx1_msg_mute(char* out, size_t n, int ch, int on, long seq);
int amfx1_msg_solo(char* out, size_t n, int ch, int on, long seq);
int amfx1_msg_master(char* out, size_t n, float db, long seq);
int amfx1_msg_master_mute(char* out, size_t n, int on, long seq);
int amfx1_msg_pair(char* out, size_t n, const char* pin, const char* deviceName);
int amfx1_valid_ch(int ch);   /* 1 .. AMFX1_MAX_STRIPS */
int amfx1_valid_pin(const char* pin);   /* exactly six digits */
float amfx1_clamp_db(float db);
float amfx1_clamp_pan(float pan);
float amfx1_lin_to_db(float lin);   /* a meter's linear peak (1.0 = full scale) in dBFS; silence is AMFX1_SILENCE_DB */

/* the head of an HTTP/1.1 request (Connection: close). token may be NULL or "", contentLength < 0: no body headers */
int amfx1_http_head(char* out, size_t n, const char* method, const char* host, uint16_t port, const char* path, const char* token, long contentLength);
/* the WebSocket upgrade for /ws/meters with the token in the header (never in the URL: URLs end up in logs); key16: 16 random bytes */
int amfx1_ws_handshake(char* out, size_t n, const char* host, uint16_t port, const char* token, const uint8_t key16[16]);
/* a client frame (always masked, RFC 6455 5.3). opcode 1 text, 8 close, 9 ping, 10 pong; payload up to 65535 bytes */
int amfx1_ws_frame(uint8_t* out, size_t n, int opcode, const uint8_t* payload, size_t len, const uint8_t mask[4]);
int amfx1_base64(char* out, size_t n, const uint8_t* in, size_t len);

/* ---- streaming JSON scanner ---- */
#define AMFX1_SAX_DEPTH 6
#define AMFX1_SAX_KEY 14
#define AMFX1_SAX_VAL 96
enum { AMFX1_J_STRING = 1, AMFX1_J_NUMBER, AMFX1_J_TRUE, AMFX1_J_FALSE, AMFX1_J_NULL };
typedef struct amfx1_sax amfx1_sax;
typedef void (*amfx1_sax_cb)(const amfx1_sax* s, int type, void* user);
struct amfx1_sax {
  uint8_t depth;                 /* containers open (all of them, also the ones deeper than AMFX1_SAX_DEPTH) */
  uint32_t arrays;               /* bit i: level i is an array */
  char key[AMFX1_SAX_DEPTH][AMFX1_SAX_KEY + 1];
  uint16_t index[AMFX1_SAX_DEPTH];
  char val[AMFX1_SAX_VAL + 1]; uint8_t vlen;
  uint8_t st, inKey, ucount, done, error;
  amfx1_sax_cb cb; void* user;
};
void amfx1_sax_init(amfx1_sax* s, amfx1_sax_cb cb, void* user);
void amfx1_sax_reset(amfx1_sax* s);   /* a new document, same callback */
void amfx1_sax_feed(amfx1_sax* s, const char* data, size_t len);
void amfx1_sax_end(amfx1_sax* s);     /* the document ended: a pending bare number is delivered */
int amfx1_sax_is_array(const amfx1_sax* s, int level);
int amfx1_sax_key_is(const amfx1_sax* s, int level, const char* k);

/* ---- the console model ---- */
typedef struct { float db, pan; uint8_t mute, solo; float pk, rms; char name[AMFX1_NAME_MAX + 1]; } amfx1_strip;
enum { AMFX1_F_STATE = 1, AMFX1_F_METERS = 2, AMFX1_F_ACK = 4, AMFX1_F_ERROR = 8, AMFX1_F_KICKED = 16, AMFX1_F_TOKEN = 32, AMFX1_F_SAME = 64 };
typedef struct {
  int strips;                    /* strips the service reported (at most AMFX1_MAX_STRIPS kept) */
  amfx1_strip strip[AMFX1_MAX_STRIPS];
  float masterDb; uint8_t masterMute; float masterL, masterR;
  unsigned long version;         /* the service's state counter ("v"): GET /api/state?since=v answers "same" while it holds */
  long ack;                      /* the last {"ack":seq} */
  uint8_t ok, haveState;
  uint16_t flags;                /* what the current document carried (AMFX1_F_*) */
  char error[64];
  char token[AMFX1_TOKEN_MAX + 1];
  amfx1_sax sax;
} amfx1_model;
void amfx1_model_init(amfx1_model* m);
void amfx1_model_begin(amfx1_model* m);              /* before a new document (HTTP body or WebSocket message) */
void amfx1_model_feed(amfx1_model* m, const char* data, size_t len);
uint16_t amfx1_model_end(amfx1_model* m);            /* after it: the AMFX1_F_* flags it carried */

/* ---- HTTP response reader ---- */
typedef struct { uint8_t st, headersOnly, done; int status; long contentLength, bodyRead; char line[72]; uint8_t ll; } amfx1_http_rx;
void amfx1_http_rx_init(amfx1_http_rx* r, int headersOnly);
/* consumes bytes; the body goes to m (may be NULL). Returns the bytes consumed (less than len only when done) */
size_t amfx1_http_rx_feed(amfx1_http_rx* r, const char* data, size_t len, amfx1_model* m);

/* ---- WebSocket frame reader (server frames are not masked) ---- */
enum { AMFX1_WS_NONE = 0, AMFX1_WS_MESSAGE, AMFX1_WS_PING, AMFX1_WS_CLOSE, AMFX1_WS_ERROR };
typedef struct { uint8_t st, op, msgOp, fin, hdr[8], hn, need, masked; uint32_t remain; uint8_t ctl[125]; uint8_t ctln; } amfx1_ws_rx;
void amfx1_ws_rx_init(amfx1_ws_rx* r);
/* consumes bytes up to the first event (returned in *event); text messages are fed to m between model_begin and model_end
 * (the caller calls amfx1_model_end on AMFX1_WS_MESSAGE). A ping's payload is in r->ctl / r->ctln for the pong. */
size_t amfx1_ws_rx_feed(amfx1_ws_rx* r, const uint8_t* data, size_t len, amfx1_model* m, int* event);

/* ---- scenes: kept in RAM; the bytes may be stored by the caller (NVS, EEPROM) as they are ---- */
typedef struct { float db, pan; uint8_t mute, solo; } amfx1_scene_strip;
typedef struct { uint8_t used, strips; float masterDb; amfx1_scene_strip s[AMFX1_MAX_STRIPS]; } amfx1_scene;
int amfx1_scene_capture(const amfx1_model* m, amfx1_scene* sc);   /* 0 without a state yet */
/* the commands that bring the console from m to sc, one at a time: *cursor starts at 0; returns the length, 0 when done */
int amfx1_scene_next(const amfx1_model* m, const amfx1_scene* sc, int* cursor, char* out, size_t n);

#ifdef __cplusplus
}
#endif
#endif
