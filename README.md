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
| `Audio Mixer-1.13.0.exe` | `C:\Program Files\Audio Mixer` | signed setup for 64-bit Windows; checks the embedded package's SHA-256, then runs Windows Installer. `/quiet` silent, `/passive`, `/scan` (run the verification scan after installing), `/uninstall`, `INSTALLDIR="D:\Audio Mixer"` |
| `AudioMixer-1.13.0-x64.msi` | `C:\Program Files\Audio Mixer` | 64-bit Windows Installer package, all users (administrator), `msiexec /i ... /qn` |
| `AudioMixer-1.13.0-x86.msi` | `C:\Program Files (x86)\Audio Mixer` | 32-bit package with 32-bit Node.js (v22 LTS, the last line with a 32-bit build) and 32-bit Audify, for 32-bit Windows or 32-bit audio hosts |
| `audio-mixer_1.13.0_all.deb` | `/opt/audio-mixer`, `/usr/bin/audio-mixer`, `/usr/share/applications`, `/usr/lib/systemd/user` | Debian / Ubuntu / Mint; installs Node.js 18+ on first start when it is missing; remove with `apt remove audio-mixer`; start at login: `systemctl --user enable --now audio-mixer` |
| `AudioMixer-1.13.0-macos.tar.gz` | `/Applications/Audio Mixer.app` (or `~/Applications`), login item `~/Library/LaunchAgents` | double-click `install.command` / `uninstall.command`; installs Node.js 18+ when it is missing; not notarized |

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

**Layout fixes (1.13.0).** The EQ band faders and the DCA / VCA master faders no longer grow to the height of the page and spill out of their cards (the slider now fills its own track). On phones the 10 EQ faders get a usable height and the RTA badge no longer overlaps the curve title, the mixer toolbar wraps instead of scrolling sideways, and the ASIO DRIVER strip can be folded to one line with its chevron button (remembered; folded by default on screens shorter than 700 px).

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
`--scan` runs a Microsoft Defender custom scan. `node client/cli.js verify "Audio Mixer-1.13.0.exe" [--scan]` (also `.msi`) checks a downloaded installer: PE structure, the embedded package and its SHA-256,
the `.sha256` file next to it, the signature (a self-signed signature passes only when it matches `AudioMixer-signing.cer` next to the file, or `AUDIO_MIXER_SIGNING_CER`) and Defender. Exit code 0 = verified.
Manual check in PowerShell: `Get-FileHash ".\Audio Mixer-1.13.0.exe" -Algorithm SHA256` and `Get-AuthenticodeSignature ".\Audio Mixer-1.13.0.exe"`.
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

## Consumer edition: license keys, plans, plugins and OTA updates (1.13.0)

- **Plans.** Without a key the app runs as **BASIC (8 channels)**. **PRO** unlocks 16 channels, the plugin manager and OTA downloads; **STUDIO** unlocks all 32 channels. Channels above the plan are hidden and silent; the bank buttons, the LIVE SOURCES auto-patch and the plan chip (next to the FOH button) follow the plan.
- **Activation.** LICENSE tab → paste the key (`AMIX1.…`) → ACTIVATE. Keys are signed offline (ECDSA P-256); the page checks the signature with WebCrypto and the local server re-checks it with Node `crypto`, then stores the key in `~/.audio-mixer/license.json` (`BRIDGE_LICENSE_FILE` overrides). A key can be bound to one computer with the machine code shown in the LICENSE tab, and can expire. Endpoints: `GET /api/license`, `POST /api/license/activate|deactivate`.
- **Vendor tool.** `node scripts/license.js init` creates the vendor key pair (kept outside the repository, `--dir` / `AUDIO_MIXER_VENDOR_DIR`; never commit `private.pem`), `embed` writes the public key into `index.html` and `bridge/license-public.json`, `issue --plan pro --name "Name" [--email --days 365 --machine XXXX-… --seats N]` prints a key, `show <key>` decodes one.
- **Plugins.** PLUGINS tab lists `.vst3`, `.dll` and `.vst` files found in the plugin folders (PE-header validated; never loaded or run). PRO and STUDIO.
- **OTA updates.** `GET /api/update` fetches `releases/update.json` (signed manifest; default `https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json`, override with `BRIDGE_UPDATE_URL`, extra hosts with `BRIDGE_UPDATE_HOSTS`). `POST /api/update/download` (PRO / STUDIO) saves the installer for this platform to the downloads folder after its SHA-256 matches the manifest; it is never run automatically. Publish with `node scripts/make-update.js --dir <vendor dir>`.
- Limits: the plan is enforced in the client, so it stops forged keys, not someone who edits the code.

## Read + write on one interface, C++ and VBScript helpers for Windows, security hardening (1.13.0)

- **READ + WRITE (duplex).** An ASIO driver serves one client, so reading and writing the same interface with two streams failed. With both switches on, the page now opens one duplex stream (`WS /ws/duplex`, `bridge/duplex.js`; PortAudio and Audify): the interface shows "READING via ASIO duplex" and "WRITING via ASIO duplex". Switching one side off reopens the other on its own. Other host APIs (WASAPI, Core Audio, ALSA) keep separate streams.
- **C++ (Windows).** `native/win/src/AudioDevices.cpp` is a small WASAPI endpoint lister (`native/win/x64/AudioDevices.exe` and `native/win/x86/AudioDevices.exe` are built from it with MinGW, see the header of the file). When neither PortAudio nor Audify is installed, `GET /api/interfaces` still lists the Windows inputs and outputs through it (`engine: "wasapi-native"`; listing only, opening streams still needs Audify / PortAudio).
- **VBScript (Windows).** `Audio Mixer.vbs` is a double-click launcher that opens `start-pc-mode.bat` in a normal visible window. `native/win/vbs/audio-devices.vbs` (WMI) supplies the sound device names when PowerShell is blocked. Windows 11 24H2+ may have VBScript turned off (optional feature): use `start-pc-mode.bat` then.
- **Security.** See `SECURITY.md`: response headers, static file allow-list, rate and size limits, WebSocket caps, per-hop redirect checks for downloads.

**Primary Sound driver (1.13.0).** DirectSound's *Primary Sound Capture Driver* / *Primary Sound Driver* are Windows' default-device mappers, not hardware: they duplicate the real default device and often failed to open, which showed as an error on the interface list. They are now listed last as *System default input / output*, and ENABLE ALL and automatic enabling skip them (you can still switch them on by hand).

## Install layout, Bluetooth audio (1.13.0)

- **Program Files layout.** The Windows installers put `bridge\`, `client\`, `scripts\`, `deploy\` and `native\` next to `index.html` in `C:\Program Files\Audio Mixer`. `native\win\` is arranged by purpose: `x64\` and `x86\` (`AudioDevices.exe`), `src\` (C++ source), `vbs\` (VBScript). `scripts\` carries the license and update tools (`license.js`, `make-update.js`), `deploy\` the nginx / fail2ban hosting files. Editor and VCS folders (`.vscode`, `.git`, `.github`, any dotfile) are never installed: the build stops if one would be included, and a test checks it.
- **Bluetooth headsets (RtAudio).** Windows lists a headset twice: *Headphones (X Stereo)* (A2DP) and *Headset (X Hands-Free AG Audio)* (hands-free microphone, 8 / 16 kHz). They are now one interface "X": WRITE goes to the A2DP stereo endpoint, READ to the hands-free microphone, which is not opened automatically (it drops the headset to phone quality). RtAudio refused the mixer's 48 kHz on a hands-free device ("does not support 48000 Hz"); Bluetooth devices now run at their own closest rate and the bridge converts to and from the mixer's rate.

## RtAudio ASIO fixes (1.13.0)

- **One stream per driver.** An ASIO driver serves a single client, but the page (UNIVERSAL ASIO tab, LIVE SOURCES) opens reading and writing as two separate streams, which stalled or failed the first one. The bridge now keeps one registry of open ASIO devices: when the other direction of an open device is requested, the device is reopened as ONE duplex stream and the first user stays attached (same sample rate required; closing one side keeps the other running). A second request for the same direction, or any request on a device owned by `/ws/duplex`, gets a clear "already open" message.
- **No probing of a driver in use.** The page rescans every 5 s and every scan probed all APIs, including the ASIO driver that was streaming (`new RtAudio` + `getDevices`), which can glitch or kill that stream. While an ASIO stream is open the last probed device list is reused.

## Interfaces on every OS, plugin inserts for PHASE and FX (1.13.0)

- **Interface list on every OS.** Without Audify / PortAudio, `GET /api/interfaces` read nothing on macOS and Linux. The bridge now reads the interfaces from the OS itself (`bridge/sysaudio.js`): Linux from `arecord -l` / `aplay -l` (ALSA), `pactl` (PulseAudio / PipeWire) and `/proc/asound/cards`; macOS from `system_profiler SPAudioDataType -json` (Core Audio); Windows from the C++ WASAPI helper. If all of that fails, the sound devices the OS reported are listed by name. These entries are listed, not openable (`engine` says `alsa-native`, `coreaudio-native`, `wasapi-native` or `os-devices`); opening streams still needs Audify or PortAudio.
- **Plugin inserts (read / write).** The PHASE and FX pages have a PLUGIN INSERTS card (4 phase slots, 8 FX slots): pick a scanned `.vst3` / `.dll` / `.vst` per slot and bypass it. The choice is written to and read back from the local server (`GET /api/inserts`, `POST /api/inserts` with `X-Mixer-Action: inserts`, stored in `~/.audio-mixer/inserts.json`, `BRIDGE_INSERTS_FILE` overrides), so every page and every restart sees the same racks. Only plugins that passed the binary check can be chosen; writing needs the PRO or STUDIO plan. **Since 1.13.0 the slots run the plugins** through the native plugin host (below).

## Native plugin host, RtAudio ASIO / WASAPI fixes (1.13.0)

- **Native plugin host (VST 2.x: `.dll` on Windows, `.vst` on macOS / Linux).** `native/host/src/PluginHost.cpp` loads one effect plugin in its own process and processes audio the bridge sends on stdin (interleaved stereo float32 blocks, parameters, quit). `native/host/x64/PluginHost.exe` and `x86/PluginHost.exe` ship with the Windows installers (use the one that matches the plugin's bitness: the bridge picks it by the Node.js architecture); on macOS / Linux build it with `g++ -O2 -std=c++11 -o native/host/<platform>-<arch>/PluginHost native/host/src/PluginHost.cpp -ldl` or point `BRIDGE_PLUGIN_HOST` at it. The VST 2 interface is declared in the file from the public binary layout (no Steinberg code). The bridge side is `bridge/pluginhost.js` (`WS /ws/insert`).
- **In the mixer.** A plugin picked in a PHASE / FX slot (PRO / STUDIO plan, PC mode) starts automatically: the master mix runs `master limiter -> FX 1..8 -> PHASE A..D -> output`, the physical-output tap (LIVE SOURCES write) follows the end of the chain, BYPASS keeps the slot in the chain but passes the signal through, and each running plugin lists its parameters (sliders, 0 to 1). Status per slot: `STARTING`, `RUNNING`, `ERROR`. The added delay is a few 512-sample audio blocks (not measured on real hardware); if the host cannot keep up, blocks are passed dry instead of building up delay.
- **Not supported yet:** VST3 (slot shows "not supported by the native host yet"), instruments, plugin editor windows, sample-accurate automation, 64-bit and 32-bit plugins in one session (a 32-bit plugin needs the x86 host and a 32-bit Node.js).
- **RtAudio WASAPI.** A device that does not offer the mixer's sample rate (WASAPI shared mode only offers the Windows mix rate) is opened at its closest rate and the bridge converts; a device with fewer output channels than the stereo master (a mono speaker or headset) is opened with its own channel count and the master is mixed down. ASIO keeps its explicit errors, now saying where to change the rate ("set the sample rate in the ASIO driver's control panel").

## Android app (.apk, 1.13.0)

`releases/AudioMixer-<version>-android.apk` (0.6 MB, Android 7.0+ / API 24, 64-bit and 32-bit) is the mixer page in a full-screen WebView, bundled offline: Tailwind CSS, Font Awesome and the fonts are packed into the app instead of loaded from CDNs, so it starts without internet. It is signed with APK signature v2 / v3.

- **What works:** the whole mixer in web mode (Web Audio engine, EQ, dynamics, FX, scenes saved in the app), the microphone (Android asks once; only the microphone is ever granted to the page), license keys and plans (BASIC 8 channels; a PRO / STUDIO key is checked in the app), the screen stays on.
- **What needs the PC:** ASIO / WASAPI interfaces, the plugin manager and plugin inserts, OTA updates and the system volume are features of the PC-mode server (`node bridge/server.js`) and are not part of the app (the page says "start PC mode").
- **Install:** copy the `.apk` to the phone, open it and allow "install from this source", or `adb install AudioMixer-1.13.0-android.apk`. The package is `com.audiomixer.app`; the installer verifies the SHA-256 in `AudioMixer-1.13.0-android.apk.sha256`.
- **Signing key.** Releases are signed with a self-generated key (`dist/cache/signing/audiomixer-android.jks`, not in the repository). Android only installs an update over an older version when the same key signed both: keep that key, or set `ANDROID_KEYSTORE`, `ANDROID_KEYSTORE_PASS` and `ANDROID_KEY_ALIAS` to use your own (needed for Google Play, which also wants a newer `targetSdk` and an app bundle).
- **Build:** `npm run build:apk` (`scripts/build-apk.js`; sources in `android/`). Needs `javac`, `aapt`, `zipalign`, `apksigner`, `d8` or `dalvik-exchange`, an `android.jar`, and internet once for the npm packages and fonts (cache in `dist/cache/android`). On Debian / Ubuntu: `apt install aapt apksigner zipalign dalvik-exchange libandroid-23-java default-jdk`.
- **Not verified:** the APK was checked with `apksigner verify`, `aapt dump badging`, `zipalign -c` and by loading its bundled page in desktop Chromium offline (secure context, WebCrypto, 8 lanes, no page errors); it was **not run on an Android device or emulator** (none is available here), so the WebView behaviour (microphone prompt, full-screen, back button) is untested.

## iOS app, Swift helper for macOS, microphone EQ presets, RtAudio DirectSound / WASAPI / ASIO fixes (1.13.0)

- **iOS app.** `ios/` holds the Swift app (SwiftUI + WKWebView): the mixer page bundled offline (same bundle as the Android app), served by a small loopback-only server inside the app (`LocalServer.swift`, `http://127.0.0.1:47831/`, so Web Audio worklets, the microphone and license keys get a secure context and the scenes survive restarts), microphone only for that page, screen stays on, audio keeps playing in the background, audio session `playAndRecord` (speaker, Bluetooth, USB interface). `npm run build:ios` writes `releases/AudioMixer-<version>-ios-xcode-project.tar.gz` (Swift sources, XcodeGen spec, bundled page, app icon). **An iOS app can only be compiled and signed with Xcode on a Mac, so there is no `.ipa` in the releases**: on a Mac run `brew install xcodegen`, then `xcodegen generate && open AudioMixer.xcodeproj`, choose your team under Signing & Capabilities and run it on your iPhone / iPad (iOS 15+). On a Mac, `node scripts/build-ios.js --ipa` also archives an unsigned `.ipa` (re-sign it with your Apple ID, e.g. with Sideloadly / AltStore, to install). The Swift code was not compiled or run here (no Xcode available): expect to fix small compiler messages on the first build.
- **Swift for macOS.** `native/mac/AudioDevices.swift` lists the Core Audio devices (inputs / outputs, channels, sample rate, default device, transport: built-in, USB, Bluetooth ...). `install.command` builds it with `swiftc` when the Xcode command line tools are installed (`swiftc -O -o native/mac/AudioDevices native/mac/AudioDevices.swift` by hand); `bridge/sysaudio.js` uses it for the interface list and falls back to `system_profiler` when it is not built. Not compiled here either.
- **Microphone EQ presets.** The EQ page's preset menu has a MICROPHONE PRESETS group (15): male / female vocal, live singer / karaoke, handheld dynamic (SM58 style), podcast / voice-over, broadcast radio, studio condenser, headset / lavalier, guitar amp (SM57 style), acoustic guitar, kick drum, snare / toms, de-mud and de-box, de-ess, feedback safe (live stage). They are starting points across the 10 bands (31 Hz to 16 kHz), loaded like every other preset.
- **RtAudio DirectSound / WASAPI / ASIO.** (1) RtAudio reports recoverable trouble (a buffer under- or overrun on DirectSound or WASAPI) through the same error callback as fatal errors; the bridge closed the stream on the first glitch. Warnings are now counted (`warnings`, `lastWarning` on the stream) and only real errors stop it. (2) MME and DirectSound cut device names at 31 characters (`Microphone (Focusrite USB Audi`), so one interface showed up twice next to its WASAPI entry; a cut name now joins the longer name it is the start of.

## macOS app and disk image (1.13.0)

`releases/AudioMixer-<version>-macos.dmg` is the macOS disk image: open it and drag **Audio Mixer** onto **Applications** (the image holds the app, an Applications shortcut and a read-me). `AudioMixer-<version>-macos-app.zip` is the same `Audio Mixer.app` zipped, and `AudioMixer-<version>-macos.tar.gz` still has `install.command` / `uninstall.command` (installs the app, starts it at login on request, builds the Swift Core Audio helper when `swiftc` is there). The app has an icon (`AppIcon.icns`), runs on macOS 11+, and in a double-click start it finds Node.js 18+ (or offers the official build from nodejs.org, checksum verified, no administrator rights), installs the native audio module once, builds the Swift helper in the background when the Xcode command line tools are installed, starts the local server and opens the mixer in your browser.

- **Not signed or notarized** (that needs a Mac and an Apple developer account): macOS asks once. Right-click the app > Open > Open, or `xattr -dr com.apple.quarantine "/Applications/Audio Mixer.app"`. On Apple silicon the launcher is a shell script, which macOS runs without a signature.
- **Build:** `npm run build:macos` (`scripts/build-macos.js`) works on Linux, Windows and macOS: the disk image is an ISO 9660 + Rock Ridge volume (needs `genisoimage` or `xorriso`) wrapped in a UDIF `.dmg` by `scripts/mkdmg.js`, no `hdiutil` needed; the app zip needs `zip`.
- **Verified here:** the `.dmg` decompresses byte for byte with `dmg2img`, 7-Zip reads it as a DMG holding an ISO, and the volume lists `Audio Mixer.app` (executable bit kept), the `Applications` symlink and the read-me. **Not verified: mounting on a real Mac** (none is available here). The image's checksum fields are written as "none" on purpose (a wrong checksum would make macOS refuse it); if macOS does not open the `.dmg`, use the `.app.zip` or the `.tar.gz`.

## LIVE SOURCES read / write fixes (1.13.0)

An audit of the LIVE SOURCES page (READ / WRITE per interface, ASIO, WASAPI, DirectSound) found five problems, all fixed:
1. **A failed scan closed every stream.** When `/api/interfaces` failed or was refused once (server busy, rate limit), all bridge interfaces counted as unplugged and every open READ / WRITE stream was stopped. A failed scan now keeps the last interface list.
2. **Overlapping scans.** The 5-second timer, the device-change event and SCAN NOW could run at the same time and overwrite each other's result. One scan runs at a time; a request that arrives meanwhile triggers one follow-up scan.
3. **Endless restart of READ.** When the ASIO duplex stream (READ + WRITE in one) could not be opened, READ fell back to a separate stream, and the next WRITE start closed and reopened READ again, every 5 seconds, so WRITE never started. A failed duplex is now remembered for a minute and read and write use separate streams meanwhile (the bridge still shares one ASIO driver stream).
4. **Interfaces that are only listed.** Devices the OS reports without an audio engine (negative ids) were still "opened", ending in "device not found" every 30 seconds. They now say "listed only: install Audify or PortAudio" and are not opened.
5. **Detection storm on the server.** Every page scan ran PowerShell, `reg` and the RtAudio probes again, and overlapping scans piled up. The server shares one detection between concurrent and repeated requests for 3 seconds (`/api/interfaces?force=1` or `/api/drivers?force=1` skip the cache).

## RtApiAsio::probeDeviceInfo fix (1.13.0)

`RtApiAsio::probeDeviceInfo` is the RtAudio call that asks one ASIO driver for its channels and sample rates. It fails when another program (a DAW, the driver's control panel) has the driver open, when the interface was just unplugged or is waking up, and then either throws or lists the driver with no channels. The bridge rescans every few seconds, so the interface vanished from the list and came back on the next scan (its READ / WRITE ids changed with it) and the page lost it. Now an ASIO device that probed with channels is remembered (by name, in the same order, so the device ids stay the same); when its probe fails it is kept for 20 seconds, then dropped as really unplugged, and it returns with its old id when it probes again. `GET /api/audify` reports what failed in `problems` (`message`, `unprobed`, `kept`) with a hint to close the other program or replug the interface. A driver that never probed successfully is still shown as "driver only" from the registry list, as before.

## Android background engine (Java / Kotlin) and power-on animation (1.13.0)

- **Background engine.** The Android app now has a foreground service, `EngineService` (`android/src`, Kotlin twin in `android/kotlin`), that runs the app's audio engine `MiniBridge`: the same local-server protocol as `bridge/server.js` on `127.0.0.1:8765` (`/api/status`, `/api/interfaces`, `/api/drivers`, `WS /ws/input`, `WS /ws/output`), written in Java without a Node.js. The mixer page talks to it unchanged, so LIVE SOURCES lists the phone's audio interfaces (built-in microphone and speaker, USB audio-class interfaces with their channels, Bluetooth headsets with A2DP + hands-free as one device, wired headsets) and READ / WRITE them through `AudioRecord` (unprocessed source where the phone offers it, multi-channel USB interfaces through channel index masks, the chosen device through `setPreferredDevice`) and `AudioTrack` (low-latency mode on Android 8+, a bounded queue so delay cannot build up). The service keeps a partial wake lock and shows an ongoing notification with a Stop button, so audio keeps running with the screen off or the app in the background; the notification is the only trace, the engine is local to the phone (loopback only, Host / Origin checks like the Node bridge).
- **The page now comes from the engine** (`http://localhost:8765/index.html`, served from `assets/www` by `MiniBridge`, only files of that folder): a page loaded from `file://` has an opaque origin in which AudioWorklet modules cannot be loaded (LIVE SOURCES read / write and the plugin chain use them) and where the microphone and localStorage are less reliable. If the engine cannot start, the app falls back to the bundled file (web mode, as before).
- **License on Android.** The engine has no license endpoint: the page checks a key itself (it only talks to the server for activation when the server answered `/api/license`).
- **Kotlin.** `android/kotlin/EngineService.kt` is the Kotlin version of the service; `node scripts/build-apk.js --kotlin` builds the app with it instead of `EngineService.java` (needs `kotlinc`; the Kotlin stdlib is packed into the dex). The default build is Java only. **The Kotlin file was not compiled** here (no Kotlin compiler was available).
- **targetSdk is now 29** (was 33): an Android 11+ foreground service must declare `foregroundServiceType="microphone"` in the manifest to capture in the background, and the aapt of the distribution packages knows no attribute newer than API 23, so the app targets Android 10 behaviour, where a foreground service may use the microphone without the declaration. Google Play requires a newer target and the declaration: build with the Android SDK build tools and add `android:foregroundServiceType="microphone|mediaPlayback"` for a Play release.
- **Not verified on a device.** The protocol, HTTP / WebSocket layer, interface grouping and page serving run on a plain JVM in the tests and in a Chromium run (the bundled page, loaded from the JVM engine, listed the fake interfaces, READ and WRITE streamed through real WebSockets, a license key activated). The classes that need Android (`AndroidAudio`, `EngineService`, `MainActivity`) compile against the Android API and are packed into the APK, but were never run: expect to fix device-specific details (USB interface channel counts, Bluetooth routing, OEM battery savers that stop background services; allow the microphone and turn battery optimisation off for the app).
- **Power-on animation.** The console now starts like a real mixer: the power LED goes red, amber, green; the screen-on line flashes open; the logo draws itself; AUDIO MIXER spells in; twenty motorised faders sweep up and settle while the meters run their LED ladders; the OS boot lines (TITAN OS, DSP core, audio engine, I/O, faders, console) tick to OK with a progress bar (about 4 seconds). It plays once per browser session / app start; a click, tap, Enter, Space or Esc skips it, `fastBoot()` ends it at once and `prefers-reduced-motion` shortens it. It works in every build (browser, PC mode, Android, iOS, macOS).

## Start Menu shortcuts and installer options (1.14.0)

- **New Start Menu shortcuts** (feature *Tools*): *License key and machine ID*, *List installed plugins*, *Audio drivers (ASIO, WASAPI)*, *Check for updates* and *Audio Mixer diagnostics*. Each opens a console that waits for Enter. They run new commands of the client: `node client/cli.js license [status|activate <key>|deactivate]`, `plugins`, `update [download]` (checks the signed manifest; the download is verified and never run for you), and `--pause` is now accepted by `drivers`, `doctor`, `license`, `plugins` and `update`.
- **Optional MSI features:** `Main` (always), `Shortcuts`, `Tools`, `PluginHost` (native VST host, `native/host`), `WinHelpers` (native Windows device helpers, `native/win`), `Autostart`, `Desktop` (off by default). Use `ADDLOCAL=Main,Shortcuts,PluginHost` with msiexec, or the wizard's feature tree.
- **Setup `.exe` switches:** `/noplugins`, `/nohelpers`, `/notools`, `/noshortcuts`, `/noautostart`, `/desktop` (they are turned into `ADDLOCAL`; without them the installer keeps its defaults). `/?` lists all switches.
- Without the plugin host the mixer still works; VST insert slots then report that the host is missing.

## Sources reach the mixer channels (1.14.1)

- **Interfaces that were read did not feed the mixer.** Opening an interface in LIVE SOURCES (ASIO, WASAPI, DirectSound, Core Audio, ALSA, Android) only fed its meter until the PATCH button was pressed, so the channel strips showed no input. With auto mode on, a server interface that opens is now patched to the next free channel pair (CH 1/2, 3/4, ... within the license's channel limit; loopback / music goes to the last pair) and a notice reminds you to use headphones. Browser microphones are still not patched automatically (speaker feedback): use LIVE INPUT PATCH.
- **LIVE INPUT PATCH only offered browser microphones.** The device list on the routing page now also lists the interfaces of the local system server ("Local server (ASIO / WASAPI)"), opens one on demand and patches it (stereo, left or right) into any channel. The capture is shared with LIVE SOURCES: REMOVE only disconnects the channel, the READ stream keeps running. The list refreshes after every scan without closing an open dropdown.

## Upgrading with the setup .exe (1.14.2)

Running `Audio Mixer-<version>.exe` over an installed version upgrades it in place (same upgrade code; the older version is removed, settings, license key and plugins in your folders stay). A running local server used to keep `node.exe` and the native launcher locked, so Windows Installer asked for a restart. The setup program now ends only the processes that run from the install folder (`node.exe`, `AudioMixerServer.exe`) before installing or uninstalling, and starts the new server afterwards when one was running. A newer installed version is never replaced by an older setup file (Windows Installer refuses the downgrade).

## Stream close and DAC read / write detection (1.14.3)

- **Closing a stream.** The local server released an audio stream (and with it an ASIO driver, which serves one client) only when the TCP connection behind the WebSocket finally ended. A close frame from the page now frees the stream at once, exactly once, and a failing close handler can no longer leave the socket half open. A connection whose peer vanished without a close (cable pulled, computer asleep, browser crashed) used to hold the device until the operating system gave up; the server now pings every 15 seconds and ends a connection that shows no sign of life for 45 seconds.
- **DAC / read-write mode detection.** Every interface in LIVE SOURCES now shows what it can do from its real channel counts: `READ + WRITE` (audio interface), `DAC / OUTPUT: WRITE ONLY` (DAC, speakers), `INPUT: READ ONLY` (microphone, loopback) or `NO CHANNELS YET`. A DAC is never opened for reading and an input-only device never for writing, whatever an older saved setting says. Automatic choices follow the device: a driver that was listed without channels (an ASIO probe that failed, a DAC that was waking up) is enabled as soon as its channels appear, and a READ / WRITE switch you set yourself is never changed.

## Start-up (boot) screens for the apps (1.15.0)

The console always played its power-on animation inside the page. The apps now also show a start-up screen of their own while they start, in the same look (power LED red / amber / green, the logo spelling in, TITAN OS / DSP CORE / AUDIO ENGINE / I/O / FADERS / CONSOLE ticking to OK, progress bar); the page's own power-on sequence follows.

- **Windows:** `AudioMixerServer.exe /open` shows an animated window while the local server starts, then opens the mixer in the browser (if the server already runs, it just plays the screen). The Start Menu entry *Audio Mixer (PC mode)* and the desktop shortcut now use it. Click or Esc ends it; if the server does not answer within 25 s it says so. The login autostart is unchanged (no window).
- **macOS and Linux:** the app / launcher opens `boot.html` in the browser at once, before Node.js and the audio module are checked or installed (the first start can take a minute), and the page switches to the mixer when the server answers. The launcher then does not open a second window. `AUDIO_MIXER_NO_SPLASH=1` turns it off; `--no-open` and `--port` are honoured; without a graphical session nothing is opened.
- **Android:** a native start-up view (`BootView`) covers the page until it has loaded (at least 2.2 s, never longer than 12 s) and fades out.
- **iOS:** a SwiftUI `BootView` does the same until the bundled page is served.
- **Not verified:** `boot.html` and its hand-over to the server ran in Chromium (stays while the server is down, redirects when it answers) and the shell helper was tested with a stand-in browser opener. The Windows window compiles (x64 and x86) but was **not run** (no Windows here); the Android view compiles into the APK but was not run on a device; the iOS view could **not be compiled** (no Xcode here).

## Mic FFT spectrum fix (1.15.1)

The microphone trace of the header RTA (FFT RTA, 20 Hz - 20 kHz) had three faults, all fixed:
1. **Wrong frequency axis.** Bars were taken from the FFT bins with a power curve that does not match the labels under the display: a 1 kHz tone was drawn at 17 % of the width, where the label says 250 Hz. Every bar now maps to the frequency of the label axis (20, 63, 125 ... 16k, 20k Hz, log between neighbours) and shows the loudest bin of its band, so a narrow tone is not skipped at the high end. A 1 kHz tone now lands on the 1kHz label.
2. **Too coarse below 250 Hz.** The analyser used 512 points (94 Hz per bin). It now uses 4096 points (11.7 Hz per bin at 48 kHz).
3. **Only the default browser microphone was shown.** Interfaces read in LIVE SOURCES (browser microphones, ASIO, WASAPI, DirectSound, Core Audio, ALSA, Android, anything arriving through the local server's Web Audio worklet) never reached it. The interface chosen in the FFT selector of LIVE SOURCES now feeds the header trace (the button reads `MIC: SOURCE (FFT ON)`); the header's own microphone toggle still works and both can run together.

The music trace of the same display keeps its old bin mapping (not part of this fix). Checked in Chromium with a 1 kHz tone as the microphone and with the test engine's server-side interface.

## Old server kept the old page after an upgrade (1.15.2)

After installing a new version the mixer could still show the old page and features: an **older Audio Mixer server was still running** on port 8765 (started at login or by an earlier launch; Windows also keeps its files locked) and every start just reused it. Now:
- `node client/cli.js` (and the Start Menu / desktop *Audio Mixer* entry) checks the running server's version through `/api/status`; an **older** one (verified by name and process id on this PC's loopback) is ended and the current server is started in its place, with a message. A **newer** one is never replaced.
- The Windows start-up screen (`AudioMixerServer.exe /open`) now starts the client in `--ensure` mode (starts or replaces, exits when a current server already runs) and only opens the browser when the server answers **with this version**. If an older server cannot be ended (for example it runs as another user) it says so: end `node.exe` in Task Manager and start again.
- The setup `.exe` from 1.14.2 on already ends the old server before installing; the `.msi` alone does not (Windows Installer may then ask for a restart: do it, or end `node.exe` first).
- The bundled Node.js is the current LTS (x64: 24.21.0, x86: 22.23.3, checked against nodejs.org's SHASUMS256.txt when the installer is built).

## Interface read / write mode not readable (1.15.3)

An audio endpoint whose channel count Windows cannot report (device busy or in exclusive use by another program, some Bluetooth endpoints, a DAC that is waking up) came back from the native helper with 0 channels, although its direction (capture / render) was known. The interface then showed "no input • no output" with READ and WRITE switched off, so its mode could not be read or used. Now:
- the direction decides the mode and an unknown channel count is taken as stereo, on Windows (`AudioDevices.exe` listing), macOS (Core Audio helper) and wherever the helper gives a direction without a count; a reported count is always kept. A DAC / speaker therefore shows `DAC / OUTPUT: WRITE ONLY`, a microphone `INPUT: READ ONLY`.
- an interface with no readable mode says why: `DRIVER ONLY: CLOSE THE OTHER PROGRAM / REPLUG` (ASIO driver that did not probe) or `MODE UNKNOWN: DEVICE NOT READABLE`; and a device the audio engine could not probe says to close the other program and press SCAN NOW instead of the wrong advice to install an audio module.

## Devices the audio engine could not probe (1.15.4)

1.15.3 fixed endpoints whose channel count the operating system could not report. The same problem existed one level down: **a device the audio engine (Audify / RtAudio) could not probe** (WASAPI, DirectSound or Core Audio device in use by another program, in exclusive mode, or just plugged in) was listed with no channels at all, so its read / write mode could not be read and neither READ nor WRITE could be switched on, although the operating system's own list knew the device. Now:
- the device is probed once more on a fresh engine instance; if it still fails it is flagged (`probeFailed` in `/api/interfaces`);
- the interface listing takes the mode of a flagged device from the operating system's endpoint of the same name (output only: a DAC / speakers, input only: a microphone, both: an interface), so READ / WRITE are offered on the engine device that can actually be opened;
- opening such a device tries a stereo stream instead of refusing with "has no input / output channels" (a real 0 channel device still refuses; RtAudio's own message follows if the stream cannot open). With no operating-system entry to learn from the mode stays unknown (`MODE UNKNOWN: DEVICE NOT READABLE`): nothing is guessed.
