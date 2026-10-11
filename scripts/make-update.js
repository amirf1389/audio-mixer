#!/usr/bin/env node
'use strict';
// Builds and signs releases/update.json (the OTA manifest) for the release files of the systems: <os>/releases/ (android, ios, windows, macos, linux; see layout.js).
//   node scripts/make-update.js [--dir <vendor key dir>] [--base <url where the files are served>] [--notes "a|b|c"]
// Default base: https://raw.githubusercontent.com/amirf1389/audio-mixer/main  (the files are committed in <os>/releases/, so a URL ends in .../<os>/releases/<file>).
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { signManifest } = require('../bridge/update');
const { loadPrivate, vendorDir } = require('./license');

const { ROOT, osOfKey, releasesRel, findRelease } = require('./layout');
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };

// `releases`: one flat folder that holds all the files (URLs are base/<file>); without it the files are looked up in <os>/releases/ and the URLs follow that layout,
// unless `flatUrls` is set (a server that serves every file from one /releases/ folder, see scripts/ota.js).
function build({ releases, flatUrls = false, version, base, notes = [], now = new Date() } = {}) {
  const sha = f => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
  const want = {
    'win-x64-exe': `Audio Mixer-${version}.exe`, 'win-x64-msi': `AudioMixer-${version}-x64.msi`, 'win-x86-msi': `AudioMixer-${version}-x86.msi`,
    'linux-deb': `audio-mixer_${version}_all.deb`, 'linux-tar': `AudioMixer-${version}-linux.tar.gz`, 'macos': `AudioMixer-${version}-macos.tar.gz`, 'macos-dmg': `AudioMixer-${version}-macos.dmg`,
    'android-apk': `AudioMixer-${version}-android.apk`, 'ios-project': `AudioMixer-${version}-ios-xcode-project.tar.gz`,
  };
  const files = {};
  for (const [key, name] of Object.entries(want)) {
    const f = releases ? path.join(releases, name) : findRelease(name, osOfKey(key));
    if (!f || !fs.existsSync(f)) continue;
    files[key] = { name, url: `${base}/${releases || flatUrls ? '' : releasesRel(key) + '/'}${encodeURIComponent(name)}`, size: fs.statSync(f).size, sha256: sha(f) };
  }
  if (!Object.keys(files).length) throw new Error('no release files for version ' + version + ' in ' + (releases || '<os>/releases'));
  return { product: 'audio-mixer', version, channel: 'stable', released: now.toISOString().slice(0, 10), notes, files };
}

if (require.main === module) {
  const a = process.argv.slice(2);
  try {
    const version = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8')).version;
    const manifest = build({ version, base: arg(a, '--base', 'https://raw.githubusercontent.com/amirf1389/audio-mixer/main'), notes: String(arg(a, '--notes', '')).split('|').filter(Boolean) });
    const env = signManifest(manifest, loadPrivate(vendorDir(a)));
    fs.writeFileSync(path.join(ROOT, 'releases', 'update.json'), JSON.stringify(env, null, 2) + '\n');
    console.log(`releases/update.json signed: version ${version}, ${Object.keys(manifest.files).length} files`);
  } catch (e) { console.error('Error: ' + e.message); process.exit(1); }
}
module.exports = { build };
