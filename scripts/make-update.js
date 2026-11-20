#!/usr/bin/env node
'use strict';
// Builds and signs releases/update.json (the OTA manifest) for the release files of the systems: <os>/releases/ (android, ios, windows, macos, linux; see layout.js).
//   node scripts/make-update.js [--dir <vendor key dir>] [--base <url where the files are served>] [--notes "a|b|c"]
// Default base: https://raw.githubusercontent.com/amirf1389/audio-mixer/main  (the files are committed in <os>/releases/, so a URL ends in .../<os>/releases/<file>).
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { signManifest } = require('../bridge/update');
const { keyFor, vendorDir } = require('./license');

const { ROOT, osOfKey, releasesRel, findRelease } = require('./layout');
function readFeatures() { try { const f = JSON.parse(fs.readFileSync(path.join(ROOT, 'releases', 'features.json'), 'utf8')); return Array.isArray(f.features) ? f.features : []; } catch (_) { return []; } }
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };

// `releases`: one flat folder that holds all the files (URLs are base/<file>); without it the files are looked up in <os>/releases/ and the URLs follow that layout,
// unless `flatUrls` is set (a server that serves every file from one /releases/ folder, see scripts/ota.js).
// `features`: releases/features.json (the feature feed the console shows as WHAT'S NEW with a NEW badge; edited on the website's manage page or by hand)
function build({ releases, flatUrls = false, version, base, notes = [], features = readFeatures(), now = new Date() } = {}) {
  const sha = f => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
  const want = {
    // the setup program: its name since the rename to AmF X1, then the one before it
    // a channel build (1.22.1.0 Beta) names its Windows installers "<version>-beta"; both spellings are found
    'win-x64-exe': [`AmF-X1-${version}-setup.exe`, `AmF X1-${version}-beta.exe`, `AmF X1-${version}.exe`, `Audio Mixer-${version}.exe`], 'win-x64-msi': [`AudioMixer-${version}-beta-x64.msi`, `AudioMixer-${version}-x64.msi`], 'win-x86-msi': [`AudioMixer-${version}-beta-x86.msi`, `AudioMixer-${version}-x86.msi`],
    'linux-deb': `audio-mixer_${version}_all.deb`, 'linux-tar': `AudioMixer-${version}-linux.tar.gz`, 'macos': `AudioMixer-${version}-macos.tar.gz`, 'macos-dmg': `AudioMixer-${version}-macos.dmg`,
    // the Android packages carry a build number (AmF-X1-<version>-b<N>-android.apk): the highest build of this version is taken
    'android-apk': [new RegExp('^AmF-X1-' + version.replace(/\./g, '\\.') + '-b(\\d+)-android\\.apk$'), `AudioMixer-${version}-android.apk`], 'android-mixer-apk': [new RegExp('^AmF-X1-Mixer-' + version.replace(/\./g, '\\.') + '-b(\\d+)-android\\.apk$')], 'ios-project': `AudioMixer-${version}-ios-xcode-project.tar.gz`,
    'win-asio-driver': `AudioMixer-${version}-asio-driver-windows.zip`,
    'win-x64-native': ['AmF X1.exe', `AudioMixer-${version}-native-windows-x64.exe`], 'win-x86-native': ['AmF X1 (32-bit).exe', `AudioMixer-${version}-native-windows-x86.exe`],   // AmF X1.exe --update (the old name is still found)
  };
  const files = {};
  for (const [key, names] of Object.entries(want)) {
    let name = null, f = null;
    for (let n of [].concat(names)) {
      if (n instanceof RegExp) {   // a name with a build number: the newest build in the folder
        const dir = releases || path.join(ROOT, 'releases', osOfKey(key) || ''); let best = null;
        try { for (const e of fs.readdirSync(dir)) { const m = n.exec(e); if (m && (!best || Number(m[1]) > best.b)) best = { e, b: Number(m[1]) }; } } catch (_) {}
        if (best) { name = best.e; f = path.join(dir, best.e); break; }
        continue;
      }
      const c = releases ? path.join(releases, n) : findRelease(n, osOfKey(key)); if (c && fs.existsSync(c)) { name = n; f = c; break; } }
    if (!f) continue;
    const rel = releases || flatUrls ? '' : path.relative(ROOT, path.dirname(f)).split(path.sep).map(encodeURIComponent).join('/') + '/';   // where the file really is (releases/<os>/ or platforms/<os>/releases/)
    files[key] = { name, url: `${base}/${rel}${encodeURIComponent(name)}`, size: fs.statSync(f).size, sha256: sha(f) };
  }
  if (!Object.keys(files).length) throw new Error('no release files for version ' + version + ' in ' + (releases || '<os>/releases'));
  // PLUG-IN DOWNLOADS: releases/plugins.json lists the plug-ins the console may offer; their files live in releases/plugins/. Size and
  // sha256 are taken from the files here, so the signed list always matches what is published.
  const plugins = [];
  const pj = path.join(ROOT, 'releases', 'plugins.json');
  if (fs.existsSync(pj)) for (const p of (JSON.parse(fs.readFileSync(pj, 'utf8')).plugins || [])) {
    const pf = {};
    for (const [key, name] of Object.entries(p.files || {})) { const f = path.join(ROOT, 'releases', 'plugins', name); if (!fs.existsSync(f)) throw new Error('releases/plugins/' + name + ' is listed in plugins.json but is not there');
      pf[key] = { name, url: `${base}/releases/plugins/${encodeURIComponent(name)}`, size: fs.statSync(f).size, sha256: sha(f) }; }
    if (Object.keys(pf).length) plugins.push({ id: p.id, name: p.name, version: p.version, note: p.note || '', files: pf });
  }
  // THE PRESET PACK (releases/presets/presets-pack.json, scripts/make-presets.js): its version and sha256. An app takes a downloaded pack (from the
  // licence portal's /portal/presets or this URL) only when its sha256 is the one in a manifest the owner signed; otherwise it keeps its built-in pack.
  const presets = releases ? null : presetsEntry(base);   // a flat release folder (the OTA server's) has no pack
  return Object.assign({ product: 'audio-mixer', version, channel: require('./release-label').channel() || 'stable', released: now.toISOString().slice(0, 10), notes, features, files, plugins }, presets ? { presets } : {});
}
function presetsEntry(base, file = path.join(ROOT, 'releases', 'presets', 'presets-pack.json')) {
  let text; try { text = fs.readFileSync(file); } catch (_) { return null; }
  let p; try { p = JSON.parse(text); } catch (_) { throw new Error('releases/presets/presets-pack.json is not JSON'); }
  if (typeof p.version !== 'string') return null;
  return { version: p.version, url: `${base}/releases/presets/presets-pack.json`, size: text.length, sha256: crypto.createHash('sha256').update(text).digest('hex') };
}

if (require.main === module) (async () => {
  const a = process.argv.slice(2);
  try {
    const version = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8')).version;
    const manifest = build({ version, base: arg(a, '--base', 'https://raw.githubusercontent.com/amirf1389/audio-mixer/main'), notes: String(arg(a, '--notes', '')).split('|').filter(Boolean) });
    // BUILD MANAGEMENT (no key needed): every package build runs --prepare. It writes the manifest of the packages just built, UNSIGNED, to
    // releases/update.pending.json and says whether the published, signed update.json is behind. Installed consoles only take a manifest
    // signed with the vendor key, so the release goes out to them when the vendor runs this script without --prepare (it asks for the key's PIN).
    if (a.includes('--prepare') || a.includes('--check')) {
      let live = null; try { live = JSON.parse(fs.readFileSync(path.join(ROOT, 'releases', 'update.json'), 'utf8')).payload; if (typeof live === 'string') live = JSON.parse(live); } catch (_) {}
      const same = live && live.version === version && Object.entries(manifest.files).every(([k, f]) => live.files && live.files[k] && live.files[k].sha256 === f.sha256);
      if (a.includes('--prepare')) { fs.writeFileSync(path.join(ROOT, 'releases', 'update.pending.json'), JSON.stringify(manifest, null, 2) + '\n'); console.log(`releases/update.pending.json written (unsigned): version ${version}, ${Object.keys(manifest.files).join(', ')}`); }
      console.log(same ? `releases/update.json is signed for ${version} and matches these packages: installed consoles are offered this release.`
        : `releases/update.json is signed for ${live ? live.version : 'nothing'}; the packages are ${version}. Installed consoles are NOT offered ${version} until the vendor signs it:  node scripts/make-update.js`);
      process.exit(a.includes('--check') && !same ? 2 : 0);
    }
    const env = signManifest(manifest, await keyFor(vendorDir(a)));   // a locked key folder asks for the PIN
    fs.writeFileSync(path.join(ROOT, 'releases', 'update.json'), JSON.stringify(env, null, 2) + '\n');
    console.log(`releases/update.json signed: version ${version}, ${Object.keys(manifest.files).length} files`);
  } catch (e) { console.error('Error: ' + e.message); process.exit(1); }
})();
module.exports = { build, presetsEntry };
