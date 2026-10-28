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
(ASIO drivers going undetected or detected-then-lost, RtAudio opening the wrong host API). This addon therefore
does not open drivers at all: it only lists them. All ASIO streams open through `bridge/audify.js`, under the lock.

## Status

- `asio.listDrivers()` — works today (Windows only). Read-only: lists the driver names under
  `HKLM\SOFTWARE\ASIO` in **one** registry view (whichever bitness this binary is built as). It cannot open a
  driver and cannot conflict with anything.
- There is no stream-open entry point. The native stream-open stub was removed; real ASIO streams are opened by
  `bridge/audify.js` (RtAudio) and `engine/cpp`'s own ASIO backend.

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
```

Not buildable or testable here: this sandbox has no Windows node headers / MSVC toolchain, and `node-gyp` needs
the real target Node's headers to link against. The N-API surface (`src/main.cc`) and the portable half of
`src/asiosdk.cc` do compile on Linux against this machine's own Node headers as a structural smoke test (see
`test.js`); the Windows-only registry code in `src/asiosdk.cc` is behind `#ifdef _WIN32` and is untested.

