# A mix OS — roadmap and architecture decisions

*AMFB Audio · written 2026-11-09 · A mix OS 0.1.0 (native), AmF X1 web console retired at commit `2237603` (`html-app-final`)*

This is the working plan for turning the AmF X1 console into **A mix OS**, a native mixing console and DSP workstation. It records what was
decided, what exists, what is next, and what was asked for but is not possible as asked.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Interface | **Qt 6 Quick / QML**, one codebase for Windows, Linux, macOS, Android, iOS | GPU-accelerated, touch-ready, the repository already had a Qt Android project; no web runtime |
| Engine | **C++20**, the existing `engine/cpp` (`am::Mixer`, `am::Runtime`, backends) extended with `am::dsp` | It already did ASIO / WASAPI / Core Audio / ALSA with a lock-free control ring and shared test vectors |
| First platform | **Windows x64 .exe** (GitHub Actions builds it with Qt + MSVC, ASIO + WASAPI backends) | The user's main target; Linux is where the automated tests run |
| Android | **parked** for now | Asked for later; the Qt Android kit needs Qt's mobile tool chain |
| The web console | **removed** (index.html, boot.html, Electron / Android WebView / iOS wrappers, their installers and packaging scripts) | The user chose "remove it now"; everything is in git history at `html-app-final` |
| The Node bridge (`bridge/`) and the licence tools | kept as the **engine service and the vendor's tools** while their logic is ported | Licence keys, editions, OTA signing, driver knowledge and 136 tests live there |
| Real-time rules | no allocation, no locks, no waiting on the audio thread; parameter changes through the lock-free ring at block start; fixed-size double-precision DSP | Enforced in `am::Runtime::onBlock` and `am::dsp::Channel::process` |

## What exists (0.1.0)

- `native/amixos/`: the Qt console (HOME, MIXER, TONE (EQ), CONNECTIONS, LEVELS, LOUDNESS, SETTINGS native; the other 30 pages listed, dimmed, with a note of what each will carry).
- `engine/cpp/include/audiomixer/dsp.hpp`: per-channel polarity → delay (sub-sample, 0-50 ms) → crossover (Butterworth / Linkwitz-Riley / Bessel, orders 1-8) → 10 parametric bands (peaking, shelves, pass, notch, all-pass 1st/2nd) → peak limiter; `dsp_test` checks corner gains, slopes, LR summing, delay samples, limiter ceiling.
- `am::Runtime`: `setEqBand / setDelayMs / setPolarity / setCrossover / setChannelLimiter` per strip through the control ring; one scratch block per strip.
- Automated: engine `ctest` (vectors, dsp, runtime, stream, update_logic), `amixos --smoke` (offscreen: interface loaded, null device running), `amixos --shots DIR` (a PNG per page), GitHub Actions for Linux and the Windows x64 zip.

## Added after 0.1.0 (2026-11-09)

- MIXER upgrade: DSP badges, IN / OUT / GR meters, groups and sends readout, select-to-edit channel panel, strip colours.
- 83 interface languages (22 verified + 61 machine-translated, marked auto), RTL mirroring.
- Decision (user): the engine also runs as a **background service (headless daemon)** with a local API, and a **React Native (TypeScript)
  app** is the remote control from a phone or tablet. The Qt console stays the desktop interface. Android as a full console stays parked;
  the Kotlin app covers phones.
- **amixos-daemon** (`engine/cpp/app/daemon`): HTTP + WebSocket JSON API on 127.0.0.1:8765 (token in `~/.amixos/daemon.token`,
  `--allow-lan` for a phone, unencrypted without `deploy/nginx`), tested on the null backend (ctest `daemon`), cross-compiled for Windows.
- **Remote** (was `remote/`, React Native + TypeScript, Expo; replaced 2026-11-10 by the Kotlin remote in `android/`, pure-Kotlin client tested on the JVM against the daemon): Connect, Mixer, Channel (EQ / compressor / gate), Devices. The client
  (`src/api/client.ts`) is tested under Node against the real daemon; the app typechecks. A phone build needs Expo on a PC / Mac.
- **MIXER additions**: analogue VU meters (300 ms ballistics, 0 VU reference), MORE menu per strip, MONITOR control-room section
  (source, level, DIM, MONO, MUTE after the master meters). **ROUTING** page: inputs → strips and sources → outputs matrices
  (`Runtime::setInputRoute / setOutputRoute`), persisted with the console; daemon ops `monitor`, `inRoute`, `outRoute`.
- **Native LICENSE**: `engine/cpp/app/license` verifies AMIX1 keys and AMFXE1 edition tokens offline with the vendor public key compiled in
  (self-contained SHA-256 + ECDSA P-256, checked against Node-made vectors), machine code, store / activate / deactivate, plan gating (strips
  beyond the plan are muted and shown locked); LICENSE page. Key Generator gets a TOTP second factor (`scripts/license-totp.js`): PIN + code.
- **Driver intelligence** (`engine/cpp/src/driver_intel.cpp`): hardware match, ladder ASIO → WASAPI exclusive → shared, Realtek pin, AI
  driver locks, SMART START on CONNECTIONS.
- **ARRANGE native** (`engine/cpp/app/arrange`, `native/amixos/src/Arrange.cpp`, `ArrangePage.qml`): WAV clips on tracks, gain / pan / M / S,
  volume automation, markers, snap to bars / beats, transport through the master chain (`Runtime::ExtraSource`), 24-bit WAV export, project in
  `~/.amixos/arrangement.json`. WAV only for now (no decoder for MP3 / FLAC in the native build); no recording, by design.
- **VST3 hosting** (`engine/cpp/app/vst3`, headers in `drivers/vst3/sdk` under Steinberg's dual licence): loads .vst3 modules (Linux and
  Windows bundles, single-file DLLs), lists classes, mono / stereo main buses, lock-free parameter changes, state save / restore, a
  `Runtime::Insert` chain per strip after the built-in DSP. Tested against a self-written test plug-in (vst3_test, smoke). The PLUGIN RACK
  loads real plug-ins with generic controls; no plug-in editor window yet; macOS bundle loading not yet.
- **Settings system**, **console features** (undo / redo, A/B, stereo link, trim, shortcuts), **MIXER action bar + PLUGIN RACK + SETTINGS gear**.
- **ASIO host hardening**: reset / resync / buffer-size requests handled on a worker thread, time-info, channel clamping, latencies;
  verified by mingw cross-compile only — the first run against real drivers is on the user's Windows PC.
- **Recording** (`engine/cpp/app/record`: lock-free tap ring, writer thread, 24-bit / float WAV per armed strip + master), **auto detect /
  smart start** of the interface, **VST2 hosting** (`engine/cpp/app/vst2`, clean-room ABI from the public documentation, no Steinberg VST2
  file) and the **compact rack view**.
- **EFFECTS** (`engine/cpp/app/fx`: plate reverb, tape delay, stereo chorus, tube drive as `Mixer::BusProcessor` on each aux bus, params with
  display text, MIX, BYPASS), **WAVEFORM** (oscilloscope on master L / R / strip, timebase 1..500 ms, trigger, freeze, Vpp / Vrms / freq /
  crest) and **PHASE** (goniometer, correlation meter, master STEREO WIDTH 0..200 % mid/side). The scope reads from a `Runtime::Tap` shared
  with the recorder through a fan-out.
- **MIC EQ** (the strip's 10 EQ bands as a graphic equaliser on the ISO octave centres + high-pass, live 1/3-octave spectrum after the DSP,
  presets, COPY TO ALL), **LIVE SOURCES** (interface auto-scan, READ through the driver ladder, master / strip spectra, auto-patch; no
  web NOW PLAYING in the native build) and **TOOLS** (tap tempo, metronome click and test generator as a second `ExtraSource` summed with
  the ARRANGE player, delay tower calculator -> strip delay, scribble strips + colours, scene snapshots, diagnostic scan; the browser
  build's DAW LINK and SQL vault were mock-ups and are not ported).
- **CALIBRATE** (clock & engine, round-trip latency with the polarity pulse through a loopback cable, noise floor, loudness log to CSV,
  session: autosave every 60 s, restore, backup, crash notice), **DIAGNOSTICS** (an honest power-on self test of what the build has, SAFE
  MODE, SOFT RESTART, FAST BOOT, FACTORY RESET with preview) and **HELP** (ten steps with a button to each page, keyboard, no-sound
  checklist, where the files live). The browser build's POST screen listed SHARC / Dante / AES50 hardware the app does not have; the
  native one lists only what is real.
- Fixed: the smoke harness used two file-name strings after their scope had ended (a dangling reference since the ARRANGE smoke),
  which corrupted the heap and crashed the console at exit about one run in three on Release builds. Effect parameters are atomics now.
- **ACCOUNT / ACCESS / ADMIN** (`native/amixos/src/Access.*`): four operator profiles with a permission matrix (faders, DSP, routing,
  loudness, archive, system) that hides pages from the rail and makes the fader surface read-only; a 4..8 digit PIN stored as a salted
  SHA-256 (default 1234 only until set, and the pages say so) that locks the surface, guards the FOH MASTER role and ADMIN mode (30 min);
  an append-only audit log (~/.amixos/audit.log) with CSV export; ADMIN: PRO / CONSUMER edition, backups, audit, the honest state of
  updates (signed installer, no OTA / A/B / hardware vault: those were mock-ups in the browser build).
- **SDK and packaging** (`docs/sdk.md`): a stable C API (`sdk/c/include/amix.h`, `libamix`) over the engine; Perl XS module, Rust
  crates, Kotlin / Android app through JNI, a Visual Studio solution with a VB.NET launcher, a freestanding WebAssembly DSP core, a
  Linux tarball script, and Actions jobs that produce the tar.gz, the APK / AAB, the exe and the dmg. One engine, no duplicates.
- **AI MIX** (gain-sharing AUTOMIX per strip with weights and live shares; master AUTO LEVEL toward a LUFS target under the limiter:
  roadmap item 10), **PLUGINS** (manager: search, folders, rescan, INSERT into the selected strip), **NETWORK** (an OSC 1.0 UDP server:
  faders, mutes, solos, pans, master, buses in; meters out; this computer only unless LAN is allowed, and the page says it is
  unauthenticated). COMPRESSOR / NOISE GATE / SEND TO / GROUP FADERS / PRESETS / SCENES were long ported and are now marked so.
- **STREAM & MEDIA** (a WAV playlist player into the master: third extra source, resampled to the engine rate, gain, loop, auto-next,
  peak), **SOUND CARD** (device state, re-open at another rate / block, AUTO ASIO and SMART START switches, the driver ladder, the
  DSP profiler: block time, worst case, load %, jitter, overruns, roadmap 14; the output stage: DC removal before the limiter and
  TPDF dither at 16 / 24 bit after it, roadmap 12). output_stage_test covers the blocker, the dither grid and the profiler.
- **FACTORY SETUP** (`SetupPage.qml`, like a phone's first start): WELCOME (language) -> TERMS -> AUDIO (auto detect) -> CONSOLE (rate,
  block, start page, licence key) -> OPERATOR (PIN, profile) -> UPDATES -> DONE; shows until FINISH (no SKIP SETUP: the setup is mandatory), again after RERUN
  SETUP (SETTINGS / DIAGNOSTICS) or FACTORY RESET. **UPDATES** (`Update.*`): the signed manifest (ECDSA P-256 over the payload text,
  vendor key) is verified before it is parsed, only the project's hosts are allowed, a download is sha256-checked and saved, never
  run. The product is **AmF X1 by AMFB Audio** (Theme.product / Theme.vendor; header, title, HOME, setup). NOTE: releases/update.json is
  still signed with the previous vendor key; re-sign it with scripts/make-update.js, or every client (console, native --update) rejects it.

- **16 buses, 8 AUX + 8 FX** (`kMaxBuses` 16, C SDK `AMIX_API_VERSION` 2 / `AMIX_MAX_BUSES` 16, Rust/Perl tests follow): AUX 1-8 for
  monitor mixes, FX 1-8 for effects; SEND TO colours them, OSC takes `/bus/{1..16}/level`. **MIXER redesign**: FADERS / AUX SENDS / FX
  SENDS on the action bar flip every strip to eight mini faders with PRE/POST (sends on faders), and an INPUT chip on each strip picks
  the device input (or none) without leaving the page. **300 FX presets** (`engine/cpp/app/fx/presets.*`: 110 reverb, 100 delay, 50
  chorus, 40 drive, generated from families, every value 0..1, fx_test checks all 300 load) with a browser on EFFECTS (family tabs,
  search, one click loads kind + values + mix on the bus). **100 samples** (`engine/cpp/app/samples/*`: drums, percussion, bass, leads,
  pads, keys, FX, test signals, rendered in the program, samples_test renders all 100) on a SAMPLES page: pads, 8-voice player into the
  master (`Samples.*`, fanout slot d), keys 1-0, EXPORT writes 100 WAVs to ~/.amixos/samples. **NETWORK**: the IPv4 cards of the
  computer, bind to one card or any, the REMOTE ADDRESS to type into a controller, an ALLOWED IPs list (addresses or CIDR) that refuses
  other senders. **RTA** on TONE (EQ): the strip's 1/3-octave spectrum under the curve (MIC EQ / LIVE SOURCES already had theirs).
  **AUTO LICENCE**: the console writes ~/.amixos/key-request.json itself and watches ~/.amixos and Downloads for the vendor's key file
  (*.amixkey, *.key, key*.txt), verifies it against the vendor's public key and activates it; keys are never generated in the console.

- **MIXER strip, second pass**: the channel fader stays on the strip in the AUX / FX send views (sends beside it); an **AI chip** per
  strip switches automix (gain sharing between open channels) and shows the gain it applies right now (bar + number; right-click opens
  AI MIX); the MORE button is gone: the strip menu is on ⋯ and on a right-click anywhere on the strip; SETTINGS has STRIP toggles
  (INPUT chip, DSP badges, pan, AI chip, groups readout) for a leaner strip. **TONE (EQ)**: a scroll bar, RTA, and **EQ PRESETS**: 10
  locked factory curves (AmiX W Shape, AmiX Golden EQ, Vocal Presence, Warm Bass, Air Lift, De-Mud, Telephone, Broadcast Voice, Kick
  Punch, Acoustic Guitar) and 10 CUSTOM slots (SAVE from the channel, CLEAR), each tile drawing its curve; a band edit clears the mark.
  **ASIO pass** (`backend_asio.cpp`): every failed open releases the driver and the single-client slot (a second try no longer says
  "another one is already open"), a driver reset waits for the bufferSwitch in flight before disposing buffers, stop() does the same,
  and a stopped or resetting stream writes silence into the driver's buffers instead of stale output. Cross-compiled with mingw -Werror.

- **RTA EQ** (`RtaEqPage.qml`, page id `rtaeq`, after TONE): the beta's "10-BAND PROGRAM BUS EQ & DIAMOND RTA" on the native engine:
  PAGE 1 the DIAMOND response curve (drag a diamond, wheel for 0.5 dB) over a 64-bar spectrum behind it (the strip after its DSP, from
  the engine analyser; RTA ON/OFF, FREEZE), PAGE 2 the ten ISO faders +-12 dB, SPLIT both; the toolbar with GOLDEN EQ (Amix by AMFB
  tuned +1,+5,+3,+3,+4,+2,+2,+3,+3,+1), CUSTOM EQ / SAVE CUSTOM (slot 1), ANIX W-SHAPE, the 15 standard + 15 MIC presets, FLAT; ten
  CUSTOM PRESET USER SLOTS (LOAD / SAVE / DELETE, `geqSlots` in QSettings). The bands are the strip's own ten (TONE / MIC EQ edit the same).

- **DirectSound backend** (`backend_dsound.cpp`, backend id `dsound`): render + capture through DirectSound, 16-bit, 8 periods, no
  exclusive lock, the path every Bluetooth headset / speaker answers to; listed by `listDevices` ("{guid}" ids, "default"), in the C SDK
  (`amix.h` backend "dsound"), the VS projects, build-native.js (`-ldsound -lwinmm`) and the Windows .exe. **Driver ladder step 4** =
  DirectSound twin; the **fallback policy** (`drv::fallback` / `setFallback`, kept in the lock file: "all" = WASAPI exclusive, shared,
  DirectSound; "dsound" = DirectSound only, WASAPI optional and never a lock; "none" = ASIO only) with AFTER ASIO chips on SOUND CARD.
  **BLUETOOTH page** (ready): the endpoints whose name says Bluetooth / hands-free / A2DP / brand, DirectSound first, USE AS OUTPUT /
  INPUT, START ENGINE ON BLUETOOTH, what to expect (A2DP vs HFP, codec latency). **RTA EQ parametric control**: TYPE / FREQUENCY / Q /
  GAIN sliders for the selected band (click a diamond or B1-B10), ISO CENTRE, BAND OFF; the diamonds read the real bands.
  None of the Windows backends has run on a Windows PC from here: the UM2 run needs the user's computer (Remote Control) or a Windows build.

- **One install layout** (`engine/cpp/app/paths`, `install/layout.json`, `docs/install-layout.md`): program / data / cache / prefetch / temp per
  OS (Windows: Program Files + %APPDATA% + %LOCALAPPDATA% + %TEMP%, installer script `install/windows/amfx1.iss`; Android: filesDir + cacheDir
  through `Engine.setDirs` / `amix_set_dirs`; Linux and macOS: data next to the program when writable). Licence, driver lock, daemon token,
  audit, arrangement, key request, samples export and downloads all go through it; `~/.amixos` from earlier builds keeps working.

- **AmF X1.exe, built clean for Defender** (`scripts/build-native.js`): the engine program is now "AmF X1.exe" (+ "AmF X1 (32-bit).exe") with a
  version resource (CompanyName AMFB Audio, ProductName, description, version, OriginalFilename) and a manifest (asInvoker, Windows 8.1-11,
  DPI aware) from `install/windows/amfx1.rc`; the ASIO driver DLL is no longer embedded and written out at run time (the PUP trigger) but
  ships next to the program and `--install-driver` registers it in place (`embedDriver` stays opt-in). `scripts/sign-windows.js` signs with
  the vendor's own certificate (osslsigncode / signtool, never creates one); `docs/windows-defender.md` lists the release steps (sign, installer,
  false-positive submission). The installer script installs AmF X1.exe + the DLL into Program Files; make-update.js knows the new names.

- **The console .exe, cross-built** (`scripts/build-console-windows.sh`, `install/windows/mingw-cross.cmake`): amixos.exe for Windows x64
  from Linux with mingw-w64 and the official Qt 6.4.2 win64_mingw kit (download.qt.io reachable again; the kit's moc / rcc / qmlcachegen /
  qmltyperegistrar are replaced by the host's of the same version), with a version resource + manifest (`native/amixos/win/amixos.rc`).
  `releases/windows/AmF-X1-<version>-windows-x64.zip`: amixos.exe, amixos-daemon.exe, AmF X1.exe, the ASIO DLL, the Qt runtime (DLLs,
  qwindows platform plugin, image plugins, QML modules), qt.conf, README. Unsigned, and not run on Windows from the build machine.

- **Packages** (`scripts/build-all-packages.sh`): the Windows setup (`install/windows/amfx1.nsi`, NSIS: Program Files, shortcuts, uninstaller
  that asks before removing the data, optional ASIO driver registration), the console zip, the APK (`android/`, Gradle + NDK 26: the
  engine's `std::atomic<std::shared_ptr>` became `std::atomic_load / atomic_store` on a shared_ptr, which libc++ lacks; app name AmF X1;
  signed with the debug key until the vendor's keystore is set) and the Linux tarball, all under `releases/` with checksums. The console
  writes `<data>/console.log` (every Qt / QML message) and takes `--software` / `--opengl` for the render backend.

- **Colour and ASIO review** (asked for): every colour literal in the console is an explicit #RRGGBB / #RGB or Qt.rgba (no 8-digit hex, so no
  channel order mistakes); what was light on Windows were the stock controls (Slider, ComboBox, TextField, Menu, Dialog), which the Basic
  style paints with hard-coded light colours. The console now sets an explicit dark QPalette (every entry #RRGGBB) and uses the Fusion style,
  the one that draws from the palette; the language box carries its own background. ASIO: `CoInitializeEx(COINIT_APARTMENTTHREADED)` at
  the very start of main() in the console and the engine program, `COINIT_MULTITHREADED` on the driver reset worker; the registry is read in
  both views (`KEY_WOW64_64KEY` SOFTWARE\ASIO and `KEY_WOW64_32KEY` = WOW6432Node) so 32-bit drivers are listed with their bits and
  refused with a clear message instead of a load failure; a failed `CoCreateInstance` reports FormatMessage text + the HRESULT. Language
  system: 83 languages, RTL mirroring (LayoutMirroring from i18n.rtl); only the navigation (page and group names) is translated, the pages
  themselves are English.

- **Dual theme, orange faders, app audio**: Theme.qml has `dark` (Main.qml binds it to settings.darkTheme; a ☾ / ☀ button in the header);
  every page colour that assumed dark (209 white overlays, 28 hard-coded backgrounds) now comes from Theme.ov() / deep / chrome, and the Qt
  palette follows the switch; `--shots` renders home / mixer / dsp in the light look too. The fader 0 dB tick and the fader readout are
  orange. **APP AUDIO** (STREAM & MEDIA): every WASAPI render endpoint is also listed as the input `loopback:<id>` ("App audio: what plays on
  ..."), opened with AUDCLNT_STREAMFLAGS_LOOPBACK (shared, polled); CAPTURE INTO THE MIXER feeds a player, a browser (YouTube / YouTube
  Music), Spotify, Apple Music, TIDAL, Deezer, Roon ... into CH 1-2 and on to the ASIO / WASAPI / DirectSound output. Secure by construction:
  no account, no decoding, no DRM, nothing leaves the computer; AUTO DETECT never picks a loopback as the device.

- **Fader = input / output indicator, boot screen, auto on**: the fader track carries two level columns, cyan = the input as the driver
  delivers it (post-converter, pre-fader, `inDb` from the device block), orange = the output after the fader (`peakDb`), red above 0 dBFS or
  on a clip; the master fader shows RMS / peak, the monitor fader the control-room level. **BOOT** (`BootPage.qml`): the mark, 32 strips
  lighting up, a progress bar and the stages (interface, folders, sound cards and drivers, audio engine, licence) with what each found; at
  least 1.6 s, fades to the start page; `--smoke` skips the wait. AUTO START is on by default (the remembered device, else AUTO DETECT).
  `Engine::setNullTestInput` gives the null device a -12 dBFS test signal so `--shots` show real meters. Checks: engine tests, clang-tidy
  (bugprone + analyzer: no finding in project code), MinGW -Werror on every Windows source, console smoke with screenshots in both themes.

- **Real-time check, device-named strips, no boot screen, setup alert**: `--shots` now measures the live path on the null device's test
  input: strip 1 reads -12.0 dBFS in (as fed), out = in + fader, an unrouted strip is silent, re-routing it to input 1 brings -12.0, and the
  INPUT chip label ends in the channel. The strip INPUT chip names the open device ("UM2 · IN 3", `Engine::inputLabel`, `inputDeviceName`
  / `outputDeviceName`) with the full driver name in the tooltip. The BOOT screen is gone (asked for). An amber alert bar under the header
  appears when no sound card is remembered and the engine is idle, with FACTORY SETUP and CONNECTIONS buttons.

## Next, in order

1. **Windows .exe in hand**: run the Actions workflow, download `amixos-windows-x64.zip`, test ASIO (UM2, Focusrite, ASIO4ALL, Realtek) on the user's PC. Fix what the real drivers show.
2. **Mixer parity**: ~~compressor and noise gate per channel (curve pages)~~ done 2026-11-09; ~~sends / buses (pre / post), group faders (DCA), mute groups~~ done 2026-11-09; ~~scenes, presets~~ done 2026-11-09; the surround master pages (stereo / quad / 7.1.2) and crossfeed from the web console's math.
3. **Routing**: input → strip and master → output matrix; multi-channel outputs (the runtime writes channels 0/1 today).
4. **Licence / editions**: the AMFXE1 edition tokens and the vendor public key (`bridge/license-public.json`) verified natively (Ed25519 / ECDSA as the Node code does); hardware-bound keys; Lite / Pro tiers unlock pages and strip counts. "JWT / authenticator" from the request = signed tokens + a TOTP second factor for the vendor's Key Generator, not for end users' audio.
5. **Driver intelligence**: the AI driver lock ladder, the Realtek WASAPI pin and the ASIO4ALL default-endpoint fallback, ported from `bridge/driver-lock.js` into the runtime's device layer (in-app routing only, never OS or driver tampering).
6. **ARRANGE**: timeline, clips, automation, markers, offline WAV export (no recording), native.
7. ~~VST3 hosting~~ done 2026-11-10 (generic controls; still open: the plug-in's own editor window, macOS bundles). VST2 hosting added 2026-11-10 as a clean-room host of the public ABI (no Steinberg VST2 SDK file is used).
8. **Bluetooth** as ordinary inputs / outputs (they appear through WASAPI / Core Audio / ALSA already; the page shows codec and latency).
9. ~~Spatial / 360~~: removed 2026-11-10 at the user's request (the SURROUND page and the Resonance Audio path are gone; no spatial rendering is built in).
10. ~~Volume optimise ("no low, but loud")~~ done 2026-11-10 as AUTO LEVEL on AI MIX: a master loudness stage = loudness-normalising gain + look-ahead true-peak limiter to a target (−14 LUFS streaming / −1 dBTP), with the "quiet source" detector raising gain smoothly; never clipping, always measured on the LOUDNESS page.
11. **High-resolution engine**: 96 / 192 kHz paths are already native (double precision, device rate); add sample-rate conversion for mixed-rate sources and 32-bit float / 24-bit WAV export.
12. ~~Low noise from the media player~~ done 2026-11-10 (DC removal, TPDF dither, player gain; denormals were already flushed): dither on output when the device is 16-bit, DC removal, denormal protection, and a "player gain" stage so the OS mixer is never needed.
13. **Factory reset**: a SETTINGS / BOOT page action that wipes the saved console state (QSettings), presets and licence cache after a confirmation, and a `--factory-reset` command line switch.
14. ~~Telemetry and diagnostics~~ profiler done 2026-11-10 on SOUND CARD; the plain-language log is DIAGNOSTICS: the DIAGNOSTICS log (plain language) and the profiler (block time, jitter, xruns per device) natively; crash-safe dumps.
15. **Remote / network**: TCP / UDP control and metering (standalone Asio or Qt Network), mTLS where it leaves the machine; OSC and MIDI learn.
16. **Android / iOS** builds of the same Qt code (HAL detection and battery policy were already designed in the retired Android app: `Hal.java`, `Power.java` at `html-app-final`).
17. Later research items from the master prompt: SIMD kernels, amp modelling / convolution, ambisonics encoder-decoder with head tracking, an embedded C++ plug-in SDK, hot-swappable DSP modules, "40-bit" summing (double is 53-bit mantissa: already better), a custom lossless codec (FLAC exists; evaluate before inventing).

## Asked for, and what was done with it

- **"Use the ASIO4ALL SDK / API"**: ASIO4ALL is a driver, not an SDK. The engine talks to it like any ASIO driver through the Steinberg ASIO SDK (already in `drivers/asio/sdk`). The ASIO4ALL-specific behaviour (default-endpoint fallback, never first next to a native driver) is logic in our code.
- **Strict ASIO-only engine (the Rust / cpal prompt)**: the engine already supports an ASIO-only start (`--backend asio`) with no fallback; a *separate* Rust/N-API engine would duplicate `engine/cpp`. If the user still wants it, it goes in `engine/rust` beside the existing crate, behind the same test vectors.
- **Samsung "audio SDK"**: there is no public Samsung audio SDK for third-party apps; Samsung devices expose their codecs through the Android HAL (AAudio / Oboe), which is what the parked Android work uses.
- **Play Protect / antivirus "safe" categorisation**: not something software can do to itself; the honest route is code signing (EV certificate), a stable publisher identity and a clean history. Declined as asked; signing is on the list.
- **Realtek "prevention" by renaming DLLs / registry edits / scheduled tasks**: declined; the in-app WASAPI pin does the job without touching the OS or another vendor's driver.

## Trademarks

Dolby Atmos, DTS:X, THX, Auro-3D, Sony 360 Reality Audio, IMAX Enhanced, Steinberg ASIO and VST are trademarks of their owners. A mix OS detects and hands audio to the owners' renderers where they exist; it does not implement, decode or badge itself with them. ASIO and VST hosting follow Steinberg's licences.
