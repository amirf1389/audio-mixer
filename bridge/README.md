# Audio Mixer local bridge (Node.js sandbox)

Zero-dependency Node.js (>=18) helper that runs on your own PC and tells the mixer page which
native audio drivers exist (WASAPI, DirectSound, WDM-KS, MME, installed ASIO drivers on Windows;
CoreAudio on macOS; ALSA / PipeWire / JACK on Linux).

    node bridge/server.js        # then open http://localhost:8765

- Listens on `127.0.0.1` only; read-only; the API accepts only localhost / `file://` origins
  (add more with `BRIDGE_ORIGINS=https://example.com`). Port: `BRIDGE_PORT`.
- The page auto-connects (`universalAsioSdk.connectBridge()`); detected drivers get a `[DETECTED]`
  tag in the DRIVERS tab and "Auto-Detect" uses the bridge's recommendation. Without the bridge
  the page works exactly as before.
- Endpoints: `GET /api/status`, `GET /api/drivers`. Tests: `npm test` in `bridge/`.

## Web Audio ↔ ASIO and VST notifications
On connect the page shows notifications and log lines for Web Audio API status, Web Audio ↔ ASIO
(installed ASIO drivers) and VST compatibility (VST3/VST2 plugins found in the standard folders).
Without the bridge it notifies that ASIO/VST status is unknown and tells you to start it.
`GET /api/drivers` now includes `vst: { vst3: [...], vst2: [...] }`. The bridge only reports
plugins; hosting them inside the browser is not possible.

## ASIO output through PortAudio
    cd bridge && npm install      # installs naudiodon2 (PortAudio, native build; optional dependency)
    node server.js

With `naudiodon2` installed, `/api/drivers` also returns `portaudio: { hostApis, devices }`, the DRIVERS
tab gets a "PORTAUDIO HOST API OUTPUTS" group in the output device list (ASIO is pre-selected when
found), and **WRITE** / **DUPLEX** stream the mixer's master output (Int16 stereo over the
`ws://localhost:8765/ws/output` WebSocket) to that device. While live, the browser's own output is muted
and restored on stop. Without `naudiodon2` the page logs how to enable it and nothing else changes.
Tested with a stubbed PortAudio; not yet verified on real ASIO hardware. Output only (no input capture).

## VS Code
Open the repo folder in VS Code (`.vscode/` is included):
- **Terminal → Run Task**: `Bridge: npm install (ASIO / PortAudio)`, `Bridge: start`, `Bridge: test`.
- **Run and Debug → "Bridge + Mixer (Chrome)"** starts the bridge with the debugger attached and opens
  the mixer at http://localhost:8765 in Chrome (or pick the Edge entry).

## Input, system volume and the external-source spectrum
- `WS /ws/input`: the bridge captures an ASIO / WASAPI / other PortAudio input (ASIO preferred) and streams Int16 PCM
  to the page. Pick a "PORTAUDIO HOST API INPUTS" device in DRIVERS -> READ ENGINE, then **READ STREAM**.
- The READ ENGINE card shows a live **external source FFT spectrum line** (20 Hz - 20 kHz, dBFS, with peak hold) for the
  selected input (browser microphone or bridge input), and real input / output peak meters (no simulated data).
- `GET /api/volume`: system output / input volume and mute of the default devices (Windows: WASAPI endpoint volume via
  PowerShell, macOS: `osascript`, Linux: `pactl`). Shown as SYSTEM INPUT / OUTPUT VOLUME in the DRIVERS tab.
- Honest scope: this is a user-mode native engine (PortAudio -> ASIO / WASAPI host APIs). A signed kernel-mode Windows
  audio driver (WDM / KS miniport) needs the Windows Driver Kit and driver signing and is not part of this repo.
  The Windows volume reader and real ASIO / WASAPI hardware paths are not verified on a Windows machine yet.

## Security
- Loopback only; the `Host` header must be `localhost`, `127.0.0.1` or `[::1]` (blocks DNS-rebinding; add names with
  `BRIDGE_HOSTS=`). Foreign `Origin`s are refused on the API and on `/ws/output`.
- `Origin: null` (file:// pages) is accepted by default. Sandboxed iframes on any site also send `null`, so if you open
  the mixer through `http://localhost:8765`, start with `BRIDGE_ALLOW_NULL_ORIGIN=0`.
- Dotfiles (`.git`, `.github`, `.vscode`, `.env` ...) and `bridge/` sources are never served; malformed URLs get 400
  instead of crashing the process; `nosniff` / `no-referrer` / `X-Frame-Options: SAMEORIGIN` headers are set.
- WebSocket: RSV bits, oversized control frames, orphan continuation frames and messages over 1 MB (also when
  fragmented) close the connection. PCM output accepts only whole frames and standard sample rates.

### Vulnerability checker
    node bridge/audit.js                          # starts the bridge in-process and probes it
    node bridge/audit.js --url http://127.0.0.1:8765   # probes a running bridge
    node bridge/audit.js --json                   # machine-readable; exit code 1 on any FAIL

It checks origin/Host enforcement, path traversal, malformed URLs, hidden files, security headers, WebSocket origin
and frame-size limits, and statically scans the mixer page (HTML injection in notifications / Bluetooth names, PIN
lockout, third-party scripts without SRI, hard-coded PIN, `eval`). `npm test` fails if the audit reports a FAIL.

## Routing tab (mixer page)
**ROUTING** (after PATCHBAY) drives the real Web Audio graph:
- Assign each of the 32 channels to MAIN or one of 16 subgroups (individually or in bulk); subgroups have fader, mute,
  name and a live meter, and sum into MAIN before the master comp / EQ / limiter (so the bridge ASIO output follows).
- **Live input patch**: put a physical input (mic / interface, stereo, left or right) onto any channel strip.
- **Snapshots**: save / recall / delete, export and import as JSON (validated on import). Routing is restored on load.
- Respects KNOX lock and the operator "PATCHBAY & MATRIX ROUTING" permission.

## BUS & MATRIX tab (mixer page)
Real Web Audio graph, saved in the browser (localStorage) and respecting the KNOX lock / operator routing permission.
- Channel strip: input -> HPF -> trim -> polarity -> **PRE tap** -> fader -> **POST tap** (before pan) -> pan -> MAIN.
- **16 mix buses**: fader, mute, name, meter, **PRE / POST** send tap per bus, **LISTEN**, per-channel send levels (CHANNEL SENDS page, with ALL 0 dB / CLEAR / COPY).
- **8 matrix outputs** (MAIN PA, FRONT FILLS, BALCONY DELAY, SUBWOOFERS, MTX 5-8): fader, mute, polarity, **delay 0-500 ms** (also driven by the
  old delay sliders and the acoustic-delay calculator), and a crosspoint grid with levels from MAIN L/R, the 16 mix buses and the 16 routing subgroups.
- **CHANNEL INPUT**: per-channel high-pass filter (20-400 Hz), trim (+/-18 dB) and polarity.
- **LISTEN** solos a bus or matrix output on the browser output (the bridge ASIO/WASAPI program output is not affected). Matrix outputs are metered
  and auditionable; they are not yet sent to separate physical outputs.
- **SCENES**: save / recall / delete, JSON export and import (validated).
- The ROUTING tab now has 16 subgroups (was 8); the sidebar is grouped (MIX / ROUTE / METER / SOURCE / DEVICE / SYSTEM).
