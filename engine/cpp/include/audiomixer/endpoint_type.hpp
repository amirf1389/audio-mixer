// WHAT KIND OF SOUND CARD AN ENDPOINT IS, from the properties Windows keeps for it (pure functions: no Windows header, tested on Linux).
//
// classifyEndpoint() reads the facts the device list has already read from the endpoint's property store (MMDevice):
//   enumerator     PKEY_Device_EnumeratorName of the device node: "USB", "BTHENUM", "BTHHFENUM", "BTHLEDEVICE", "HDAUDIO", "PCI", "ROOT", "SWD" ...
//   instanceId     PKEY_Device_InstanceId ("USB\VID_xxxx&PID_xxxx&MI_00\..." / "PCI\VEN_..." / "SWD\MMDEVAPI\...")
//   hardwareIds    the compatible / hardware ids joined (a USB Audio Class device names its class: "USB\Class_01&SubClass_02&Prot_20" = UAC2)
//   interfaceName  PKEY_DeviceInterface_FriendlyName: the adapter ("USB Audio CODEC", "NVIDIA High Definition Audio", "Headset (... Hands-Free)")
//   name           the endpoint's friendly name ("Speakers (USB DAC)")
//   formFactor     PKEY_AudioEndpoint_FormFactor (1 speakers 2 line 3 headphones 4 microphone 5 headset 6 handset 7 digital passthrough 8 S/PDIF 9 display audio)
// and answers with one TYPE, the BADGE the lists show and sensible defaults. The order matters: the bus says more than a name, a name only
// decides what the bus leaves open. Unknown stays unknown (no badge): nothing is guessed from a brand.
//
// exclusiveCandidates() is the format negotiation of WASAPI exclusive mode: the word lengths to ask IsFormatSupported for, in order, at the
// rate the engine runs at (the device's own mix format first, then the one the user chose, then the usual ladder). negotiate() walks it.
#pragma once
#include <cctype>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace am::ep {

struct Props {
  std::string name, enumerator, instanceId, hardwareIds, interfaceName;
  int formFactor = -1;
  bool capture = false;
};

struct Class {
  std::string type = "unknown";   // usb | spdif | hdmi | displayport | bt-a2dp | bt-hfp | thunderbolt | pcie | virtual | network | webcam | onboard | unknown
  std::string badge;              // what the lists show: "USB AUDIO", "S/PDIF", "HDMI", "DISPLAYPORT", "BT A2DP", "BT HANDS-FREE", "THUNDERBOLT", "PCIE", "VIRTUAL", "NETWORK AUDIO", "WEBCAM", "" = none
  std::string bus;                // usb | bluetooth | pci | hdaudio | thunderbolt | software | network | ""
  std::string uac;                // USB Audio Class: "UAC2", "UAC1" or "" (not told)
  bool preferExclusive = false;   // a dedicated converter: exclusive mode is worth it (bit-perfect, lower latency)
  bool usb = false;
  double defaultRate = 48000;     // a sensible rate to start at
  int defaultBits = 24;           // and word length (16 / 24 / 32, -32 float)
};

inline std::string lower(const std::string& s) { std::string o; o.reserve(s.size()); for (char c : s) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return o; }
inline bool hasAny(const std::string& hay, std::initializer_list<const char*> w) { for (const char* x : w) if (hay.find(x) != std::string::npos) return true; return false; }

inline Class make(const char* type, const char* badge, const char* bus, bool excl, double rate, int bits) {
  Class c; c.type = type; c.badge = badge; c.bus = bus; c.preferExclusive = excl; c.defaultRate = rate; c.defaultBits = bits; return c;
}

inline Class classifyEndpoint(const Props& p) {
  const std::string en = lower(p.enumerator), inst = lower(p.instanceId), hw = lower(p.hardwareIds), itf = lower(p.interfaceName), nm = lower(p.name);
  const std::string text = nm + " | " + itf;
  const auto starts = [](const std::string& s, const char* pre) { return s.rfind(pre, 0) == 0; };
  // BLUETOOTH: the hands-free profile has its own enumerator (BTHHFENUM); A2DP comes through BTHENUM; LE audio through BTHLEDEVICE
  const bool bt = en == "bthenum" || en == "bthhfenum" || en == "bthledevice" || starts(inst, "bthenum\\") || starts(inst, "bthhfenum\\") || starts(inst, "bthledevice\\");
  if (bt || hasAny(text, {"bluetooth"})) {
    const bool hfp = en == "bthhfenum" || starts(inst, "bthhfenum\\") || hasAny(text, {"hands-free", "handsfree", "hands free", "hfp", "headset gateway"}) || (p.capture && bt);
    return hfp ? make("bt-hfp", "BT HANDS-FREE", "bluetooth", false, 16000, 16) : make("bt-a2dp", "BT A2DP", "bluetooth", false, 48000, 16);
  }
  // NETWORK AUDIO (a Dante / AVB / AES67 virtual sound card): software on the bus side, told by the protocol it carries
  if (hasAny(text, {"dante", "avb", "aes67", "ravenna", "network audio", "audio over ip"})) return make("network", "NETWORK AUDIO", "network", false, 48000, 24);
  const bool usb = en == "usb" || starts(inst, "usb\\") || hw.find("usb\\vid_") != std::string::npos || hw.find("usb\\class_01") != std::string::npos;
  if (usb) {
    if (hasAny(text, {"webcam", "camera", "cam ", " cam)", "brio", "c920", "c922"}) && p.capture) { Class c = make("webcam", "WEBCAM", "usb", false, 48000, 16); c.usb = true; return c; }
    Class c = make("usb", "USB AUDIO", "usb", true, 48000, 24); c.usb = true;
    if (hw.find("prot_20") != std::string::npos) c.uac = "UAC2"; else if (hw.find("class_01") != std::string::npos) c.uac = "UAC1";
    if (c.uac == "UAC1") c.defaultBits = 16;   // full-speed UAC1: 16 / 24 bit at 48 kHz at most
    return c;
  }
  // THUNDERBOLT before PCI: a Thunderbolt interface shows up as PCIe behind the Thunderbolt bridge
  if (hasAny(inst + " " + hw + " " + text, {"thunderbolt"})) return make("thunderbolt", "THUNDERBOLT", "thunderbolt", true, 48000, 24);
  // DIGITAL CABLES (the form factor first, then the name): S/PDIF / optical / coaxial, HDMI, DisplayPort
  if (p.formFactor == 9 || hasAny(text, {"hdmi", "displayport", "display audio", "nvidia high definition", "amd high definition", "intel(r) display"})) {
    if (hasAny(text, {"displayport", " dp ", "(dp)", "dp audio"})) return make("displayport", "DISPLAYPORT", en == "hdaudio" ? "hdaudio" : "pci", false, 48000, 24);
    return make("hdmi", "HDMI", en == "hdaudio" ? "hdaudio" : "pci", false, 48000, 24);
  }
  if (p.formFactor == 8 || p.formFactor == 7 || hasAny(text, {"spdif", "s/pdif", "s-pdif", "optical", "toslink", "coaxial", "digital output", "digital audio (s"})) return make("spdif", "S/PDIF", en == "hdaudio" ? "hdaudio" : "", false, 48000, 24);
  // VIRTUAL / LOOPBACK cables: a software device (root-enumerated, software device or a driver that says so)
  const bool soft = en == "root" || starts(inst, "root\\") || (en == "swd" && !starts(inst, "swd\\mmdevapi\\"));
  if (soft || hasAny(text, {"virtual", "loopback", "cable input", "cable output", "cable in", "cable out", "vac ", "aggregate"})) return make("virtual", "VIRTUAL", "software", false, 48000, -32);
  if (p.capture && hasAny(text, {"webcam", "camera"})) return make("webcam", "WEBCAM", "", false, 48000, 16);
  if (en == "pci" || starts(inst, "pci\\")) return make("pcie", "PCIE", "pci", true, 48000, 24);   // a sound card in a slot (onboard audio is HDAUDIO)
  if (en == "hdaudio" || starts(inst, "hdaudio\\")) return make("onboard", "", "hdaudio", false, 48000, 24);
  return Class();
}

// one exclusive-mode format: float or integer, the container and the valid bits
struct Fmt { bool isFloat = false; int bits = 32, valid = 32; bool operator==(const Fmt& o) const { return isFloat == o.isFloat && bits == o.bits && valid == o.valid; } };

inline Fmt fromWordLength(int w) { return w == -32 ? Fmt{true, 32, 32} : w == 32 ? Fmt{false, 32, 32} : w == 24 ? Fmt{false, 24, 24} : Fmt{false, 16, 16}; }

// the order to ask the device in: the word length the user chose (wantBits: 0 = automatic), the device's own mix / device format
// (native: from GetMixFormat or PKEY_AudioEngine_DeviceFormat; valid bits 0 = not known), then the ladder (int32 or float32 first, 24 in 32,
// packed 24, 16). Never a duplicate.
inline std::vector<Fmt> exclusiveCandidates(int wantBits, Fmt native, bool preferInt32) {
  std::vector<Fmt> v; auto add = [&v](Fmt f) { for (const Fmt& x : v) if (x == f) return; v.push_back(f); };
  if (wantBits != 0) add(fromWordLength(wantBits));
  if (native.valid > 0 && native.bits >= 16) add(native);
  if (preferInt32) { add({false, 32, 32}); add({true, 32, 32}); } else { add({true, 32, 32}); add({false, 32, 32}); }
  add({false, 32, 24}); add({false, 24, 24}); add({false, 16, 16});
  return v;
}

// the first candidate the device takes; found = false when it takes none (the caller falls back to shared mode)
inline Fmt negotiate(const std::vector<Fmt>& candidates, const std::function<bool(const Fmt&)>& supported, bool& found) {
  for (const Fmt& f : candidates) if (supported(f)) { found = true; return f; }
  found = false; return Fmt{true, 32, 32};
}

// the rates of 44.1 .. 384 kHz a device takes in exclusive mode (the test answers for the device)
inline std::vector<double> supportedRates(const std::function<bool(double)>& takes) {
  static const double kRates[] = {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};
  std::vector<double> v; for (double r : kRates) if (takes(r)) v.push_back(r); return v;
}

// the status line when exclusive mode was refused and the stream went on in shared mode
inline std::string sharedFallbackNote(const std::string& why) { return "exclusive refused (" + (why.empty() ? std::string("the device said no") : why) + "): WASAPI shared, Windows converts"; }

}  // namespace am::ep
