// The platform-independent part of audiomixer-native's self-update (--update, win_tools.cpp): reading the signed manifest envelope the OTA server publishes
// ({"payload": "<json text>", "signature": "<base64url, 64 bytes r||s>"}), comparing versions and deciding which download hosts are allowed.
// Header only and free of Windows calls so the unit test can run it on any system (engine/cpp/test/update_logic_test.cpp). The signature itself is checked by win_tools.cpp (CNG).
#pragma once
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

inline std::string hostOf(const std::string& url) {
  if (url.compare(0, 8, "https://") != 0) return std::string();
  std::size_t e = url.find_first_of("/?#:", 8); std::string h = url.substr(8, e == std::string::npos ? std::string::npos : e - 8);
  for (auto& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return h;
}
// https only, and only the hosts the project's releases are served from
inline bool hostAllowed(const std::string& url) {
  static const char* const ok[] = {"github.com", "raw.githubusercontent.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com"};
  const std::string h = hostOf(url); if (h.empty() || url.find('@') != std::string::npos) return false;
  for (const char* o : ok) if (h == o) return true;
  return false;
}

inline bool isHex64(const std::string& s) { if (s.size() != 64) return false; for (char c : s) if (!std::isxdigit(static_cast<unsigned char>(c))) return false; return true; }

}  // namespace upd
}  // namespace am
