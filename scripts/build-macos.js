#!/usr/bin/env node
'use strict';
// macOS packages: the app bundle and a disk image.
//   node scripts/build-macos.js [--out releases]
//   AudioMixer-<version>-macos.dmg          disk image (UDIF, zlib): "Audio Mixer.app", an Applications shortcut and a read-me. Drag the app onto Applications.
//   AudioMixer-<version>-macos-app.zip      the same "Audio Mixer.app" zipped (permissions kept)
//   AudioMixer-<version>-macos.tar.gz       app + install.command / uninstall.command (build-unix.js)
// Built on Linux / Windows / macOS: the disk image is an ISO 9660 + Rock Ridge volume (genisoimage or xorriso) wrapped in UDIF by scripts/mkdmg.js, no hdiutil needed.
// The app is not signed or notarized (that needs a Mac and an Apple developer account): Gatekeeper asks once, see "Read me first.txt".
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const { build } = require('./build');
const { buildMac } = require('./build-unix');
const { makeDmg } = require('./mkdmg');

const ROOT = path.resolve(__dirname, '..');
const sha = f => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const sum = f => fs.writeFileSync(f + '.sha256', `${sha(f)}  ${path.basename(f)}\n`);

const readme = version => `Audio Mixer ${version} for macOS

1. Drag "Audio Mixer" onto "Applications".
2. Open it from Launchpad or Applications. The app is not signed or notarized, so macOS asks once:
   right-click "Audio Mixer" > Open > Open (or, if there is no Open button: System Settings > Privacy & Security > Open Anyway).
   In Terminal: xattr -dr com.apple.quarantine "/Applications/Audio Mixer.app"
3. Audio Mixer needs Node.js 18 or newer. If it is missing the app offers to download the official build from nodejs.org (checksum verified,
   no administrator rights); install it yourself from https://nodejs.org if you prefer. The first start also installs the native audio module.
4. The mixer opens in your browser. Plugins (VST3 / .vst) are read from /Library/Audio/Plug-Ins/VST3, ~/Library/Audio/Plug-Ins/VST3 and ~/AudioMixerPlugins.

5. Command line (optional): double-click "Add audio-mixer command.command" once. Then in Terminal: audio-mixer (start), audio-mixer doctor,
   audio-mixer drivers, audio-mixer npm install audify, audio-mixer uninstall ...

Remove it: double-click "Uninstall Audio Mixer.command" (removes the app, the start-at-login entry and the command; your drivers, plugins and
license stay), or drag the app to the Bin. The private Node.js and audio module live in ~/.local/share/audio-mixer.
`;

function isoTool() {
  for (const [cmd, args] of [['genisoimage', ['-quiet']], ['mkisofs', ['-quiet']], ['xorriso', ['-as', 'mkisofs', '-quiet']]]) {
    if (spawnSync(cmd, ['--version'], { encoding: 'utf8' }).error === undefined || spawnSync(cmd, ['-version'], { encoding: 'utf8' }).error === undefined) return { cmd, args };
  }
  return null;
}

function buildMacApp({ out = path.join(ROOT, 'releases') } = {}) {
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'macos-'));
  const app = build({ out: work });
  const m = buildMac({ out: work, app });
  const version = m.version, outAbs = path.resolve(out); fs.mkdirSync(outAbs, { recursive: true });
  const res = { version, dmg: null, zip: null, tar: null };
  // 1. app zip (permissions and the executable bit kept by zip's unix extra fields)
  res.zip = path.join(outAbs, `AudioMixer-${version}-macos-app.zip`); fs.rmSync(res.zip, { force: true });
  const z = spawnSync('zip', ['-qry', res.zip, 'Audio Mixer.app'], { cwd: path.dirname(m.bundle), encoding: 'utf8' });
  if (z.status !== 0) throw new Error('zip failed: ' + (z.stderr || z.error));
  sum(res.zip);
  // 2. disk image
  const tool = isoTool();
  if (!tool) throw new Error('genisoimage (or xorriso) is needed for the .dmg: apt install genisoimage');
  const stage = path.join(work, 'dmg'); fs.mkdirSync(stage);
  fs.cpSync(m.bundle, path.join(stage, 'Audio Mixer.app'), { recursive: true, verbatimSymlinks: true });
  fs.symlinkSync('/Applications', path.join(stage, 'Applications'));
  fs.writeFileSync(path.join(stage, 'Read me first.txt'), readme(version));
  // after dragging the app to Applications: the audio-mixer command on PATH, and the uninstaller (removes the app, the login item and the command)
  const { MAC_COMMAND, MAC_UNINSTALL } = require('./build-unix');
  fs.writeFileSync(path.join(stage, 'Add audio-mixer command.command'), MAC_COMMAND, { mode: 0o755 });
  fs.writeFileSync(path.join(stage, 'Uninstall Audio Mixer.command'), MAC_UNINSTALL, { mode: 0o755 });
  for (const f of ['Add audio-mixer command.command', 'Uninstall Audio Mixer.command']) fs.chmodSync(path.join(stage, f), 0o755);   // double-clickable in Finder
  const iso = path.join(work, 'audio-mixer.iso');
  const r = spawnSync(tool.cmd, [...tool.args, '-V', `Audio Mixer ${version}`.slice(0, 32), '-D', '-R', '-J', '-joliet-long', '-no-pad', '-o', iso, stage], { encoding: 'utf8' });
  if (r.status !== 0) throw new Error(tool.cmd + ' failed: ' + (r.stderr || r.stdout));
  res.dmg = path.join(outAbs, `AudioMixer-${version}-macos.dmg`);
  const d = makeDmg(iso, res.dmg); res.dmgInfo = d; sum(res.dmg);
  // 3. the tar.gz with install.command
  res.tar = path.join(outAbs, path.basename(m.tar)); fs.copyFileSync(m.tar, res.tar); sum(res.tar);
  fs.rmSync(work, { recursive: true, force: true });
  return res;
}

if (require.main === module) {
  const a = process.argv.slice(2), oi = a.indexOf('--out');
  try {
    const r = buildMacApp({ out: oi >= 0 ? a[oi + 1] : undefined });
    console.log(`DMG: ${r.dmg}  (${(fs.statSync(r.dmg).size / 1048576).toFixed(1)} MB)\nApp: ${r.zip}\nTar: ${r.tar}\nNot signed or notarized: see "Read me first.txt" in the disk image.`);
  } catch (e) { console.error('macOS build failed: ' + e.message); process.exit(1); }
}
module.exports = { buildMacApp, readme, isoTool };
