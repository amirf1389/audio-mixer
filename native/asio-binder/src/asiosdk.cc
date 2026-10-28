// EXPERIMENTAL, UNWIRED: see native/asio-binder/README.md.
#include "asiosdk.h"

namespace asiobinder {

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
