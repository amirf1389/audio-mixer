# Audio Mixer engine

The measuring and mixing code of Audio Mixer as native libraries, in several languages, **all held to the same numbers**.

| Folder | What | Language | Built and tested here |
|---|---|---|---|
| `embedded-c/` | the core: no malloc, no stdio, no threads, fixed buffers; for microcontrollers / DSPs, WebAssembly and the Apple wrappers | C99 | yes: `-Werror -pedantic`, with libm and with the built-in math (`AMC_NO_LIBM`) |
| `cpp/` | the engine as a library (`am::Mixer`, `am::Loudness`, ...) and the **runtime**: the mixer on a sound card (ALSA, WASAPI, **ASIO** (any installed driver, through the Steinberg ASIO SDK interface), Core Audio, a timed null device) | C++17 | engine and runtime: yes (null device, ALSA `null` device); WASAPI backend: compiled with mingw-w64, not run; Core Audio backend: **not compiled** (no Xcode) |
| `rust/` | the same engine as a crate (`audiomixer-engine`) | Rust (std, no dependencies) | yes (`cargo test --offline`) |
| `apple/` | Swift package + Objective-C wrapper over the C core (macOS 10.15+, iOS 13+) | Swift, Objective-C | **not compiled** (no Xcode); the Objective-C file passes a syntax check against a stub header |
| `wasm/` | the C core as WebAssembly (14 KB, no imports): **the page uses it** for the loudness meter and the scope arithmetic | C -> wasm32 | yes: the embedded binary is run against the vectors |
| `node/` | Node.js addon (N-API) of the C++ engine: the bridge's level meter, the loudness meter, the scope and the mixer | C++ | yes (parity with the JavaScript meter); optional, built with `node scripts/build-engine.js --node` |
| `tests/` | `vectors.txt` (the numbers), `gen-vectors.js` (writes it from the reference code), `reference.js`, `signals.js` | JavaScript | yes |

## What it contains

| Part | What it does | Reference it follows |
|---|---|---|
| level meter | peak / RMS (fast attack, slow release), peak hold, **clip detection** (3 samples in a row at the threshold, or one at full scale; latched; events closer than 250 ms count once) | `rtEngine` of the page |
| loudness | ITU-R BS.1770-4 / EBU R128: K-weighting at any sample rate, momentary 400 ms, short-term 3 s, gated integrated (-70 / -10 LU), loudness range, 4x true peak | `rt-lufs` of the page |
| phase | stereo correlation with width and polarity inverts | `readStereoPhase` of the page |
| scope | sweep length, trigger (level, edge), Vpp / RMS / crest / frequency | `meter-source` of the page |
| VU | needle ballistics (300 ms spring and damper) | `initVuMeterCanvas` of the page |
| Int16 meter | interleaved 16-bit PCM: peak, RMS, clip, clip runs, clipping channels | `bridge/levels.js` |
| mixer | up to 32 mono strips (gain -60..+10 dB, constant-power pan, mute, solo) -> stereo master (gain, brick-wall peak limiter), a meter on every strip and the master, loudness and phase | new (the page mixes with Web Audio nodes) |
| runtime (C++) | opens a device, runs the mixer in the real-time callback, takes control from any thread through a lock-free ring, publishes the meters with a sequence counter | new |

## The numbers are shared

`tests/vectors.txt` has 49 cases (397 numbers): loudness of sines, noise, steps and alternating levels at 8, 44.1, 48 and 96 kHz; meters with clipping; phase; scope; VU; the Int16 meter; five mixer scenes. `tests/gen-vectors.js` writes it by running **the reference code**: the loudness, meter, phase and scope code is cut out of `index.html` and run on the signals, the Int16 meter is `bridge/levels.js`, the VU formula and the mixer are written in the generator. Every engine builds the same deterministic signals (`tests/signals.js`: sines, squares, noise from a 32-bit LCG, steps, fades) and must give the same numbers: loudness within 0.02 LU, true peak 0.01 dB, meters 1e-6 relative.

```
node scripts/build-engine.js              everything this machine can build and test
node scripts/build-engine.js --c --cpp --rust --wasm --node --io --apple
node engine/tests/gen-vectors.js          rewrite vectors.txt from the reference code (the committed file is what the engines are held to)
cmake -S engine/cpp -B build && cmake --build build && ctest --test-dir build
```

## Things to know

- **Meters update once per `process` call** (one reading window per block, smoothing per window), like the page reads its analysers about every 30 ms: with blocks of 64 to 2048 samples the numbers are what the page shows; very small blocks make the release faster in time. The loudness meter closes its 100 ms blocks at the end of a feed call once 100 ms of samples are in (the page does the same with 2048-sample blocks; the mixer feeds it in chunks of 512).
- **The embedded C core keeps 10 minutes of gating blocks** (`AMC_LUFS_MAX_BLOCKS` 6000, 144 KB of state); the page, the C++ engine, the Rust crate and the WebAssembly build keep 6 hours (216000). Raise it where there is memory.
- The C core needs only `<math.h>`; define `AMC_NO_LIBM` for a build without a C library. `AMC_MAX_STRIPS`, `AMC_LUFS_MAX_BLOCKS` and `AMC_CHUNK` can be set at compile time.
- The Rust crate uses `std`; a `no_std` build would need a `libm` crate, which is not included.
- **In the page** (`index.html`, script `engine-wasm`): when WebAssembly is available the LUFS meter and the scope arithmetic run in the C core (the LUFS page then says "LIVE ... WASM"); otherwise, or if loading fails, the JavaScript versions run with the same numbers. Rebuild and re-embed with `node scripts/build-engine.js --wasm`.
- **In the bridge** (`bridge/levels.js`): when `engine/node/build/Release/am_engine.node` exists the native streams' level meter is the C++ one, otherwise the JavaScript one. The installers and packages do not contain `engine/` or the addon (it is a per-system binary): the bridge runs on JavaScript there. `AUDIO_MIXER_NATIVE=0` turns the addon off.
- The runtime's ALSA backend was tried on ALSA's own `null` device (no sound card here): the loop, the format negotiation and the recovery code ran, a real card was not heard. WASAPI, ASIO (compiled for x64 and x86 with mingw-w64) and Core Audio were not run.
- Not included: Kotlin / JNI, Python, Go bindings (the C core and the Node addon are the starting points).
