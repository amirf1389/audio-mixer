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

### Windows installer (.msi)
`npm run build:msi` builds `dist/AudioMixer-<version>.msi` from the same files as the `.exe` (needs `wixl` from msitools: `apt install wixl` on Linux,
`brew install msitools` on macOS). It is a per-user package (no administrator rights, installs to `%LOCALAPPDATA%\Programs\AudioMixer`) with the
same content: server, client, bundled Node.js, Audify, Start Menu shortcuts (including *Verify installation* and *Plugins folder*) and the
`%USERPROFILE%\AudioMixerPlugins` folder. Install with a double-click or `msiexec /i AudioMixer-1.4.0.msi`; silent: add `/qn`; remove with *Settings > Apps*
or `msiexec /x`. Features: `Main`, `Shortcuts`, `Autostart` (start the server hidden at login) and `Desktop`; default is all but `Desktop`, e.g.
`msiexec /i AudioMixer-1.4.0.msi ADDLOCAL=Main,Shortcuts,Desktop` leaves autostart off. A newer `.msi` upgrades an older one in place. It refuses to install
over the `.exe` version (uninstall that first). Unsigned, like the `.exe`: check `AudioMixer-<version>.msi.sha256` or run `node client/cli.js verify <file>`.
Built with wixl and checked by unpacking the package and comparing it to the staged files; not yet installed on a real Windows PC.

### Windows verification scan
`node client/cli.js verify [--scan]` (Start Menu: *Verify installation (security scan)*) checks an install: every file against `MANIFEST.sha256`
(changed, missing and unlisted code files are reported), the Authenticode signature of the bundled Node.js runtime (must be the OpenJS Foundation),
that the server listens on loopback only (not on any LAN address), and with `--scan` runs a Microsoft Defender custom scan of the folder.
`node client/cli.js verify path\to\AudioMixer-Setup-1.4.0.exe [--scan]` checks a downloaded installer: PE/NSIS structure, its SHA-256 against the
`.sha256` file next to it, signature and Defender. Exit code 0 = verified. Manual check in PowerShell:
`Get-FileHash .\AudioMixer-Setup-1.4.0.exe -Algorithm SHA256` and `Get-AuthenticodeSignature .\AudioMixer-Setup-1.4.0.exe`.
The installer in `releases/` is unsigned, so the signature check reports a warning, not a pass; the SHA-256 is the proof of integrity.

### Installers (.exe and .msi) and the live interface (1.4.0)
`npm run build:installers` builds both `dist/AudioMixer-Setup-<version>.exe` (NSIS wizard) and `dist/AudioMixer-<version>.msi` from one staging folder;
`build:installer` and `build:msi` build one each. The NSIS script `installer/audio-mixer.nsi` is only the recipe for the `.exe`; nothing needs it at install time.
Either installer puts everything together: the mixer page, the Node.js server and client, the bundled official Node.js runtime, Audify (ASIO / WASAPI / DirectSound),
the plugin folder, the verify scan and the Start Menu entries.

Interface changes in the page:
- **Smooth faders**: 20 ms gain glide (no zipper noise), 10x finer resolution once a fader is touched, `Shift` / `Alt` + wheel for fine moves (the plain wheel still scrolls the page),
  arrow keys / PageUp / PageDown, double-click resets (channel faders to 0 dB). The EQ bands glide as well.
- **EQ pages**: the custom user slots now show on every page, the list is unlimited (**+ NEW SLOT**, delete per slot) and scrolls, and the up / down buttons scroll the EQ page.
- **Master limiter** (DYNAMICS tab): on / bypass, ceiling, release, attack, knee, quick presets and a live gain-reduction meter; saved in the browser and applied before every output (including ASIO).
- **Live system strip** (PC MODE panel): server link round-trip time, Node.js version and uptime, Web Audio state / sample rate / latency, the open native streams (ASIO / WASAPI, engine, buffer size, latency)
  from `GET /api/status`, and the limiter. It reconnects by itself when the server comes back.

### Audify, ASIO4ALL and the plugin system (installer 1.3.0)
- **Audify (RtAudio)** is bundled in the installer with its official Windows x64 prebuilt binaries (ASIO, WASAPI, DirectSound; pinned by SHA-256 at
  build time), so ASIO works without a compiler. From source run `npm run setup:audify` (`cd bridge && npm install audify`; `npm run setup` installs
  Audify and naudiodon2). See `bridge/README.md` for the `/api/audify` and `/api/framesize` endpoints and the automatic ASIO buffer size.
- **ASIO4ALL**: the driver list can now download it. The server reads the official asio4all.org page, takes the newest installer linked there
  (https and asio4all.org only) and saves it to the downloads folder without running it. Check the file and run it yourself.
- **Plugins (.vst3, .dll, .vst)**: the installer creates `%USERPROFILE%\AudioMixerPlugins` (kept when you uninstall; Start Menu: *Plugins folder*).
  `GET /api/plugins` lists plugins from there and from the standard VST folders and checks every Windows binary (PE header and exports):
  a real VST3 exports `GetPluginFactory`, a VST2 `VSTPluginMain` / `main`; plain DLLs and 32-bit plugins are reported as rejected with the reason.
  The PLUGINS tab scanner shows the result when the server is connected. This lists and validates plugins; it does not run them yet.

## Live sources, music / mic FFT and interface auto-scan (LIVE SOURCES tab)
- **Now playing (API mode)**: with PC mode the server reads your operating system's media sessions and recognises **Spotify, YouTube, YouTube Music,
  TIDAL, Apple Music, Amazon Music, Deezer, SoundCloud, Qobuz, Pandora, VLC, foobar2000, MusicBee** and more (Windows: System Media Transport Controls,
  macOS: Spotify / Music apps and the active Chrome / Safari / Edge / Brave tab, Linux: MPRIS). No accounts, tokens or network access are used.
- **FFT spectrum for music**: *Share system / tab audio* (browser) or, in PC mode, a loopback interface (Stereo Mix, monitor, virtual cable) is used
  automatically when music starts. **FFT spectrum for mic / audio interface**: any interface that is detected is read and shown as soon as the audio
  engine runs (browser microphones need the permission once).
- **Audio interface auto-scan**: every 5 seconds and on plug / unplug the server lists each interface once, with all its host APIs (ASIO, WASAPI,
  DirectSound, WDM-KS, MME, Core Audio, ALSA, JACK). New interfaces are enabled for **READ and WRITE** automatically (switchable), **ENABLE ALL ASIO**
  turns on every ASIO interface. ASIO drivers are single-client, so when one is already open the next interface falls back to WASAPI / Core Audio / ALSA.
  WRITE sends the master mix to each enabled interface; READ can pick any input pair of multichannel interfaces.
- **Virtual mixer patch**: *Auto-patch live sources* puts each read interface on a stereo pair of channels from CH 1 and the music source on CH 31 / 32,
  so everything runs through the BUS & MATRIX mixer, EQ, dynamics and the phase / level tools.
- Endpoints: `GET /api/nowplaying`, `GET /api/interfaces` (see `bridge/README.md`). Not verified on real Windows / macOS hardware; the Linux path was
  tested with a fake D-Bus and a stubbed PortAudio.
