#include "audiomixer/apo_info.hpp"

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace am::apo {
namespace {
std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string bare(const std::string& clsid) { std::string s = lower(clsid); s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '{' || c == '}' || std::isspace(static_cast<unsigned char>(c)); }), s.end()); return s; }
bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }
constexpr const char* kFx = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}";
constexpr const char* kSysFx = "{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";
constexpr const char* kName = "{a45c254e-df1c-4efd-8020-67d146a850e0},2";   // PKEY_Device_FriendlyName
constexpr const char* kDesc = "{a45c254e-df1c-4efd-8020-67d146a850e0},14";  // PKEY_DeviceInterface... the endpoint's own name ("Speakers")
}  // namespace

std::string vendorOf(const std::string& clsid, const std::string& name) {
  const std::string n = lower(name), c = bare(clsid);
  if (has(n, "realtek")) return "Realtek";
  if (has(n, "dolby")) return "Dolby";
  if (has(n, "dts")) return "DTS";
  if (has(n, "waves") || has(n, "maxxaudio")) return "Waves";
  if (has(n, "nahimic") || has(n, "a-volute") || has(n, "avolute")) return "Nahimic";
  // Windows' own APOs (the "Microsoft" effect proxies and the classic enhancements)
  if (c == "62dc1a93-ae24-464c-a43e-452f824c4250" || c == "637c490d-eee3-4c0a-973f-371958802da2" || c == "5860e1c5-f95c-4a7a-8ec8-8aef24f379a1" ||
      c == "e7f8fc2e-0e5c-4d2a-9c3b-1d9f5c6c2a11" || has(n, "microsoft") || has(n, "windows"))
    return "Windows";
  return "";
}

bool Endpoint::effectsActive() const { return !sysFxDisabled && std::any_of(effects.begin(), effects.end(), [](const Effect& e) { return e.vendor != "Windows"; }); }

std::vector<std::string> Endpoint::vendors() const {
  std::vector<std::string> v; bool win = false;
  for (const auto& e : effects) { if (e.vendor == "Windows") { win = true; continue; } const std::string x = e.vendor.empty() ? "Other" : e.vendor; if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x); }
  if (win) v.push_back("Windows");
  return v;
}

Endpoint fromProperties(const std::string& id, bool capture, const std::map<std::string, std::string>& props, const std::map<std::string, std::string>& names) {
  std::map<std::string, std::string> p; for (const auto& [k, v] : props) p[lower(k)] = v;
  std::map<std::string, std::string> nm; for (const auto& [k, v] : names) nm[bare(k)] = v;
  Endpoint e; e.id = id; e.capture = capture;
  const auto get = [&](const std::string& k) { const auto it = p.find(k); return it == p.end() ? std::string() : it->second; };
  e.name = get(kName); if (e.name.empty()) e.name = get(kDesc); if (e.name.empty()) e.name = id;
  const std::string off = get(kSysFx); e.sysFxDisabled = !off.empty() && off != "0";
  const std::pair<int, const char*> slots[] = {{5, "stream"}, {6, "mode"}, {7, "endpoint"}, {1, "pre-mix"}, {2, "post-mix"}, {3, "ui"}, {13, "stream"}, {14, "mode"}, {15, "endpoint"}};
  for (const auto& [pid, slot] : slots) {
    std::string v = get(std::string(kFx) + "," + std::to_string(pid)); std::size_t a = 0;
    while (a <= v.size()) {
      std::size_t b = v.find('\n', a); if (b == std::string::npos) b = v.size();
      const std::string c = bare(v.substr(a, b - a)); a = b + 1;
      if (c.size() != 36 || c[8] != '-') continue;   // not a CLSID
      if (std::any_of(e.effects.begin(), e.effects.end(), [&](const Effect& x) { return x.clsid == c; })) continue;
      const auto it = nm.find(c); const std::string name = it == nm.end() ? std::string() : it->second;
      e.effects.push_back({c, name, vendorOf(c, name), slot});
    }
  }
  return e;
}

#ifdef _WIN32
namespace {
std::string narrow(const std::wstring& w) { if (w.empty()) return {}; const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr); std::string s(static_cast<std::size_t>(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr); return s; }
// every value of one key as text (REG_SZ, REG_EXPAND_SZ, REG_MULTI_SZ joined by '\n', REG_DWORD as a number); read access only
std::map<std::string, std::string> values(HKEY root, const std::wstring& path) {
  std::map<std::string, std::string> out; HKEY k = nullptr;
  if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return out;
  for (DWORD i = 0;; ++i) {
    wchar_t name[256]; DWORD nl = 256, type = 0, size = 0;
    if (RegEnumValueW(k, i, name, &nl, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) break;
    if (size > 64 * 1024) continue;
    std::vector<BYTE> buf(size + 4, 0); DWORD sz = size; nl = 256;
    if (RegEnumValueW(k, i, name, &nl, nullptr, &type, buf.data(), &sz) != ERROR_SUCCESS) continue;
    std::string v;
    if (type == REG_DWORD && sz >= 4) { DWORD d = 0; std::copy(buf.begin(), buf.begin() + 4, reinterpret_cast<BYTE*>(&d)); v = std::to_string(d); }
    else if (type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ) {
      const wchar_t* w = reinterpret_cast<const wchar_t*>(buf.data()); const std::size_t n = sz / sizeof(wchar_t); std::wstring s(w, w + n);
      while (!s.empty() && s.back() == L'\0') s.pop_back();
      for (auto& ch : s) if (ch == L'\0') ch = L'\n';
      v = narrow(s);
    } else continue;
    out[narrow(name)] = v;
  }
  RegCloseKey(k); return out;
}
std::string clsidName(const std::string& clsid) {
  std::wstring w(clsid.begin(), clsid.end()); auto v = values(HKEY_CLASSES_ROOT, L"CLSID\\{" + w + L"}"); auto it = v.find(""); return it == v.end() ? std::string() : it->second;
}
}  // namespace

std::vector<Endpoint> scan() {
  std::vector<Endpoint> out;
  for (const bool capture : {false, true}) {
    const std::wstring base = std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\") + (capture ? L"Capture" : L"Render");
    HKEY k = nullptr; if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, base.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) continue;
    for (DWORD i = 0; i < 512; ++i) {
      wchar_t sub[128]; DWORD sl = 128; if (RegEnumKeyExW(k, i, sub, &sl, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
      const std::wstring ep = base + L"\\" + sub;
      const auto top = values(HKEY_LOCAL_MACHINE, ep); const auto st = top.find("DeviceState");
      const bool active = st != top.end() && st->second == "1"; if (!active) continue;   // only the endpoints in use (not unplugged / disabled)
      auto props = values(HKEY_LOCAL_MACHINE, ep + L"\\Properties"); for (auto& [kk, vv] : values(HKEY_LOCAL_MACHINE, ep + L"\\FxProperties")) props[kk] = vv;
      std::map<std::string, std::string> names;
      for (const auto& [kk, vv] : props) if (lower(kk).rfind(kFx, 0) == 0) { std::size_t a = 0; while (a <= vv.size()) { std::size_t b = vv.find('\n', a); if (b == std::string::npos) b = vv.size(); const std::string c = bare(vv.substr(a, b - a)); a = b + 1; if (c.size() == 36 && !names.count(c)) names[c] = clsidName(c); } }
      Endpoint e = fromProperties(narrow(sub), capture, props, names); e.active = true; out.push_back(std::move(e));
    }
    RegCloseKey(k);
  }
  return out;
}
#else
std::vector<Endpoint> scan() { return {}; }   // no Windows APOs on this system
#endif

}  // namespace am::apo
