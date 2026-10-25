# asio-binder — EXPERIMENTAL, NOT WIRED INTO THE SERVER

A hand-written Node.js native addon (`node-gyp` + [node-addon-api](https://github.com/nodejs/node-addon-api), N-API)
that can load the Steinberg ASIO SDK headers in `../../drivers/asio/sdk`. **Nothing in `bridge/`, `client/` or
`engine/cpp` requires or loads this package.** It exists because it was asked for directly; it is not part of the
app's normal build or install (`npm install` at the repo root or in `bridge/` never touches this folder).

## Why it is kept separate

Audio Mixer already has two working ASIO paths:

- **`bridge/audify.js`** — the local Node server's native audio, via the `audify` package (RtAudio). This is what
  `bridge/server.js` actually uses today.
- **`engine/cpp`** — the standalone native engine (`audiomixer-native`), with its own ASIO backend
  (`engine/cpp/src/backend_asio.cpp`) built against the same SDK headers this addon uses.

**ASIO drivers are single-client**: one program may have a given driver open at a time (see the header comment in
`drivers/asio/audiomixer_asio.cpp`). `bridge/asio-lock.js` exists to serialize access for the server's own ASIO
use. A third, independent path that opens a driver directly — bypassing that lock — can collide with it: both
opens fail, or one silently stops working. That was the shape of several bugs already fixed in this repo
(ASIO drivers going undetected or detected-then-lost, RtAudio opening the wrong host API). This addon is built
to not repeat that: `initializeDriver()` is a stub that refuses to run (see below) until it is deliberately wired
through the lock.

## Status

- `asio.listDrivers()` — works today (Windows only). Read-only: lists the driver names under
  `HKLM\SOFTWARE\ASIO` in **one** registry view (whichever bitness this binary is built as). It cannot open a
  driver and cannot conflict with anything.
- `asio.initializeDriver()` — **throws**. It is a deliberate no-op until someone wires it in for real (see below).
- `asio.wired` — `false`. Flip only as part of doing the wiring below, as a visible marker that it happened.

## Build (Windows only)

```
npm install -g node-gyp          # once; needs Python 3, and the Visual Studio Build Tools "Desktop development
                                  # with C++" workload (node-gyp's own docs cover this per Node version)
cd native/asio-binder
npm install
npm run build                    # -> build/Release/asio_binder.node
```

```js
const asio = require('./native/asio-binder');
asio.listDrivers();        // ['Audio Mixer', 'FlexASIO', ...] or [] if none are registered
asio.initializeDriver();   // throws: "not wired into Audio Mixer: ..."
```

Not buildable or testable here: this sandbox has no Windows node headers / MSVC toolchain, and `node-gyp` needs
the real target Node's headers to link against. The N-API surface (`src/main.cc`) and the portable half of
`src/asiosdk.cc` do compile on Linux against this machine's own Node headers as a structural smoke test (see
`test.js`); the Windows-only registry code in `src/asiosdk.cc` is behind `#ifdef _WIN32` and is untested.

## Before wiring this in for real

Whoever turns `initializeDriver()` into something real should, at minimum:

1. **Claim the driver through `bridge/asio-lock.js` first**, exactly as `bridge/audify.js` does, and release it on
   close / process exit. Opening a driver this addon has claimed from `bridge/audify.js` (or the other way round)
   is the bug this stub exists to prevent.
2. **Reuse `bridge/detect.js`'s `readAsioRegistry`** for driver listing instead of extending the single-view read
   in `asiosdk.cc` — it already reads both registry views, retries a failed read, and keeps the last known-good
   list for 10 minutes instead of flickering. Re-deriving that logic here would very likely reintroduce the bug
   it fixed.
3. Decide *why* this addon should exist alongside `bridge/audify.js` rather than extending it — a second
   from-scratch native ASIO client is a maintenance cost, so that reason is worth writing down here.
4. Flip `exports.Set("wired", ...)` to `true` in `src/main.cc` only once 1–2 are actually done, so `asio.wired`
   stays an honest signal.
