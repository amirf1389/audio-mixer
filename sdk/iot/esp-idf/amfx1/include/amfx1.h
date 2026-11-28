/* AmF X1 IoT SDK for ESP-IDF (and any POSIX system): a C client for the AmF X1 Network service.
 *
 * BSD sockets (lwIP on ESP-IDF), no other component needed. One connection: the WebSocket /ws/meters carries the console's state and
 * meters to the device and the device's moves to the console; AMFX1_TRANSPORT_HTTP polls GET /api/state and /api/meters instead and
 * sends each move as POST /api/control. Channels are 1-based (CH 1 = the console's first strip). Not thread-safe: call every
 * function from one task. LAN only and unencrypted, like the service itself: see docs/iot-sdk.md.
 */
#ifndef AMFX1_H
#define AMFX1_H
#include "amfx1_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct amfx1_client amfx1_client_t;
typedef enum { AMFX1_TRANSPORT_AUTO = 0, AMFX1_TRANSPORT_WEBSOCKET, AMFX1_TRANSPORT_HTTP } amfx1_transport_t;
typedef void (*amfx1_event_cb)(amfx1_client_t* c, void* user);

enum { AMFX1_OK = 0, AMFX1_ERR_ARG = -1, AMFX1_ERR_CONNECT = -2, AMFX1_ERR_AUTH = -3, AMFX1_ERR_PROTOCOL = -4, AMFX1_ERR_REFUSED = -5, AMFX1_ERR_NOMEM = -6 };

amfx1_client_t* amfx1_create(const char* host, uint16_t port);   /* host: the console's IPv4 address (a name is resolved once) */
void amfx1_destroy(amfx1_client_t* c);
void amfx1_set_transport(amfx1_client_t* c, amfx1_transport_t t);
void amfx1_set_timeout(amfx1_client_t* c, int ms);              /* connect and HTTP answers, default 3000 */
void amfx1_set_poll(amfx1_client_t* c, int stateMs, int meterMs); /* HTTP transport only; meterMs 0: no meters */

/* PAIRING: trade the PIN the console shows (it changes every minute) for a control token of this device's own. Store the token
 * (amfx1_token) in NVS and pass it to amfx1_begin next time; it belongs to this device's address and lasts 30 days. */
int amfx1_pair(amfx1_client_t* c, const char* pin, const char* deviceName);
const char* amfx1_token(const amfx1_client_t* c);
int amfx1_begin(amfx1_client_t* c, const char* token);         /* connects; AMFX1_OK or an AMFX1_ERR_* */
int amfx1_connected(const amfx1_client_t* c);
int amfx1_using_websocket(const amfx1_client_t* c);
void amfx1_stop(amfx1_client_t* c);
/* the pump: reads what came in (up to waitMs for something), reconnects after a drop, polls in HTTP mode, fires the callbacks */
int amfx1_loop(amfx1_client_t* c, int waitMs);

int amfx1_set_fader(amfx1_client_t* c, int ch, float db);
int amfx1_set_mute(amfx1_client_t* c, int ch, int on);
int amfx1_set_solo(amfx1_client_t* c, int ch, int on);
int amfx1_set_pan(amfx1_client_t* c, int ch, float pan);
int amfx1_set_master(amfx1_client_t* c, float db);
int amfx1_set_master_mute(amfx1_client_t* c, int on);
int amfx1_send_raw(amfx1_client_t* c, const char* json);      /* any command of POST /api/control, as JSON */
int amfx1_save_scene(amfx1_client_t* c, int n);               /* 1 .. AMFX1_MAX_SCENES, from the last state received */
int amfx1_recall_scene(amfx1_client_t* c, int n);
amfx1_scene* amfx1_scene_data(amfx1_client_t* c, int n);      /* to store in NVS and put back */

const amfx1_model* amfx1_model_of(const amfx1_client_t* c);
float amfx1_get_fader(const amfx1_client_t* c, int ch);
int amfx1_get_mute(const amfx1_client_t* c, int ch);
int amfx1_get_solo(const amfx1_client_t* c, int ch);
float amfx1_get_pan(const amfx1_client_t* c, int ch);
float amfx1_get_meter(const amfx1_client_t* c, int ch);        /* peak, dBFS */
float amfx1_get_master_meter(const amfx1_client_t* c);         /* the louder side, dBFS */
const char* amfx1_last_error(const amfx1_client_t* c);
void amfx1_on_state(amfx1_client_t* c, amfx1_event_cb cb, void* user);
void amfx1_on_meter(amfx1_client_t* c, amfx1_event_cb cb, void* user);

#ifdef __cplusplus
}
#endif
#endif
