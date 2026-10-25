# wasapi-exclusive — EXPERIMENTAL, NOT WIRED INTO THE SERVER

A hand-written Node.js native addon (`node-gyp` + [node-addon-api](https://github.com/nodejs/node-addon-api), N-API)
over the real Windows Core Audio (WASAPI) COM interfaces. **Nothing in `bridge/`, `client/` or `engine/cpp`
requires or loads this package.** It exists because it was asked for directly; it is not part of the app's normal
build or install (`npm install` at the repo root or in `bridge/` never touches this folder).

## Why it is kept separate

`bridge/audify.js` already opens WASAPI devices today, through the `audify` package (RtAudio). RtAudio's WASAPI
backend has exactly one share mode: **shared**. There is no `RTAUDIO_` flag or option that asks it for exclusive
mode - see `bridge/audify.js`'s own `streamFlags()` comment ("WASAPI shared mode (the only mode RtAudio has) ...
stays shared, and the result says so instead of pretending"). Getting real exclusive-mode access needs a native
WASAPI client that calls `IAudioClient::Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, ...)` directly, which is what this
addon is for - and exactly why it has to stay unwired until it can coexist safely with `bridge/audify.js`:

**WASAPI exclusive mode gives one process sole ownership of the endpoint.** Opening a device exclusively from here
while `bridge/server.js` already has that same device open in shared mode (or another program has it open
exclusively) does not degrade gracefully: the later open simply fails, or the earlier stream is silently cut off.
This is the same single-client hazard `native/asio-binder` was built to avoid for ASIO, applied to WASAPI.

## Status

- `wasapiExclusive.listEndpoints()` — works today (Windows only). Read-only: `IMMDeviceEnumerator::EnumAudioEndpoints`
  for every active render + capture endpoint, with name and default-device flag. Opens no stream, takes no
  ownership of anything, safe to call alongside the server's own streams.
- `wasapiExclusive.probeExclusiveFormat(endpointId, sampleRate, channels, bitsPerSample)` — works today (Windows
  only). Read-only: `IAudioClient::IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, ...)` against the given endpoint,
  for the exact PCM format given. This only asks the device whether it *would* accept that format exclusively - it
  activates an `IAudioClient` to ask the question and releases it immediately afterward; it never calls
  `Initialize()`, so it never actually reserves the device. Also safe alongside an existing stream. Unlike a
  shared-mode probe, exclusive mode has no "closest match": the WASAPI contract requires the closest-match
  out-parameter to be null for `AUDCLNT_SHAREMODE_EXCLUSIVE`, so the answer is a plain yes/no.
- `wasapiExclusive.openExclusive()` — **throws**. It is a deliberate no-op until someone wires it in for real (see
  below).
- `wasapiExclusive.wired` — `false`. Flip only as part of doing the wiring below, as a visible marker that it
  happened.

## Build (Windows only)

```
npm install -g node-gyp          # once; needs Python 3, and the Visual Studio Build Tools "Desktop development
                                  # with C++" workload (node-gyp's own docs cover this per Node version)
cd native/wasapi-exclusive
npm install
npm run build                    # -> build/Release/wasapi_exclusive.node
```

```js
const wasapi = require('./native/wasapi-exclusive');
wasapi.listEndpoints();                            // [{ id, name, direction: 'render'|'capture', isDefault }, ...]
wasapi.probeExclusiveFormat(id, 48000, 2, 24);     // true/false
wasapi.openExclusive();                            // throws: "not wired into Audio Mixer: ..."
```

Not buildable or testable here: this sandbox has no Windows node headers / MSVC toolchain, and `node-gyp` needs the
real target Node's headers to link against. The N-API surface (`src/main.cc`) and the portable half of
`src/wasapi.cc` do compile on Linux against this machine's own Node headers as a structural smoke test (see
`test.js`); the Windows-only COM code in `src/wasapi.cc` is behind `#ifdef _WIN32` and is untested.

## Before wiring this in for real

Whoever turns `openExclusive()` into something real should, at minimum:

1. **Decide how it coexists with `bridge/audify.js`'s own stream on the same device.** At minimum, refuse to open a
   device exclusively while the server already has a stream open on it (mirroring what `bridge/asio-lock.js` does
   for ASIO), and release the exclusive claim promptly on close so the server's normal shared-mode path still works
   afterward.
2. **Reuse `bridge/audify.js`'s device identity, not a parallel one.** `audify`/RtAudio devices and this addon's
   `IMMDevice` ids are not the same key space; a real integration needs a mapping (or to key everything off the
   WASAPI endpoint id directly) so the UI, `bridge/detect.js` and this addon agree on "the same device".
3. **Actually implement the render/capture loop** - this addon only lists endpoints and probes formats today;
   `Initialize()`, the event-driven buffer loop (`IAudioRenderClient` / `IAudioCaptureClient`, a dedicated thread,
   `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`) and clean shutdown are all still to write.
4. Decide *why* this addon should exist alongside `bridge/audify.js` rather than extending it - a second
   from-scratch native WASAPI client is a maintenance cost, so that reason is worth writing down here.
5. Flip `exports.Set("wired", ...)` to `true` in `src/main.cc` only once 1-3 are actually done, so
   `wasapiExclusive.wired` stays an honest signal.
