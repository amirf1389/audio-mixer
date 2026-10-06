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

## Optional: real host-API listing (PortAudio)
`npm i naudiodon2` inside `bridge/` (needs a native build toolchain) makes `/api/drivers` also
return `portaudio: { hostApis, devices }` (ASIO / WASAPI / DirectSound devices with channel counts),
and ASIO devices found this way count as installed ASIO drivers. Without it everything above still
works. Not verified on real Windows hardware.
