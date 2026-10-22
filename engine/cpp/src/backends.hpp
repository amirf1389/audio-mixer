// The backends of the runtime (one source file each; only the ones of this system are built).
#pragma once
#include <memory>
#include "audiomixer/runtime.hpp"
namespace am {
std::unique_ptr<Backend> makeNullBackend();
std::unique_ptr<Backend> makeAlsaBackend();
std::unique_ptr<Backend> makeWasapiBackend();
std::unique_ptr<Backend> makeAsioBackend();
std::unique_ptr<Backend> makeCoreAudioBackend();
}  // namespace am
