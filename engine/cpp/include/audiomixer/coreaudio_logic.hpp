// CORE AUDIO NEGOTIATION (macOS): the pure decisions of the Core Audio backend, kept free of Apple headers so they build and are tested on
// every system (test/coreaudio_logic_test.cpp). The backend (src/backend_coreaudio.cpp, __APPLE__ only) asks the HAL for the facts
// (rate ranges, buffer range, transport type, channel counts) and lets these functions decide.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace am::ca {

struct RateRange { double min = 0, max = 0; };   // kAudioDevicePropertyAvailableNominalSampleRates (a discrete rate has min == max)

// the rate to run at: the wanted one when a range holds it, else the nearest offered rate (ties: the higher), else `current`
double pickSampleRate(double wanted, const std::vector<RateRange>& ranges, double current);
// true when the device's nominal rate must be changed: it differs, the property is settable and the device is not an aggregate whose
// sub-devices run on their own clocks (changing an aggregate's rate from one client reclocks every sub-device under other programs)
bool shouldSetRate(double current, double target, bool settable, bool aggregate);
// frames per block inside the device's kAudioDevicePropertyBufferFrameSizeRange; 0 = 256; an empty range (max < min) leaves it as asked
unsigned clampBufferFrames(unsigned wanted, unsigned minFrames, unsigned maxFrames);
// channels to open: what was asked, never more than the device has; 0 when the device has none in that direction
int clampChannels(int wanted, int available);

// the four-character transport types of kAudioDevicePropertyTransportType
constexpr std::uint32_t fourcc(const char (&s)[5]) { return (std::uint32_t(std::uint8_t(s[0])) << 24) | (std::uint32_t(std::uint8_t(s[1])) << 16) | (std::uint32_t(std::uint8_t(s[2])) << 8) | std::uint32_t(std::uint8_t(s[3])); }
bool isAggregateTransport(std::uint32_t transport);   // 'grup' (Audio MIDI Setup aggregate / multi-output), 'fgrp' (auto aggregate)
// the DeviceInfo::kind of a device from its transport: bluetooth | interface | speakers | microphone | digital | wrapper | unknown
std::string kindOfTransport(std::uint32_t transport, bool inputOnly);

// which devices one HAL unit opens. In / out ids: "" = the system default, "-" = none. One HAL output unit runs ONE device, so different
// input and output devices need an aggregate device (Audio MIDI Setup); that is said in `err` instead of drifting between two clocks.
struct DevicePlan { bool ok = false; std::string err, inId, outId; bool wantIn = false, wantOut = false; };
DevicePlan planDevices(const std::string& inId, const std::string& outId, int inChannels, int outChannels, const std::string& defaultIn, const std::string& defaultOut);

// the cheap fingerprint of the device list (ids, in order): the console's poll rescans when it changes
std::string fingerprint(const std::vector<std::string>& deviceUids);

}  // namespace am::ca
