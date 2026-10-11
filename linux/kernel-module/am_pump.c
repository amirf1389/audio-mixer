/* am-pump: connects /dev/audiomixer (the snd-audiomixer kernel module) to the running Audio Mixer.
 *   audio the sound card played (read from the device)  ->  am_link_play()  ->  the mixer: LIVE SOURCES → READ
 *   audio the mixer sends (on_record)                   ->  written to the device  ->  applications that record from the sound card
 * Stereo, signed 16-bit little-endian, 48 kHz (the card's fixed format). Reconnects to the mixer about once a second; while the mixer is not running the played
 * audio is dropped. Usage: am-pump [/dev/audiomixer]   (AUDIO_MIXER_PORT selects the mixer's port). Stops on SIGINT / SIGTERM. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../../drivers/common/am_link.h"

static volatile sig_atomic_t stop;
static void on_signal(int s) { (void)s; stop = 1; }

static int dev_fd = -1;

/* mixer -> sound card: runs on the link thread; a full device FIFO drops the block (the card must never wait for the mixer) */
static void on_record(void *user, const int16_t *frames, size_t nframes) {
  (void)user;
  const char *p = (const char *)frames;
  size_t left = nframes * 4;
  while (left > 0) {
    ssize_t w = write(dev_fd, p, left);
    if (w <= 0) { if (w < 0 && errno == EINTR) continue; return; }
    p += w; left -= (size_t)w;
  }
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "/dev/audiomixer";
  dev_fd = open(path, O_RDWR | O_NONBLOCK);
  if (dev_fd < 0) { fprintf(stderr, "am-pump: cannot open %s: %s (is snd-audiomixer loaded? sudo modprobe snd-audiomixer)\n", path, strerror(errno)); return 1; }
  signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
  am_link_config cfg; memset(&cfg, 0, sizeof cfg);
  cfg.name = "Audio Mixer"; cfg.channels = 2; cfg.rate = 48000; cfg.on_record = on_record;
  am_link *link = NULL;
  int warned = 0;
  int16_t buf[1920 * 2];   /* 40 ms of stereo */
  while (!stop) {
    if (!link || !am_link_alive(link)) {
      if (link) { am_link_close(link); link = NULL; }
      char err[160];
      link = am_link_open(&cfg, err, sizeof err);
      if (!link) { if (!warned) { fprintf(stderr, "am-pump: waiting for Audio Mixer (%s)\n", err); warned = 1; } }
      else { fprintf(stderr, "am-pump: connected, device id %d\n", am_link_device_id(link)); warned = 0; }
    }
    struct pollfd pf = { dev_fd, POLLIN, 0 };
    int pr = poll(&pf, 1, link ? 100 : 1000);
    if (pr > 0 && (pf.revents & POLLIN)) {
      ssize_t r = read(dev_fd, buf, sizeof buf);
      if (r > 0 && link) am_link_play(link, buf, (size_t)r / 4);
    }
  }
  if (link) am_link_close(link);
  close(dev_fd);
  return 0;
}
