/* The C SDK from plain C: version, devices JSON, an offline mixer processing a tone through a fader and the master meter, a null runtime. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amix.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main(void) {
  CHECK(amix_api_version() == AMIX_API_VERSION && strlen(amix_version()) >= 5);
  char* devs = amix_devices_json(); CHECK(devs && strstr(devs, "\"null\"")); amix_free(devs);
  char* bk = amix_backends_json(); CHECK(bk && bk[0] == '['); amix_free(bk);
  amix_mixer* m = amix_mixer_create(4, 48000.0); CHECK(m && amix_mixer_strips(m) == 4);
  float tone[256], outL[256], outR[256]; const float* in[4] = {tone, NULL, NULL, NULL};
  for (int i = 0; i < 256; ++i) tone[i] = 0.5f * (float)sin(2 * 3.14159265358979 * 1000.0 * i / 48000.0);
  amix_mixer_set_limiter(m, 0, -0.5, 100); amix_mixer_set_gain(m, 0, -6.0); amix_mixer_set_pan(m, 0, 0.0);
  float pk = 0; for (int k = 0; k < 20; ++k) { CHECK(amix_mixer_process(m, in, 256, outL, outR) == 256); for (int i = 0; i < 256; ++i) if (fabsf(outL[i]) > pk) pk = fabsf(outL[i]); }
  /* -6 dB fader and a centre pan (-3 dB) on a 0.5 peak: 0.5 * 0.501 * 0.707 = 0.177 */
  CHECK(fabsf(pk - 0.177f) < 0.01f);
  amix_levels lv; amix_mixer_levels(m, &lv); CHECK(lv.master[0].peak > 0.15 && lv.master[0].peak < 0.2 && lv.strips[0].peak > 0.2);
  amix_mixer_set_mute(m, 0, 1); for (int k = 0; k < 5; ++k) amix_mixer_process(m, in, 256, outL, outR); CHECK(fabsf(outL[100]) < 1e-6f);
  amix_mixer_destroy(m);
  amix_rt* rt = amix_rt_create(8); CHECK(rt);
  amix_config cfg; memset(&cfg, 0, sizeof cfg); cfg.backend = "null"; cfg.in_device = ""; cfg.out_device = ""; cfg.sample_rate = 48000; cfg.frames = 128; cfg.in_channels = 2; cfg.out_channels = 2;
  char err[256]; CHECK(amix_rt_open(rt, &cfg, err, sizeof err) == 0 && amix_rt_running(rt) && strcmp(amix_rt_backend(rt), "null") == 0 && fabs(amix_rt_sample_rate(rt) - 48000) < 1e-6);
  amix_rt_set_gain(rt, 0, -3); amix_rt_set_eq(rt, 0, 0, 1, 1000, 1.0, 3); amix_rt_set_master(rt, 0); amix_rt_levels(rt, &lv);
  amix_rt_stop(rt); CHECK(!amix_rt_running(rt)); amix_rt_destroy(rt);
  char* lic = amix_license_json(); CHECK(lic && strstr(lic, "\"state\"")); amix_free(lic);
  char* mid = amix_machine_id(); CHECK(mid && strlen(mid) > 4); amix_free(mid);
  printf("sdk_c_test: C API version %s, offline mixer, null runtime, licence JSON OK\n", amix_version());
  return 0;
}
