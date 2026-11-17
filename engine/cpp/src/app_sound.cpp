#include "audiomixer/app_sound.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <mmdeviceapi.h>
#  include <audiopolicy.h>
#endif

namespace am::app {

static std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

void describe(const std::string& exe, std::string& label, std::string& kind) {
  std::string base = exe; const std::size_t slash = base.find_last_of("/\\"); if (slash != std::string::npos) base = base.substr(slash + 1);
  const std::string n = lower(base);
  struct Known { const char* file; const char* label; const char* kind; };
  static const Known known[] = {
    {"spotify.exe", "Spotify", "streaming"}, {"tidal.exe", "TIDAL", "streaming"}, {"deezer.exe", "Deezer", "streaming"},
    {"roon.exe", "Roon", "streaming"}, {"roonappliance.exe", "Roon", "streaming"}, {"raatserver.exe", "Roon (RAAT)", "streaming"},
    {"qobuz.exe", "Qobuz", "streaming"}, {"amazon music.exe", "Amazon Music", "streaming"}, {"amazonmusic.exe", "Amazon Music", "streaming"},
    {"applemusic.exe", "Apple Music", "streaming"}, {"itunes.exe", "iTunes", "player"}, {"youtubemusic.exe", "YouTube Music", "streaming"},
    {"soundcloud.exe", "SoundCloud", "streaming"}, {"pandora.exe", "Pandora", "streaming"},
    {"chrome.exe", "Chrome (YouTube, web players)", "browser"}, {"msedge.exe", "Edge (YouTube, web players)", "browser"},
    {"firefox.exe", "Firefox (YouTube, web players)", "browser"}, {"brave.exe", "Brave (YouTube, web players)", "browser"},
    {"opera.exe", "Opera (YouTube, web players)", "browser"}, {"vivaldi.exe", "Vivaldi (YouTube, web players)", "browser"},
    {"vlc.exe", "VLC", "player"}, {"foobar2000.exe", "foobar2000", "player"}, {"musicbee.exe", "MusicBee", "player"}, {"aimp.exe", "AIMP", "player"},
    {"wmplayer.exe", "Windows Media Player", "player"}, {"microsoft.media.player.exe", "Media Player", "player"}, {"mpc-hc64.exe", "MPC-HC", "player"},
  };
  for (const Known& k : known) if (n == k.file) { label = k.label; kind = k.kind; return; }
  label = base.size() > 4 && n.compare(n.size() - 4, 4, ".exe") == 0 ? base.substr(0, base.size() - 4) : base;
  kind = "other";
}

std::string deviceId(std::uint32_t pid) { return "loopback:app:" + std::to_string(pid); }
bool parseDeviceId(const std::string& id, std::uint32_t& pid) {
  static const std::string pre = "loopback:app:";
  if (id.size() <= pre.size() || id.compare(0, pre.size(), pre) != 0) return false;
  const std::string num = id.substr(pre.size());
  if (num.size() > 10 || !std::all_of(num.begin(), num.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
  const unsigned long long v = std::strtoull(num.c_str(), nullptr, 10); if (v == 0 || v > 0xFFFFFFFFull) return false;
  pid = static_cast<std::uint32_t>(v); return true;
}

#ifdef _WIN32
namespace {
const GUID kClsidEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidSessionManager2 = {0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
const GUID kIidSessionControl2 = {0xbfb7ff88, 0x7239, 0x4fc9, {0x8f, 0xa2, 0x07, 0xc9, 0x50, 0xbe, 0x9c, 0x6d}};
template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }
bool processLoopbackWindows() {   // Windows 10 2004 (build 19041) and later
  using Fn = LONG(WINAPI*)(OSVERSIONINFOW*); HMODULE nt = GetModuleHandleW(L"ntdll.dll"); if (!nt) return false;
  const auto f = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion"))); if (!f) return false;
  OSVERSIONINFOW v{}; v.dwOSVersionInfoSize = sizeof v; if (f(&v) != 0) return false;
  return v.dwMajorVersion > 10 || (v.dwMajorVersion == 10 && v.dwBuildNumber >= 19041);
}
std::string exeOf(DWORD pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!h) return {};
  wchar_t path[MAX_PATH]; DWORD n = MAX_PATH; std::string out;
  if (QueryFullProcessImageNameW(h, 0, path, &n)) { char b[MAX_PATH * 3]; const int k = WideCharToMultiByte(CP_UTF8, 0, path, -1, b, sizeof b, nullptr, nullptr); if (k > 0) out = b; }
  CloseHandle(h); return out;
}
}  // namespace

std::vector<Session> sessions() {
  std::vector<Session> out; if (!processLoopbackWindows()) return out;
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // already initialised by the caller: fine
  IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioSessionManager2* mgr = nullptr; IAudioSessionEnumerator* list = nullptr;
  if (SUCCEEDED(CoCreateInstance(kClsidEnumerator, nullptr, CLSCTX_ALL, kIidEnumerator, reinterpret_cast<void**>(&en))) && SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))
      && SUCCEEDED(dev->Activate(kIidSessionManager2, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr))) && SUCCEEDED(mgr->GetSessionEnumerator(&list))) {
    int n = 0; list->GetCount(&n); std::set<DWORD> seen; const DWORD self = GetCurrentProcessId();
    for (int i = 0; i < n; ++i) {
      IAudioSessionControl* c = nullptr; IAudioSessionControl2* c2 = nullptr;
      if (FAILED(list->GetSession(i, &c))) continue;
      if (SUCCEEDED(c->QueryInterface(kIidSessionControl2, reinterpret_cast<void**>(&c2)))) {
        DWORD pid = 0; AudioSessionState st = AudioSessionStateInactive;
        // pid 0: Windows' own system sounds; this program and the expired sessions of closed programs are left out
        if (SUCCEEDED(c2->GetProcessId(&pid)) && pid != 0 && pid != self && c2->IsSystemSoundsSession() != S_OK && SUCCEEDED(c->GetState(&st)) && st != AudioSessionStateExpired && !seen.count(pid)) {
          const std::string exe = exeOf(pid);
          if (!exe.empty()) { seen.insert(pid); Session s; s.pid = pid; s.exe = exe.substr(exe.find_last_of("\\/") + 1); describe(s.exe, s.label, s.kind); s.active = st == AudioSessionStateActive; out.push_back(s); }
        }
        release(c2);
      }
      release(c);
    }
  }
  release(list); release(mgr); release(dev); release(en);
  std::stable_sort(out.begin(), out.end(), [](const Session& a, const Session& b) { return a.active > b.active; });
  return out;
}
#else
std::vector<Session> sessions() { return {}; }
#endif

}  // namespace am::app
