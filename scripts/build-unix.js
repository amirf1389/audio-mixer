'use strict';
// Linux and macOS packages that put the files where each system expects them.
//   dist/audio-mixer_<version>_all.deb          Debian / Ubuntu / Mint:  app in /opt/audio-mixer (root-owned, read-only), command /usr/bin/audio-mixer,
//                                               menu entry in /usr/share/applications, optional systemd user unit in /usr/lib/systemd/user
//   dist/AudioMixer-<version>-macos.tar.gz      macOS: "Audio Mixer.app" + install.command / uninstall.command; installs to /Applications
//                                               (or ~/Applications without administrator rights), start-at-login is a LaunchAgent in ~/Library/LaunchAgents
// User data stays in the user's own folders and is never touched by (un)installing: ~/AudioMixerDrivers (downloaded drivers),
// ~/AudioMixerPlugins and the standard VST folders (~/.vst3, ~/Library/Audio/Plug-Ins/VST3). Both packages need Node.js 18+ on the machine.
// Neither package is signed or notarized: macOS Gatekeeper asks for confirmation on first start (install.command clears the quarantine flag).
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const { build } = require('./build');
const { png, iconPixel } = require('./build-apk');

const ROOT = path.resolve(__dirname, '..');
const OPT = 'opt/audio-mixer';
const HOMEPAGE = 'https://github.com/amirf1389/audio-mixer';

const LAUNCHER_LINUX = `#!/bin/sh
# Audio Mixer: starts the local server and opens the mixer in the browser (PC mode). Options are passed on, for example --no-open or --port 8800.
# Node.js 18+ is installed automatically from nodejs.org when missing (into ~/.local/share/audio-mixer, checksum verified, no administrator rights),
# and the native audio module (Audify) is installed once. AUDIO_MIXER_YES=1 skips the questions, AUDIO_MIXER_NO_NATIVE=1 skips the audio module.
APP=/${OPT}
. "$APP/ensure-node.sh"
am_splash "$APP" "$@"
am_ensure_node || exit 1
am_ensure_audio "$APP"
exec "$NODE" "$APP/client/cli.js" $AM_SPLASH_ARGS "$@"
`;

// The "audio-mixer" command of the tarball / macOS installs: lives in the app folder, is linked onto PATH, finds the app folder through the link.
// Any command goes to the client: audio-mixer doctor | drivers | license | plugins | update | npm install audify | uninstall | ... ; without one it starts the mixer.
const LAUNCHER_PORTABLE = `#!/bin/sh
# Audio Mixer command line. "audio-mixer" starts the mixer; "audio-mixer <command>" runs a client command (doctor, drivers, setup, npm, uninstall ...).
SELF="$0"
while [ -h "$SELF" ]; do L="$(readlink "$SELF")"; case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac; done
APP="$(cd "$(dirname "$SELF")" && pwd)"
. "$APP/ensure-node.sh"
am_splash "$APP" "$@"
am_ensure_node || exit 1
case "\${1:-}" in ''|-*|start) am_ensure_audio "$APP" ;; esac
exec "$NODE" "$APP/client/cli.js" $AM_SPLASH_ARGS "$@"
`;

const desktopEntry = () => `[Desktop Entry]
Type=Application
Name=Audio Mixer
Comment=Virtual mixing console with a local server for ASIO, ALSA, JACK and PipeWire audio
Exec=audio-mixer
Icon=audio-card
Terminal=false
Categories=AudioVideo;Audio;Mixer;
Keywords=mixer;audio;asio;jack;pipewire;
`;

// The system's own places for "where is it installed", "which file type is this" and "who opens audiomixer:// links" (freedesktop.org):
//   environment: /etc/profile.d/audio-mixer.sh (shells at login) and environment.d (systemd user sessions: the desktop, the user service) set AUDIO_MIXER_DIR
//   file type:   a shared-mime-info entry for *.amix and a hidden .desktop entry that claims it and the audiomixer: scheme and runs  audio-mixer open %u
const MIME_XML = `<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-audio-mixer-scene">
    <comment>Audio Mixer scene archive</comment>
    <glob pattern="*.amix"/>
    <sub-class-of type="application/json"/>
  </mime-type>
</mime-info>
`;
const openDesktopEntry = exec => `[Desktop Entry]
Type=Application
Name=Audio Mixer (open a scene file or link)
Exec=${exec} open %u
Icon=audio-card
Terminal=false
NoDisplay=true
MimeType=application/x-audio-mixer-scene;x-scheme-handler/audiomixer;
`;
const profileSh = dir => `# Audio Mixer: where it is installed (read by login shells)\n[ -d ${dir} ] && export AUDIO_MIXER_DIR=${dir}\n`;
const environmentConf = (dir, path) => `AUDIO_MIXER_DIR=${dir}\n${path ? `PATH=${path}:\${PATH}\n` : ''}`;

const systemdUserUnit = () => `[Unit]
Description=Audio Mixer local server
After=default.target

[Service]
ExecStart=/usr/bin/env node /${OPT}/bridge/server.js
Restart=on-failure

[Install]
WantedBy=default.target
`;

function controlFile({ version, installedSizeKb }) {
  return `Package: audio-mixer
Version: ${version}
Section: sound
Priority: optional
Architecture: all
Recommends: nodejs (>= 18), pipewire | jackd2 | alsa-utils
Installed-Size: ${installedSizeKb}
Maintainer: Audio Mixer <noreply@users.noreply.github.com>
Homepage: ${HOMEPAGE}
Description: virtual mixing console with a local audio server
 Web mixer (EQ, dynamics, limiter, buses, matrix, FX) plus a local Node.js
 server that connects it to ALSA, JACK and PipeWire audio, detects VST plugins
 and reads what Spotify, YouTube Music, TIDAL and others are playing.
 Start it with the "audio-mixer" command or from the application menu.
 If Node.js 18+ is not installed, the command offers to download the official
 build from nodejs.org (checksum verified, no administrator rights).
 To start it at login: systemctl --user enable --now audio-mixer
`;
}

const POSTINST = `#!/bin/sh
set -e
# register the *.amix file type and the audiomixer: link handler (menu and mime caches; skipped where the tools are missing)
command -v update-mime-database >/dev/null 2>&1 && update-mime-database /usr/share/mime >/dev/null 2>&1 || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
echo "Audio Mixer installed in /${OPT}. Run: audio-mixer   (start at login: systemctl --user enable --now audio-mixer)"
exit 0
`;
const POSTRM = `#!/bin/sh
set -e
command -v update-mime-database >/dev/null 2>&1 && update-mime-database /usr/share/mime >/dev/null 2>&1 || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
# Downloaded drivers (~/AudioMixerDrivers) and plugins (~/AudioMixerPlugins) belong to the user and are left in place.
exit 0
`;

function listFiles(dir, base = dir, out = []) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const f = path.join(dir, e.name);
    if (e.isDirectory()) listFiles(f, base, out); else out.push(path.relative(base, f).split(path.sep).join('/'));
  }
  return out.sort();
}
function dirSizeKb(dir) { return Math.ceil(listFiles(dir).reduce((n, f) => n + fs.statSync(path.join(dir, f)).size, 0) / 1024); }
function setModes(root) {
  const walk = d => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const f = path.join(d, e.name);
      if (e.isDirectory()) { fs.chmodSync(f, 0o755); walk(f); } else fs.chmodSync(f, 0o644);
    }
  };
  fs.chmodSync(root, 0o755); walk(root);
}

// ── Debian package ──
function buildDeb({ out = path.join(ROOT, 'dist'), app = build({ out }) } = {}) {
  const outAbs = path.resolve(out), version = app.version;
  const root = path.join(outAbs, 'unix', 'deb');
  fs.rmSync(root, { recursive: true, force: true });
  const opt = path.join(root, OPT);
  fs.cpSync(app.dest, opt, { recursive: true });
  for (const f of ['start-pc-mode.bat', 'start-local-server.bat', 'start-pc-mode.sh']) fs.rmSync(path.join(opt, f), { force: true });   // Windows launchers / project-folder launcher
  const put = (rel, text, mode = 0o644) => { const f = path.join(root, rel); fs.mkdirSync(path.dirname(f), { recursive: true }); fs.writeFileSync(f, text); fs.chmodSync(f, mode); };
  put('usr/bin/audio-mixer', LAUNCHER_LINUX, 0o755);
  put('usr/share/applications/audio-mixer.desktop', desktopEntry());
  put('usr/lib/systemd/user/audio-mixer.service', systemdUserUnit());
  put('etc/profile.d/audio-mixer.sh', profileSh('/' + OPT));
  put('usr/lib/environment.d/60-audio-mixer.conf', environmentConf('/' + OPT));
  put('usr/share/mime/packages/audio-mixer.xml', MIME_XML);
  put('usr/share/applications/audio-mixer-open.desktop', openDesktopEntry('audio-mixer'));
  const lic = path.join(ROOT, 'LICENSE');
  put('usr/share/doc/audio-mixer/copyright', `Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\nUpstream-Name: audio-mixer\nSource: ${HOMEPAGE}\nLicense: MIT\n\n` + (fs.existsSync(lic) ? fs.readFileSync(lic, 'utf8') : 'MIT License\n'));
  put('usr/share/doc/audio-mixer/README', fs.readFileSync(path.join(ROOT, 'README.md'), 'utf8'));
  // directories 755, files 644, the launcher and the entry scripts executable
  const modeTree = d => { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const f = path.join(d, e.name); if (e.isDirectory()) { fs.chmodSync(f, 0o755); modeTree(f); } else if (!f.endsWith('/usr/bin/audio-mixer')) fs.chmodSync(f, 0o644); } };
  modeTree(root);
  for (const f of ['client/cli.js', 'bridge/server.js']) fs.chmodSync(path.join(opt, f), 0o755);
  const files = listFiles(root);
  const deb = path.join(root, 'DEBIAN');
  fs.mkdirSync(deb);
  fs.writeFileSync(path.join(deb, 'control'), controlFile({ version, installedSizeKb: dirSizeKb(root) }));
  fs.writeFileSync(path.join(deb, 'md5sums'), files.map(f => `${crypto.createHash('md5').update(fs.readFileSync(path.join(root, f))).digest('hex')}  ${f}`).join('\n') + '\n');
  for (const [n, t] of [['postinst', POSTINST], ['postrm', POSTRM]]) { fs.writeFileSync(path.join(deb, n), t); fs.chmodSync(path.join(deb, n), 0o755); }
  fs.chmodSync(deb, 0o755);
  const file = path.join(outAbs, `audio-mixer_${version}_all.deb`);
  const r = spawnSync('dpkg-deb', ['--root-owner-group', '-Zxz', '--build', root, file], { encoding: 'utf8' });
  if (r.error && r.error.code === 'ENOENT') throw new Error('dpkg-deb not found (Debian / Ubuntu: apt install dpkg)');
  if (r.status !== 0) throw new Error('dpkg-deb failed: ' + (r.stderr || r.stdout));
  return { deb: file, version, sha256: crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex') };
}

// ── macOS app bundle ──
const MAC_LAUNCHER = `#!/bin/bash
# Audio Mixer.app: finds Node.js 18+ (or downloads the official build from nodejs.org after asking, checksum verified, no administrator rights),
# installs the native audio module once, starts the local server and opens the mixer in the browser.
APP="$(cd "$(dirname "$0")/.." && pwd)/Resources/app"
. "$APP/ensure-node.sh"
am_splash "$APP" "$@"
am_ensure_node || { osascript -e 'display dialog "Audio Mixer needs Node.js 18 or newer and it could not be installed automatically. Install it from nodejs.org, then start Audio Mixer again." buttons {"OK"} with icon caution' >/dev/null 2>&1; open "https://nodejs.org/en/download" 2>/dev/null; exit 1; }
am_ensure_audio "$APP"
# Core Audio helper (Swift) for the interface list: built once when the Xcode command line tools are there (optional, runs in the background)
if [ ! -x "$APP/native/mac/AudioDevices" ] && command -v swiftc >/dev/null 2>&1 && [ -f "$APP/native/mac/AudioDevices.swift" ]; then
  ( swiftc -O -o "$APP/native/mac/AudioDevices" "$APP/native/mac/AudioDevices.swift" >/dev/null 2>&1 ) &
fi
exec "$NODE" "$APP/client/cli.js" $AM_SPLASH_ARGS "$@"
`;

const infoPlist = version => `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Audio Mixer</string>
  <key>CFBundleDisplayName</key><string>Audio Mixer</string>
  <key>CFBundleIdentifier</key><string>com.audiomixer.app</string>
  <key>CFBundleVersion</key><string>${version}</string>
  <key>CFBundleShortVersionString</key><string>${version}</string>
  <key>CFBundleExecutable</key><string>AudioMixer</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.music</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSMicrophoneUsageDescription</key><string>Audio Mixer reads your audio interface and microphone as mixer inputs.</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
`;

// AppIcon.icns: PNG entries ic07 (128), ic08 (256), ic09 (512), ic10 (1024); the same mixer-fader tile as the Android icon
function icns() {
  const parts = [['ic07', 128], ['ic08', 256], ['ic09', 512], ['ic10', 1024]].map(([type, size]) => {
    const data = png(size, iconPixel), h = Buffer.alloc(8); h.write(type, 0, 'latin1'); h.writeUInt32BE(data.length + 8, 4); return Buffer.concat([h, data]);
  });
  const total = 8 + parts.reduce((n, p) => n + p.length, 0), head = Buffer.alloc(8); head.write('icns', 0, 'latin1'); head.writeUInt32BE(total, 4);
  return Buffer.concat([head, ...parts]);
}

// macOS keeps environment and PATH in two places of its own: launchd (the environment of every program you start from Finder, the Dock or Spotlight; a
// LaunchAgent sets it again at every login) and /etc/paths.d (one file = one folder added to the PATH of every new shell; needs administrator rights, so it is asked).
// AUDIO_MIXER_DIR names the app's folder. A document type and the audiomixer:// link are not registered on macOS: the app's program is a shell script, which cannot receive
// the Apple events that Finder and the browser send, so a registration would start Audio Mixer and lose the file; use  audio-mixer open <file.amix>  instead.
const MAC_SYSTEM_ENV = `# environment for programs started from Finder / the Dock (launchd) now and at every login
mkdir -p "$HOME/Library/LaunchAgents"
cat > "$HOME/Library/LaunchAgents/com.audiomixer.env.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>com.audiomixer.env</string>
  <key>ProgramArguments</key><array><string>/bin/launchctl</string><string>setenv</string><string>AUDIO_MIXER_DIR</string><string>$APP</string></array>
  <key>RunAtLoad</key><true/>
</dict></plist>
PLIST
launchctl setenv AUDIO_MIXER_DIR "$APP" 2>/dev/null && echo "Environment: AUDIO_MIXER_DIR=$APP (launchd)"
# the app's folder on the PATH of every new shell (/etc/paths.d); optional because it needs your password
if [ -t 0 ]; then
  read -r -p "Add Audio Mixer to the system PATH (/etc/paths.d, asks for your password)? [y/N] " a
  if [ "$a" = "y" ] || [ "$a" = "Y" ]; then printf '%s\\n' "$APP" | sudo tee /etc/paths.d/audio-mixer >/dev/null && echo "PATH: $APP (/etc/paths.d/audio-mixer)"; fi
fi`;

const MAC_INSTALL = `#!/bin/bash
# Installs Audio Mixer: /Applications (or ~/Applications when you cannot write there). Double-click this file in Finder.
cd "$(dirname "$0")" || exit 1
DEST=/Applications
[ -w "$DEST" ] || DEST="$HOME/Applications"
mkdir -p "$DEST"
rm -rf "$DEST/Audio Mixer.app"
cp -R "Audio Mixer.app" "$DEST/" || { echo "Copy failed"; exit 1; }
xattr -dr com.apple.quarantine "$DEST/Audio Mixer.app" 2>/dev/null   # the app is not notarized
mkdir -p "$HOME/AudioMixerPlugins"
echo "Audio Mixer installed in $DEST"
APP="$DEST/Audio Mixer.app/Contents/Resources/app"
. "$APP/ensure-node.sh"
# Core Audio helper (Swift): lists the audio interfaces with channels and sample rates; optional, needs the Xcode command line tools
if command -v swiftc >/dev/null 2>&1 && [ -f "$APP/native/mac/AudioDevices.swift" ]; then
  swiftc -O -o "$APP/native/mac/AudioDevices" "$APP/native/mac/AudioDevices.swift" 2>/dev/null && echo "Core Audio helper built" || echo "Core Audio helper not built (system_profiler is used instead)"
fi
# Node.js 18+ and the native audio module are installed now if they are missing (official builds, checksum verified, no administrator rights)
if am_ensure_node; then
  am_ensure_audio "$APP"
  read -r -p "Start the local server when I log in? [y/N] " a
  if [ "$a" = "y" ] || [ "$a" = "Y" ]; then "$NODE" "$APP/client/cli.js" service install; fi
else
  echo "Node.js 18+ is not installed: https://nodejs.org/en/download (Audio Mixer asks again the first time it starts)."
fi
# the "audio-mixer" command (doctor, drivers, npm, setup, uninstall ...) on PATH
BINDIR=/usr/local/bin; [ -w "$BINDIR" ] || BINDIR="$HOME/.local/bin"
mkdir -p "$BINDIR" && ln -sf "$APP/audio-mixer" "$BINDIR/audio-mixer" && echo "Command line: $BINDIR/audio-mixer" || echo "Could not link the audio-mixer command."
case ":$PATH:" in *":$BINDIR:"*) ;; *) echo "Add $BINDIR to your PATH to use it:  echo 'export PATH=\"$BINDIR:\$PATH\"' >> ~/.zshrc" ;; esac
${MAC_SYSTEM_ENV}
echo "Done. Open Audio Mixer from $DEST. Remove it later with uninstall.command (or: audio-mixer uninstall)."
`;

const MAC_UNINSTALL = `#!/bin/bash
# Removes Audio Mixer and its start-at-login entry. Downloaded drivers (~/AudioMixerDrivers) and plugins (~/AudioMixerPlugins) stay.
for DEST in /Applications "$HOME/Applications"; do
  APP="$DEST/Audio Mixer.app"
  if [ -d "$APP" ]; then
    NODE="$(command -v node 2>/dev/null || ls "$HOME/.local/share/audio-mixer/node/bin/node" /opt/homebrew/bin/node /usr/local/bin/node 2>/dev/null | head -1)"
    [ -n "$NODE" ] && "$NODE" "$APP/Contents/Resources/app/client/cli.js" service uninstall
    rm -rf "$APP" && echo "Removed $APP"
  fi
done
for L in /usr/local/bin/audio-mixer "$HOME/.local/bin/audio-mixer"; do   # the command line link, when it points into the app
  [ -L "$L" ] && case "$(readlink "$L")" in *"Audio Mixer.app"*) rm -f "$L" && echo "Removed $L" ;; esac
done
rm -f "$HOME/Library/LaunchAgents/com.audiomixer.bridge.plist"
rm -f "$HOME/Library/LaunchAgents/com.audiomixer.env.plist"; launchctl unsetenv AUDIO_MIXER_DIR 2>/dev/null
[ -f /etc/paths.d/audio-mixer ] && { echo "Removing /etc/paths.d/audio-mixer (asks for your password)"; sudo rm -f /etc/paths.d/audio-mixer; }
rm -rf "$HOME/.local/share/audio-mixer"   # the private Node.js and audio module that Audio Mixer downloaded
echo "Audio Mixer removed."
`;

// For the disk image (the app is dragged to Applications): puts the audio-mixer command on PATH, finding the app where it was dragged.
const MAC_COMMAND = `#!/bin/bash
# Puts the "audio-mixer" command (doctor, drivers, npm, setup, uninstall ...) on your PATH. Double-click this file in Finder after dragging Audio Mixer to Applications.
for DEST in /Applications "$HOME/Applications"; do [ -d "$DEST/Audio Mixer.app" ] && break; done
APP="$DEST/Audio Mixer.app/Contents/Resources/app"
[ -x "$APP/audio-mixer" ] || { echo "Audio Mixer is not in Applications yet: drag it there first."; exit 1; }
BINDIR=/usr/local/bin; [ -w "$BINDIR" ] || BINDIR="$HOME/.local/bin"
mkdir -p "$BINDIR" && ln -sf "$APP/audio-mixer" "$BINDIR/audio-mixer" && echo "Command line: $BINDIR/audio-mixer"
case ":$PATH:" in *":$BINDIR:"*) ;; *) echo "Add $BINDIR to your PATH to use it:  echo 'export PATH=\"$BINDIR:\$PATH\"' >> ~/.zshrc" ;; esac
${MAC_SYSTEM_ENV}
`;

function buildMac({ out = path.join(ROOT, 'dist'), app = build({ out }) } = {}) {
  const outAbs = path.resolve(out), version = app.version;
  const root = path.join(outAbs, 'unix', 'macos', 'AudioMixer');
  fs.rmSync(path.join(outAbs, 'unix', 'macos'), { recursive: true, force: true });
  const bundle = path.join(root, 'Audio Mixer.app');
  const res = path.join(bundle, 'Contents', 'Resources', 'app');
  fs.cpSync(app.dest, res, { recursive: true });
  for (const f of ['start-pc-mode.bat', 'start-local-server.bat', 'start-pc-mode.sh']) fs.rmSync(path.join(res, f), { force: true });
  const put = (rel, text, mode) => { const f = path.join(root, rel); fs.mkdirSync(path.dirname(f), { recursive: true }); fs.writeFileSync(f, text); fs.chmodSync(f, mode); };
  put('Audio Mixer.app/Contents/Info.plist', infoPlist(version), 0o644);
  put('Audio Mixer.app/Contents/MacOS/AudioMixer', MAC_LAUNCHER, 0o755);
  fs.mkdirSync(path.join(bundle, 'Contents', 'Resources'), { recursive: true });
  fs.writeFileSync(path.join(bundle, 'Contents', 'Resources', 'AppIcon.icns'), icns());
  put('Audio Mixer.app/Contents/Resources/app/audio-mixer', LAUNCHER_PORTABLE, 0o755);   // the command line (linked onto PATH by install.command)
  put('Audio Mixer.app/Contents/Resources/app/uninstall.sh', MAC_UNINSTALL, 0o755);       // "audio-mixer uninstall" runs it
  put('install.command', MAC_INSTALL, 0o755);
  put('uninstall.command', MAC_UNINSTALL, 0o755);
  put('README.txt', `Audio Mixer ${version} for macOS\n\n1. Install Node.js 18 or newer if you do not have it: https://nodejs.org/en/download\n2. Double-click install.command (copies the app to /Applications, or ~/Applications).\n   The app is not notarized: if macOS blocks it, right-click > Open once, or run: xattr -dr com.apple.quarantine "/Applications/Audio Mixer.app"\n3. Start "Audio Mixer": the mixer opens in your browser. Start at login: answer y in install.command, or run the app's client with: service install\n\nFiles: app in /Applications/Audio Mixer.app, login item in ~/Library/LaunchAgents, downloaded drivers in ~/AudioMixerDrivers,\nVST plugins are read from /Library/Audio/Plug-Ins/VST3 and ~/Library/Audio/Plug-Ins/VST3 (and ~/AudioMixerPlugins).\nRemove everything with uninstall.command.\n`, 0o644);
  fs.chmodSync(path.join(res, 'client', 'cli.js'), 0o755);
  const file = path.join(outAbs, `AudioMixer-${version}-macos.tar.gz`);
  const r = spawnSync('tar', ['--owner=0', '--group=0', '--numeric-owner', '-czf', file, '-C', path.dirname(root), 'AudioMixer'], { encoding: 'utf8' });
  if (r.status !== 0) throw new Error('tar failed: ' + (r.stderr || r.error));
  return { tar: file, version, root, bundle, sha256: crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex') };
}

const LINUX_INSTALL = `#!/bin/sh
# Installs Audio Mixer on any Linux distribution: as root into /opt/audio-mixer (command in /usr/local/bin, menu entry for everyone), otherwise into
# ~/.local/share/audio-mixer-app (command in ~/.local/bin, menu entry for you). Node.js 18+ with npm is downloaded when missing (official build from
# nodejs.org, checksum verified, into ~/.local/share/audio-mixer, no administrator rights) and the native audio module (Audify) is installed.
#   sh install.sh [--yes]        --yes: no questions (also AUDIO_MIXER_YES=1);  AUDIO_MIXER_NO_NATIVE=1 skips the audio module
set -e
cd "$(dirname "$0")"
[ "\${1:-}" = "--yes" ] && export AUDIO_MIXER_YES=1
if [ "$(id -u)" = 0 ]; then DEST=/opt/audio-mixer; BIN=/usr/local/bin; APPS=/usr/share/applications
else DEST="$HOME/.local/share/audio-mixer-app"; BIN="$HOME/.local/bin"; APPS="$HOME/.local/share/applications"; fi
rm -rf "$DEST"; mkdir -p "$DEST" "$BIN" "$APPS"
cp -R app/. "$DEST/"
chmod 755 "$DEST/audio-mixer" "$DEST/uninstall.sh" "$DEST/client/cli.js" "$DEST/bridge/server.js"
ln -sf "$DEST/audio-mixer" "$BIN/audio-mixer"
cat > "$APPS/audio-mixer.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Audio Mixer
Comment=Virtual mixing console with a local server for ASIO, ALSA, JACK and PipeWire audio
Exec=$DEST/audio-mixer
Icon=audio-card
Terminal=false
Categories=AudioVideo;Audio;Mixer;
Keywords=mixer;audio;asio;jack;pipewire;
DESK
cat > "$APPS/audio-mixer-open.desktop" <<DESK
${openDesktopEntry('$BIN/audio-mixer').replace(/\n$/, '')}
DESK
# environment, file type and link handler in the system's own places (nothing in your shell start-up files is edited):
#   root: /etc/profile.d (login shells), /usr/lib/environment.d (systemd user sessions), /usr/share/mime;  user: ~/.config/environment.d (also puts $BIN on PATH there), ~/.local/share/mime
if [ "$(id -u)" = 0 ]; then
  mkdir -p /etc/profile.d /usr/lib/environment.d; MIMEDIR=/usr/share/mime
  printf '# Audio Mixer: where it is installed (read by login shells)\\n[ -d %s ] && export AUDIO_MIXER_DIR=%s\\n' "$DEST" "$DEST" > /etc/profile.d/audio-mixer.sh
  printf 'AUDIO_MIXER_DIR=%s\\n' "$DEST" > /usr/lib/environment.d/60-audio-mixer.conf
else
  mkdir -p "$HOME/.config/environment.d"; MIMEDIR="$HOME/.local/share/mime"
  printf 'AUDIO_MIXER_DIR=%s\\nPATH=%s:\${PATH}\\n' "$DEST" "$BIN" > "$HOME/.config/environment.d/60-audio-mixer.conf"
fi
mkdir -p "$MIMEDIR/packages"
cat > "$MIMEDIR/packages/audio-mixer.xml" <<'MIMEXML'
${MIME_XML.replace(/\n$/, '')}
MIMEXML
command -v update-mime-database >/dev/null 2>&1 && update-mime-database "$MIMEDIR" >/dev/null 2>&1 || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1 || true
mkdir -p "$HOME/AudioMixerPlugins"
echo "Audio Mixer installed in $DEST  (command: $BIN/audio-mixer)"
. "$DEST/ensure-node.sh"
if am_ensure_node; then
  am_ensure_audio "$DEST"
  if am_ask "Start the local server when I log in?"; then "$NODE" "$DEST/client/cli.js" service install || true; fi
else
  echo "Node.js 18+ is not installed: https://nodejs.org/en/download (Audio Mixer asks again the first time it starts)."
fi
case ":$PATH:" in *":$BIN:"*) ;; *) echo "Add $BIN to your PATH to use the command:  echo 'export PATH=\"$BIN:\$PATH\"' >> ~/.profile" ;; esac
echo "Start it with: audio-mixer    Commands: audio-mixer doctor | drivers | npm | uninstall    Remove it with: sh $DEST/uninstall.sh"
`;

const LINUX_UNINSTALL = `#!/bin/sh
# Removes Audio Mixer that install.sh put in place (run it as the same user): the program, the command, the menu entry and the start-at-login entry.
# Downloaded drivers (~/AudioMixerDrivers), plugins (~/AudioMixerPlugins) and the license key stay. --purge also removes the private Node.js and audio
# module that Audio Mixer downloaded into ~/.local/share/audio-mixer.
HERE="$(cd "$(dirname "$0")" && pwd)"
NODE="$(command -v node 2>/dev/null || ls "$HOME/.local/share/audio-mixer/node/bin/node" 2>/dev/null | head -1)"
[ -n "$NODE" ] && [ -f "$HERE/client/cli.js" ] && "$NODE" "$HERE/client/cli.js" service uninstall >/dev/null 2>&1
for B in /usr/local/bin/audio-mixer "$HOME/.local/bin/audio-mixer"; do
  [ -L "$B" ] && case "$(readlink "$B")" in "$HERE"/*) rm -f "$B" && echo "Removed $B" ;; esac
done
rm -f /usr/share/applications/audio-mixer.desktop "$HOME/.local/share/applications/audio-mixer.desktop" 2>/dev/null
rm -f /usr/share/applications/audio-mixer-open.desktop "$HOME/.local/share/applications/audio-mixer-open.desktop" /etc/profile.d/audio-mixer.sh /usr/lib/environment.d/60-audio-mixer.conf "$HOME/.config/environment.d/60-audio-mixer.conf" /usr/share/mime/packages/audio-mixer.xml "$HOME/.local/share/mime/packages/audio-mixer.xml" 2>/dev/null
command -v update-mime-database >/dev/null 2>&1 && { update-mime-database /usr/share/mime >/dev/null 2>&1; update-mime-database "$HOME/.local/share/mime" >/dev/null 2>&1; } || true
[ "\${1:-}" = "--purge" ] && rm -rf "$HOME/.local/share/audio-mixer" && echo "Removed the downloaded Node.js and audio module."
case "$HERE" in
  /opt/audio-mixer|"$HOME"/.local/share/audio-mixer-app) cd / && rm -rf "$HERE" && echo "Removed $HERE" ;;
  *) echo "Not removed (not an install.sh location): $HERE" ;;
esac
echo "Audio Mixer removed."
`;

// AudioMixer-<version>-linux.tar.gz: install.sh / uninstall.sh for every distribution (the .deb is for Debian / Ubuntu)
function buildLinuxTar({ out = path.join(ROOT, 'dist'), app = build({ out }) } = {}) {
  const outAbs = path.resolve(out), version = app.version;
  const base = path.join(outAbs, 'unix', 'linux'), root = path.join(base, `audio-mixer-${version}`);
  fs.rmSync(base, { recursive: true, force: true });
  const dest = path.join(root, 'app');
  fs.cpSync(app.dest, dest, { recursive: true });
  for (const f of ['start-pc-mode.bat', 'start-local-server.bat', 'start-pc-mode.sh']) fs.rmSync(path.join(dest, f), { force: true });
  const put = (rel, text, mode) => { const f = path.join(root, rel); fs.mkdirSync(path.dirname(f), { recursive: true }); fs.writeFileSync(f, text); fs.chmodSync(f, mode); };
  put('app/audio-mixer', LAUNCHER_PORTABLE, 0o755);
  put('app/uninstall.sh', LINUX_UNINSTALL, 0o755);
  put('install.sh', LINUX_INSTALL, 0o755);
  put('uninstall.sh', LINUX_UNINSTALL.replace('HERE="$(cd "$(dirname "$0")" && pwd)"', 'HERE="\${AUDIO_MIXER_APP:-/opt/audio-mixer}"; [ -d "$HERE" ] || HERE="$HOME/.local/share/audio-mixer-app"'), 0o755);   // from the unpacked folder: removes the installed copy
  put('README.txt', `Audio Mixer ${version} for Linux (any distribution)\n\n1. Unpack:   tar xzf AudioMixer-${version}-linux.tar.gz && cd audio-mixer-${version}\n2. Install:  sh install.sh        (as root: /opt/audio-mixer + /usr/local/bin; as a user: ~/.local/share/audio-mixer-app + ~/.local/bin)\n   It downloads Node.js 18+ with npm when missing (official build, checksum verified, no administrator rights) and installs the native audio module.\n3. Start:    audio-mixer           Commands: audio-mixer doctor | drivers | license | plugins | update | npm install audify | service install | uninstall\n4. Remove:   audio-mixer uninstall   (or: sh uninstall.sh, run as the same user; --purge also removes the downloaded Node.js)\n\nDebian / Ubuntu: use the audio-mixer_${version}_all.deb package instead (apt install ./audio-mixer_${version}_all.deb, remove with apt remove audio-mixer).\nDownloaded drivers (~/AudioMixerDrivers), plugins (~/AudioMixerPlugins) and the license key are left in place.\n`, 0o644);
  fs.chmodSync(path.join(dest, 'client', 'cli.js'), 0o755);
  const file = path.join(outAbs, `AudioMixer-${version}-linux.tar.gz`);
  const r = spawnSync('tar', ['--owner=0', '--group=0', '--numeric-owner', '-czf', file, '-C', base, `audio-mixer-${version}`], { encoding: 'utf8' });
  if (r.status !== 0) throw new Error('tar failed: ' + (r.stderr || r.error));
  return { tar: file, version, root, sha256: crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex') };
}

if (require.main === module) {
  const app = build({ out: path.join(ROOT, 'dist') });
  const d = buildDeb({ app }), m = buildMac({ app }), l = buildLinuxTar({ app });
  console.log(`Linux: ${l.tar}\n     SHA-256 ${l.sha256}`);
  console.log(`DEB: ${d.deb}\n     SHA-256 ${d.sha256}\nmacOS: ${m.tar}\n     SHA-256 ${m.sha256}\nNeither package is signed or notarized.`);
}
module.exports = { MAC_SYSTEM_ENV, MIME_XML, openDesktopEntry, profileSh, environmentConf, icns, buildDeb, buildMac, controlFile, desktopEntry, systemdUserUnit, infoPlist, LAUNCHER_LINUX, MAC_LAUNCHER, MAC_INSTALL, MAC_UNINSTALL, MAC_COMMAND, LAUNCHER_PORTABLE, LINUX_INSTALL, LINUX_UNINSTALL, buildLinuxTar };
