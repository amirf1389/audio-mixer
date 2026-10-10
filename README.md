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
   with `service uninstall`. It uses a registry Run entry on Windows (no scripts; the installed signed launcher starts the server without a console window), a LaunchAgent on macOS and a systemd user service on Linux, needs no
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
| `npm run build:installers` | build and sign the Windows installers (`.exe`, x64 / x86 `.msi`), see "Installers" below |
| `npm run build:unix` | build the Linux `.deb` and the macOS `.app` package |
| `npm test` | run the server tests |

Copy the built folder (or the archive) to any PC with Node.js 18+ and run `start-pc-mode.bat` (Windows) or `./start-pc-mode.sh`.

### Installers: Windows, Linux, macOS
`npm run build:installers` builds the signed Windows installers into `dist/`; `npm run build:unix` builds the Linux and macOS packages. Prebuilt files are in `releases/`.
Windows build needs `wixl` (`apt install wixl`), `gcc-mingw-w64-i686`, `osslsigncode` and internet once (official Node.js runtimes, checked against nodejs.org's `SHASUMS256.txt`,
and the Audify prebuilt binaries, pinned by SHA-256).

| File | Installs to | Notes |
|---|---|---|
| `Audio Mixer-1.7.0.exe` | `C:\Program Files\Audio Mixer` | signed setup for 64-bit Windows; checks the embedded package's SHA-256, then runs Windows Installer. `/quiet` silent, `/passive`, `/scan` (run the verification scan after installing), `/uninstall`, `INSTALLDIR="D:\Audio Mixer"` |
| `AudioMixer-1.7.0-x64.msi` | `C:\Program Files\Audio Mixer` | 64-bit Windows Installer package, all users (administrator), `msiexec /i ... /qn` |
| `AudioMixer-1.7.0-x86.msi` | `C:\Program Files (x86)\Audio Mixer` | 32-bit package with 32-bit Node.js (v22 LTS, the last line with a 32-bit build) and 32-bit Audify, for 32-bit Windows or 32-bit audio hosts |
| `audio-mixer_1.7.0_all.deb` | `/opt/audio-mixer`, `/usr/bin/audio-mixer`, `/usr/share/applications`, `/usr/lib/systemd/user` | Debian / Ubuntu / Mint; installs Node.js 18+ on first start when it is missing; remove with `apt remove audio-mixer`; start at login: `systemctl --user enable --now audio-mixer` |
| `AudioMixer-1.7.0-macos.tar.gz` | `/Applications/Audio Mixer.app` (or `~/Applications`), login item `~/Library/LaunchAgents` | double-click `install.command` / `uninstall.command`; installs Node.js 18+ when it is missing; not notarized |

Every Windows installer carries the mixer page, the Node.js server and client, the bundled Node.js runtime, Audify (ASIO / WASAPI / DirectSound), the verify scan, Start Menu entries
(*Audio Mixer (PC mode)*, *local server only*, *Verify installation*, *Plugins folder*, *Uninstall Audio Mixer*) and an entry in *Settings > Apps* (Add or remove programs) with a working Uninstall.
Features (`ADDLOCAL=Main,Shortcuts,Autostart,Desktop`): the default is everything except the desktop shortcut; autostart runs the server hidden at login for all users. Uninstalling removes the
program files, shortcuts and autostart entry; downloaded drivers (`%USERPROFILE%\AudioMixerDrivers`) and plugins (`%USERPROFILE%\AudioMixerPlugins`) stay. A newer package of the same kind upgrades an older one.
The no-administrator flavour (`%LOCALAPPDATA%\Programs\AudioMixer`) is still available: `node scripts/build-msi.js --scope user`. Where other systems keep things: Linux app files are root-owned
under `/opt`, user data stays in `~/AudioMixerDrivers`, `~/AudioMixerPlugins` and `~/.vst3`; macOS uses `/Applications`, `~/Library/LaunchAgents` and `~/Library/Audio/Plug-Ins/VST3`.
The installer technology is Windows Installer (MSI) built with wixl, not the commercial InstallShield product, and the NSIS script is gone.

**Signing.** The Windows files are Authenticode-signed with `scripts/sign.js`. Without your own certificate the build creates a self-signed one ("Audio Mixer (self-signed)"; the key stays in
`dist/cache/signing`, git-ignored) and publishes the public part as `releases/AudioMixer-signing.cer`. That proves the file was not changed after it was built and lets the verification scan
pin the publisher, but Windows still shows "unknown publisher" / SmartScreen until you trust the certificate (right-click the `.cer` > Install, "Trusted Root" and "Trusted Publishers") or
sign with a commercial certificate (`SIGN_PFX=cert.pfx SIGN_PFX_PASSWORD=... npm run build:installers`; `SIGN_TIMESTAMP_URL=http://timestamp.digicert.com` adds a timestamp). Thumbprint of the
published certificate: see `releases/AudioMixer-signing.cer`. The Linux and macOS packages are not signed.

**Layout fixes (1.7.0).** The EQ band faders and the DCA / VCA master faders no longer grow to the height of the page and spill out of their cards (the slider now fills its own track). On phones the 10 EQ faders get a usable height and the RTA badge no longer overlaps the curve title, the mixer toolbar wraps instead of scrolling sideways, and the ASIO DRIVER strip can be folded to one line with its chevron button (remembered; folded by default on screens shorter than 700 px).

**Automatic installation of Node.js and the audio runtime.** The Windows `.exe` / `.msi` already contain Node.js. Everywhere else a missing prerequisite is installed for you, after asking
(`AUDIO_MIXER_YES=1` skips the questions):
- Linux `audio-mixer`, macOS `Audio Mixer.app` and `./start-pc-mode.sh` (`ensure-node.sh`): when Node.js 18+ is missing they download the official Node.js 22 build from nodejs.org, check its SHA-256
  against nodejs.org's `SHASUMS256.txt` (a mismatch installs nothing) and unpack it into `~/.local/share/audio-mixer/node`; no administrator rights, nothing outside your home folder. The Audify native
  audio module (prebuilt, no compiler) is installed once into `~/.local/share/audio-mixer/modules`; `AUDIO_MIXER_NO_NATIVE=1` skips it.
- Windows `start-pc-mode.bat` (portable folder): installs the official Node.js LTS with `winget`, or the checksum-verified zip from nodejs.org into `%LOCALAPPDATA%\AudioMixer\node`.
- Windows setup `.exe`: installs the Microsoft Visual C++ runtime (needed by Audify, ASIO / WASAPI) with `winget` when it is missing (`/novcredist` skips it); if that fails the mixer still works with browser audio
  and the message says where to get it. `node client/cli.js doctor` explains why the audio module did not load; `node client/cli.js setup --user` installs Audify by hand.

**Universal ASIO driver and driver meters (1.5.0).** `GET /api/universal` lists every input and output source on the PC across ASIO, WASAPI, Core Audio, JACK, ALSA ... (PortAudio and Audify), ranked ASIO first with
loopback devices last, per-channel labels (`IN 1-2` ...), the automatic best input / output (a duplex interface is kept together), and a `changeId` that changes when an interface is plugged in or removed.
`deviceId: "universal"` (or `universal: true`) in a stream `start` message opens the best device on whichever engine has it; the page offers it first in the input / output lists and uses it by default.
The mixer page has an **ASIO DRIVER** strip above the channels: the universal driver's detection (refreshed every 5 s; a new interface triggers a re-scan) and, for every open native stream, per-channel RMS bars,
a peak-hold tick, the peak in dBFS, a clip light and the driver / buffer size / latency. The levels are measured by the server on the audio that is really written to, or read from, the driver (`{type:"levels"}` on the stream's WebSocket, about 12 per second).

**Antivirus false positives.** Installers that unpack a payload and start `msiexec`, or programs that start another program in the background, are what heuristic scanners look at,
and a self-signed file has no reputation yet. To keep the footprint plain: nothing installs a script (the old hidden `wscript` launcher and the `cmd /c` shortcut are gone), nothing is written
to the Startup folder (autostart is one registry Run value that starts the signed native `AudioMixerServer.exe`), all files live under Program Files, the setup program only runs `msiexec` on its own
checked payload, and every file is listed in `MANIFEST.sha256`. Source for both native programs is in `installer/`. If a scanner still flags a file, compare it with `verify`, then report it
as a false positive to the vendor (Microsoft: https://www.microsoft.com/wdsi/filesubmission); a certificate from a public CA (`SIGN_PFX`) removes most of these warnings.

### Windows verification scan
`node client/cli.js verify [--scan]` (Start Menu: *Verify installation (security scan)*) checks an install: every file against `MANIFEST.sha256` (changed, missing and unlisted code files, including
native files under `bridge/node_modules`, are reported), the Authenticode signature of the bundled Node.js runtime (must be the OpenJS Foundation), that the server listens on loopback only, and with
`--scan` runs a Microsoft Defender custom scan. `node client/cli.js verify "Audio Mixer-1.7.0.exe" [--scan]` (also `.msi`) checks a downloaded installer: PE structure, the embedded package and its SHA-256,
the `.sha256` file next to it, the signature (a self-signed signature passes only when it matches `AudioMixer-signing.cer` next to the file, or `AUDIO_MIXER_SIGNING_CER`) and Defender. Exit code 0 = verified.
Manual check in PowerShell: `Get-FileHash ".\Audio Mixer-1.7.0.exe" -Algorithm SHA256` and `Get-AuthenticodeSignature ".\Audio Mixer-1.7.0.exe"`.
Tested on Linux (hashes, packaging, signature verification with osslsigncode); the Windows-only parts (running the setup, Authenticode and Defender checks, uninstall) are untested on a real PC.

### Live interface (1.4.0 and 1.5.0)
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

## Consumer edition: license keys, plans, plugins and OTA updates (1.7.0)

- **Plans.** Without a key the app runs as **BASIC (8 channels)**. **PRO** unlocks 16 channels, the plugin manager and OTA downloads; **STUDIO** unlocks all 32 channels. Channels above the plan are hidden and silent; the bank buttons, the LIVE SOURCES auto-patch and the plan chip (next to the FOH button) follow the plan.
- **Activation.** LICENSE tab → paste the key (`AMIX1.…`) → ACTIVATE. Keys are signed offline (ECDSA P-256); the page checks the signature with WebCrypto and the local server re-checks it with Node `crypto`, then stores the key in `~/.audio-mixer/license.json` (`BRIDGE_LICENSE_FILE` overrides). A key can be bound to one computer with the machine code shown in the LICENSE tab, and can expire. Endpoints: `GET /api/license`, `POST /api/license/activate|deactivate`.
- **Vendor tool.** `node scripts/license.js init` creates the vendor key pair (kept outside the repository, `--dir` / `AUDIO_MIXER_VENDOR_DIR`; never commit `private.pem`), `embed` writes the public key into `index.html` and `bridge/license-public.json`, `issue --plan pro --name "Name" [--email --days 365 --machine XXXX-… --seats N]` prints a key, `show <key>` decodes one.
- **Plugins.** PLUGINS tab lists `.vst3`, `.dll` and `.vst` files found in the plugin folders (PE-header validated; never loaded or run). PRO and STUDIO.
- **OTA updates.** `GET /api/update` fetches `releases/update.json` (signed manifest; default `https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json`, override with `BRIDGE_UPDATE_URL`, extra hosts with `BRIDGE_UPDATE_HOSTS`). `POST /api/update/download` (PRO / STUDIO) saves the installer for this platform to the downloads folder after its SHA-256 matches the manifest; it is never run automatically. Publish with `node scripts/make-update.js --dir <vendor dir>`.
- Limits: the plan is enforced in the client, so it stops forged keys, not someone who edits the code.

## Read + write on one interface, C++ and VBScript helpers for Windows, security hardening (1.7.0)

- **READ + WRITE (duplex).** An ASIO driver serves one client, so reading and writing the same interface with two streams failed. With both switches on, the page now opens one duplex stream (`WS /ws/duplex`, `bridge/duplex.js`; PortAudio and Audify): the interface shows "READING via ASIO duplex" and "WRITING via ASIO duplex". Switching one side off reopens the other on its own. Other host APIs (WASAPI, Core Audio, ALSA) keep separate streams.
- **C++ (Windows).** `native/win/AudioDevices.cpp` is a small WASAPI endpoint lister (`AudioDevices-x64.exe` / `-x86.exe` are built from it with MinGW, see the header of the file). When neither PortAudio nor Audify is installed, `GET /api/interfaces` still lists the Windows inputs and outputs through it (`engine: "wasapi-native"`; listing only, opening streams still needs Audify / PortAudio).
- **VBScript (Windows).** `Audio Mixer.vbs` is a double-click launcher that opens `start-pc-mode.bat` in a normal visible window. `native/win/audio-devices.vbs` (WMI) supplies the sound device names when PowerShell is blocked. Windows 11 24H2+ may have VBScript turned off (optional feature): use `start-pc-mode.bat` then.
- **Security.** See `SECURITY.md`: response headers, static file allow-list, rate and size limits, WebSocket caps, per-hop redirect checks for downloads.
