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
  block, start page, licence key) -> OPERATOR (PIN, profile) -> UPDATES -> DONE; shows until finished or skipped, again after RERUN
  SETUP (SETTINGS / DIAGNOSTICS) or FACTORY RESET. **UPDATES** (`Update.*`): the signed manifest (ECDSA P-256 over the payload text,
  vendor key) is verified before it is parsed, only the project's hosts are allowed, a download is sha256-checked and saved, never
  run. The product is **AmF X1 by AMFB Audio** (Theme.product / Theme.vendor; header, title, HOME, setup). NOTE: releases/update.json is
  still signed with the previous vendor key; re-sign it with scripts/make-update.js, or every client (console, native --update) rejects it.

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
