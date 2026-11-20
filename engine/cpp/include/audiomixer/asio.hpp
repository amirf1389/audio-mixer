// ASIO driver list (Windows): the drivers registered under HKEY_LOCAL_MACHINE\SOFTWARE\ASIO (and, on 64-bit Windows, the 32-bit ones under SOFTWARE\WOW6432Node\ASIO), any vendor's (Steinberg-style IASIO COM drivers: interfaces, FlexASIO, ASIO4ALL,
// the Audio Mixer ASIO driver ...). The "asio" backend (backend_asio.cpp) opens one of them by name through the Steinberg ASIO SDK interface.
#pragma once
#include <string>
#include <vector>

namespace am {
struct AsioDriverInfo { std::string name, clsid; int bits = 64; };   // bits: 64 from SOFTWARE\ASIO, 32 from SOFTWARE\WOW6432Node\ASIO (a 32-bit driver loads only into the 32-bit program)
// What one driver reports through the Steinberg interface (IASIO): its real channel counts and the sample rates it accepts.
// error is set when the driver could not be loaded or started; the counts are then 0.
// bufMin / bufMax / bufPreferred / bufGranularity: the driver's getBufferSize (frames; granularity -1 = powers of two, 0 = only min == max).
struct AsioProbeInfo { std::string name, clsid, error; long inChannels = 0, outChannels = 0; std::vector<double> sampleRates;
                       long bufMin = 0, bufMax = 0, bufPreferred = 0, bufGranularity = 0; };
// The buffer size a stream asks for: the allowed size nearest to `want` (<= 0 = the driver's preferred size), from getBufferSize's answer.
// Used by the "asio" backend when it opens a driver and by the C SDK (amix_asio_choose_buffer); plain arithmetic, on every system.
inline long asioChooseBuffer(long want, long mn, long mx, long pref, long gran) {
  if (mn == mx) return mn;
  if (want <= 0) return pref;
  long best = pref;
  if (gran == -1) {                                           // powers of two between min and max
    long bestD = 1L << 30; for (long b = mn; b > 0 && b <= mx; b *= 2) { const long d = want > b ? want - b : b - want; if (d < bestD) { bestD = d; best = b; } }
  } else if (gran > 0) {                                      // min + k * granularity
    long k = (want - mn + gran / 2) / gran; if (k < 0) k = 0; best = mn + k * gran; if (best > mx) best = mx;
  } else best = want < mn ? mn : want > mx ? mx : want;
  return best;
}
#ifdef _WIN32
std::vector<AsioDriverInfo> listAsioDrivers();
AsioProbeInfo probeAsioDriver(const AsioDriverInfo& d);
#else
inline std::vector<AsioDriverInfo> listAsioDrivers() { return {}; }
inline AsioProbeInfo probeAsioDriver(const AsioDriverInfo& d) { AsioProbeInfo i; i.name = d.name; i.clsid = d.clsid; i.error = "ASIO is Windows-only"; return i; }
#endif
}  // namespace am
