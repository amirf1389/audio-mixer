// The Core Audio backend's pure negotiation (runs on every system; the HAL itself is only on macOS).
#include "audiomixer/coreaudio_logic.hpp"

#include <cstdio>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
  using namespace am::ca;
  const std::vector<RateRange> discrete = {{44100, 44100}, {48000, 48000}, {96000, 96000}};
  CHECK(pickSampleRate(48000, discrete, 44100) == 48000);
  CHECK(pickSampleRate(88200, discrete, 44100) == 96000);   // 88200 is 7800 from 96000, 40200 from 48000
  CHECK(pickSampleRate(46050, discrete, 44100) == 48000);   // a tie (1950 each way): the higher
  CHECK(pickSampleRate(192000, discrete, 44100) == 96000);
  CHECK(pickSampleRate(50000, {{8000, 192000}}, 44100) == 50000);   // a continuous range
  CHECK(pickSampleRate(48000, {}, 44100) == 44100);                // nothing known: keep the device's
  CHECK(pickSampleRate(0, discrete, 44100) == 44100);

  CHECK(shouldSetRate(44100, 48000, true, false));
  CHECK(!shouldSetRate(48000, 48000, true, false));
  CHECK(!shouldSetRate(44100, 48000, false, false));
  CHECK(!shouldSetRate(44100, 48000, true, true));   // aggregate: never reclock it from here

  CHECK(clampBufferFrames(256, 14, 4096) == 256);
  CHECK(clampBufferFrames(8, 14, 4096) == 14);
  CHECK(clampBufferFrames(8192, 14, 4096) == 4096);
  CHECK(clampBufferFrames(0, 14, 4096) == 256);
  CHECK(clampBufferFrames(100, 0, 0) == 100);

  CHECK(clampChannels(2, 8) == 2); CHECK(clampChannels(16, 8) == 8); CHECK(clampChannels(2, 0) == 0); CHECK(clampChannels(0, 2) == 0);

  CHECK(fourcc("grup") == 0x67727570u);
  CHECK(isAggregateTransport(fourcc("grup")) && isAggregateTransport(fourcc("fgrp")) && !isAggregateTransport(fourcc("usb ")));
  CHECK(kindOfTransport(fourcc("blue"), false) == "bluetooth");
  CHECK(kindOfTransport(fourcc("usb "), false) == "interface");
  CHECK(kindOfTransport(fourcc("bltn"), true) == "microphone");
  CHECK(kindOfTransport(fourcc("bltn"), false) == "speakers");
  CHECK(kindOfTransport(fourcc("hdmi"), false) == "digital");
  CHECK(kindOfTransport(0, false) == "unknown");

  DevicePlan p = planDevices("", "", 2, 2, "mic", "spk");   // two different defaults: output only
  CHECK(p.ok && p.wantOut && !p.wantIn && p.outId == "spk");
  p = planDevices("", "", 2, 2, "iface", "iface");
  CHECK(p.ok && p.wantIn && p.wantOut && p.inId == "iface");
  p = planDevices("mic", "spk", 2, 2, "mic", "spk");          // named apart: needs an aggregate
  CHECK(!p.ok && p.err.find("aggregate") != std::string::npos);
  p = planDevices("-", "", 2, 2, "mic", "spk");
  CHECK(p.ok && !p.wantIn && p.outId == "spk");
  p = planDevices("agg", "agg", 2, 2, "mic", "spk");
  CHECK(p.ok && p.wantIn && p.wantOut);
  p = planDevices("-", "-", 2, 2, "mic", "spk");
  CHECK(!p.ok);
  p = planDevices("", "", 2, 2, "", "");
  CHECK(!p.ok);

  CHECK(fingerprint({"A", "B"}) == "ca:A|ca:B|");
  if (fails == 0) std::printf("coreaudio_logic: all passed\n");
  return fails ? 1 : 0;
}
