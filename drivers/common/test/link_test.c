/* Test program for am_link (used by bridge/test.js): plays a ramp to the mixer, records what the mixer sends back, prints the result. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../am_link.h"

static volatile long recorded_frames = 0;
static volatile int first_sample = -99999;
static void on_record(void *user, const int16_t *f, size_t n) {
  (void)user;
  if (recorded_frames == 0 && n) first_sample = f[0];
  recorded_frames += (long)n;
}

static void sleep_ms(int ms) { struct timespec t; t.tv_sec = ms / 1000; t.tv_nsec = (long)(ms % 1000) * 1000000L; nanosleep(&t, NULL); }

int main(int argc, char **argv) {
  char err[200] = "";
  am_link_config cfg; am_link *l; int16_t block[480 * 2]; int sent = 0;
  memset(&cfg, 0, sizeof cfg);
  cfg.name = argc > 1 ? argv[1] : "Link Test"; cfg.channels = 2; cfg.rate = 48000; cfg.on_record = on_record;
  l = am_link_open(&cfg, err, sizeof err);
  if (!l) { printf("open failed: %s\n", err); return 2; }
  printf("ready id=%d\n", am_link_device_id(l)); fflush(stdout);
  sleep_ms(600);                                            /* the test attaches its readers meanwhile */
  for (int b = 0; b < 25; b++) {
    for (int i = 0; i < 480; i++) { block[i * 2] = (int16_t)(1000 + i); block[i * 2 + 1] = (int16_t)(-1000 - i); }
    if (am_link_play(l, block, 480) == 0) sent++;
    sleep_ms(20);
  }
  printf("played=%d\n", sent); fflush(stdout);
  for (int w = 0; w < 150 && recorded_frames < 960; w++) sleep_ms(10);
  printf("recorded=%ld first=%d alive=%d\n", recorded_frames, first_sample, am_link_alive(l));
  am_link_close(l);
  printf("closed\n");
  return 0;
}
