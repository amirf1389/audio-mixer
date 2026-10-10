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

const ROOT = path.resolve(__dirname, '..');
const OPT = 'opt/audio-mixer';
const HOMEPAGE = 'https://github.com/amirf1389/audio-mixer';

const LAUNCHER_LINUX = `#!/bin/sh
# Audio Mixer: starts the local server and opens the mixer in the browser (PC mode). Options are passed on, for example --no-open or --port 8800.
# Node.js 18+ is installed automatically from nodejs.org when missing (into ~/.local/share/audio-mixer, checksum verified, no administrator rights),
# and the native audio module (Audify) is installed once. AUDIO_MIXER_YES=1 skips the questions, AUDIO_MIXER_NO_NATIVE=1 skips the audio module.
APP=/${OPT}
. "$APP/ensure-node.sh"
am_ensure_node || exit 1
am_ensure_audio "$APP"
exec "$NODE" "$APP/client/cli.js" "$@"
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
echo "Audio Mixer installed in /${OPT}. Run: audio-mixer   (start at login: systemctl --user enable --now audio-mixer)"
exit 0
`;
const POSTRM = `#!/bin/sh
set -e
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
am_ensure_node || { osascript -e 'display dialog "Audio Mixer needs Node.js 18 or newer and it could not be installed automatically. Install it from nodejs.org, then start Audio Mixer again." buttons {"OK"} with icon caution' >/dev/null 2>&1; open "https://nodejs.org/en/download" 2>/dev/null; exit 1; }
am_ensure_audio "$APP"
exec "$NODE" "$APP/client/cli.js" "$@"
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
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSMicrophoneUsageDescription</key><string>Audio Mixer reads your audio interface and microphone as mixer inputs.</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
`;

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
echo "Done. Open Audio Mixer from $DEST. Remove it later with uninstall.command."
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
rm -f "$HOME/Library/LaunchAgents/com.audiomixer.bridge.plist"
rm -rf "$HOME/.local/share/audio-mixer"   # the private Node.js and audio module that Audio Mixer downloaded
echo "Audio Mixer removed."
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
  put('install.command', MAC_INSTALL, 0o755);
  put('uninstall.command', MAC_UNINSTALL, 0o755);
  put('README.txt', `Audio Mixer ${version} for macOS\n\n1. Install Node.js 18 or newer if you do not have it: https://nodejs.org/en/download\n2. Double-click install.command (copies the app to /Applications, or ~/Applications).\n   The app is not notarized: if macOS blocks it, right-click > Open once, or run: xattr -dr com.apple.quarantine "/Applications/Audio Mixer.app"\n3. Start "Audio Mixer": the mixer opens in your browser. Start at login: answer y in install.command, or run the app's client with: service install\n\nFiles: app in /Applications/Audio Mixer.app, login item in ~/Library/LaunchAgents, downloaded drivers in ~/AudioMixerDrivers,\nVST plugins are read from /Library/Audio/Plug-Ins/VST3 and ~/Library/Audio/Plug-Ins/VST3 (and ~/AudioMixerPlugins).\nRemove everything with uninstall.command.\n`, 0o644);
  fs.chmodSync(path.join(res, 'client', 'cli.js'), 0o755);
  const file = path.join(outAbs, `AudioMixer-${version}-macos.tar.gz`);
  const r = spawnSync('tar', ['--owner=0', '--group=0', '--numeric-owner', '-czf', file, '-C', path.dirname(root), 'AudioMixer'], { encoding: 'utf8' });
  if (r.status !== 0) throw new Error('tar failed: ' + (r.stderr || r.error));
  return { tar: file, version, sha256: crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex') };
}

if (require.main === module) {
  const app = build({ out: path.join(ROOT, 'dist') });
  const d = buildDeb({ app }), m = buildMac({ app });
  console.log(`DEB: ${d.deb}\n     SHA-256 ${d.sha256}\nmacOS: ${m.tar}\n     SHA-256 ${m.sha256}\nNeither package is signed or notarized.`);
}
module.exports = { buildDeb, buildMac, controlFile, desktopEntry, systemdUserUnit, infoPlist, LAUNCHER_LINUX, MAC_LAUNCHER, MAC_INSTALL, MAC_UNINSTALL };
