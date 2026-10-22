// ASIO driver list (Windows): the drivers registered under HKEY_LOCAL_MACHINE\SOFTWARE\ASIO, any vendor's (Steinberg-style IASIO COM drivers: interfaces, FlexASIO, ASIO4ALL,
// the Audio Mixer ASIO driver ...). The "asio" backend (backend_asio.cpp) opens one of them by name through the Steinberg ASIO SDK interface.
#pragma once
#include <string>
#include <vector>

namespace am {
struct AsioDriverInfo { std::string name, clsid; };
#ifdef _WIN32
std::vector<AsioDriverInfo> listAsioDrivers();
#else
inline std::vector<AsioDriverInfo> listAsioDrivers() { return {}; }
#endif
}  // namespace am
