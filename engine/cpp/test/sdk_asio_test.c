/* The C SDK's ASIO API (API 8) from plain C: the device list shape, the unsupported path off Windows, the buffer choice arithmetic. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amix.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main(void) {
  char err[256]; char* j = NULL;
  CHECK(amix_api_version() >= 8);
  CHECK(amix_device_list(NULL, NULL) == AMIX_ERR_ARG);
  /* every backend: an array of objects with the documented keys; the null device is always there */
  CHECK(amix_device_list(NULL, &j) == AMIX_OK && j && j[0] == '[' && strstr(j, "\"backend\":\"null\"") && strstr(j, "\"id\":") && strstr(j, "\"inChannels\":") && strstr(j, "\"outChannels\":") && strstr(j, "\"defaultRate\":")); amix_free(j);
  CHECK(amix_device_list("null", &j) == AMIX_OK && strstr(j, "\"backend\":\"null\"") && !strstr(j, "\"backend\":\"alsa\"")); amix_free(j);
  CHECK(amix_device_list("no-such-backend", &j) == AMIX_OK && strcmp(j, "[]") == 0); amix_free(j);
  { /* buffer choice from a driver's getBufferSize answer: works on every system */
    amix_asio_info p2; memset(&p2, 0, sizeof p2); p2.buffer_min = 64; p2.buffer_max = 2048; p2.buffer_preferred = 256; p2.buffer_granularity = -1;
    CHECK(amix_asio_choose_buffer(&p2, AMIX_ASIO_BUFFER_PREFERRED) == 256 && amix_asio_choose_buffer(&p2, AMIX_ASIO_BUFFER_MIN) == 64 && amix_asio_choose_buffer(&p2, AMIX_ASIO_BUFFER_MAX) == 2048);
    CHECK(amix_asio_choose_buffer(&p2, 100) == 128 && amix_asio_choose_buffer(&p2, 5000) == 2048 && amix_asio_choose_buffer(&p2, 1) == 64);
    amix_asio_info g = p2; g.buffer_min = 32; g.buffer_max = 1024; g.buffer_granularity = 32;
    CHECK(amix_asio_choose_buffer(&g, 100) == 96 && amix_asio_choose_buffer(&g, 4000) == 1024);
    amix_asio_info f = p2; f.buffer_min = f.buffer_max = f.buffer_preferred = 512; f.buffer_granularity = 0;
    CHECK(amix_asio_choose_buffer(&f, 64) == 512 && amix_asio_choose_buffer(NULL, 64) == 0);
  }
#ifdef _WIN32
  CHECK(amix_asio_supported() == 1);
  CHECK(amix_device_list("asio", &j) == AMIX_OK && j[0] == '['); amix_free(j);   /* whatever drivers this machine has */
  { amix_asio_info inf; CHECK(amix_asio_driver_info("no such ASIO driver, surely", &inf, err, sizeof err) == AMIX_ERR_NOT_FOUND && err[0]); }
#else
  CHECK(amix_asio_supported() == 0);
  j = NULL; CHECK(amix_device_list("asio", &j) == AMIX_ERR_UNSUPPORTED && j && strcmp(j, "[]") == 0); amix_free(j);
  { amix_asio_info inf; inf.in_channels = 7;
    CHECK(amix_asio_driver_info("Any Driver", &inf, err, sizeof err) == AMIX_ERR_UNSUPPORTED && inf.in_channels == 0 && strstr(err, "Windows")); }
  { amix_rt* rt = amix_rt_create(8); amix_asio_open_params p; memset(&p, 0, sizeof p); p.driver = "Any Driver"; p.in_channels = 8; p.out_channels = 8;
    CHECK(rt && amix_asio_open(rt, &p, err, sizeof err) == AMIX_ERR_UNSUPPORTED && !amix_rt_running(rt)); amix_rt_destroy(rt); }
  j = amix_asio_drivers_json(); CHECK(strcmp(j, "[]") == 0); amix_free(j);
#endif
  CHECK(amix_asio_driver_info("x", NULL, err, sizeof err) == AMIX_ERR_ARG || !amix_asio_supported());
  printf("sdk_asio_test ok\n");
  return 0;
}
