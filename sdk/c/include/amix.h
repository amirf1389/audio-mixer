/* A mix OS engine: the C SDK (stable ABI). One header, one shared library (libamix.so / amix.dll / libamix.dylib).
 * Everything the console, the daemon, the Perl / Rust / Kotlin bindings and the WASM build need, with no C++ in the interface.
 *
 *   amix_mixer   an offline mixer: feed blocks, get the master and the meters (tests, bindings, servers, WASM; no sound card)
 *   amix_rt      the live runtime: opens a device (ASIO / WASAPI / Core Audio / ALSA / null) and runs the same mixer on it
 *
 * Strings returned as char* are malloc'd by the library: free them with amix_free(). Errors are 0 = ok, negative = failed (see the
 * message buffer where there is one). All functions are thread-safe to call from one control thread while the audio thread runs.
 */
#ifndef AMIX_H
#define AMIX_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(AMIX_BUILD_DLL)
#define AMIX_API __declspec(dllexport)
#elif defined(_WIN32) && !defined(AMIX_STATIC)
#define AMIX_API __declspec(dllimport)
#else
#define AMIX_API __attribute__((visibility("default")))
#endif

#define AMIX_API_VERSION 7   // 2: 16 buses (8 AUX + 8 FX); 4: 360 audio; 5: network audio (AES67) and MIDI; 6: high-resolution audio (amix_hires.h); 7: ASIO details, latency, block callback
#define AMIX_MAX_STRIPS 32
#define AMIX_MAX_BUSES 16
#define AMIX_MAX_INSERTS 4

typedef struct amix_meter { double peak, rms, hold; uint32_t clips; double comp_gr_db, gate_db, in_peak; } amix_meter;   /* linear 0..1+, dB for the reductions */
typedef struct amix_levels {
  amix_meter strips[AMIX_MAX_STRIPS];
  amix_meter master[2];
  amix_meter buses[AMIX_MAX_BUSES];
  double monitor_peak[2];
  double momentary_lufs, short_term_lufs, integrated_lufs, lra_lu, true_peak_dbtp, correlation;
  uint64_t blocks, frames, xruns;
} amix_levels;

/* filter types for amix_*_eq: 0 off, 1 peaking, 2 low shelf, 3 high shelf, 4 low pass, 5 high pass, 6 notch, 7 all-pass 1st, 8 all-pass 2nd */

AMIX_API const char* amix_version(void);          /* the engine version, static string */
AMIX_API int         amix_api_version(void);      /* AMIX_API_VERSION of the library you linked */
AMIX_API void        amix_free(void* p);

/* ---- devices ---- */
AMIX_API char* amix_backends_json(void);          /* ["alsa","null"] */
AMIX_API char* amix_devices_json(void);           /* [{"backend","id","name","inChannels","outChannels","defaultRate","defaultIn","defaultOut"}] */

/* ---- offline mixer ---- */
typedef struct amix_mixer amix_mixer;
AMIX_API amix_mixer* amix_mixer_create(int strips, double sample_rate);
AMIX_API void        amix_mixer_destroy(amix_mixer*);
AMIX_API int         amix_mixer_strips(const amix_mixer*);
AMIX_API void        amix_mixer_set_gain(amix_mixer*, int strip, double db);        /* <= -60 silence, +10 max */
AMIX_API void        amix_mixer_set_pan(amix_mixer*, int strip, double pan);        /* -1 .. +1 */
AMIX_API void        amix_mixer_set_mute(amix_mixer*, int strip, int on);
AMIX_API void        amix_mixer_set_solo(amix_mixer*, int strip, int on);
AMIX_API void        amix_mixer_set_master(amix_mixer*, double db);
AMIX_API void        amix_mixer_set_limiter(amix_mixer*, int on, double ceiling_db, double release_ms);
AMIX_API void        amix_mixer_set_send(amix_mixer*, int strip, int bus, double db, int pre);
AMIX_API void        amix_mixer_set_bus_level(amix_mixer*, int bus, double db);
AMIX_API void        amix_mixer_set_bus_return(amix_mixer*, int bus, double db);
AMIX_API void        amix_mixer_set_width(amix_mixer*, double width);               /* 0 mono .. 1 .. 2 wide */
/* in: strips pointers of `frames` floats (NULL = silence); out_l / out_r: `frames` floats. Returns frames processed. */
AMIX_API int         amix_mixer_process(amix_mixer*, const float* const* in, int frames, float* out_l, float* out_r);
/* INSERTS on the offline mixer: AMIX_MAX_INSERTS AmF X1 rack effects per strip (on the strip's signal) and on the master (strip -1, after
 * the mix). kind from amix_fx_catalog_json ("reverb", "delay", "comp" ...), "" or NULL empties the slot. Parameters are 0..1; "mix" (0 dry ..
 * 1 wet) and "bypass" (0 / 1) work on every effect. Safe to call while amix_mixer_process runs on the audio thread. */
AMIX_API char* amix_fx_catalog_json(void);                                                   /* [{"kind","name"}] */
AMIX_API int   amix_mixer_insert(amix_mixer*, int strip, int slot, const char* kind);       /* 0 = ok, -2 = unknown effect */
AMIX_API char* amix_mixer_insert_json(amix_mixer*, int strip, int slot);                    /* {"kind","name","bypass","mix","params":[{"id","title","value","default","text"}]} or null */
AMIX_API int   amix_mixer_insert_set(amix_mixer*, int strip, int slot, const char* id, double value);
/* SURROUND 5.0: outs = L, R, C, Ls, Rs; a strip's depth (+1 front .. -1 rear, default +1) with its pan places it in the field; buses and
   effects stay with the stereo process. Added in API version 3. */
AMIX_API void        amix_mixer_set_depth(amix_mixer*, int strip, double depth);
AMIX_API int         amix_mixer_process5(amix_mixer*, const float* const* in, int frames, float* const* outs5);
/* 360 AUDIO (v4): place a strip around the head for headphones in amix_mixer_process (binaural: time and level difference, head shadow).
   az degrees (0 front, 90 right, 180 behind), el -90..90, dist metres (1 = unity), spread 0..1. on = 0 returns the strip to its pan. */
AMIX_API void        amix_mixer_set_spatial(amix_mixer* m, int strip, int on, double az_deg, double el_deg, double dist_m, double spread);
AMIX_API void        amix_mixer_levels(const amix_mixer*, amix_levels* out);

/* ---- live runtime ---- */
typedef struct amix_rt amix_rt;
typedef struct amix_config {
  const char* backend;      /* "auto", "asio", "wasapi", "dsound" (DirectSound: Bluetooth, no lock), "coreaudio", "alsa", "null" */
  const char* in_device;    /* "" default, "-" none */
  const char* out_device;
  double sample_rate;       /* 0 = 48000 */
  int frames;               /* 0 = 256 */
  int in_channels, out_channels;   /* 0 = 2 */
  int exclusive;            /* WASAPI exclusive mode */
} amix_config;
AMIX_API amix_rt* amix_rt_create(int strips);
AMIX_API void     amix_rt_destroy(amix_rt*);
AMIX_API int      amix_rt_open(amix_rt*, const amix_config* cfg, char* err, size_t err_len);   /* opens and starts; 0 = ok */
AMIX_API void     amix_rt_stop(amix_rt*);
AMIX_API int      amix_rt_running(const amix_rt*);
AMIX_API const char* amix_rt_backend(const amix_rt*);      /* valid while the runtime lives */
AMIX_API double   amix_rt_sample_rate(const amix_rt*);
AMIX_API void     amix_rt_set_gain(amix_rt*, int strip, double db);
AMIX_API void     amix_rt_set_pan(amix_rt*, int strip, double pan);
AMIX_API void     amix_rt_set_mute(amix_rt*, int strip, int on);
AMIX_API void     amix_rt_set_solo(amix_rt*, int strip, int on);
AMIX_API void     amix_rt_set_trim(amix_rt*, int strip, double db);
AMIX_API void     amix_rt_set_master(amix_rt*, double db);
AMIX_API void     amix_rt_set_limiter(amix_rt*, int on, double ceiling_db, double release_ms);
AMIX_API void     amix_rt_set_eq(amix_rt*, int strip, int band, int type, double freq, double q, double gain_db);   /* 10 bands */
AMIX_API void     amix_rt_set_compressor(amix_rt*, int strip, int on, double threshold_db, double ratio, double knee_db, double attack_ms, double release_ms, double makeup_db);
AMIX_API void     amix_rt_set_gate(amix_rt*, int strip, int on, double threshold_db, double range_db, double attack_ms, double hold_ms, double release_ms);
AMIX_API void     amix_rt_set_send(amix_rt*, int strip, int bus, double db, int pre);
AMIX_API void     amix_rt_set_bus_level(amix_rt*, int bus, double db);
AMIX_API void     amix_rt_set_bus_mute(amix_rt*, int bus, int on);
AMIX_API void     amix_rt_set_bus_return(amix_rt*, int bus, double db);
AMIX_API void     amix_rt_set_width(amix_rt*, double width);
AMIX_API void     amix_rt_set_monitor(amix_rt*, int source, double db, int dim, int mono, int mute);   /* source 0 master, 1.. bus */
AMIX_API void     amix_rt_set_input_route(amix_rt*, int strip, int device_input);    /* -1 = silence */
AMIX_API void     amix_rt_set_output_route(amix_rt*, int device_output, int source); /* 0 off, 1 master L, 2 master R, 3 monitor L, 4 monitor R, 5.. bus k L/R */
AMIX_API void     amix_rt_clear_clips(amix_rt*);
AMIX_API void     amix_rt_levels(const amix_rt*, amix_levels* out);

/* ---- ASIO AND THE OPEN DEVICE (API 7). Open an ASIO interface with amix_rt_open and backend "asio", in_device / out_device = the driver
 * name from amix_asio_drivers_json. ASIO exists on Windows only (the Steinberg ASIO SDK, used under its licence); elsewhere the list is []. */
AMIX_API char* amix_asio_drivers_json(void);   /* [{"name","inChannels","outChannels","defaultRate","rates":[..],"wrapper":bool,"family","probeError"}]
                                                  wrapper: ASIO4ALL / FlexASIO and the like (not the device's own driver); probeError: why it
                                                  could not be read now (busy in another program, unplugged); the list loads each driver once */
AMIX_API void  amix_rt_channels(const amix_rt*, int* in_channels, int* out_channels);   /* the open device's channels (0 while closed) */
AMIX_API void  amix_rt_latency(const amix_rt*, long* in_frames, long* out_frames);      /* as the driver reports them, buffer included; 0 = not reported */
AMIX_API char* amix_rt_latency_json(const amix_rt*);   /* {"inMs","outMs","blockMs","totalMs","reported","level":"LOW LATENCY"|"OK"|"HIGH","verdict","advice":[..]} */
AMIX_API char* amix_rt_sample_format(const amix_rt*);  /* what the device carries, e.g. "in 24-bit integer · out 32-bit integer" */
AMIX_API char* amix_rt_output_names_json(const amix_rt*);   /* the driver's own output channel names (ASIO), [] when it does not tell */
/* BLOCK CALLBACK: called on the audio thread once per block with each strip's input (before the fader; NULL where a strip has none) and
 * the stereo master. Never block, allocate or lock in it. NULL detaches; detach before amix_rt_destroy. */
typedef void (*amix_block_fn)(void* user, const float* const* strips, int n_strips, const float* master_l, const float* master_r, int frames, double sample_rate);
AMIX_API void  amix_rt_set_block_callback(amix_rt*, amix_block_fn fn, void* user);

/* ---- network audio over IP: the open AES67 profile (RTP, L16 / L24 big-endian, 1 ms packets, SAP / SDP announcements) ----
 * SEND: the master as channels 1 / 2 of one stream (1 channel = the mono sum; more than 2 = the strips before their faders follow).
 * RECEIVE: one stream as up to AMIX_NET_INPUTS extra inputs; amix_rt_set_input_route(rt, strip, AMIX_NET_INPUT_BASE + k) makes a strip
 * listen to network channel k. address: a multicast group (239.x.x.x) or, to send, one receiver / to receive, "0.0.0.0" for packets
 * sent straight to this machine. iface: the local IPv4 address of the network card to use, NULL or "" for the system's choice.
 * No PTP clock is run (the receiver follows each stream's own clock) and nothing is encrypted: the audio is in clear on the network.
 * Other makers' own protocols are not implemented; amix_net_transports_json() says what this build can do. 0 = ok, negative = failed. */
#define AMIX_NET_INPUT_BASE 256
#define AMIX_NET_INPUTS 16
AMIX_API int   amix_rt_net_send_start(amix_rt*, const char* name, const char* address, int port, int channels, int bits, const char* iface, int announce, char* err, size_t err_len);
AMIX_API void  amix_rt_net_send_stop(amix_rt*);
AMIX_API int   amix_rt_net_receive_start(amix_rt*, const char* address, int port, int channels, int bits, int rate, int latency_ms, const char* iface, char* err, size_t err_len);
AMIX_API void  amix_rt_net_receive_stop(amix_rt*);
AMIX_API char* amix_rt_net_stats_json(amix_rt*);         /* {"send":{"running","packets","droppedFrames",...},"receive":{"running","packets","lost","late","underruns","fillMs","idleMs","from",...}} */
AMIX_API char* amix_net_discover_json(int wait_ms, const char* iface);   /* listens for announcements for wait_ms: [{"name","origin","address","port","channels","bits","rate","ptimeMs","refClock"}] */
AMIX_API char* amix_net_transports_json(void);           /* [{"id","name","available","note"}] */
AMIX_API char* amix_net_addresses_json(void);            /* ["192.168.1.20"]: the IPv4 addresses of this machine */

/* ---- MIDI: controllers move the mixer. Default map: CC 7 on MIDI channel n = fader of strip n (channel 16 = master), CC 10 = pan,
 * notes 0..31 = mute, 32..63 = solo. amix_midi_action() turns one message into what it means; the application applies it. ---- */
typedef struct amix_midi_action { int kind; int strip; double value; } amix_midi_action;   /* kind: 0 nothing, 1 fader (dB), 2 pan (-1..+1), 3 mute toggle, 4 solo toggle, 5 master (dB) */
AMIX_API void  amix_midi_action_default(int status, int data1, int data2, amix_midi_action* out);
AMIX_API char* amix_midi_inputs_json(void);              /* [{"id","name"}]: the MIDI inputs of this computer */

/* ---- licence (read-only) ---- */
AMIX_API void  amix_set_dirs(const char* data, const char* cache, const char* temp);   /* where the library keeps its files (Android: filesDir, cacheDir, cacheDir); NULL leaves one alone */
AMIX_API char* amix_paths_json(void);             /* {"os","program","data","cache","prefetch","temp","downloads"} */
AMIX_API char* amix_license_json(void);           /* {"state","plan","planName","channels","source","expires","machine","message"} */
AMIX_API char* amix_machine_id(void);

#ifdef __cplusplus
}
#endif
#endif
