# A mix OS — the native console

A mix OS is the native successor of the AmF X1 web console: a **Qt 6 Quick** interface on the **C++20 engine** in `engine/cpp`
(`am::Runtime`: ASIO and WASAPI on Windows, Core Audio on macOS, ALSA on Linux, a timed `null` test device everywhere;
`am::dsp`: the per-channel DSP). No browser, no web runtime, no Node.js in the app.

The web console (index.html, 1.22.1.0 Beta) was retired at git tag / commit `html-app-final` (`2237603`); its pages return here one
by one as native pages. Pages that are not native yet are dimmed on the rail and say so when opened.

## What is native in 0.1.x

| Page | State |
|---|---|
| HOME | the console's start page |
| MIXER | 32 strips (name, pan, mute, solo, fader, meter), master with limiter and clip clear; pages 1-16 / 17-32 / all |
| COMPRESSOR | per channel: threshold, ratio, soft knee, attack, release, make-up; transfer curve, live input / output dot, gain-reduction meter, presets |
| NOISE GATE | per channel: threshold (3 dB hysteresis), range, attack, hold, release; transfer curve, OPEN / CLOSED lamp, presets |
| SEND TO | 8 stereo aux buses: per-channel send level PRE (raw input: monitor mixes) or POST (after fader / mute / solo: effects); bus level, mute, meter, RETURN into the master; bus k on output channels 3+2k / 4+2k when the card has them |
| GROUP FADERS | 8 DCA groups (group fader added in dB to the members, group mute) and 8 mute groups, with member pickers |
| SCENES | 16 snapshots of the whole console (STORE / RECALL / CLEAR / rename, double-click to recall); RECALL SAFE channels are left alone |
| PRESETS | a channel's DSP chain by name: 15 factory presets (voice, drums, instruments, car audio, live, effect) and your own (SAVE CHANNEL AS / APPLY / delete) |
| TONE (EQ) | per channel: polarity, delay (sub-sample, 0-50 ms), crossover (Butterworth / Linkwitz-Riley / Bessel, 6-48 dB/oct), 10-band parametric EQ (peaking, shelves, pass, notch, all-pass) with the live response curve, channel peak limiter |
| CONNECTIONS | drivers built into the copy, inputs / outputs with channels and rates, sample rate, buffer, START / STOP, block and xrun counters |
| LEVELS | peak / RMS / hold / clip on every strip and the master |
| LOUDNESS | EBU R128 momentary / short-term / integrated / range, true peak, correlation |
| SETTINGS | engine facts, saved state, porting status |

Console state (names, levels, channel DSP, sends, groups, buses, master), scenes, presets and the language are saved with `QSettings` and restored at start.

## Languages

`i18n/verified-22.json` holds the 22 languages of the web console (checked); `i18n/auto-58.json` adds 61 more as **machine translations**,
marked "auto" in the language menu (header and SETTINGS) until a native speaker reviews them. Page names, groups and the main buttons are
translated; long help texts stay English for now. Right-to-left languages (Persian, Arabic, Urdu, Hebrew, Pashto) mirror the layout.
`amixos --lang fa` starts in a language; `--smoke` checks that every language covers all page names.

## Build

Linux (Ubuntu 24.04, Qt 6.4 from apt):

```sh
sudo apt-get install cmake g++ libasound2-dev qt6-base-dev qt6-declarative-dev qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-window qml6-module-qtquick-templates qml6-module-qtqml-workerscript
cmake -S native/amixos -B build/amixos && cmake --build build/amixos -j
ctest --test-dir build/amixos --output-on-failure        # offscreen smoke test: interface loaded, null device running
build/amixos/amixos                                      # the console
QT_QPA_PLATFORM=offscreen build/amixos/amixos --shots shots   # a PNG of every native page, without a display
```

Windows x64 (the main target): install Qt 6.5+ for MSVC 2019/2022 64-bit (Qt Online Installer) and Visual Studio 2022 Build Tools, then

```powershell
cmake -S native/amixos -B build/amixos -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2019_64
cmake --build build/amixos --config Release
C:\Qt\6.7.3\msvc2019_64\bin\windeployqt --qmldir native/amixos/qml build/amixos/Release/amixos.exe
```

GitHub Actions (`.github/workflows/build-apps.yml`) does exactly this and uploads `amixos-windows-x64.zip`; its `macos` job builds
`amixos-macos.dmg` (Qt 6 clang, Core Audio backend, `macdeployqt -dmg`, not notarised: right-click > Open the first time), and its
`android` job builds the Kotlin app's APK / AAB (`android/README.md`: local mixer + remote control). On a Mac by hand: install Qt 6.5+ for macOS, then
`cmake -S native/amixos -B build/amixos -DCMAKE_PREFIX_PATH=~/Qt/6.7.3/macos && cmake --build build/amixos && ~/Qt/6.7.3/macos/bin/macdeployqt build/amixos/amixos.app -qmldir=native/amixos/qml -dmg`. The ASIO backend uses the
Steinberg ASIO SDK headers in `drivers/asio/sdk` (dual licence: GPLv3 or the Steinberg proprietary licence; this repository is MIT +
the ASIO licence agreement in the root, read it before distributing). ASIO4ALL, FlexASIO and the devices' own drivers are all ordinary
ASIO drivers to the engine: there is no separate "ASIO4ALL SDK".

## Layout

```
native/amixos/
  CMakeLists.txt     Qt 6 Quick application, links engine/cpp (audiomixer_engine + audiomixer_runtime)
  src/main.cpp       start-up, --smoke, --shots
  src/Engine.*       the QObject the interface talks to: devices, open/start/stop, strips, channel DSP, meters 30 x/s, QSettings
  qml/Main.qml       window, header, rail, page loader           qml/Theme.qml   colours and type (the AmF X1 design language)
  qml/*Page.qml      one file per page                           qml/Fader.qml, MeterBar.qml, Panel.qml, NavRail.qml
```

Real-time rules (engine): the audio thread never allocates, locks or waits; control changes travel through `am::Runtime`'s lock-free ring and
are applied at the start of the next block; the channel DSP (`engine/cpp/include/audiomixer/dsp.hpp`) is fixed-size, double precision and
has its own test (`dsp_test`).

## Roadmap

See `docs/a-mix-os-roadmap.md`.
