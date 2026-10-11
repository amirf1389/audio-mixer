/* ALSA plugin "audiomixer": a virtual sound card for Audio Mixer on Linux (ALSA, and so PulseAudio / PipeWire / JACK through their ALSA bridges).
 *
 *   playback  (aplay -D audiomixer, any application that plays to the "Audio Mixer" device)  ->  the mixer's input source
 *   capture   (arecord -D audiomixer, an application that records from the "Audio Mixer" device)  <-  what the mixer sends to the device
 *
 * The plugin is a user-space ioplug: no kernel module, no root. It talks to the running Audio Mixer program through drivers/common/am_link.c.
 * ALSA wants a hardware clock; there is none, so a small thread paces the stream at the real sample rate (like the "null" plugin does).
 * Build: see linux/alsa-plugin/Makefile (needs libasound2-dev / alsa-lib-devel). Install: copy the .so to the ALSA plugin folder and add asound.conf.
 */
#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../drivers/common/am_link.h"

#define RING_FRAMES 16384    /* capture ring: what the mixer sent and the application has not read yet (about 340 ms at 48 kHz) */

typedef struct {
  snd_pcm_ioplug_t io;
  am_link *link;
  char name[64];
  int channels, rate, port;
  int pfd[2];                       /* wake-up pipe for poll() */
  pthread_t pace;
  volatile int running, pace_started;
  struct timespec t0;               /* time of start */
  snd_pcm_uframes_t base;           /* frames already counted when (re)started */
  int16_t *ring;                    /* capture */
  size_t rhead, rtail, rcount;
  pthread_mutex_t lock;
  time_t last_try;
} am_t;

static double since(const struct timespec *a) {
  struct timespec n; clock_gettime(CLOCK_MONOTONIC, &n);
  return (double)(n.tv_sec - a->tv_sec) + (double)(n.tv_nsec - a->tv_nsec) / 1e9;
}

/* ───────────── link to the mixer ───────────── */
static void on_record(void *user, const int16_t *f, size_t n) {
  am_t *am = (am_t *)user; size_t ch = (size_t)am->channels;
  pthread_mutex_lock(&am->lock);
  for (size_t i = 0; i < n; i++) {
    if (am->rcount == RING_FRAMES) { am->rtail = (am->rtail + 1) % RING_FRAMES; am->rcount--; }   /* too slow a reader: drop the oldest frame */
    memcpy(am->ring + am->rhead * ch, f + i * ch, ch * sizeof(int16_t));
    am->rhead = (am->rhead + 1) % RING_FRAMES; am->rcount++;
  }
  pthread_mutex_unlock(&am->lock);
}

static void connect_mixer(am_t *am) {   /* tries at most once a second; without the mixer the device still works and is silent */
  char err[160]; am_link_config c; time_t now = time(NULL);
  if (am->link && am_link_alive(am->link)) return;
  if (now == am->last_try) return;
  am->last_try = now;
  if (am->link) { am_link_close(am->link); am->link = NULL; }
  memset(&c, 0, sizeof c);
  c.name = am->name; c.channels = am->channels; c.rate = am->rate; c.port = am->port;
  c.on_record = am->io.stream == SND_PCM_STREAM_CAPTURE ? on_record : NULL; c.user = am;
  am->link = am_link_open(&c, err, sizeof err);
}

/* ───────────── pacing ───────────── */
static void *pace(void *arg) {
  am_t *am = (am_t *)arg;
  snd_pcm_uframes_t period = am->io.period_size ? am->io.period_size : 480;
  long ns = (long)((double)period / (double)am->rate * 1e9);
  struct timespec t; t.tv_sec = ns / 1000000000L; t.tv_nsec = ns % 1000000000L;
  while (am->running) {
    nanosleep(&t, NULL);
    if (write(am->pfd[1], "x", 1) < 0) { /* pipe full: the poller has not drained it yet */ }
  }
  return NULL;
}

static int am_start(snd_pcm_ioplug_t *io) {
  am_t *am = io->private_data;
  clock_gettime(CLOCK_MONOTONIC, &am->t0); am->base = 0;
  am->running = 1;
  am->pace_started = pthread_create(&am->pace, NULL, pace, am) == 0;
  return am->pace_started ? 0 : -EAGAIN;
}

static int am_stop(snd_pcm_ioplug_t *io) {
  am_t *am = io->private_data;
  am->running = 0;
  if (am->pace_started) { pthread_join(am->pace, NULL); am->pace_started = 0; }
  return 0;
}

static snd_pcm_sframes_t am_pointer(snd_pcm_ioplug_t *io) {
  am_t *am = io->private_data;
  if (!am->running) return 0;
  return (snd_pcm_sframes_t)(((snd_pcm_uframes_t)(since(&am->t0) * am->rate)) % io->buffer_size);
}

static int am_prepare(snd_pcm_ioplug_t *io) {
  am_t *am = io->private_data;
  char junk[64];
  while (read(am->pfd[0], junk, sizeof junk) > 0) { /* drain */ }
  pthread_mutex_lock(&am->lock); am->rhead = am->rtail = am->rcount = 0; pthread_mutex_unlock(&am->lock);
  connect_mixer(am);
  return 0;
}

static int am_poll_revents(snd_pcm_ioplug_t *io, struct pollfd *pfd, unsigned int nfds, unsigned short *revents) {
  am_t *am = io->private_data; char junk[64]; (void)nfds;
  while (read(am->pfd[0], junk, sizeof junk) > 0) { /* one wake-up per period is enough */ }
  *revents = pfd->revents;
  (void)am;
  return 0;
}

/* ───────────── audio ───────────── */
static snd_pcm_sframes_t am_transfer(snd_pcm_ioplug_t *io, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t offset, snd_pcm_uframes_t size) {
  am_t *am = io->private_data;
  int16_t *p = (int16_t *)((char *)areas[0].addr + (areas[0].first + areas[0].step * offset) / 8);
  size_t ch = (size_t)am->channels;
  if (io->stream == SND_PCM_STREAM_PLAYBACK) {
    connect_mixer(am);
    if (am->link) am_link_play(am->link, p, size);              /* without the mixer the audio is dropped, the clock goes on */
    return (snd_pcm_sframes_t)size;
  }
  pthread_mutex_lock(&am->lock);
  for (snd_pcm_uframes_t i = 0; i < size; i++) {
    if (am->rcount) { memcpy(p + i * ch, am->ring + am->rtail * ch, ch * sizeof(int16_t)); am->rtail = (am->rtail + 1) % RING_FRAMES; am->rcount--; }
    else memset(p + i * ch, 0, ch * sizeof(int16_t));            /* nothing from the mixer yet: silence */
  }
  pthread_mutex_unlock(&am->lock);
  connect_mixer(am);
  return (snd_pcm_sframes_t)size;
}

static int am_close(snd_pcm_ioplug_t *io) {
  am_t *am = io->private_data;
  am_stop(io);
  if (am->link) am_link_close(am->link);
  close(am->pfd[0]); close(am->pfd[1]);
  pthread_mutex_destroy(&am->lock);
  free(am->ring); free(am);
  return 0;
}

static const snd_pcm_ioplug_callback_t callbacks = {
  .start = am_start, .stop = am_stop, .pointer = am_pointer, .transfer = am_transfer,
  .close = am_close, .prepare = am_prepare, .poll_revents = am_poll_revents,
};

/* pcm.audiomixer { type audiomixer  name "Audio Mixer"  channels 2  rate 48000  port 0 } */
SND_PCM_PLUGIN_DEFINE_FUNC(audiomixer)
{
  snd_config_iterator_t i, next;
  am_t *am; long channels = 2, rate = 48000, port = 0; const char *dev = "Audio Mixer"; int err;
  static const unsigned int access_list[] = { SND_PCM_ACCESS_RW_INTERLEAVED };
  static const unsigned int format_list[] = { SND_PCM_FORMAT_S16_LE };

  snd_config_for_each(i, next, conf) {
    snd_config_t *n = snd_config_iterator_entry(i); const char *id;
    if (snd_config_get_id(n, &id) < 0) continue;
    if (!strcmp(id, "comment") || !strcmp(id, "type") || !strcmp(id, "hint")) continue;
    if (!strcmp(id, "name")) { if (snd_config_get_string(n, &dev) < 0) { SNDERR("name must be a string"); return -EINVAL; } continue; }
    if (!strcmp(id, "channels")) { if (snd_config_get_integer(n, &channels) < 0 || channels < 1 || channels > 32) { SNDERR("channels must be 1 to 32"); return -EINVAL; } continue; }
    if (!strcmp(id, "rate")) { if (snd_config_get_integer(n, &rate) < 0 || rate < 8000 || rate > 384000) { SNDERR("rate must be 8000 to 384000"); return -EINVAL; } continue; }
    if (!strcmp(id, "port")) { if (snd_config_get_integer(n, &port) < 0 || port < 0 || port > 65535) { SNDERR("port must be 0 to 65535"); return -EINVAL; } continue; }
    SNDERR("Unknown field %s", id);
    return -EINVAL;
  }
  if (strlen(dev) >= sizeof am->name || strpbrk(dev, "\"\\")) { SNDERR("name is too long or has quotes"); return -EINVAL; }

  am = calloc(1, sizeof *am);
  if (!am) return -ENOMEM;
  am->ring = calloc(RING_FRAMES, (size_t)channels * sizeof(int16_t));
  if (!am->ring) { free(am); return -ENOMEM; }
  strcpy(am->name, dev); am->channels = (int)channels; am->rate = (int)rate; am->port = (int)port;
  pthread_mutex_init(&am->lock, NULL);
  if (pipe(am->pfd) < 0) { free(am->ring); free(am); return -errno; }
  fcntl(am->pfd[0], F_SETFL, O_NONBLOCK); fcntl(am->pfd[1], F_SETFL, O_NONBLOCK);

  am->io.version = SND_PCM_IOPLUG_VERSION;
  am->io.name = "Audio Mixer virtual device";
  am->io.mmap_rw = 0;
  am->io.callback = &callbacks;
  am->io.private_data = am;
  am->io.poll_fd = am->pfd[0];
  am->io.poll_events = POLLIN;

  err = snd_pcm_ioplug_create(&am->io, name, stream, mode);
  if (err < 0) { close(am->pfd[0]); close(am->pfd[1]); free(am->ring); free(am); return err; }

  snd_pcm_ioplug_set_param_list(&am->io, SND_PCM_IOPLUG_HW_ACCESS, 1, access_list);
  snd_pcm_ioplug_set_param_list(&am->io, SND_PCM_IOPLUG_HW_FORMAT, 1, format_list);
  snd_pcm_ioplug_set_param_minmax(&am->io, SND_PCM_IOPLUG_HW_CHANNELS, (unsigned)channels, (unsigned)channels);
  snd_pcm_ioplug_set_param_minmax(&am->io, SND_PCM_IOPLUG_HW_RATE, (unsigned)rate, (unsigned)rate);
  snd_pcm_ioplug_set_param_minmax(&am->io, SND_PCM_IOPLUG_HW_PERIODS, 2, 16);
  snd_pcm_ioplug_set_param_minmax(&am->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 64 * 2 * (unsigned)channels, 8192 * 2 * (unsigned)channels);
  snd_pcm_ioplug_set_param_minmax(&am->io, SND_PCM_IOPLUG_HW_BUFFER_BYTES, 128 * 2 * (unsigned)channels, 65536 * 2 * (unsigned)channels);

  *pcmp = am->io.pcm;
  return 0;
}
SND_PCM_PLUGIN_SYMBOL(audiomixer);
