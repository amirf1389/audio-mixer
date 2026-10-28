// ASIO driver list (Windows): the drivers registered under HKEY_LOCAL_MACHINE\SOFTWARE\ASIO, any vendor's (Steinberg-style IASIO COM drivers: interfaces, FlexASIO, ASIO4ALL,
// the Audio Mixer ASIO driver ...). The "asio" backend (backend_asio.cpp) opens one of them by name through the Steinberg ASIO SDK interface.
#pragma once
#include <string>
#include <vector>

namespace am {
struct AsioDriverInfo { std::string name, clsid; };
// What one driver reports through the Steinberg interface (IASIO): its real channel counts and the sample rates it accepts.
// error is set when the driver could not be loaded or started; the counts are then 0.
struct AsioProbeInfo { std::string name, clsid, error; long inChannels = 0, outChannels = 0; std::vector<double> sampleRates; };
#ifdef _WIN32
std::vector<AsioDriverInfo> listAsioDrivers();
AsioProbeInfo probeAsioDriver(const AsioDriverInfo& d);
#else
inline std::vector<AsioDriverInfo> listAsioDrivers() { return {}; }
#endif
}  // namespace am
