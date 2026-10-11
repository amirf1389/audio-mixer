/* am_link: the connection between an operating-system audio driver and the Audio Mixer program on the same computer.
 *
 * A driver cannot do network work inside its real-time audio callback, and it must not depend on the mixer being there: this small library keeps the
 * connection on its own thread. It speaks the "virtual device" protocol of bridge/virtual.js (a WebSocket on 127.0.0.1, text for control, binary for audio):
 *
 *   am_link_play()      PCM that applications PLAYED to the virtual device  -> the mixer reads it as an input source
 *   on_record callback  PCM that the mixer sends to the virtual device      <- applications record it from the virtual device
 *
 * PCM is interleaved signed 16-bit little-endian, `channels` per frame, at the rate given to am_link_open (the mixer converts other rates).
 * C99, no dependencies: Winsock on Windows, POSIX sockets and pthreads elsewhere. Used by drivers/windows-asio, linux-alsa, macos-coreaudio, android-hal.
 */
#ifndef AM_LINK_H
#define AM_LINK_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct am_link am_link;

/* called on the link thread for every block the mixer sends to the virtual device: `frames` holds `nframes` * channels samples */
typedef void (*am_link_record_cb)(void *user, const int16_t *frames, size_t nframes);

typedef struct {
  const char *name;       /* device name shown in the mixer (default "Audio Mixer") */
  int channels;           /* 1..32 (default 2) */
  int rate;               /* sample rate of the PCM (default 48000) */
  int port;               /* the mixer's port; 0 = try 8765..8774 (or the AUDIO_MIXER_PORT environment variable) */
  am_link_record_cb on_record;
  void *user;
} am_link_config;

/* connects, says hello and waits (up to ~2 s) until the mixer answers "ready". Returns NULL and fills `err` when the mixer is not running or refuses. */
am_link *am_link_open(const am_link_config *cfg, char *err, size_t errlen);
/* sends PCM played by applications; returns 0 when sent, -1 when the link is down (the caller drops the audio and may try am_link_open again) */
int am_link_play(am_link *l, const int16_t *frames, size_t nframes);
int am_link_alive(const am_link *l);      /* 1 while the connection is up */
int am_link_device_id(const am_link *l);  /* the id the mixer gave the device (>= 9000) */
void am_link_close(am_link *l);

/* helpers shared by the drivers */
void am_s16_from_s32(int16_t *dst, const int32_t *src, size_t n);   /* top 16 bits, no dither (what a DAW gives an Int32LSB driver) */
void am_s32_from_s16(int32_t *dst, const int16_t *src, size_t n);

#ifdef __cplusplus
}
#endif
#endif
