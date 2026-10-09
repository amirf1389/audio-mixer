# audio-mixer
mixer


## PC mode (local system server + client)
The mixer is a web page. In **PC mode** it talks to a small Node.js system server on your own PC, which gives it real ASIO / WASAPI /
Core Audio / ALSA access, system volume, and a list of official audio drivers.

1. Install [Node.js](https://nodejs.org/) 18 or newer.
2. Start it:
   - Windows: double-click `start-pc-mode.bat`
   - macOS / Linux: `./start-pc-mode.sh`
   - Windows, server only (no browser window): double-click `start-local-server.bat`, then open `http://localhost:8765` yourself
   - or anywhere: `node client/cli.js` (same as `npm start`)

   The client starts the server on `http://localhost:8765` and opens the mixer in your browser. In the **DRIVERS** tab the *PC MODE*
   panel turns green. Without the server the page keeps working in web mode (browser audio only).
   **Enable it at login** (so the page always finds the server): `node client/cli.js service install`; check with `service status`, remove
   with `service uninstall`. It uses a Startup-folder script on Windows, a LaunchAgent on macOS and a systemd user service on Linux, needs no
   admin rights, and only starts `bridge/server.js` on `127.0.0.1`.
3. For ASIO / WASAPI input and output run `cd bridge && npm install` once (installs the PortAudio module; needs a C++ build toolchain).

Official drivers (DRIVERS tab or command line):

    node client/cli.js drivers          # official drivers for this PC, installed / not found
    node client/cli.js download flexasio # saves the official FlexASIO installer (checked with SHA-256, never run for you)
    node client/cli.js doctor           # checks Node.js, port, PortAudio, ASIO drivers, download folder

WASAPI, DirectSound, WDM-KS, MME, Core Audio and AAudio are part of the operating system. ASIO drivers come from the hardware vendor,
ASIO4ALL or FlexASIO; the list links to each official site. Installers are saved to `~/AudioMixerDrivers` (`BRIDGE_DOWNLOAD_DIR` to change)
and are never started automatically. See `bridge/README.md` for the server's API and security settings.

### Commands
| Command | What it does |
|---|---|
| `npm start` (also `npm run pc`) / `node client/cli.js` | **PC mode**: start the local system server and open the mixer. Works from the project folder and from `bridge/` |
| `npm run pc:headless` | PC mode without opening a browser window |
| `npm run server` | start only the server (`node bridge/server.js`) |
| `npm run setup` | install the PortAudio native module for ASIO / WASAPI (needs npm + a C++ toolchain) |
| `npm run drivers` / `doctor` | official drivers for this PC / health check |
| `npm run service:install` / `service:uninstall` | start the server at login / remove it |
| `npm run build` | build the portable package in `dist/audio-mixer-pc/` (page + server + client, SHA-256 manifest) |
| `npm run build:archive` | same, plus `dist/audio-mixer-pc-<version>.tar.gz` to copy to another PC |
| `npm run build:installer` | build the Windows installer `dist/AudioMixer-Setup-<version>.exe` (see below) |
| `npm test` | run the server tests |

Copy the built folder (or the archive) to any PC with Node.js 18+ and run `start-pc-mode.bat` (Windows) or `./start-pc-mode.sh`.

### Windows installer (.exe)
`npm run build:installer` builds `dist/AudioMixer-Setup-<version>.exe` with [NSIS](https://nsis.sourceforge.io/) (Windows: install NSIS; Linux: `apt install nsis`;
macOS: `brew install makensis`). It needs internet once to fetch the official Node.js LTS runtime for Windows from nodejs.org (checked against its
`SHASUMS256.txt`, cached in `dist/cache`; `NODE_VERSION=v22.x.y` pins a version).

The installer is per-user (no administrator rights) and bundles that Node.js runtime, so the target PC needs nothing installed. It installs to
`%LOCALAPPDATA%\Programs\AudioMixer` and offers: Start Menu shortcuts (*Audio Mixer (PC mode)* and *local server only*), an optional desktop
shortcut, and *Start the local server when I log in* (the autostart from `service install`, using the bundled Node.js). The uninstaller removes the
files, shortcuts and autostart entry and leaves downloaded drivers in `%USERPROFILE%\AudioMixerDrivers`. `/S` installs silently.

The installer is not code-signed, so Windows SmartScreen shows a warning ("More info" -> "Run anyway") until you sign it with your own certificate.
For ASIO / WASAPI audio run `npm run setup` once after installing (needs npm and a C++ toolchain); without it the server runs in web mode.
