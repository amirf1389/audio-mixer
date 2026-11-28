// Endpoint type test (endpoint_type.hpp): what a WASAPI endpoint is, from mock property sets (no Windows needed), and the exclusive-mode
// format negotiation against mock devices. Exit 0 = all good.
#include "audiomixer/endpoint_type.hpp"

#include <cstdio>
#include <string>

static int failures = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }

using am::ep::Props; using am::ep::Fmt; using am::ep::classifyEndpoint;
static Props P(const char* name, const char* en, int ff = -1, bool cap = false, const char* hw = "", const char* itf = "", const char* inst = "") {
  Props p; p.name = name; p.enumerator = en; p.formFactor = ff; p.capture = cap; p.hardwareIds = hw; p.interfaceName = itf; p.instanceId = inst; return p;
}

int main() {
  // USB Audio Class: DACs, mixers / interfaces, headsets and microphones
  { auto c = classifyEndpoint(P("Speakers (USB DAC)", "USB", 1, false, "USB\\VID_262A&PID_9302&REV_0001 USB\\Class_01&SubClass_02&Prot_20"));
    expect(c.type == "usb" && c.badge == "USB AUDIO" && c.usb && c.preferExclusive, "a USB DAC is USB AUDIO, exclusive preferred");
    expect(c.uac == "UAC2" && c.defaultBits == 24, "protocol 0x20 is UAC2, 24-bit default"); }
  { auto c = classifyEndpoint(P("Line (USB Audio CODEC)", "USB", 2, false, "USB\\Class_01&SubClass_01&Prot_00"));
    expect(c.type == "usb" && c.uac == "UAC1" && c.defaultBits == 16, "a UAC1 codec (a small USB mixer) is USB AUDIO, 16-bit default"); }
  { auto c = classifyEndpoint(P("Headset Microphone (USB Headset)", "USB", 5, true)); expect(c.type == "usb" && c.bus == "usb", "a USB headset microphone is USB AUDIO"); }
  { auto c = classifyEndpoint(P("Microphone (Mixer)", "", 4, true, "", "", "USB\\VID_1397&PID_0508&MI_00\\7&1")); expect(c.type == "usb", "USB told by the instance id when the enumerator is missing"); }
  { auto c = classifyEndpoint(P("Microphone (HD Pro Webcam C920)", "USB", 4, true)); expect(c.type == "webcam" && c.badge == "WEBCAM" && c.usb, "a USB webcam microphone is a WEBCAM"); }
  // digital cables
  { auto c = classifyEndpoint(P("Realtek Digital Output", "HDAUDIO", 8)); expect(c.type == "spdif" && c.badge == "S/PDIF", "form factor 8 is S/PDIF"); }
  { auto c = classifyEndpoint(P("Speakers (Optical Out)", "", -1)); expect(c.type == "spdif", "optical by name"); }
  { auto c = classifyEndpoint(P("LG TV (NVIDIA High Definition Audio)", "HDAUDIO", 9)); expect(c.type == "hdmi" && c.badge == "HDMI", "display audio is HDMI"); }
  { auto c = classifyEndpoint(P("DELL U2720Q (DisplayPort Audio)", "HDAUDIO", 9)); expect(c.type == "displayport" && c.badge == "DISPLAYPORT", "DisplayPort audio"); }
  // Bluetooth profiles
  { auto c = classifyEndpoint(P("Headphones (WH Stereo)", "BTHENUM", 3)); expect(c.type == "bt-a2dp" && c.badge == "BT A2DP" && c.defaultRate == 48000, "BTHENUM headphones are A2DP"); }
  { auto c = classifyEndpoint(P("Headset (WH Hands-Free AG Audio)", "BTHHFENUM", 5)); expect(c.type == "bt-hfp" && c.defaultRate == 16000 && c.defaultBits == 16, "BTHHFENUM is hands-free, 16 kHz"); }
  { auto c = classifyEndpoint(P("Headset Microphone (Buds)", "BTHENUM", 5, true)); expect(c.type == "bt-hfp", "a Bluetooth microphone is always hands-free"); }
  { auto c = classifyEndpoint(P("Speaker (Bluetooth Speaker)", "", 1)); expect(c.type == "bt-a2dp", "Bluetooth by name"); }
  // Thunderbolt / PCIe / onboard
  { auto c = classifyEndpoint(P("Analog 1-2 (Thunderbolt Audio)", "PCI", 2, false, "", "", "PCI\\VEN_1234")); expect(c.type == "thunderbolt" && c.preferExclusive, "Thunderbolt before PCIe"); }
  { auto c = classifyEndpoint(P("Speakers (Studio Card)", "PCI", 1, false, "", "", "PCI\\VEN_10EE&DEV_3FC6")); expect(c.type == "pcie" && c.badge == "PCIE" && c.preferExclusive, "a card in a slot is PCIE"); }
  { auto c = classifyEndpoint(P("Speakers (Realtek(R) Audio)", "HDAUDIO", 1)); expect(c.type == "onboard" && c.badge.empty() && !c.preferExclusive, "onboard audio has no badge"); }
  // virtual and network
  { auto c = classifyEndpoint(P("CABLE Input (Virtual Audio Cable)", "ROOT", 1)); expect(c.type == "virtual" && c.badge == "VIRTUAL" && c.defaultBits == -32, "a root-enumerated virtual cable"); }
  { auto c = classifyEndpoint(P("Loopback Output", "SWD", 1, false, "", "", "SWD\\VIRTUALAUDIO\\1")); expect(c.type == "virtual", "a software device that is not MMDEVAPI is virtual"); }
  { auto c = classifyEndpoint(P("Speakers (Dante Virtual Soundcard)", "ROOT", 1)); expect(c.type == "network" && c.badge == "NETWORK AUDIO", "Dante before virtual"); }
  { auto c = classifyEndpoint(P("AVB Stream 1-2", "", 2)); expect(c.type == "network", "AVB is network audio"); }
  { auto c = classifyEndpoint(P("Speakers", "", -1)); expect(c.type == "unknown" && c.badge.empty(), "nothing known: unknown, no badge"); }

  // FORMAT NEGOTIATION (exclusive)
  { auto v = am::ep::exclusiveCandidates(0, Fmt{false, 32, 24}, true);
    expect(v.size() == 5 && v[0] == (Fmt{false, 32, 24}), "the device's own 24-in-32 format first, no duplicates");
    expect(v[1] == (Fmt{false, 32, 32}) && v[2] == (Fmt{true, 32, 32}), "then int32 before float32"); }
  { auto v = am::ep::exclusiveCandidates(16, Fmt{false, 24, 24}, false);
    expect(v[0] == (Fmt{false, 16, 16}) && v[1] == (Fmt{false, 24, 24}) && v[2] == (Fmt{true, 32, 32}), "the user's 16 bit, the native 24, then float first"); }
  { auto v = am::ep::exclusiveCandidates(0, Fmt{false, 0, 0}, true); expect(v.size() == 5 && v[0] == (Fmt{false, 32, 32}), "unknown native: the ladder only"); }
  { bool found = false; // a USB DAC that takes packed 24 and 16 only
    const Fmt f = am::ep::negotiate(am::ep::exclusiveCandidates(0, Fmt{false, 0, 0}, true), [](const Fmt& t) { return !t.isFloat && t.bits == t.valid && t.bits <= 24; }, found);
    expect(found && f == (Fmt{false, 24, 24}), "a 16 / 24 bit DAC gets 24 bit"); }
  { bool found = true; am::ep::negotiate(am::ep::exclusiveCandidates(32, Fmt{false, 32, 32}, true), [](const Fmt&) { return false; }, found);
    expect(!found, "a device that takes nothing: not found (shared fallback)"); }
  { const auto r = am::ep::supportedRates([](double x) { return x <= 192000 && x != 176400; });
    expect(r.size() == 5 && r.front() == 44100 && r.back() == 192000, "rates 44.1 .. 192 kHz as the device answers"); }
  expect(am::ep::sharedFallbackNote("in use").find("WASAPI shared") != std::string::npos, "the fallback says shared mode");

  std::printf(failures ? "endpoint_type_test: %d FAILED\n" : "endpoint_type_test: all passed\n", failures);
  return failures ? 1 : 0;
}
