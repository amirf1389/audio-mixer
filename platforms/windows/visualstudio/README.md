# Audio Mixer: Visual Studio project (Windows)

Open this folder (`platforms/windows/visualstudio`) in Visual Studio 2022 with **File > Open > Folder**. It reads `CMakePresets.json`, so pick **Visual Studio 2022, 64-bit** (or 32-bit) in the toolbar and build.

## What it builds

| Target | Sources | Output |
|---|---|---|
| `audiomixer-native` | `engine/cpp` | the native engine |
| `AudioMixerASIO` | `drivers/asio`, `drivers/common/am_link.c` | `AudioMixerASIO64.dll` / `AudioMixerASIO32.dll` |
| `AudioDevices` | `platforms/windows/native/src` | `AudioDevices.exe` |

Nothing is copied: the project reads the existing sources, so edits here change the real files.

## Requirements

- Visual Studio 2022 with the **Desktop development with C++** workload (includes CMake).
- Or MinGW-w64 and Ninja for the `mingw-x64` preset (the compiler the release build uses).

## Notes

- The release installers (`.msi`, setup `.exe`) are still built with `npm run build:installers`, which uses MinGW and wixl.
- The ASIO driver is registered with `scripts/register-asio-driver.ps1` (administrator), not by building it.
- The release build compiles with MinGW. MSVC builds of these targets have not been tested yet.
