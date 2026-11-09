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

## Next, in order

1. **Windows .exe in hand**: run the Actions workflow, download `amixos-windows-x64.zip`, test ASIO (UM2, Focusrite, ASIO4ALL, Realtek) on the user's PC. Fix what the real drivers show.
2. **Mixer parity**: ~~compressor and noise gate per channel (curve pages)~~ done 2026-11-09; ~~sends / buses (pre / post), group faders (DCA), mute groups~~ done 2026-11-09; ~~scenes, presets~~ done 2026-11-09; the surround master pages (stereo / quad / 7.1.2) and crossfeed from the web console's math.
3. **Routing**: input → strip and master → output matrix; multi-channel outputs (the runtime writes channels 0/1 today).
4. **Licence / editions**: the AMFXE1 edition tokens and the vendor public key (`bridge/license-public.json`) verified natively (Ed25519 / ECDSA as the Node code does); hardware-bound keys; Lite / Pro tiers unlock pages and strip counts. "JWT / authenticator" from the request = signed tokens + a TOTP second factor for the vendor's Key Generator, not for end users' audio.
5. **Driver intelligence**: the AI driver lock ladder, the Realtek WASAPI pin and the ASIO4ALL default-endpoint fallback, ported from `bridge/driver-lock.js` into the runtime's device layer (in-app routing only, never OS or driver tampering).
6. **ARRANGE**: timeline, clips, automation, markers, offline WAV export (no recording), native.
7. **VST3 hosting** (Windows first): the VST3 SDK host with the plug-in's own window (HWND) on a separate thread, lock-free audio hand-off; then VST2 / DLL where licences allow (VST2 SDK is no longer distributed by Steinberg: only plug-ins the user already owns).
8. **Bluetooth** as ordinary inputs / outputs (they appear through WASAPI / Core Audio / ALSA already; the page shows codec and latency).
9. **Spatial**: 360 binaural (Resonance Audio, already optional in the engine), detection of Windows spatial formats and PCM hand-off (never claiming or decoding Dolby / DTS / THX / Auro / Sony / IMAX), height panning, Cinema DSP presets, AMFB DNA.
10. **Volume optimise ("no low, but loud")**: a master loudness stage = loudness-normalising gain + look-ahead true-peak limiter to a target (−14 LUFS streaming / −1 dBTP), with the "quiet source" detector raising gain smoothly; never clipping, always measured on the LOUDNESS page.
11. **High-resolution engine**: 96 / 192 kHz paths are already native (double precision, device rate); add sample-rate conversion for mixed-rate sources and 32-bit float / 24-bit WAV export.
12. **Low noise from the media player**: dither on output when the device is 16-bit, DC removal, denormal protection, and a "player gain" stage so the OS mixer is never needed.
13. **Factory reset**: a SETTINGS / BOOT page action that wipes the saved console state (QSettings), presets and licence cache after a confirmation, and a `--factory-reset` command line switch.
14. **Telemetry and diagnostics**: the DIAGNOSTICS log (plain language) and the profiler (block time, jitter, xruns per device) natively; crash-safe dumps.
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
