/* Audio Mixer audio HAL for Android (AOSP "legacy" audio_hw_device API, loaded by the audio HAL service / the HIDL / AIDL wrapper as audio.audiomixer.default.so).
 *
 * It adds a virtual audio device "Audio Mixer" to the phone's audio system:
 *   output stream (apps / the system play to the device)  ->  the Audio Mixer app's engine reads it as an input source (LIVE SOURCES: READ)
 *   input stream  (apps record from the device)           <-  what the mixer sends to the device                      (LIVE SOURCES: WRITE)
 * The engine is the foreground service of the Audio Mixer app (android/src/.../MiniBridge.java, WS /ws/virtual on 127.0.0.1:8765..8774).
 *
 * STATUS: a template written against AOSP's hardware/libhardware/include/hardware/audio.h (HAL API 3.x). It is NOT compiled or run here: the AOSP tree is
 * needed to build it (see README.md), and the exact struct members differ between Android releases. Adapt it to your release and test on a device.
 * Streams are 16-bit PCM, 2 channels, 48 kHz; the audio flinger converts to and from whatever the apps use.
 */
#define LOG_TAG "audio_hw_audiomixer"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <system/audio.h>

#include "../../drivers/common/am_link.h"

#define RATE 48000
#define CHANNELS 2
#define PERIOD_FRAMES 480            /* 10 ms */
#define RING_FRAMES 16384

struct am_device {
  struct audio_hw_device hw;
  pthread_mutex_t lock;
  am_link *link;                     /* one link for the device, shared by both streams */
  int16_t ring[RING_FRAMES * CHANNELS];   /* what the mixer sent, for the input stream */
  size_t head, tail, count;
  int in_open;
  time_t last_try;
};

struct am_out { struct audio_stream_out stream; struct am_device *dev; struct timespec t0; uint64_t written; };
struct am_in { struct audio_stream_in stream; struct am_device *dev; struct timespec t0; uint64_t read; };

static void on_record(void *user, const int16_t *f, size_t n) {
  struct am_device *d = user;
  pthread_mutex_lock(&d->lock);
  for (size_t i = 0; i < n; i++) {
    if (d->count == RING_FRAMES) { d->tail = (d->tail + 1) % RING_FRAMES; d->count--; }
    memcpy(&d->ring[d->head * CHANNELS], &f[i * CHANNELS], CHANNELS * sizeof(int16_t));
    d->head = (d->head + 1) % RING_FRAMES; d->count++;
  }
  pthread_mutex_unlock(&d->lock);
}

static void connect_mixer(struct am_device *d) {   /* at most once a second; without the app the streams run silent */
  char err[160]; am_link_config c; time_t now = time(NULL);
  if (d->link && am_link_alive(d->link)) return;
  if (now == d->last_try) return;
  d->last_try = now;
  if (d->link) { am_link_close(d->link); d->link = NULL; }
  memset(&c, 0, sizeof c);
  c.name = "Audio Mixer"; c.channels = CHANNELS; c.rate = RATE; c.on_record = on_record; c.user = d;
  d->link = am_link_open(&c, err, sizeof err);
  if (!d->link) ALOGV("no link to the Audio Mixer app: %s", err);
}

static void sleep_until(const struct timespec *t0, uint64_t frames) {   /* the stream runs at the real sample rate */
  struct timespec target = *t0;
  uint64_t ns = frames * 1000000000ull / RATE;
  target.tv_sec += (time_t)(ns / 1000000000ull); target.tv_nsec += (long)(ns % 1000000000ull);
  if (target.tv_nsec >= 1000000000L) { target.tv_sec++; target.tv_nsec -= 1000000000L; }
  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL);
}

/* ───────────── output stream: apps -> mixer ───────────── */
static uint32_t out_get_sample_rate(const struct audio_stream *s) { (void)s; return RATE; }
static int out_set_sample_rate(struct audio_stream *s, uint32_t r) { (void)s; return r == RATE ? 0 : -EINVAL; }
static size_t out_get_buffer_size(const struct audio_stream *s) { (void)s; return PERIOD_FRAMES * CHANNELS * sizeof(int16_t); }
static audio_channel_mask_t out_get_channels(const struct audio_stream *s) { (void)s; return AUDIO_CHANNEL_OUT_STEREO; }
static audio_format_t out_get_format(const struct audio_stream *s) { (void)s; return AUDIO_FORMAT_PCM_16_BIT; }
static int out_set_format(struct audio_stream *s, audio_format_t f) { (void)s; return f == AUDIO_FORMAT_PCM_16_BIT ? 0 : -EINVAL; }
static int out_standby(struct audio_stream *s) { struct am_out *o = (struct am_out *)s; o->written = 0; clock_gettime(CLOCK_MONOTONIC, &o->t0); return 0; }
static int out_dump(const struct audio_stream *s, int fd) { (void)s; (void)fd; return 0; }
static audio_devices_t out_get_device(const struct audio_stream *s) { (void)s; return AUDIO_DEVICE_OUT_BUS; }
static int out_set_device(struct audio_stream *s, audio_devices_t d) { (void)s; (void)d; return 0; }
static int out_set_parameters(struct audio_stream *s, const char *kv) { (void)s; (void)kv; return 0; }
static char *out_get_parameters(const struct audio_stream *s, const char *keys) { (void)s; (void)keys; return strdup(""); }
static int out_add_effect(const struct audio_stream *s, effect_handle_t e) { (void)s; (void)e; return 0; }
static int out_remove_effect(const struct audio_stream *s, effect_handle_t e) { (void)s; (void)e; return 0; }
static uint32_t out_get_latency(const struct audio_stream_out *s) { (void)s; return 20; }
static int out_set_volume(struct audio_stream_out *s, float l, float r) { (void)s; (void)l; (void)r; return 0; }

static ssize_t out_write(struct audio_stream_out *s, const void *buffer, size_t bytes) {
  struct am_out *o = (struct am_out *)s; struct am_device *d = o->dev;
  size_t frames = bytes / (CHANNELS * sizeof(int16_t));
  pthread_mutex_lock(&d->lock);
  connect_mixer(d);
  if (d->link) am_link_play(d->link, buffer, frames);
  pthread_mutex_unlock(&d->lock);
  o->written += frames;
  sleep_until(&o->t0, o->written);                    /* blocks like real hardware would */
  return (ssize_t)bytes;
}
static int out_get_render_position(const struct audio_stream_out *s, uint32_t *dsp) { *dsp = (uint32_t)((const struct am_out *)s)->written; return 0; }
static int out_get_presentation_position(const struct audio_stream_out *s, uint64_t *frames, struct timespec *ts) {
  *frames = ((const struct am_out *)s)->written; clock_gettime(CLOCK_MONOTONIC, ts); return 0;
}
static int out_get_next_write_timestamp(const struct audio_stream_out *s, int64_t *ts) { (void)s; *ts = 0; return -EINVAL; }

/* ───────────── input stream: mixer -> apps ───────────── */
static uint32_t in_get_sample_rate(const struct audio_stream *s) { (void)s; return RATE; }
static int in_set_sample_rate(struct audio_stream *s, uint32_t r) { (void)s; return r == RATE ? 0 : -EINVAL; }
static size_t in_get_buffer_size(const struct audio_stream *s) { (void)s; return PERIOD_FRAMES * CHANNELS * sizeof(int16_t); }
static audio_channel_mask_t in_get_channels(const struct audio_stream *s) { (void)s; return AUDIO_CHANNEL_IN_STEREO; }
static audio_format_t in_get_format(const struct audio_stream *s) { (void)s; return AUDIO_FORMAT_PCM_16_BIT; }
static int in_set_format(struct audio_stream *s, audio_format_t f) { (void)s; return f == AUDIO_FORMAT_PCM_16_BIT ? 0 : -EINVAL; }
static int in_standby(struct audio_stream *s) { struct am_in *i = (struct am_in *)s; i->read = 0; clock_gettime(CLOCK_MONOTONIC, &i->t0); return 0; }
static int in_dump(const struct audio_stream *s, int fd) { (void)s; (void)fd; return 0; }
static audio_devices_t in_get_device(const struct audio_stream *s) { (void)s; return AUDIO_DEVICE_IN_BUS; }
static int in_set_device(struct audio_stream *s, audio_devices_t d) { (void)s; (void)d; return 0; }
static int in_set_parameters(struct audio_stream *s, const char *kv) { (void)s; (void)kv; return 0; }
static char *in_get_parameters(const struct audio_stream *s, const char *keys) { (void)s; (void)keys; return strdup(""); }
static int in_add_effect(const struct audio_stream *s, effect_handle_t e) { (void)s; (void)e; return 0; }
static int in_remove_effect(const struct audio_stream *s, effect_handle_t e) { (void)s; (void)e; return 0; }
static int in_set_gain(struct audio_stream_in *s, float g) { (void)s; (void)g; return 0; }
static uint32_t in_get_input_frames_lost(struct audio_stream_in *s) { (void)s; return 0; }

static ssize_t in_read(struct audio_stream_in *s, void *buffer, size_t bytes) {
  struct am_in *i = (struct am_in *)s; struct am_device *d = i->dev;
  int16_t *out = buffer; size_t frames = bytes / (CHANNELS * sizeof(int16_t));
  pthread_mutex_lock(&d->lock);
  connect_mixer(d);
  for (size_t f = 0; f < frames; f++) {
    if (d->count) { memcpy(&out[f * CHANNELS], &d->ring[d->tail * CHANNELS], CHANNELS * sizeof(int16_t)); d->tail = (d->tail + 1) % RING_FRAMES; d->count--; }
    else memset(&out[f * CHANNELS], 0, CHANNELS * sizeof(int16_t));     /* nothing from the mixer: silence */
  }
  pthread_mutex_unlock(&d->lock);
  i->read += frames;
  sleep_until(&i->t0, i->read);
  return (ssize_t)bytes;
}

/* ───────────── the device ───────────── */
static int dev_init_check(const struct audio_hw_device *dev) { (void)dev; return 0; }
static int dev_set_voice_volume(struct audio_hw_device *dev, float v) { (void)dev; (void)v; return -ENOSYS; }
static int dev_set_master_volume(struct audio_hw_device *dev, float v) { (void)dev; (void)v; return -ENOSYS; }
static int dev_set_mode(struct audio_hw_device *dev, audio_mode_t m) { (void)dev; (void)m; return 0; }
static int dev_set_mic_mute(struct audio_hw_device *dev, bool s) { (void)dev; (void)s; return -ENOSYS; }
static int dev_get_mic_mute(const struct audio_hw_device *dev, bool *s) { (void)dev; *s = false; return -ENOSYS; }
static int dev_set_parameters(struct audio_hw_device *dev, const char *kv) { (void)dev; (void)kv; return 0; }
static char *dev_get_parameters(const struct audio_hw_device *dev, const char *keys) { (void)dev; (void)keys; return strdup(""); }
static size_t dev_get_input_buffer_size(const struct audio_hw_device *dev, const struct audio_config *c) { (void)dev; (void)c; return PERIOD_FRAMES * CHANNELS * sizeof(int16_t); }
static int dev_dump(const struct audio_hw_device *dev, int fd) { (void)dev; (void)fd; return 0; }

static int dev_open_output_stream(struct audio_hw_device *dev, audio_io_handle_t handle, audio_devices_t devices, audio_output_flags_t flags,
                                  struct audio_config *config, struct audio_stream_out **stream_out, const char *address) {
  struct am_out *o; (void)handle; (void)devices; (void)flags; (void)address;
  config->sample_rate = RATE; config->channel_mask = AUDIO_CHANNEL_OUT_STEREO; config->format = AUDIO_FORMAT_PCM_16_BIT;   /* what this device does; the framework adapts */
  o = calloc(1, sizeof *o);
  if (!o) return -ENOMEM;
  o->dev = (struct am_device *)dev;
  o->stream.common.get_sample_rate = out_get_sample_rate; o->stream.common.set_sample_rate = out_set_sample_rate;
  o->stream.common.get_buffer_size = out_get_buffer_size; o->stream.common.get_channels = out_get_channels;
  o->stream.common.get_format = out_get_format; o->stream.common.set_format = out_set_format;
  o->stream.common.standby = out_standby; o->stream.common.dump = out_dump;
  o->stream.common.get_device = out_get_device; o->stream.common.set_device = out_set_device;
  o->stream.common.set_parameters = out_set_parameters; o->stream.common.get_parameters = out_get_parameters;
  o->stream.common.add_audio_effect = out_add_effect; o->stream.common.remove_audio_effect = out_remove_effect;
  o->stream.get_latency = out_get_latency; o->stream.set_volume = out_set_volume; o->stream.write = out_write;
  o->stream.get_render_position = out_get_render_position; o->stream.get_next_write_timestamp = out_get_next_write_timestamp;
  o->stream.get_presentation_position = out_get_presentation_position;
  clock_gettime(CLOCK_MONOTONIC, &o->t0);
  *stream_out = &o->stream;
  return 0;
}
static void dev_close_output_stream(struct audio_hw_device *dev, struct audio_stream_out *s) { (void)dev; free(s); }

static int dev_open_input_stream(struct audio_hw_device *dev, audio_io_handle_t handle, audio_devices_t devices, struct audio_config *config,
                                 struct audio_stream_in **stream_in, audio_input_flags_t flags, const char *address, audio_source_t source) {
  struct am_in *i; (void)handle; (void)devices; (void)flags; (void)address; (void)source;
  config->sample_rate = RATE; config->channel_mask = AUDIO_CHANNEL_IN_STEREO; config->format = AUDIO_FORMAT_PCM_16_BIT;
  i = calloc(1, sizeof *i);
  if (!i) return -ENOMEM;
  i->dev = (struct am_device *)dev;
  i->stream.common.get_sample_rate = in_get_sample_rate; i->stream.common.set_sample_rate = in_set_sample_rate;
  i->stream.common.get_buffer_size = in_get_buffer_size; i->stream.common.get_channels = in_get_channels;
  i->stream.common.get_format = in_get_format; i->stream.common.set_format = in_set_format;
  i->stream.common.standby = in_standby; i->stream.common.dump = in_dump;
  i->stream.common.get_device = in_get_device; i->stream.common.set_device = in_set_device;
  i->stream.common.set_parameters = in_set_parameters; i->stream.common.get_parameters = in_get_parameters;
  i->stream.common.add_audio_effect = in_add_effect; i->stream.common.remove_audio_effect = in_remove_effect;
  i->stream.set_gain = in_set_gain; i->stream.read = in_read; i->stream.get_input_frames_lost = in_get_input_frames_lost;
  clock_gettime(CLOCK_MONOTONIC, &i->t0);
  *stream_in = &i->stream;
  return 0;
}
static void dev_close_input_stream(struct audio_hw_device *dev, struct audio_stream_in *s) { (void)dev; free(s); }

static int dev_close(hw_device_t *device) {
  struct am_device *d = (struct am_device *)device;
  if (d->link) am_link_close(d->link);
  pthread_mutex_destroy(&d->lock);
  free(d);
  return 0;
}

static int dev_open(const hw_module_t *module, const char *name, hw_device_t **device) {
  struct am_device *d;
  if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) return -EINVAL;
  d = calloc(1, sizeof *d);
  if (!d) return -ENOMEM;
  pthread_mutex_init(&d->lock, NULL);
  d->hw.common.tag = HARDWARE_DEVICE_TAG;
  d->hw.common.version = AUDIO_DEVICE_API_VERSION_3_0;
  d->hw.common.module = (struct hw_module_t *)module;
  d->hw.common.close = dev_close;
  d->hw.init_check = dev_init_check; d->hw.set_voice_volume = dev_set_voice_volume; d->hw.set_master_volume = dev_set_master_volume;
  d->hw.set_mode = dev_set_mode; d->hw.set_mic_mute = dev_set_mic_mute; d->hw.get_mic_mute = dev_get_mic_mute;
  d->hw.set_parameters = dev_set_parameters; d->hw.get_parameters = dev_get_parameters; d->hw.get_input_buffer_size = dev_get_input_buffer_size;
  d->hw.open_output_stream = dev_open_output_stream; d->hw.close_output_stream = dev_close_output_stream;
  d->hw.open_input_stream = dev_open_input_stream; d->hw.close_input_stream = dev_close_input_stream;
  d->hw.dump = dev_dump;
  *device = &d->hw.common;
  return 0;
}

static struct hw_module_methods_t hal_module_methods = { .open = dev_open };

struct audio_module HAL_MODULE_INFO_SYM = {
  .common = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
    .hal_api_version = HARDWARE_HAL_API_VERSION,
    .id = AUDIO_HARDWARE_MODULE_ID,
    .name = "Audio Mixer audio HAL",
    .author = "Audio Mixer",
    .methods = &hal_module_methods,
  },
};
