// Where AmF X1 keeps its files: ONE place that every program of the product (console, engine, daemon, SDK, Android app) asks, so the
// files land where each operating system expects them and an uninstall / cache clear finds them all.
//
//   Windows   program  %ProgramFiles%\AmF X1              (the installer puts the .exe, DLLs, drivers and edition token there)
//             data     %APPDATA%\AMFB Audio\AmF X1          licence, console, scenes, presets, arrangement, audit, key request
//             cache    %LOCALAPPDATA%\AMFB Audio\AmF X1\cache   rendered samples, VST scan, downloads (safe to delete)
//             prefetch %LOCALAPPDATA%\AMFB Audio\AmF X1\prefetch  precomputed tables for a fast start (safe to delete)
//             temp     %TEMP%\AmF X1                        exports in progress, smoke files (safe to delete)
//   Android   data     the app's filesDir, cache / prefetch / temp the app's cacheDir (set by the app through amix_set_dirs: Android clears it when space is short)
//   Linux     program  the folder the program runs from (/opt/amf-x1 or the unpacked tarball); data = <program>/data when it is writable
//             ("save in the program"), else ~/.local/share/amf-x1; cache <program>/cache or ~/.cache/amf-x1; temp /tmp/amf-x1
//   macOS     program  the .app's folder (/Applications/AmF X1); data = <program>/AmF X1 Data when writable, else ~/Library/Application Support/AmF X1
//
// Overrides: AMIX_DATA_DIR, AMIX_CACHE_DIR, AMIX_TEMP_DIR (tests, portable installs). Compatibility: a ~/.amixos from an earlier build is used
// as the data folder while it exists and the new one does not, so nothing is lost on update. Every function creates the folder it names.
#pragma once
#include <string>

namespace am::paths {
void setProgramDir(const std::string& dir);          // the folder of the running program (main() passes argv[0]'s folder; Android passes nothing)
void setDirs(const std::string& data, const std::string& cache, const std::string& temp);   // the app (Android) hands its folders over; "" leaves one alone
std::string programDir();
std::string dataDir();
std::string cacheDir();
std::string prefetchDir();
std::string tempDir();
std::string downloadsDir();                           // <cache>/downloads
std::string file(const std::string& name);            // <data>/<name>
std::string legacyHome();                             // ~/.amixos (read for compatibility only)
const char* osName();                                 // "windows", "android", "macos", "linux", "other"
}  // namespace am::paths
