# Audio Mixer drivers: virtual audio devices for every operating system

Each system's folder holds a small driver that adds a virtual audio device called **Audio Mixer** to one operating system's audio architecture, so other programs can send audio into the mixer and record from it:

```
 an application ── plays ──▶  [ Audio Mixer virtual device ] ──▶  mixer: LIVE SOURCES → READ   (an input source, like any interface)
 an application ◀─ records ─  [ Audio Mixer virtual device ] ◀──  mixer: LIVE SOURCES → WRITE  (the mixer's output)
```

| Folder | System | Architecture | Built here | Tested here |
|---|---|---|---|---|
| `windows/asio-driver/` | Windows (x64 and x86) | **ASIO driver** (in-process COM DLL, `HKLM\SOFTWARE\ASIO`) | yes (mingw-w64, `-Werror`) | structure only (exports, imports, interface layout); **not on a Windows ASIO host** |
| `linux/alsa-plugin/` | Linux (ALSA; PulseAudio, PipeWire and JACK through their ALSA bridges) | **ALSA PCM plugin** `audiomixer` (user space, no kernel module) | yes | **yes: `aplay` and `arecord` against the real bridge, in real time** |
| `macos/coreaudio-driver/` | macOS (Apple silicon and Intel) | **Core Audio AudioServerPlugIn** (`Audio Mixer.driver`, user space) | no (needs Xcode) | no |
| `android/hal/` | Android (AOSP builds, device makers) | **audio HAL module** `audio.audiomixer.default` (legacy `audio_hw_device` API) + audio policy + SELinux snippet | no (needs the AOSP tree) | no (the app side of it, `/ws/virtual` in the Android engine, is tested on a JVM) |
| `drivers/common/` (this folder) | all | `am_link`: the connection of a driver to the mixer (C99, Winsock or POSIX sockets) | yes | **yes: a real native client against the real bridge** |

iOS has no driver architecture for third-party virtual devices (apps cannot add system audio devices), so there is none. Linux, Android and macOS drivers follow the same pattern as the Windows one; adding another system (a vendor audio stack, JACK, a BSD) means a new folder that uses `am_link`.

## How a driver talks to the mixer

All drivers use `drivers/common/am_link.c`, which speaks the **virtual device protocol** of `bridge/virtual.js` (and of `MiniBridge.java` in the Android app): a WebSocket to `ws://127.0.0.1:8765` (8765 to 8774 are tried, or `AUDIO_MIXER_PORT`) on path `/ws/virtual`.

1. The driver sends `{"type":"hello","name":"Audio Mixer","channels":2,"rate":48000}`; the mixer answers `{"type":"ready","id":9000}`. (A second driver with the same name, 1 to 32 channels, 8 to 384 kHz are the rules; errors come back as `{"type":"error","message":...}`.)
2. Binary frames **from** the driver are interleaved signed 16-bit little-endian PCM that applications played to the device. The mixer reads them as an input source.
3. Binary frames **to** the driver are PCM that the mixer sends to the device (WRITE). Applications that record from the device get it.
4. When the driver disconnects, the device disappears and the readers are told. The mixer converts channel counts and sample rates, so a DAW running at 96 kHz works with a mixer at 48 kHz.

The device then shows up like any interface: `GET /api/virtual` lists the connected drivers, `GET /api/interfaces` contains it (id from 9000, API "Audio Mixer Virtual"), and LIVE SOURCES opens it with READ / WRITE. The connection is loopback only: `/ws/virtual` refuses any request that carries an `Origin` header (a web page cannot pose as a driver), and the `Host` check of every bridge route applies.

Real-time rules the drivers follow: no network work in the audio callback (the link has its own thread), no blocking when the mixer is not running (the device keeps its clock and is silent; the driver reconnects about once a second).

## Install (each one is a deliberate step; the Audio Mixer setup programs install no driver)

### Windows: ASIO driver
Get `AudioMixer-<version>-asio-driver-windows.zip` (built by `node scripts/build-drivers.js`), unpack it somewhere permanent, run `register.bat` as administrator (it runs `regsvr32` on the DLLs), start Audio Mixer, and choose **Audio Mixer** as the ASIO driver in your program. `unregister.bat` removes it. Details in the zip's `README.txt`. The interface was implemented from its published description; no Steinberg SDK file is included ("ASIO" is a trademark of Steinberg Media Technologies GmbH).

### Linux: ALSA plugin
```
sudo apt install libasound2-dev           # Debian / Ubuntu   (Fedora: alsa-lib-devel)
cd linux/alsa-plugin && make && sudo make install
cat asound.conf.example >> ~/.asoundrc    # or /etc/asound.conf
aplay -D audiomixer music.wav             # plays into the mixer      arecord -D audiomixer -d 10 x.wav   records what the mixer sends
```
PulseAudio / PipeWire programs: `pactl load-module module-alsa-sink device=audiomixer` (and `module-alsa-source` for recording).

### macOS: Core Audio plug-in
On a Mac with the Xcode command line tools: `cd macos/coreaudio-driver && make && sudo make install` (copies `Audio Mixer.driver` to `/Library/Audio/Plug-Ins/HAL` and restarts `coreaudiod`). The bundle is ad-hoc signed; on current macOS versions system extensions of this kind may need a Developer ID signature and notarization, or "Allow" in System Settings. **Not compiled or run in this repository**: expect to debug it (`log stream --predicate 'process == "coreaudiod"'`).

### Android: audio HAL
For device makers and custom ROMs: copy `android/hal/` and `drivers/common/` into the AOSP tree, add `audio.audiomixer.default` to `PRODUCT_PACKAGES`, include `audio_policy_configuration_audiomixer.xml` in the audio policy configuration, and add the SELinux lines of `sepolicy/` after review (a HAL process normally may not open loopback sockets). The Audio Mixer app's engine accepts the HAL on `127.0.0.1:8765` while its foreground service runs. Ordinary phones cannot load a HAL without root and a custom image. **Not compiled or run here**; the HAL API changes between Android releases (Android 14 and later use the AIDL HAL; the legacy module loads through the wrapper), so treat `audio_hw.c` as a template.

## Build everything this machine can

```
node scripts/build-drivers.js              # needs mingw-w64 for the ASIO DLLs; writes windows/releases/AudioMixer-<version>-asio-driver-windows.zip (add --source for ...-drivers-source.tar.gz)
cd bridge && npm test                      # builds am_link and the ALSA plugin with -Werror and runs them against the bridge; cross-compiles and inspects the ASIO DLLs
```

## What is not covered

- No driver was tried in the program it is made for (a DAW, a Mac, a phone): real hosts differ, so first reports from real use are expected to find problems.
- The virtual device is 2 channels. The protocol allows up to 32; the drivers would need larger channel counts and a bigger buffer ring.
- Latency is that of a buffer (64 to 2048 frames on Windows) plus the loopback socket; there is no sample-accurate clock sync between the OS device and the mixer, only the drivers' own pacing.
