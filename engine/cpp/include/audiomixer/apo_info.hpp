// SYSTEM EFFECTS (APOs) per Windows endpoint, READ ONLY: Windows sound enhancements are Audio Processing Objects that a driver package
// (Realtek, Dolby, DTS, Waves MaxxAudio, Nahimic ...) or Windows itself puts into the endpoint's FX property store. They can colour the sound
// (EQ, "loudness", virtual surround, voice effects) and add latency, before or after AmF X1. This module only READS:
//   HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\{Render|Capture}\{endpoint}\Properties   (friendly name,
//       PKEY_AudioEndpoint_Disable_SysFx {1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5 : 1 = enhancements switched off)
//   ...\{endpoint}\FxProperties   (PKEY_FX_* {d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1..7 and the composite lists ,13 ,14 ,15: APO CLSIDs)
//   HKCR\CLSID\{clsid}           (the APO's registered name, to tell the maker)
// It never writes the registry, never touches a driver and never changes a Windows setting: the SOUND CARD page offers a button that opens
// the Windows Sound control panel (mmsys.cpl), where the user decides. The parsing is pure (fromProperties), tested with mock property data.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace am::apo {

struct Effect { std::string clsid; std::string name; std::string vendor; std::string slot; };   // slot: "stream", "mode", "endpoint", "pre-mix", "post-mix", "ui"
struct Endpoint {
  std::string id, name; bool capture = false; bool active = true;
  bool sysFxDisabled = false;          // PKEY_AudioEndpoint_Disable_SysFx = 1 ("Disable all enhancements" ticked)
  std::vector<Effect> effects;         // every APO registered on the endpoint (the Windows ones too)
  bool effectsActive() const;          // effects registered and not switched off
  std::vector<std::string> vendors() const;   // the makers found, once each, Windows last
};

// the maker of an APO from its CLSID (a few well-known ones) or its registered name: "Realtek", "Dolby", "DTS", "Waves", "Nahimic",
// "Windows" (Microsoft's own), or "" (unknown)
std::string vendorOf(const std::string& clsid, const std::string& name);
// one endpoint from its property values: props maps "{fmtid},pid" (lower case or not) to the value as text (a REG_MULTI_SZ as values joined
// by '\n'; a DWORD as its decimal number); names maps a CLSID (any case, with or without braces) to its registered name
Endpoint fromProperties(const std::string& id, bool capture, const std::map<std::string, std::string>& props, const std::map<std::string, std::string>& names);
// every endpoint of this computer (Windows; read-only registry access). Other systems: empty (no APOs there).
std::vector<Endpoint> scan();

}  // namespace am::apo
