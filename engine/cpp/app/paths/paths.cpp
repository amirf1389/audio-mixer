#include "paths/paths.hpp"

#include <cstdlib>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace am::paths {
namespace {
std::string g_program, g_data, g_cache, g_temp;
std::string env(const char* k) { const char* v = std::getenv(k); return v ? v : ""; }
std::string home() { std::string h = env("HOME"); if (h.empty()) h = env("USERPROFILE"); return h.empty() ? "." : h; }
bool exists(const std::string& p) { struct stat st; return !p.empty() && ::stat(p.c_str(), &st) == 0; }
bool mkdirOne(const std::string& p) {
#ifdef _WIN32
  return ::_mkdir(p.c_str()) == 0 || exists(p);
#else
  return ::mkdir(p.c_str(), 0700) == 0 || exists(p);
#endif
}
std::string ensure(const std::string& p) {   // creates every level; returns p
  std::string cur; for (std::size_t i = 0; i <= p.size(); ++i) { if (i == p.size() || p[i] == '/' || p[i] == '\\') { if (!cur.empty() && cur.back() != ':') mkdirOne(cur); } if (i < p.size()) cur += p[i]; }
  return p;
}
[[maybe_unused]] bool writable(const std::string& dir) {
  if (!exists(dir) && !mkdirOne(dir)) return false;
#ifdef _WIN32
  return ::_access(dir.c_str(), 2) == 0;
#else
  return ::access(dir.c_str(), W_OK) == 0;
#endif
}
std::string join(const std::string& a, const std::string& b) { return a.empty() ? b : (a.back() == '/' || a.back() == '\\') ? a + b : a + "/" + b; }
}  // namespace

const char* osName() {
#if defined(_WIN32)
  return "windows";
#elif defined(__ANDROID__)
  return "android";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "other";
#endif
}
void setProgramDir(const std::string& dir) { g_program = dir; }
void setDirs(const std::string& data, const std::string& cache, const std::string& temp) { if (!data.empty()) g_data = data; if (!cache.empty()) g_cache = cache; if (!temp.empty()) g_temp = temp; }
std::string programDir() {
  if (!g_program.empty()) return g_program;
#ifdef _WIN32
  char b[MAX_PATH] = {0}; if (GetModuleFileNameA(nullptr, b, MAX_PATH) > 0) { std::string p = b; const std::size_t s = p.find_last_of("/\\"); if (s != std::string::npos) return g_program = p.substr(0, s); }
#elif defined(__linux__)
  char b[4096]; const ssize_t n = ::readlink("/proc/self/exe", b, sizeof b - 1); if (n > 0) { b[n] = 0; std::string p = b; const std::size_t s = p.find_last_of('/'); if (s != std::string::npos) return g_program = p.substr(0, s); }
#endif
  return g_program = ".";
}
std::string legacyHome() { return join(home(), ".amixos"); }
std::string dataDir() {
  if (!g_data.empty()) return ensure(g_data);
  const std::string o = env("AMIX_DATA_DIR"); if (!o.empty()) return ensure(g_data = o);
  std::string d;
#if defined(_WIN32)
  const std::string a = env("APPDATA"); d = join(a.empty() ? home() : a, "AMFB Audio/AmF X1");
#elif defined(__ANDROID__)
  d = join(home(), ".amixos");
#elif defined(__APPLE__)
  const std::string inProgram = join(programDir(), "AmF X1 Data"); d = writable(inProgram) ? inProgram : join(home(), "Library/Application Support/AmF X1");
#else
  const std::string inProgram = join(programDir(), "data"); d = writable(inProgram) ? inProgram : join(home(), ".local/share/amf-x1");
#endif
  if (!exists(d) && exists(legacyHome())) d = legacyHome();   // an earlier build's folder stays in use until the new one exists
  return ensure(g_data = d);
}
std::string cacheDir() {
  if (!g_cache.empty()) return ensure(g_cache);
  const std::string o = env("AMIX_CACHE_DIR"); if (!o.empty()) return ensure(g_cache = o);
#if defined(_WIN32)
  const std::string a = env("LOCALAPPDATA"); return ensure(g_cache = join(a.empty() ? dataDir() : a, "AMFB Audio/AmF X1/cache"));
#elif defined(__ANDROID__)
  return ensure(g_cache = join(dataDir(), "cache"));
#elif defined(__APPLE__)
  return ensure(g_cache = join(home(), "Library/Caches/AmF X1"));
#else
  const std::string inProgram = join(programDir(), "cache"); return ensure(g_cache = writable(inProgram) ? inProgram : join(home(), ".cache/amf-x1"));
#endif
}
std::string prefetchDir() {
#if defined(_WIN32)
  const std::string a = env("LOCALAPPDATA"); if (g_cache.empty() && !a.empty() && env("AMIX_CACHE_DIR").empty()) return ensure(join(a, "AMFB Audio/AmF X1/prefetch"));
#endif
  return ensure(join(cacheDir(), "prefetch"));
}
std::string tempDir() {
  if (!g_temp.empty()) return ensure(g_temp);
  const std::string o = env("AMIX_TEMP_DIR"); if (!o.empty()) return ensure(g_temp = o);
#if defined(_WIN32)
  std::string t = env("TEMP"); if (t.empty()) t = env("TMP"); return ensure(g_temp = join(t.empty() ? cacheDir() : t, "AmF X1"));
#elif defined(__ANDROID__)
  return ensure(g_temp = join(cacheDir(), "tmp"));
#else
  const std::string t = env("TMPDIR"); return ensure(g_temp = join(t.empty() ? "/tmp" : t, "amf-x1"));
#endif
}
std::string downloadsDir() { return ensure(join(cacheDir(), "downloads")); }
std::string file(const std::string& name) { return join(dataDir(), name); }
}  // namespace am::paths
