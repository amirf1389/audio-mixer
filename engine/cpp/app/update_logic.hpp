// The platform-independent part of audiomixer-native's self-update (--update, win_tools.cpp): reading the signed manifest envelope the OTA server publishes
// ({"payload": "<json text>", "signature": "<base64url, 64 bytes r||s>"}), comparing versions and deciding which download hosts are allowed.
// Header only and free of Windows calls so the unit test can run it on any system (engine/cpp/test/update_logic_test.cpp). The signature itself is checked by win_tools.cpp (CNG).
#pragma once
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace am {
namespace upd {

// base64url (no padding) -> bytes; empty on any bad character
inline std::vector<unsigned char> b64url(const std::string& s) {
  std::vector<unsigned char> out; unsigned acc = 0; int bits = 0;
  for (char c : s) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26; else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '-' || c == '+') v = 62; else if (c == '_' || c == '/') v = 63; else if (c == '=') break; else return {};
    acc = (acc << 6) | static_cast<unsigned>(v); bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back(static_cast<unsigned char>((acc >> bits) & 0xff)); }
  }
  return out;
}

// the JSON string literal that starts at json[pos] == '"': unescaped into out, pos moves past the closing quote
inline bool readString(const std::string& j, std::size_t& pos, std::string& out) {
  if (pos >= j.size() || j[pos] != '"') return false;
  out.clear();
  for (++pos; pos < j.size(); ++pos) {
    const char c = j[pos];
    if (c == '"') { ++pos; return true; }
    if (c != '\\') { out += c; continue; }
    if (++pos >= j.size()) return false;
    switch (j[pos]) {
      case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break; case 'b': out += '\b'; break; case 'f': out += '\f'; break;
      case 'u': {
        if (pos + 4 >= j.size()) return false;
        const unsigned long cp = std::strtoul(j.substr(pos + 1, 4).c_str(), nullptr, 16); pos += 4;
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        break;
      }
      default: out += j[pos];                       // \" \\ \/
    }
  }
  return false;
}

// position of the value of "key" among the direct members of the object that starts at j[from] == '{' (nested values are skipped); npos when absent
inline std::size_t memberValue(const std::string& j, std::size_t from, const std::string& key) {
  if (from >= j.size() || j[from] != '{') return std::string::npos;
  int depth = 0;
  for (std::size_t i = from; i < j.size(); ++i) {
    const char c = j[i];
    if (c == '"') {
      std::size_t p = i; std::string s; if (!readString(j, p, s)) return std::string::npos;
      std::size_t q = p; while (q < j.size() && std::isspace(static_cast<unsigned char>(j[q]))) ++q;
      if (depth == 1 && q < j.size() && j[q] == ':' && s == key) { ++q; while (q < j.size() && std::isspace(static_cast<unsigned char>(j[q]))) ++q; return q; }
      i = p - 1; continue;
    }
    if (c == '{' || c == '[') ++depth; else if (c == '}' || c == ']') { if (--depth == 0) break; }
  }
  return std::string::npos;
}
inline bool getString(const std::string& obj, const std::string& key, std::string& out) { std::size_t p = memberValue(obj, 0, key); return p != std::string::npos && readString(obj, p, out); }
// the text of the nested object "key": {...}
inline bool getObject(const std::string& obj, const std::string& key, std::string& out) {
  const std::size_t p = memberValue(obj, 0, key); if (p == std::string::npos || obj[p] != '{') return false;
  int depth = 0;
  for (std::size_t i = p; i < obj.size(); ++i) {
    if (obj[i] == '"') { std::size_t q = i; std::string s; if (!readString(obj, q, s)) return false; i = q - 1; continue; }
    if (obj[i] == '{') ++depth; else if (obj[i] == '}' && --depth == 0) { out = obj.substr(p, i - p + 1); return true; }
  }
  return false;
}

// "1.10.2" > "1.9.0"; missing parts count as 0
inline int cmpVersion(const std::string& a, const std::string& b) {
  std::size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    long x = 0, y = 0;
    while (i < a.size() && a[i] != '.') { if (std::isdigit(static_cast<unsigned char>(a[i]))) x = x * 10 + (a[i] - '0'); ++i; } if (i < a.size()) ++i;
    while (j < b.size() && b[j] != '.') { if (std::isdigit(static_cast<unsigned char>(b[j]))) y = y * 10 + (b[j] - '0'); ++j; } if (j < b.size()) ++j;
    if (x != y) return x > y ? 1 : -1;
  }
  return 0;
}

// THE UPDATE NOTICE: how the console tells that a newer version was found. Three things, each with its own memory:
//   banner  across the top of the console, until UPDATE NOW, LATER (it comes back after laterUntilMs) or SKIP THIS VERSION
//   badge   on SOFTWARE UPDATE in the menu, until that page has been opened for this version
//   toast   one Windows notification per version, the first time it is found
// A skipped version is never announced again; the next version is. `found` = the signed manifest names a version newer than this one.
struct Notice { bool banner = false, badge = false, toast = false; };
inline Notice noticeFor(bool found, const std::string& latest, const std::string& skipped, const std::string& seen, const std::string& toasted, long long laterUntilMs, long long nowMs) {
  Notice n; if (!found || latest.empty() || latest == skipped) return n;
  n.banner = nowMs >= laterUntilMs; n.badge = latest != seen; n.toast = latest != toasted; return n;
}
inline std::string hostOf(const std::string& url) {
  if (url.compare(0, 8, "https://") != 0) return std::string();
  std::size_t e = url.find_first_of("/?#:", 8); std::string h = url.substr(8, e == std::string::npos ? std::string::npos : e - 8);
  for (auto& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return h;
}
// Hosts the owner of this console added by hand (the update server link). The list a release comes from must still carry the vendor's
// signature and each file its checksum: a host on this list can offer, it cannot make the console install.
inline std::vector<std::string>& extraHosts() { static std::vector<std::string> v; return v; }
// https only: the hosts the project's releases are served from, and those the owner added
inline bool hostAllowed(const std::string& url) {
  static const char* const ok[] = {"github.com", "raw.githubusercontent.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com",
                                 "amirfakhribadri.my", "ota.amirfakhribadri.my", "licence.amirfakhribadri.my"};   // the vendor's own domain
  const std::string h = hostOf(url); if (h.empty() || url.find('@') != std::string::npos) return false;
  for (const char* o : ok) if (h == o) return true;
  for (const std::string& e : extraHosts()) if (h == e) return true;
  return false;
}

// UPDATE SOURCE: the public OTA server (the default) or an OTA server on this computer (ota-server/server.js, plain http on 127.0.0.1).
// Either way the list must carry the vendor's signature and each file its checksum; the source only says where it is read.
inline const char* publicManifestUrl() { return "https://ota.amirfakhribadri.my/update.json"; }
inline const char* localManifestUrl() { return "http://127.0.0.1:8780/update.json"; }
inline bool localManifest(const std::string& url) {
  for (const char* p : {"http://127.0.0.1:", "http://localhost:"}) {
    const std::string pre(p); if (url.compare(0, pre.size(), pre) != 0) continue;
    std::size_t i = pre.size(), d = 0; while (i < url.size() && std::isdigit(static_cast<unsigned char>(url[i]))) { ++i; ++d; }
    return d >= 2 && d <= 5 && i < url.size() && url[i] == '/' && url.find('@') == std::string::npos;
  }
  return false;
}
inline std::string sourceOf(const std::string& url) { return url == publicManifestUrl() ? "public" : localManifest(url) ? "local" : "custom"; }

inline bool isHex64(const std::string& s) { if (s.size() != 64) return false; for (char c : s) if (!std::isxdigit(static_cast<unsigned char>(c))) return false; return true; }
// PLUG-IN DOWNLOADS: a plug-in in the signed list is one file saved into the console's own plug-in folder. Its name is used as a file
// name on this computer, so it is held to plain characters: letters, digits, space, dot, dash, underscore and brackets, ending in .vst3,
// not starting with a dot, no ".." and no path separator. Larger than 200 MB is not a plug-in this console downloads.
constexpr std::uint64_t kMaxPluginBytes = 200ull * 1024 * 1024;
inline bool pluginFileOk(const std::string& name) {
  if (name.size() < 6 || name.size() > 80 || name[0] == '.' || name[0] == ' ' || name.find("..") != std::string::npos) return false;
  for (unsigned char c : name) if (!(std::isalnum(c) || c == ' ' || c == '.' || c == '-' || c == '_' || c == '(' || c == ')')) return false;
  std::string ext = name.substr(name.size() - 5); for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".vst3";
}

}  // namespace upd
}  // namespace am
