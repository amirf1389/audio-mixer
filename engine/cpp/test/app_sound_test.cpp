// APP SOUND: the names the console gives the programs, the input ids, and the live list (empty off Windows).
#include "audiomixer/app_sound.hpp"

#include <cstdio>

using namespace am::app;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static std::string label(const char* exe, std::string* kind = nullptr) { std::string l, k; describe(exe, l, k); if (kind) *kind = k; return l; }

int main() {
  std::string k;
  CHECK(label("Spotify.exe", &k) == "Spotify" && k == "streaming");
  CHECK(label("C:\\Users\\a\\AppData\\Local\\TIDAL\\app-2.38\\TIDAL.exe", &k) == "TIDAL" && k == "streaming");
  CHECK(label("Deezer.exe") == "Deezer"); CHECK(label("Roon.exe") == "Roon"); CHECK(label("RAATServer.exe") == "Roon (RAAT)");
  CHECK(label("Qobuz.exe") == "Qobuz"); CHECK(label("AppleMusic.exe") == "Apple Music");
  CHECK(label("chrome.exe", &k) == "Chrome (YouTube, web players)" && k == "browser");
  CHECK(label("msedge.exe", &k).find("YouTube") != std::string::npos && k == "browser");
  CHECK(label("vlc.exe", &k) == "VLC" && k == "player");
  CHECK(label("Zoom.exe", &k) == "Zoom" && k == "other");
  CHECK(label("noext", &k) == "noext");

  std::uint32_t pid = 0;
  CHECK(deviceId(4321) == "loopback:app:4321");
  CHECK(parseDeviceId("loopback:app:4321", pid) && pid == 4321);
  CHECK(!parseDeviceId("loopback:system", pid)); CHECK(!parseDeviceId("loopback:app:", pid)); CHECK(!parseDeviceId("loopback:app:0", pid));
  CHECK(!parseDeviceId("loopback:app:12x", pid)); CHECK(!parseDeviceId("loopback:app:99999999999", pid)); CHECK(!parseDeviceId("loopback:app:-5", pid));
  CHECK(!parseDeviceId("{0.0.1.00000000}.{guid}", pid));

  const auto list = sessions();   // live: on Windows the programs with a sound stream; elsewhere none
#ifndef _WIN32
  CHECK(list.empty());
#endif
  for (const Session& s : list) { CHECK(s.pid != 0); CHECK(!s.label.empty()); std::printf("app_sound: %u %s (%s)%s\n", s.pid, s.label.c_str(), s.exe.c_str(), s.active ? " playing" : ""); }
  if (fails) std::printf("app_sound: %d FAILED\n", fails); else std::printf("app_sound: all passed (%zu live sessions)\n", list.size());
  return fails ? 1 : 0;
}
