---
name: run-audio-mixer
description: Run, build, test, and drive the Audio Mixer app (HAL-X WING mixer page plus its local Node bridge). Use when asked to start the mixer, run the app, build it, run its tests, take a screenshot of a mixer tab or the ASIO / Drivers panel, or click through the UI and check the result.
---

The mixer is one browser page (`index.html`) served by a local Node bridge (`bridge/server.js`, port 8765). Agents drive it with `.claude/skills/run-audio-mixer/driver.mjs`: a headless Chromium script that opens the page, switches tabs the way the UI does, reads state, and takes screenshots. All paths below are relative to the repo root.

## Prerequisites

Verified on Ubuntu in this container. Node 22 and the preinstalled Playwright Chromium were already present; nothing extra was installed for the run path.

```bash
node --version          # v22.x (the bridge needs Node 18+)
```

For the Windows installers only (not needed to run the app here): `bash scripts/setup-build-tools.sh`, which installs the mingw-w64 C and C++ compilers, wixl, and osslsigncode. Run it once before `npm run build:installers`.

## Build

Portable Node.js package (verified, 74 files into `dist/audio-mixer-pc`):

```bash
npm run build
```

Windows installers (verified after `bash scripts/setup-build-tools.sh`: two MSIs, the setup .exe, and `AudioMixer-signing.cer` in `dist/`):

```bash
npm run build:installers
```

Without the setup script the build fails with `x86_64-w64-mingw32-windres not found` (or, for the ASIO driver, `x86_64-w64-mingw32-g++ is needed`).

## Run (agent path)

Start the bridge in the background and wait for the port. Do not `sleep`; poll:

```bash
node bridge/server.js > /tmp/claude-0/bridge.log 2>&1 &
timeout 60 bash -c 'until curl -sf http://localhost:8765/ >/dev/null; do sleep 1; done' && echo SERVER UP
```

Then drive it. Run every command from the repo root:

```bash
node .claude/skills/run-audio-mixer/driver.mjs open                  # page title + console errors + open.png
node .claude/skills/run-audio-mixer/driver.mjs tab dsp dsp.png       # switch tab, screenshot
node .claude/skills/run-audio-mixer/driver.mjs asio asio-hero.png    # Drivers tab: ASIO status panel as JSON + panel screenshot
node .claude/skills/run-audio-mixer/driver.mjs eval 'window.universalAsioSdk.pcAuto()'   # run JS in the page
```

Screenshots land in `.claude/skills/run-audio-mixer/shots/` (git-ignored). Each command ends with `errors: none` or the list of page and console errors, so check that line before trusting the result. Tab ids include `mixer`, `dsp` (the 10-band EQ page), `asio` (Drivers), `io`, `routing`, `sys`; the full list is the `id:` values in the tab array in `index.html` near line 930.

The driver hides the first-run "Factory Hardware Setup" overlay (`#setup-wizard`) before it does anything else. That is a test convenience, not something a real user can skip with this overlay on screen.

## Run (human path)

`node bridge/server.js` (or `npm run server`), then open http://localhost:8765 in a browser. On Windows, `start-pc-mode.bat` does the same thing from the packaged build. Headless containers can't show this.

## Test

```bash
cd bridge && npm test
```

Currently 184 tests: 182 pass, 1 fails, 1 skip. The failure is `repository layout` (`bridge/test.js:1615`): commit `27e3249` added a `docs/` folder that the test's expected top-level list does not include. The one skip is environment-dependent (no ALSA development headers or C compiler for the Linux plugin test). The Asio tree is vendored in `native/asio/`, so no clone is needed for tests.

## Stop

```bash
lsof -ti:8765 -sTCP:LISTEN | xargs -r kill
```

## Gotchas

- **The bridge must be running for the page to show the real state.** Without it the page shows `PC MODE OFF`. The bridge's own log is at `/tmp/claude-0/bridge.log` if you redirect it as above. The `ERR_CONNECTION_REFUSED` console messages in that state are expected; the driver filters them out.
- **Boot takes about 8 seconds.** The driver waits for `window.switchTab` and then a fixed boot delay. Screenshots taken earlier show the boot splash ("FACTORY HARDWARE SETUP" or the logo), not the app.
- **The ASIO panel has no real driver here.** This container has no ASIO and no `audify` or `naudiodon2`, so the Drivers tab reads `NO ASIO DRIVER FOUND` / `NATIVE ENGINE: NOT INSTALLED`. That is the correct state for this machine. `ASIO ACTIVE` needs a Windows machine with an ASIO driver; it has not been run.
- **The bridge is a separate process from the page.** `pkill -f` on a broad pattern can match your own shell. Stop it by port, as above.

## Troubleshooting

- `EADDRINUSE` on 8765: an earlier bridge is still running. Stop it with the port command above, then start again.
- `Cannot find module 'playwright'`: the driver falls back to the global npm root, which is where the container's Playwright lives. Check with `npm root -g`.
- `x86_64-w64-mingw32-windres not found` or `x86_64-w64-mingw32-g++ is needed`: only for `npm run build:installers`. Install the toolchain with `bash scripts/setup-build-tools.sh`.
- Empty or splash-only screenshot: the page was captured during boot. Re-run the command; it waits for the boot delay.
