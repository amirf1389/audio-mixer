# 1. Install

## Windows
Run `AmF-X1-<version>-setup.exe`. It installs into `C:\Program Files\AmF X1`: the console (`amixos.exe`), the engine command line (`AmF X1.exe`), the daemon for the Android remote, the ASIO driver DLL and the Qt runtime. It offers to register the ASIO driver (needs administrator rights once). Start menu and desktop shortcuts are created. The zip (`AmF-X1-<version>-windows-x64.zip`) is the same folder without an installer: unzip anywhere and start `amixos.exe`.

Your data goes to `%APPDATA%\AMFB Audio\AmF X1` (licence, console, scenes, presets, arrangement, audit log), caches to `%LOCALAPPDATA%\AMFB Audio\AmF X1`, temporary files to `%TEMP%\AmF X1`. Uninstall removes the program and the cache and asks before removing your data.

Release files are signed by AMFB Audio with its code-signing certificate; an unsigned build shows a SmartScreen warning on first run (see chapter 10).

## Android
Install `AmF-X1-<version>-android.apk` (sideload: allow the source once). The app is a local mixer on the C SDK and a remote for the console's daemon. Its data lives in the app's own storage; the cache may be cleared by Android when space is short.

## Linux
Unpack `amixos-<version>-linux-x86_64.tar.gz`; `bin/amixos-daemon` is the engine as a service, `lib/libamix.so` + `include/amix.h` the C SDK, `perl/` the Perl module. With the folder writable, data stays inside it (`data/`, `cache/`); otherwise `~/.local/share/amf-x1`. Install to `/opt/amf-x1` for a system-wide copy. The Qt console builds from `native/amixos` with your distribution's Qt 6.

## macOS
Drag the `AmF X1` folder to Applications. Data: `AmF X1 Data` next to the app when writable, else `~/Library/Application Support/AmF X1`; cache `~/Library/Caches/AmF X1`.

The full table is in `install/layout.json`; DIAGNOSTICS › FOLDERS shows what the running copy resolved.
