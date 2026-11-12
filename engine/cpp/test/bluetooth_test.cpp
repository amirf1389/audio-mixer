#include <cstdio>
#include "audiomixer/bluetooth.hpp"
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { ++fails; std::printf("FAIL %s\n", m); } } while (0)
int main() {
  bool audio = false;
  CHECK(am::bluetoothKindOf(0x240418, &audio) == "headphones" && audio, "0x240418 = headphones with audio");   // Sony WH-1000XM4 reports this class
  CHECK(am::bluetoothKindOf(0x240404, &audio) == "headset" && audio, "0x240404 = headset");
  CHECK(am::bluetoothKindOf(0x240414, &audio) == "speaker" && audio, "0x240414 = loudspeaker");
  CHECK(am::bluetoothKindOf(0x5A020C, &audio) == "phone", "0x5A020C = phone");
  CHECK(am::bluetoothKindOf(0x002540, &audio) == "peripheral" && !audio, "keyboard = peripheral");
  std::string note; const auto devs = am::bluetoothDevices(0, &note);   // no adapter here: must come back empty with a note, never crash
  const auto radio = am::bluetoothRadio();
  std::printf("radio %s, %zu devices: %s\n", radio.present ? radio.name.c_str() : "absent", devs.size(), note.c_str());
  CHECK(!note.empty(), "a note is always given");
  for (const auto& d : devs) CHECK(d.address.size() == 17 && !d.name.empty() && !d.kind.empty(), "device fields filled");
  std::printf(fails ? "bluetooth_test: %d FAILED\n" : "bluetooth_test: all passed\n", fails);
  return fails ? 1 : 0;
}
