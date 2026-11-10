# Install layout: one program, the files where each system keeps them

Every program of AmF X1 (console, engine, daemon, C SDK, Android app) asks one module, `engine/cpp/app/paths` (`am::paths`), where its files go. `install/layout.json` is the same table for packagers.

| | Program | Data (licence, scenes, presets, arrangement, audit) | Cache (samples, VST scan, downloads) | Prefetch | Temp |
|---|---|---|---|---|---|
| Windows | `%ProgramFiles%\AmF X1` (installer: `install/windows/amfx1.iss`) | `%APPDATA%\AMFB Audio\AmF X1` | `%LOCALAPPDATA%\AMFB Audio\AmF X1\cache` | `%LOCALAPPDATA%\AMFB Audio\AmF X1\prefetch` | `%TEMP%\AmF X1` |
| Android | the APK | `filesDir` | `cacheDir` | `cacheDir/prefetch` | `cacheDir/tmp` |
| Linux | `/opt/amf-x1` or the unpacked tarball | `<program>/data` (writable) else `~/.local/share/amf-x1` | `<program>/cache` else `~/.cache/amf-x1` | `<cache>/prefetch` | `/tmp/amf-x1` |
| macOS | `/Applications/AmF X1` | `<program>/AmF X1 Data` (writable) else `~/Library/Application Support/AmF X1` | `~/Library/Caches/AmF X1` | `<cache>/prefetch` | `$TMPDIR/amf-x1` |

- Nothing is written into the program folder on Windows or macOS at run time; on Linux and macOS the data sits next to the program when that folder is writable, as asked.
- The cache and prefetch folders hold only files the program can make again (Android clears its cache when space is short).
- Overrides: `AMIX_DATA_DIR`, `AMIX_CACHE_DIR`, `AMIX_TEMP_DIR`; `AMIXOS_LICENSE_FILE` and `AMIXOS_DRIVER_LOCK_FILE` for single files.
- Compatibility: an earlier build's `~/.amixos` keeps being used as the data folder while it exists and the new one does not.
- What a running copy resolved: DIAGNOSTICS › FOLDERS in the console, `amix_paths_json()` in the C SDK, `Engine.pathsJson()` on Android.
