#!/usr/bin/env node
'use strict';
// Builds the portable PC-mode package: the mixer page, the local system server and the client launcher, ready to copy to any PC with Node.js.
//   node scripts/build.js                 -> dist/audio-mixer-pc/
//   node scripts/build.js --archive       -> also dist/audio-mixer-pc-<version>.tar.gz
//   node scripts/build.js --out <folder>  -> build somewhere else
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const FILES = [
  'index.html', 'boot.html', 'README.md', 'LICENSE', 'package.json', 'start-pc-mode.bat', 'start-pc-mode.sh', 'ensure-node.sh', 'start-local-server.bat',
  'client/cli.js', 'client/service.js', 'client/verify.js',
  'bridge/server.js', 'bridge/osdetect.js', 'bridge/detect.js', 'bridge/catalog.js', 'bridge/nowplaying.js', 'bridge/interfaces.js', 'bridge/asio-lock.js', 'bridge/audify.js', 'bridge/plugins.js', 'bridge/streams.js', 'bridge/levels.js', 'bridge/universal.js', 'bridge/license.js', 'bridge/license-public.json', 'bridge/update.js', 'bridge/duplex.js', 'bridge/winnative.js', 'bridge/security.js', 'bridge/sysaudio.js', 'bridge/inserts.js', 'bridge/pluginhost.js', 'bridge/bluetooth.js', 'bridge/input.js', 'bridge/output.js', 'bridge/volume.js', 'bridge/ws.js',
  'bridge/package.json', 'bridge/README.md',
  'Audio Mixer.vbs', 'native/win/src/AudioDevices.cpp', 'native/win/x64/AudioDevices.exe', 'native/win/x86/AudioDevices.exe', 'native/win/vbs/audio-devices.vbs',
  'native/mac/AudioDevices.swift', 'native/host/src/PluginHost.cpp', 'native/host/test/GainPlugin.cpp', 'native/host/x64/PluginHost.exe', 'native/host/x86/PluginHost.exe',
  'scripts/license.js', 'scripts/make-update.js',
];

// Whole folders that ship with the app (deploy/ = nginx / fail2ban hosting files). Never dotfiles or folders (.vscode, .git, .github ...).
const DIRS = ['deploy'];
function listDir(rel) {
  const out = [];
  const walk = r => {
    for (const e of fs.readdirSync(path.join(ROOT, r), { withFileTypes: true })) {
      if (e.name.startsWith('.') || e.name === 'node_modules') continue;
      const q = r + '/' + e.name;
      if (e.isDirectory()) walk(q); else out.push(q);
    }
  };
  walk(rel);
  return out.sort();
}

function sha256(file) { return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'); }

function build({ out = path.join(ROOT, 'dist'), archive = false } = {}) {
  const pkg = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
  const dest = path.join(path.resolve(out), 'audio-mixer-pc');
  if (path.basename(dest) !== 'audio-mixer-pc') throw new Error('refusing to clean an unexpected folder: ' + dest);
  fs.rmSync(dest, { recursive: true, force: true });
  const ALL = [...FILES, ...DIRS.flatMap(listDir)];

  for (const rel of ALL) {
    const from = path.join(ROOT, rel), to = path.join(dest, rel);
    if (!fs.existsSync(from)) throw new Error('missing build input: ' + rel);
    fs.mkdirSync(path.dirname(to), { recursive: true });
    fs.copyFileSync(from, to);
    if (rel.endsWith('.sh') || rel.startsWith('client/cli')) fs.chmodSync(to, 0o755);
  }
  // The package has no tests or dev scripts: keep only what runs the product.
  for (const rel of ['package.json', 'bridge/package.json']) {
    const p = path.join(dest, rel), j = JSON.parse(fs.readFileSync(p, 'utf8'));
    j.scripts = Object.fromEntries(Object.entries(j.scripts || {}).filter(([k]) => !/^(test|audit)/.test(k)));
    fs.writeFileSync(p, JSON.stringify(j, null, 2) + '\n');
  }
  for (const rel of ALL.filter(f => f.endsWith('.js'))) {
    const r = spawnSync(process.execPath, ['--check', path.join(dest, rel)], { encoding: 'utf8' });
    if (r.status !== 0) throw new Error('syntax check failed for ' + rel + ': ' + r.stderr);
  }
  // The installed tree must never carry editor / VCS folders (.vscode, .git, .github ...).
  const stray = ALL.filter(f => f.split('/').some(p => p.startsWith('.')));
  if (stray.length) throw new Error('dotfiles must not be installed: ' + stray.join(', '));
  const manifest = [...ALL].sort().map(rel => `${sha256(path.join(dest, rel))}  ${rel}`).join('\n') + '\n';
  fs.writeFileSync(path.join(dest, 'MANIFEST.sha256'), manifest);

  let archivePath = null;
  if (archive) {
    archivePath = path.join(path.resolve(out), `audio-mixer-pc-${pkg.version}.tar.gz`);
    const r = spawnSync('tar', ['-czf', archivePath, '-C', path.resolve(out), 'audio-mixer-pc'], { encoding: 'utf8' });
    if (r.status !== 0) throw new Error('tar failed: ' + (r.stderr || r.error));
  }
  return { dest, files: ALL.length + 1, archive: archivePath, version: pkg.version };
}

if (require.main === module) {
  const a = process.argv.slice(2), oi = a.indexOf('--out');
  try {
    const r = build({ archive: a.includes('--archive'), out: oi >= 0 ? a[oi + 1] : undefined });
    console.log(`Built ${r.files} files (v${r.version}) -> ${r.dest}${r.archive ? '\nArchive: ' + r.archive : ''}`);
    console.log('On the target PC (Node.js 18+): start-pc-mode.bat (Windows) or ./start-pc-mode.sh (macOS / Linux)');
  } catch (e) { console.error('Build failed: ' + e.message); process.exit(1); }
}
module.exports = { build, FILES, DIRS, listDir };
