// 1000 SOUND EFFECTS, synthesised like the 100 built-in samples (no recordings, nothing to license, identical on every machine):
// 20 families (whooshes, risers, impacts, lasers, UI sounds, alarms, weather, bells ...) x 50 variations each. A variation moves the
// family's recipe along its own axes (pitch, length, brightness, motion) in a fixed, deterministic way, so "Laser 17" is the same sound on
// the console, the remote service and the phone. Rendered on demand (a few milliseconds each), never kept all at once.
#pragma once
#include <string>
#include <vector>

namespace am::sfx {

constexpr int kFamilies = 20, kPerFamily = 50, kCount = kFamilies * kPerFamily;   // ids 1..1000

struct Info { int id; std::string name, family; double seconds; };
const std::vector<std::string>& families();                 // the 20 family names, in id order (family f holds ids f*50+1 .. f*50+50)
Info info(int id);                                           // id 0 when out of range
bool render(int id, double sampleRate, std::vector<float>& L, std::vector<float>& R);   // deterministic; false when out of range

}  // namespace am::sfx
