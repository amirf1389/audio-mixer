// EXPERIMENTAL, UNWIRED: see native/asio-binder/README.md before using anything here.
#pragma once
#include <string>
#include <vector>

namespace asiobinder {

#ifdef _WIN32
// The ASIO drivers registered under HKLM\SOFTWARE\ASIO, one registry view only (64-bit if this binary is 64-bit,
// 32-bit if it is 32-bit). Listing is harmless on its own; it is not used to open anything. Real detection
// (both views, retried, cached) stays in bridge/detect.js::readAsioRegistry — do not duplicate that logic
// further than this smoke test does.
std::vector<std::string> ListRegisteredDrivers();
#endif

}  // namespace asiobinder
