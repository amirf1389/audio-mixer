# Audio Mixer: Xcode project (macOS)

Generate the Xcode project from the repository root, then open it:

```
cmake -S platforms/macos/xcode -B build -G Xcode
open build/AudioMixerMac.xcodeproj
```

## What it builds

| Target | Sources | Output |
|---|---|---|
| `audiomixer-native` | `engine/cpp` | the native engine on Core Audio |
| `AudioMixerDriver` | `platforms/macos/coreaudio-driver`, `drivers/common/am_link.c` | `AudioMixer.driver` (Core Audio HAL plug-in) |
| `AudioDevices` | `platforms/macos/native/AudioDevices.swift` | the device lister (Xcode generator only) |

Builds are universal (arm64 and x86_64).

## Requirements

- Xcode (or the Xcode command line tools) and CMake 3.20+ (`brew install cmake`).

## Notes

- The Makefile build names the bundle `Audio Mixer.driver`; this project names it `AudioMixer.driver`. Both install to `/Library/Audio/Plug-Ins/HAL` and load the same way.
- Install the driver with `sudo make install` in `platforms/macos/coreaudio-driver`, or copy the bundle to `/Library/Audio/Plug-Ins/HAL` and run `sudo launchctl kickstart -k system/com.apple.audio.coreaudiod`.
- The macOS app package is still built with `npm run build:macos`.
- Not yet built on a Mac: this project was written and checked on Linux, so the first Xcode build may need small fixes.
