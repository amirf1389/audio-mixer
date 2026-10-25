// EXPERIMENTAL, UNWIRED: see native/wasapi-exclusive/README.md before using anything here.
#pragma once
#include <string>
#include <vector>

namespace wasapiexclusive {

// Always returns false right now (fills `reason`). openExclusive() stays a stub until a real integration decides
// how it coexists with bridge/audify.js's own (shared-mode) stream on the same device - see README.md "Before
// wiring this in for real". WASAPI exclusive mode gives one process sole ownership of the endpoint: opening it
// here while the server already has that device open in shared mode (or another program has it exclusive) does
// not degrade gracefully the way shared mode does - the later open simply fails, or the earlier stream is cut off.
bool NotYetIntegrated(std::string& reason);

struct Endpoint {
  std::string id;      // the endpoint's PnP device id (IMMDevice::GetId); what a later openExclusive() would take
  std::string name;     // the friendly name (PKEY_Device_FriendlyName)
  bool render;          // true = output (eRender), false = input (eCapture)
  bool isDefault;
};

#ifdef _WIN32
// Every active render + capture endpoint, read-only (IMMDeviceEnumerator::EnumAudioEndpoints). Harmless: it opens
// no stream and takes no ownership of anything, so it can run alongside the server's own shared-mode streams.
std::vector<Endpoint> ListEndpoints();

// Asks the device itself (IAudioClient::IsFormatSupported, AUDCLNT_SHAREMODE_EXCLUSIVE) whether it would accept
// this exact PCM format in exclusive mode, WITHOUT opening or reserving the device - also harmless, also safe to
// call alongside an existing stream. Unlike a shared-mode probe, exclusive mode never offers a "closest match": by
// the WASAPI contract the closest-match out pointer must be null for AUDCLNT_SHAREMODE_EXCLUSIVE, and the call
// either succeeds (this exact format is supported) or fails - there is nothing to suggest instead. Returns the
// supported bool; on an unexpected failure (bad endpoint id, device gone, activation refused) returns false and
// fills `error`.
bool ProbeExclusiveFormat(const std::string& endpointId, int sampleRate, int channels, int bitsPerSample,
                           bool& supported, std::string& error);
#endif

}  // namespace wasapiexclusive
