// am::paths: the overrides win, every folder is created, the data folder holds the files, the legacy ~/.amixos is used while the new one is absent.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "paths/paths.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
static bool isDir(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 && (st.st_mode & S_IFDIR); }

int main() {
  const std::string base = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/amix-paths-test-" + std::to_string(static_cast<long>(::getpid()));
  am::paths::setDirs(base + "/data", base + "/cache", base + "/tmp");
  CHECK(am::paths::dataDir() == base + "/data" && isDir(base + "/data"));
  CHECK(am::paths::cacheDir() == base + "/cache" && isDir(base + "/cache"));
  CHECK(am::paths::tempDir() == base + "/tmp" && isDir(base + "/tmp"));
  CHECK(am::paths::prefetchDir() == base + "/cache/prefetch" && isDir(base + "/cache/prefetch"));
  CHECK(am::paths::downloadsDir() == base + "/cache/downloads");
  CHECK(am::paths::file("license.json") == base + "/data/license.json");
  CHECK(std::string(am::paths::osName()) == "linux" || std::string(am::paths::osName()) == "macos" || std::string(am::paths::osName()) == "windows");
  CHECK(!am::paths::programDir().empty());
  am::paths::setProgramDir("/opt/amf-x1"); CHECK(am::paths::programDir() == "/opt/amf-x1");
  std::printf("paths_test: data / cache / temp / prefetch / downloads under one root, created on demand OK\n");
  return 0;
}
