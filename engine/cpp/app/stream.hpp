// --stream: the engine as the bridge's audio I/O. See stream.cpp for the protocol.
#pragma once
#include "audiomixer/runtime.hpp"

namespace am {
// Opens cfg's device and moves PCM between it and the standard streams until stdin ends. Returns the process exit code.
int runStream(const DeviceConfig& cfg);
}  // namespace am
