// EXPERIMENTAL, UNWIRED: see native/asio-binder/README.md.
#include "asiosdk.h"

namespace asiobinder {

bool NotYetIntegrated(std::string& reason) {
  reason =
      "asio_binder.initializeDriver() is not wired into Audio Mixer: ASIO drivers accept one client at a time, "
      "and this stub does not go through bridge/asio-lock.js's single-client claim. Opening a driver from here "
      "while bridge/server.js already has it open (via bridge/audify.js / RtAudio) would make one or both fail. "
      "See native/asio-binder/README.md, section \"Before wiring this in for real\", before removing this guard.";
  return false;
}

#ifdef _WIN32
#include <windows.h>

std::vector<std::string> ListRegisteredDrivers() {
  std::vector<std::string> names;
  HKEY key;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &key) != ERROR_SUCCESS) return names;
  char name[256];
  for (DWORD i = 0;; ++i) {
    DWORD len = sizeof(name);
    if (RegEnumKeyExA(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
    names.emplace_back(name, len);
  }
  RegCloseKey(key);
  return names;
}
#endif

}  // namespace asiobinder
