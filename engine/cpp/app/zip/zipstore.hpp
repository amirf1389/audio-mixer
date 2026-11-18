// A SMALL ZIP WRITER: files stored as they are (no compression) in the plain ZIP format every tool opens (Windows itself, WinZip, WinRAR,
// 7-Zip, unzip). Used for the console's EXPORT: your scenes, pads and arrangement in one file. Names inside the archive are relative and held
// to plain characters; nothing with "..", a drive, a leading slash or a backslash gets in. No ZIP64: 2000 files, 64 MB each, 256 MB in all.
// listZip reads an archive made here back (names, sizes, and whether each file's CRC-32 still matches): the test of the writer.
#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

namespace am::zip {

struct Entry { std::string name; std::string data; };
struct Listed { std::string name; std::uint32_t size = 0; bool crcOk = false; };
constexpr std::size_t kMaxFiles = 2000, kMaxFileBytes = 64u << 20, kMaxTotalBytes = 256u << 20;

inline std::uint32_t crc32(const std::string& s) {
  static std::uint32_t table[256]; static bool made = false;
  if (!made) { for (std::uint32_t i = 0; i < 256; ++i) { std::uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } made = true; }
  std::uint32_t c = 0xFFFFFFFFu; for (unsigned char ch : s) c = table[(c ^ ch) & 0xFF] ^ (c >> 8); return c ^ 0xFFFFFFFFu;
}
// a name inside the archive: folders with '/', letters, digits, space, dot, dash, underscore, brackets; no "..", no leading '/', no ':' or '\'
inline bool safeName(const std::string& n) {
  if (n.empty() || n.size() > 180 || n[0] == '/' || n[0] == '.' || n.back() == '/' || n.find("..") != std::string::npos || n.find("//") != std::string::npos) return false;
  for (unsigned char c : n) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '-' || c == '_' || c == '(' || c == ')' || c == '/')) return false;
  return true;
}
namespace detail {
inline void u16(std::string& o, std::uint32_t v) { o += static_cast<char>(v & 0xFF); o += static_cast<char>((v >> 8) & 0xFF); }
inline void u32(std::string& o, std::uint32_t v) { u16(o, v & 0xFFFF); u16(o, (v >> 16) & 0xFFFF); }
inline std::uint32_t r16(const std::string& s, std::size_t p) { return static_cast<unsigned char>(s[p]) | (static_cast<unsigned char>(s[p + 1]) << 8); }
inline std::uint32_t r32(const std::string& s, std::size_t p) { return r16(s, p) | (r16(s, p + 2) << 16); }
}  // namespace detail

inline bool writeZip(const std::string& path, const std::vector<Entry>& entries, std::string& err) {
  using namespace detail;
  if (entries.empty()) { err = "nothing to put into the archive"; return false; }
  if (entries.size() > kMaxFiles) { err = "too many files for one archive"; return false; }
  std::size_t total = 0; for (std::size_t i = 0; i < entries.size(); ++i) { const Entry& e = entries[i];
    if (!safeName(e.name)) { err = "a file name is not allowed in the archive: " + e.name; return false; }
    if (e.data.size() > kMaxFileBytes) { err = e.name + " is too large for the archive"; return false; }
    for (std::size_t k = 0; k < i; ++k) if (entries[k].name == e.name) { err = "the same name twice: " + e.name; return false; }
    total += e.data.size(); }
  if (total > kMaxTotalBytes) { err = "the archive would be too large"; return false; }
  const std::time_t now = std::time(nullptr); std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  const std::uint32_t dosTime = static_cast<std::uint32_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2)), dosDate = static_cast<std::uint32_t>(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
  std::string out, central;
  for (const Entry& e : entries) {
    const std::uint32_t crc = crc32(e.data), size = static_cast<std::uint32_t>(e.data.size()), offset = static_cast<std::uint32_t>(out.size());
    u32(out, 0x04034b50); u16(out, 20); u16(out, 0); u16(out, 0); u16(out, dosTime); u16(out, dosDate); u32(out, crc); u32(out, size); u32(out, size); u16(out, static_cast<std::uint32_t>(e.name.size())); u16(out, 0); out += e.name; out += e.data;
    u32(central, 0x02014b50); u16(central, 20); u16(central, 20); u16(central, 0); u16(central, 0); u16(central, dosTime); u16(central, dosDate); u32(central, crc); u32(central, size); u32(central, size);
    u16(central, static_cast<std::uint32_t>(e.name.size())); u16(central, 0); u16(central, 0); u16(central, 0); u16(central, 0); u32(central, 0); u32(central, offset); central += e.name; }
  const std::uint32_t cdOffset = static_cast<std::uint32_t>(out.size()), cdSize = static_cast<std::uint32_t>(central.size()), n = static_cast<std::uint32_t>(entries.size());
  out += central; u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, n); u16(out, n); u32(out, cdSize); u32(out, cdOffset); u16(out, 0);
  std::ofstream f(path, std::ios::binary | std::ios::trunc); if (!f) { err = "cannot write " + path; return false; }
  f.write(out.data(), static_cast<std::streamsize>(out.size())); f.close(); if (!f) { err = "cannot write " + path; return false; }
  return true;
}

inline bool listZip(const std::string& path, std::vector<Listed>& out, std::string& err) {
  using namespace detail; out.clear();
  std::ifstream f(path, std::ios::binary); if (!f) { err = "cannot read " + path; return false; }
  std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (s.size() < 22 || r32(s, s.size() - 22) != 0x06054b50) { err = "not an archive written here"; return false; }
  const std::uint32_t n = r16(s, s.size() - 12), cdOffset = r32(s, s.size() - 6); std::size_t p = cdOffset;
  for (std::uint32_t i = 0; i < n; ++i) { if (p + 46 > s.size() || r32(s, p) != 0x02014b50) { err = "the archive's list is damaged"; return false; }
    const std::uint32_t crc = r32(s, p + 16), size = r32(s, p + 24), nameLen = r16(s, p + 28), extra = r16(s, p + 30), comment = r16(s, p + 32), local = r32(s, p + 42);
    if (p + 46 + nameLen > s.size() || local + 30 > s.size()) { err = "the archive's list is damaged"; return false; }
    Listed l; l.name = s.substr(p + 46, nameLen); l.size = size; const std::size_t data = local + 30 + r16(s, local + 26) + r16(s, local + 28);
    l.crcOk = data + size <= s.size() && crc32(s.substr(data, size)) == crc; out.push_back(l); p += 46 + nameLen + extra + comment; }
  return true;
}

}  // namespace am::zip
