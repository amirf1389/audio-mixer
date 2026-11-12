// The Bluetooth devices this computer knows and, on request, the ones in the air around it.
// Windows: the system's own Bluetooth API (bluetoothapis.h, bthprops): paired / remembered devices, and an
// inquiry for devices in range. Linux: BlueZ through bluetoothctl. Read-only: nothing is paired, connected
// or changed here. The audio of a connected headset still comes through the normal audio devices.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace am {

struct BluetoothDevice {
  std::string address;      // AA:BB:CC:DD:EE:FF
  std::string name;
  std::string kind;         // headphones | headset | speaker | phone | computer | wearable | other  (from the class of device)
  bool connected = false, paired = false, remembered = false, audio = false;   // audio: the device says it renders or captures sound
  std::uint32_t classOfDevice = 0;
  int rssi = 0;             // Linux only when known, else 0
};

struct BluetoothRadio { std::string name, address; bool present = false; };

BluetoothRadio bluetoothRadio();                                          // this computer's adapter
// inquirySeconds 0 = the known devices only; >0 also listens for devices in range for about that long (1..20)
std::vector<BluetoothDevice> bluetoothDevices(int inquirySeconds = 0, std::string* note = nullptr);
std::string bluetoothKindOf(std::uint32_t classOfDevice, bool* audio = nullptr);   // from the class-of-device bits

}  // namespace am
