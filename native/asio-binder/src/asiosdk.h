// EXPERIMENTAL, UNWIRED: see native/asio-binder/README.md before using anything here.
#pragma once
#include <string>
#include <vector>

namespace asiobinder {

// Always returns false right now (fills `reason`). The addon stays a stub until a real integration claims the
// driver through bridge/asio-lock.js before opening it (ASIO drivers are single-client: see
// drivers/asio/audiomixer_asio.cpp's header comment and bridge/audify.js's ASIO handling for why) and reuses
// bridge/detect.js's readAsioRegistry (both registry views, retried, cached) instead of the single-view read
// below. See README.md "Before wiring this in for real".
bool NotYetIntegrated(std::string& reason);

#ifdef _WIN32
// The ASIO drivers registered under HKLM\SOFTWARE\ASIO, one registry view only (64-bit if this binary is 64-bit,
// 32-bit if it is 32-bit). Listing is harmless on its own; it is not used to open anything. Real detection
// (both views, retried, cached) stays in bridge/detect.js::readAsioRegistry — do not duplicate that logic
// further than this smoke test does.
std::vector<std::string> ListRegisteredDrivers();
#endif

}  // namespace asiobinder
