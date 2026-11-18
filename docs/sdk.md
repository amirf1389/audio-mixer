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
| `amix_mixer_*` | an **offline mixer**: create, faders, pans, mute / solo, master, limiter, sends, 16 buses (API version 2), width, `process(in, frames, L, R)`, `levels` |
| `amix_rt_*` | the **live runtime**: open a device (ASIO / WASAPI / Core Audio / ALSA / null), every control the console has (EQ, dynamics, routing, monitor), `levels` |
| `amix_license_json`, `amix_machine_id` | the licence state and this machine's code (read-only) |

Build: `cmake -S engine/cpp -B build && cmake --build build` gives `libamix.so` (Linux), `amix.dll` (Windows: `windows/vs/AmixOS.sln` or MinGW),
`libamix.dylib` (macOS) and the static twin `amix_static`. Only `amix_*` symbols are exported. `sdk_c_test` (plain C) is part of `ctest`.

### Network audio and MIDI (API version 5)

The engine carries network audio on the open **AES67** profile: RTP with L24 or L16 linear PCM, 1 ms packets, announced and found
with SAP / SDP. The same wire format carries RAVENNA and SMPTE ST 2110-30 streams, and equipment of other makers is reached through
its AES67 mode.

| Call | What it does |
| --- | --- |
| `amix_rt_net_send_start(rt, name, address, port, channels, bits, iface, announce, err, n)` | The master as one stream (1 = mono sum, 2 = master, more = the strips before their faders follow) |
| `amix_rt_net_receive_start(rt, address, port, channels, bits, rate, latency_ms, iface, err, n)` | One stream as up to `AMIX_NET_INPUTS` extra inputs: `amix_rt_set_input_route(rt, strip, AMIX_NET_INPUT_BASE + k)` |
| `amix_rt_net_stats_json(rt)` | Packets, lost, late, gaps, buffer fill |
| `amix_net_discover_json(wait_ms, iface)` | The streams announced on the network |
| `amix_net_transports_json()` | What this build can carry, and what it cannot and why |
| `amix_midi_action_default(status, d1, d2, &action)` | What a MIDI message means on the default map (CC 7 = fader, CC 10 = pan, notes = mute / solo) |
| `amix_midi_inputs_json()` | The MIDI inputs of the computer |

Limits, stated plainly:

- **No PTP clock.** The sender's media clock is the sound card's; the receiver follows each stream's own clock by resampling (0.3 % at
  most). Equipment that insists on PTP lock may refuse the sender.
- **No encryption, no password.** The audio is in clear on the local network. Nothing runs until it is switched on.
- **Dante** is its maker's own licensed protocol and is not implemented; Dante devices are reached through their AES67 mode.
  **NDI** needs its maker's SDK and runtime under their licence. **SRT** needs the open-source SRT library built in. None of the
  three is in this build, and `amix_net_transports_json()` says so.
- Network MIDI is RTP-MIDI (RFC 6295) as a session others connect to by address; it is not announced by Bonjour, and the recovery
  journal is ignored.

The C++ classes are in `engine/cpp/app/net` (`am::net::Sender`, `Receiver`, `Discovery`, `Aoip`, `RtpMidi`, `MidiPorts`, `MidiMap`),
tested by `engine/cpp/test/net_test.cpp` with a real stream through UDP on the test machine.

### ASIO, the open device and latency (API version 7)

ASIO is a Windows-only standard (the Steinberg ASIO SDK, used under its licence). On Windows, open an ASIO interface with `amix_rt_open`
and `backend = "asio"`, with `in_device` / `out_device` set to the driver name. Elsewhere the ASIO list is empty and the other backends
work as before.

| Call | What it gives |
| --- | --- |
| `amix_asio_drivers_json()` | Every ASIO driver: name, input / output channels, rates, `wrapper` (ASIO4ALL, FlexASIO and the like, not the device's own driver), `family`, `probeError` (busy in another program, unplugged). Each driver is loaded once to read it, so do not call it while one of your ASIO streams is open. |
| `amix_rt_channels(rt, &in, &out)` | The open device's channel counts. |
| `amix_rt_latency(rt, &in, &out)` | The input / output latency the driver reports, in frames, buffer included (ASIO `getLatencies`; WASAPI stream latency plus buffer; DirectSound its periods). 0 = not reported. |
| `amix_rt_latency_json(rt)` | The same in plain words: `level` (`LOW LATENCY` under 10 ms, `OK` 10-20 ms, `HIGH` above), `verdict`, and `advice` (exclusive mode, ASIO, a smaller or larger buffer, wired instead of Bluetooth). |
| `amix_rt_sample_format(rt)` | What the device itself carries, e.g. `in 24-bit integer · out 32-bit integer`. |
| `amix_rt_output_names_json(rt)` | The driver's own output channel names, when it reports them (ASIO). |
| `amix_rt_set_block_callback(rt, fn, user)` | `fn` runs on the audio thread once per block, with each strip's input and the stereo master. Never block, allocate or lock in it. Pass `NULL` to detach, and detach before `amix_rt_destroy`. |

On Android the same low-latency idea goes through AAudio: USB audio interfaces work there without drivers (see `android-sdk.md`).

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
proxy (`deploy/nginx`). The Kotlin remote (`android/`, `RemoteActivity`) is its reference client.

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
