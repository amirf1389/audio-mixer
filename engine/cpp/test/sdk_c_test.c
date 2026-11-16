/* The C SDK from plain C: version, devices JSON, an offline mixer processing a tone through a fader and the master meter, a null runtime. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "amix.h"
#include "amix_hires.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main(void) {
  CHECK(amix_api_version() == 6 && amix_api_version() == AMIX_API_VERSION && strlen(amix_version()) >= 5);
  { amix_set_dirs("/tmp/amix-sdk-paths/data", "/tmp/amix-sdk-paths/cache", NULL); char* pj = amix_paths_json(); CHECK(pj && strstr(pj, "\"data\":\"/tmp/amix-sdk-paths/data\"") && strstr(pj, "\"downloads\":\"/tmp/amix-sdk-paths/cache/downloads\"") && strstr(pj, "\"os\":")); amix_free(pj); }
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
  { /* network audio: send to this machine and receive it back on the network inputs */
    char e[128] = ""; char* j;
    CHECK(amix_rt_net_receive_start(rt, "127.0.0.1", 47318, 2, 24, 48000, 20, NULL, e, sizeof e) == 0);
    CHECK(amix_rt_net_send_start(rt, "SDK test", "127.0.0.1", 47318, 2, 24, NULL, 0, e, sizeof e) == 0);
    CHECK(amix_rt_net_send_start(rt, "bad", "nowhere", 1, 2, 24, NULL, 0, e, sizeof e) < 0 && strlen(e) > 3);
    CHECK(amix_rt_net_send_start(rt, "SDK test", "127.0.0.1", 47318, 2, 24, NULL, 0, e, sizeof e) == 0);
    amix_rt_set_input_route(rt, 1, AMIX_NET_INPUT_BASE);
    { struct timespec ts = {0, 300000000}; nanosleep(&ts, NULL); }
    j = amix_rt_net_stats_json(rt); CHECK(strstr(j, "\"send\":{\"running\":true") && strstr(j, "\"receive\":{\"running\":true") && !strstr(j, "\"packets\":0,\"lost\"")); amix_free(j);
    amix_rt_net_send_stop(rt); amix_rt_net_receive_stop(rt);
    j = amix_net_transports_json(); CHECK(strstr(j, "aes67") && strstr(j, "rtpmidi")); amix_free(j);
    j = amix_net_addresses_json(); CHECK(j[0] == '['); amix_free(j);
    j = amix_midi_inputs_json(); CHECK(j[0] == '['); amix_free(j);
    { amix_midi_action a; amix_midi_action_default(0xB2, 7, 100, &a); CHECK(a.kind == 1 && a.strip == 2 && fabs(a.value) < 1e-9); amix_midi_action_default(0x90, 33, 100, &a); CHECK(a.kind == 4 && a.strip == 1); }
  }
  amix_rt_stop(rt); CHECK(!amix_rt_running(rt)); amix_rt_destroy(rt);
  char* lic = amix_license_json(); CHECK(lic && strstr(lic, "\"state\"")); amix_free(lic);
  char* mid = amix_machine_id(); CHECK(mid && strlen(mid) > 4); amix_free(mid);
  { /* HIGH-RESOLUTION AUDIO: 96 kHz streamed down to 44.1 kHz with r8brain, a 24-bit FLAC written and read back, dither */
    char e[256] = {0}; amix_hr_resampler* r = amix_hr_resampler_create(96000, 44100, 2, "r8brain", AMIX_HR_QUALITY_24BIT, e, sizeof e); CHECK(r && strcmp(amix_hr_resampler_engine(r), "r8brain") == 0);
    static double inL[960], inR[960], oL[4096], oR[4096]; const double* in[2] = {inL, inR}; double* out[2] = {oL, oR}; int got = 0;
    for (int b = 0; b < 100; ++b) { for (int i = 0; i < 960; ++i) { inL[i] = inR[i] = 0.5 * sin(2 * M_PI * 1000 * (b * 960 + i) / 96000.0); } got += amix_hr_resampler_process(r, in, 960, out, 4096); }
    got += amix_hr_resampler_process(r, NULL, 0, out, 4096); CHECK(got >= 44100 && got <= 44100 + 4096); amix_hr_resampler_destroy(r);
    CHECK(amix_hr_resampler_create(48000, 44100, 2, "nope", 1, e, sizeof e) == NULL && strlen(e) > 5);
    static double tone[2 * 4800]; for (int i = 0; i < 4800; ++i) tone[2 * i] = tone[2 * i + 1] = 0.25 * sin(2 * M_PI * 440 * i / 192000.0);
    CHECK(amix_hr_write("/tmp/amix-sdk-hires.flac", tone, 4800, 2, 192000, "flac24", "tpdf", e, sizeof e) == 0);
    { amix_hr_info pi; CHECK(amix_hr_probe("/tmp/amix-sdk-hires.flac", &pi, e, sizeof e) == 0 && pi.rate == 192000 && pi.frames == 4800 && pi.bits == 24); }
    { amix_hr_writer* w = amix_hr_writer_open("/tmp/amix-sdk-stream.wav", 2, 96000, "wav24", "none", e, sizeof e); CHECK(w); static float fb[2 * 480]; for (int i = 0; i < 960; ++i) fb[i] = 0.1f;
      for (int k = 0; k < 10; ++k) CHECK(amix_hr_writer_write_f32(w, fb, 480, 2) == 0); CHECK(amix_hr_writer_frames(w) == 4800 && amix_hr_writer_close(w, e, sizeof e) == 0);
      amix_hr_info pi; CHECK(amix_hr_probe("/tmp/amix-sdk-stream.wav", &pi, e, sizeof e) == 0 && pi.frames == 4800 && pi.rate == 96000 && pi.bits == 24); remove("/tmp/amix-sdk-stream.wav"); }
    amix_hr_info inf; double* back = amix_hr_read("/tmp/amix-sdk-hires.flac", &inf, e, sizeof e); remove("/tmp/amix-sdk-hires.flac");
    CHECK(back && inf.rate == 192000 && inf.channels == 2 && inf.frames == 4800 && inf.bits == 24 && strcmp(inf.container, "flac") == 0 && fabs(back[2000] - tone[2000]) < 1e-6); amix_free(back);
    double* rs = NULL; int64_t rn = 0; CHECK(amix_hr_resample(tone, 4800, 2, 192000, 48000, "auto", 1, &rs, &rn, e, sizeof e) == 0 && rn == 1200); amix_free(rs);
    int32_t q[4]; const double qi[4] = {0.5, -0.5, 1.5, -1.5}; CHECK(amix_hr_quantize(qi, 4, 2, 16, "none", 1, q) == 0 && q[0] == 16384 && q[1] == -16384 && q[2] == 32767 && q[3] == -32768);
    char* hj = amix_hr_info_json(); CHECK(hj && strstr(hj, "r8brain") && strstr(hj, "libFLAC")); amix_free(hj); }
  printf("sdk_c_test: C API version %s, offline mixer, null runtime, licence JSON, hi-res audio OK\n", amix_version());
  return 0;
}
