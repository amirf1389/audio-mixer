# A mix OS SDK: one engine, every platform

The audio engine is one C++20 code base (`engine/cpp`). Everything else sits on it through a small, stable **C API**, so the console,
the daemon, the bindings and the packages all run the same DSP. Nothing is duplicated per language.

```
                engine/cpp  (Mixer, Runtime, DSP, buses, fx, loudness, VST hosts, licence)
                      │
          sdk/c/include/amix.h  +  libamix  (C ABI, AMIX_API_VERSION 1)
     ┌──────────┬───────────┬────────────┬───────────────┬──────────────┐
  Perl XS    Rust crates   Kotlin/JNI    VB.NET launcher   daemon HTTP API
 (bindings/  (bindings/    (android/)    (windows/vs/)      (engine/cpp/app/daemon)
   perl)       rust)
                                        sdk/wasm: the DSP core for browsers (separate freestanding C, same algorithms)
```

## The C API (`sdk/c/include/amix.h`)

| group | what |
|---|---|
| `amix_version`, `amix_api_version`, `amix_free` | version strings, API level, freeing returned strings |
| `amix_backends_json`, `amix_devices_json` | the drivers built in and the sound cards found, as JSON |
| `amix_mixer_*` | an **offline mixer**: create, faders, pans, mute / solo, master, limiter, sends, buses, width, `process(in, frames, L, R)`, `levels` |
| `amix_rt_*` | the **live runtime**: open a device (ASIO / WASAPI / Core Audio / ALSA / null), every control the console has (EQ, dynamics, routing, monitor), `levels` |
| `amix_license_json`, `amix_machine_id` | the licence state and this machine's code (read-only) |

Build: `cmake -S engine/cpp -B build && cmake --build build` gives `libamix.so` (Linux), `amix.dll` (Windows: `windows/vs/AmixOS.sln` or MinGW),
`libamix.dylib` (macOS) and the static twin `amix_static`. Only `amix_*` symbols are exported. `sdk_c_test` (plain C) is part of `ctest`.

## Bindings

- **Perl** (`bindings/perl/Audio-AmixOS`): XS over the C API. `AMIX_LIB=../../build perl Makefile.PL && make && make test`.
  `Audio::AmixOS::Mixer->new(4, 48000)`, `process(\@inputs, frames)`, `levels`, `Audio::AmixOS::devices_json()`.
- **Rust** (`bindings/rust`): `amix-sys` (FFI) and `amix` (safe `Mixer`, `Runtime`, `Levels`, `Filter`). `AMIX_LIB_DIR=... cargo test`.
- **Kotlin / Android** (`android/`): Gradle project; the NDK builds `libamix.so` from the engine sources, `amix_jni.cpp` exposes it to
  `com.amfb.amixos.Engine`, `AudioLoop.kt` runs the microphone through a strip and out to the speaker. `./gradlew assembleRelease` → APK,
  `bundleRelease` → AAB. The engine has no AAudio backend yet: Kotlin owns the device I/O.
- **VB.NET / Windows** (`windows/vs`): `AmixOS.sln` with `amix.vcxproj` (DLL), `amixos-daemon.vcxproj` (EXE) and `AmixLauncher.vbproj`
  (WinForms front end that starts the daemon and opens the console). Visual Studio 2022, x64.
- **WebAssembly** (`sdk/wasm`): `amix_dsp.c` is a freestanding mixer core (16 strips, 4-band EQ, pan, mute / solo, master limiter, peak
  meters, its own libm) built with plain `clang --target=wasm32` into a 7 KB `amix_dsp.wasm`; `amix.mjs` loads it in a browser or Node.
  It carries the same algorithms as the native engine for a local web mix; buses, loudness, plug-ins and devices stay native.

## The daemon's HTTP API

`amixos-daemon` (`engine/cpp/app/daemon`) serves the engine on `127.0.0.1:8765`: `GET /api/status`, `GET /api/devices`, `POST /api/op`
with `{"op": "...", ...}` for `open`, `stop`, `gain`, `pan`, `mute`, `solo`, `master`, `limiter`, `eq`, `compressor`, `gate`, `delay`,
`polarity`, `send`, `busLevel`, `busMute`, `busReturn`, `trim`, `monitor`, `inRoute`, `outRoute`, `name`, `clearClips`, and a WebSocket
meter stream. A token (`--token-file`) guards every call. `--allow-lan` serves plain HTTP to the LAN: use it only behind your own TLS
proxy (`deploy/nginx`). The Expo remote (`remote/`) is its reference client.

## Packages

| output | how | where |
|---|---|---|
| `.exe` (Windows) | `windows/vs/AmixOS.sln` in Visual Studio, or `node scripts/build-native.js` with MinGW, or the `windows` Actions job (Qt console + daemon) | `windows/vs/out/Release`, `dist/` |
| `.apk` / `.aab` (Android) | `android/`: `./gradlew assembleRelease bundleRelease`, or the `android` Actions job | `android/app/build/outputs` |
| `.tar.gz` (Linux) | `scripts/package-linux.sh` (daemon, libamix, header, Perl module, WASM core, sha256) | `dist/amixos-<ver>-linux-<arch>.tar.gz` |
| `.dmg` (macOS) | the `macos` Actions job (`macdeployqt`) | `dist/amixos-macos.dmg` |

## CDN and updates

Release files are published under `releases/` with `update.json` signed by the vendor key (`scripts/make-update.js`); installers and the
daemon verify the signature (`engine/cpp/app/update_logic.hpp`) before applying anything, and only hosts on the allow-list are fetched.
Serve `releases/` and the website from a CDN as static files; nothing in the engine, the console or the SDK phones home on its own.

## Versioning

`native/amixos/version.txt` is the single version: the console, `amix_version()`, the Perl / Rust crates, the Android `versionName`
and the tarball name read it. The C ABI level is `AMIX_API_VERSION`; it only changes when a signature changes.
