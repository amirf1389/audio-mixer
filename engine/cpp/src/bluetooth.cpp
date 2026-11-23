#include "audiomixer/bluetooth.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <bluetoothapis.h>
#else
#  include <array>
#  include <cerrno>
#  include <fcntl.h>
#  include <string>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <vector>
#endif

namespace am {

std::string bluetoothKindOf(std::uint32_t cod, bool* audio) {
  const unsigned major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;
  const bool render = cod & (1u << 21), capture = cod & (1u << 19);   // service class bits: audio, capturing
  if (audio) *audio = render || capture || major == 4;
  switch (major) {
    case 1: return "computer";
    case 2: return "phone";
    case 4:                                                           // audio / video
      switch (minor) {
        case 1: return "headset";          // wearable headset
        case 2: return "headset";          // hands-free
        case 4: return "microphone";
        case 5: return "speaker";
        case 6: return "headphones";
        case 7: return "speaker";          // portable audio
        case 8: return "speaker";          // car audio
        case 10: return "speaker";         // hi-fi
        default: return render ? "speaker" : "headphones";
      }
    case 5: return "peripheral";
    case 7: return "wearable";
    default: return render ? "speaker" : "other";
  }
}

#ifdef _WIN32
namespace {
std::string narrow(const wchar_t* w) {
  if (!w || !*w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}
std::string addr(const BLUETOOTH_ADDRESS& a) {
  char b[18]; std::snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X", a.rgBytes[5], a.rgBytes[4], a.rgBytes[3], a.rgBytes[2], a.rgBytes[1], a.rgBytes[0]);
  return b;
}
}  // namespace

BluetoothRadio bluetoothRadio() {
  BluetoothRadio r;
  BLUETOOTH_FIND_RADIO_PARAMS p{sizeof p}; HANDLE h = nullptr;
  HBLUETOOTH_RADIO_FIND f = BluetoothFindFirstRadio(&p, &h);
  if (!f) return r;
  BLUETOOTH_RADIO_INFO info{}; info.dwSize = sizeof info;
  if (BluetoothGetRadioInfo(h, &info) == ERROR_SUCCESS) { r.present = true; r.name = narrow(info.szName); r.address = addr(info.address); }
  CloseHandle(h); BluetoothFindRadioClose(f);
  return r;
}

std::vector<BluetoothDevice> bluetoothDevices(int inquirySeconds, std::string* note) {
  std::vector<BluetoothDevice> out;
  BLUETOOTH_FIND_RADIO_PARAMS rp{sizeof rp}; HANDLE radio = nullptr;
  HBLUETOOTH_RADIO_FIND rf = BluetoothFindFirstRadio(&rp, &radio);
  if (!rf) { if (note) *note = "this computer has no Bluetooth adapter, or it is switched off"; return out; }
  BLUETOOTH_DEVICE_SEARCH_PARAMS sp{}; sp.dwSize = sizeof sp;
  sp.fReturnAuthenticated = TRUE; sp.fReturnRemembered = TRUE; sp.fReturnConnected = TRUE;
  sp.fReturnUnknown = inquirySeconds > 0; sp.fIssueInquiry = inquirySeconds > 0;
  sp.cTimeoutMultiplier = static_cast<UCHAR>(std::clamp(inquirySeconds, 0, 20) * 0.78 + 0.5);   // units of 1.28 s
  if (sp.fIssueInquiry && sp.cTimeoutMultiplier == 0) sp.cTimeoutMultiplier = 1;
  sp.hRadio = radio;
  BLUETOOTH_DEVICE_INFO d{}; d.dwSize = sizeof d;
  HBLUETOOTH_DEVICE_FIND df = BluetoothFindFirstDevice(&sp, &d);
  if (df) {
    do {
      BluetoothDevice b;
      b.address = addr(d.Address); b.name = narrow(d.szName); b.classOfDevice = d.ulClassofDevice;
      b.connected = d.fConnected; b.paired = d.fAuthenticated; b.remembered = d.fRemembered;
      b.kind = bluetoothKindOf(d.ulClassofDevice, &b.audio);
      if (b.name.empty()) b.name = b.address;
      out.push_back(b);
    } while (BluetoothFindNextDevice(df, &d));
    BluetoothFindDeviceClose(df);
  }
  CloseHandle(radio); BluetoothFindRadioClose(rf);
  if (note) *note = sp.fIssueInquiry ? "known devices and the devices in range (Windows Bluetooth API)" : "known devices (Windows Bluetooth API)";
  return out;
}

#else

namespace {
// runs a program with its arguments directly (fork + execvp, no shell: nothing in an argument is ever interpreted) and returns what it
// printed on stdout; stderr goes to /dev/null. Empty when the program is missing or fails to start.
std::string runArgv(const std::vector<std::string>& argv) {
  std::string out; if (argv.empty()) return out;
  int fd[2]; if (::pipe(fd) != 0) return out;
  const pid_t pid = ::fork();
  if (pid < 0) { ::close(fd[0]); ::close(fd[1]); return out; }
  if (pid == 0) {
    ::dup2(fd[1], STDOUT_FILENO); ::close(fd[0]); ::close(fd[1]);
    const int dn = ::open("/dev/null", O_WRONLY); if (dn >= 0) { ::dup2(dn, STDERR_FILENO); ::close(dn); }
    std::vector<char*> a; a.reserve(argv.size() + 1); for (const auto& x : argv) a.push_back(const_cast<char*>(x.c_str())); a.push_back(nullptr);
    ::execvp(a[0], a.data()); ::_exit(127);
  }
  ::close(fd[1]);
  std::array<char, 512> buf; ssize_t n;
  while ((n = ::read(fd[0], buf.data(), buf.size())) > 0 || (n < 0 && errno == EINTR)) if (n > 0) out.append(buf.data(), static_cast<std::size_t>(n));
  ::close(fd[0]); int st = 0; while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
  return out;
}
bool macOk(const std::string& a) {
  if (a.size() != 17) return false;
  for (std::size_t i = 0; i < 17; ++i) { if (i % 3 == 2) { if (a[i] != ':') return false; } else if (!std::isxdigit(static_cast<unsigned char>(a[i]))) return false; }
  return true;
}
}  // namespace

BluetoothRadio bluetoothRadio() {
  BluetoothRadio r; const std::string s = runArgv({"bluetoothctl", "show"});
  const auto c = s.find("Controller "); if (c == std::string::npos) return r;
  r.present = true; r.address = s.substr(c + 11, 17);
  const auto n = s.find("Name: "); if (n != std::string::npos) r.name = s.substr(n + 6, s.find('\n', n) - n - 6);
  return r;
}

std::vector<BluetoothDevice> bluetoothDevices(int inquirySeconds, std::string* note) {
  std::vector<BluetoothDevice> out;
  if (!bluetoothRadio().present) { if (note) *note = "this computer has no Bluetooth adapter, or BlueZ is not running"; return out; }
  if (inquirySeconds > 0) { const std::string t = std::to_string(std::clamp(inquirySeconds, 1, 20)); runArgv({"timeout", t, "bluetoothctl", "--timeout", t, "scan", "on"}); }
  const std::string list = runArgv({"bluetoothctl", "devices"});
  std::size_t pos = 0;
  while (pos < list.size()) {
    const auto nl = list.find('\n', pos); const std::string line = list.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos); pos = nl == std::string::npos ? list.size() : nl + 1;
    if (line.rfind("Device ", 0) != 0 || line.size() < 24) continue;
    BluetoothDevice b; b.address = line.substr(7, 17); if (!macOk(b.address)) continue;
    b.name = line.substr(25);
    const std::string info = runArgv({"bluetoothctl", "info", b.address});   // the address was checked (letters, digits and colons only) and goes as one argument, no shell
    auto yes = [&](const char* k) { const auto i = info.find(std::string(k) + ": yes"); return i != std::string::npos; };
    b.connected = yes("Connected"); b.paired = yes("Paired"); b.remembered = yes("Trusted") || b.paired;
    const auto cl = info.find("Class: 0x"); if (cl != std::string::npos) b.classOfDevice = static_cast<std::uint32_t>(std::strtoul(info.c_str() + cl + 9, nullptr, 16));
    const auto rs = info.find("RSSI: "); if (rs != std::string::npos) b.rssi = std::atoi(info.c_str() + rs + 6);
    b.kind = bluetoothKindOf(b.classOfDevice, &b.audio);
    out.push_back(b);
  }
  if (note) *note = inquirySeconds > 0 ? "known devices and the devices in range (BlueZ)" : "known devices (BlueZ)";
  return out;
}
#endif

}  // namespace am
