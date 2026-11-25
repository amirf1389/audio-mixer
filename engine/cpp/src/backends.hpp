// The backends of the runtime (one source file each; only the ones of this system are built).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "audiomixer/runtime.hpp"
namespace am {
std::unique_ptr<Backend> makeNullBackend();
std::unique_ptr<Backend> makeAlsaBackend();
std::unique_ptr<Backend> makeWasapiBackend();
std::unique_ptr<Backend> makeAsioBackend();
std::unique_ptr<Backend> makeDsoundBackend();
std::unique_ptr<Backend> makeCoreAudioBackend();
#ifdef __APPLE__
void addCoreAudio(std::vector<DeviceInfo>& out);              // every HAL device with streams (backend_coreaudio.cpp)
std::string coreAudioFingerprint();                           // device UIDs + the defaults, cheap
void* coreAudioWatchStart(std::function<void()> f, bool& ok); // device list / default device listeners; f runs on the HAL thread
void coreAudioWatchStop(void* watch);
#endif
}  // namespace am
