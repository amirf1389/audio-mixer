#!/usr/bin/env node
'use strict';
// Builds "AmF X1 Key Manager" as one executable with Node's single executable application support (SEA): the official Node binary of the
// SAME version as the Node running this script + a blob holding tools/key-manager/sea-entry.js and, as assets, the generator and every module it
// requires (found by following require() from tools/key-manager/main.js). Nothing else is embedded: no key folder, no private key (refused).
//   node tools/key-manager/build.js [--target win-x64|darwin-arm64|darwin-x64] [--cache DIR] [--out DIR] [--keep-exe] [--stage DIR]
//     win-x64 (default)  builds on Linux, macOS or Windows -> releases/windows/AmF-X1-Key-Manager-<version>-win-x64.zip (+ .sha256, SHA256SUMS.txt)
//     darwin-*           on a Mac only (codesign): -> releases/macos/AmF-X1-Key-Manager-<version>-macos-<arch>.zip holding "AmF X1 Key Manager.app"
//     --stage DIR        only copy the app files + manifest into DIR (what the tests run), no download, no executable
// The Node binary comes from https://nodejs.org/dist/<version>/ only: SHASUMS256.txt.asc is checked with gpgv against the Node release keys
// (github.com/nodejs/release-keys) when gpgv is installed, and the binary must match its SHA-256 in that signed list. Downloads use curl
// (it follows HTTPS_PROXY). postject is run with npx at a pinned version.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const { builtinModules } = require('node:module');

const ROOT = path.resolve(__dirname, '..', '..');
const NAME = 'AmF X1 Key Manager';
const POSTJECT = 'postject@1.0.0-alpha.6';
const FUSE = 'NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2';
const ENTRIES = ['tools/key-manager/main.js'];
const EXTRA = ['package.json', 'bridge/license-public.json', 'bridge/license-sandbox-public.json', 'bridge/license-admin-public.json'];   // public keys only
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 && a[i + 1] !== undefined ? a[i + 1] : d; };
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const PEM_PRIVATE = /-----BEGIN ((RSA|EC|DSA|OPENSSH|ENCRYPTED) )?PRIVATE KEY-----\s*\r?\n\s*[A-Za-z0-9+/=]{40,}/;

// every repository file reached by require('./…') / require('../…') from the entries (lazy requires included: the scan is textual)
function closure(root = ROOT, entries = ENTRIES) {
  const seen = new Set();
  const resolve = (from, s) => { const b = path.resolve(path.dirname(from), s); for (const c of [b, b + '.js', b + '.json', path.join(b, 'index.js')]) { try { if (fs.statSync(c).isFile()) return c; } catch (_) { /* next */ } } return null; };
  const walk = f => {
    if (seen.has(f)) return; seen.add(f);
    if (!f.endsWith('.js')) return;
    for (const m of fs.readFileSync(f, 'utf8').matchAll(/require\(\s*['"]([^'"]+)['"]\s*\)/g)) {
      const s = m[1]; if (!s.startsWith('.') || s.startsWith('node:') || builtinModules.includes(s)) continue;
      const r = resolve(f, s); if (r && r.startsWith(root + path.sep)) walk(r);
    }
  };
  entries.forEach(e => walk(path.join(root, e)));
  for (const e of EXTRA) { const f = path.join(root, e); if (fs.existsSync(f)) seen.add(f); }
  return [...seen].map(f => path.relative(root, f).split(path.sep).join('/')).sort();
}

function stage(dir, { root = ROOT } = {}) {
  const files = closure(root), man = { name: NAME, version: require(path.join(root, 'package.json')).version, files: {} };
  for (const rel of files) {
    const base = path.basename(rel);
    if (/^private\.pem|\.enc$|^edition-token/i.test(base)) throw new Error('refusing to embed ' + rel);
    const data = fs.readFileSync(path.join(root, rel));
    if (PEM_PRIVATE.test(data.toString('latin1'))) throw new Error('refusing to embed ' + rel + ': it contains a private key');
    fs.mkdirSync(path.join(dir, path.dirname(rel)), { recursive: true });
    fs.writeFileSync(path.join(dir, rel), data);
    man.files[rel] = sha(data);
  }
  man.id = sha(JSON.stringify(man.files)).slice(0, 12);
  fs.writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify(man, null, 1) + '\n');
  return man;
}

function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { stdio: 'inherit', shell: false, ...opts });
  if (r.error) throw r.error;
  if (r.status !== 0) throw new Error(cmd + ' exited with ' + r.status);
}
const curl = (url, out) => run('curl', ['-sSfL', '--retry', '3', '-o', out, url]);

// the official Node binary for the target, verified against the signed SHASUMS256 list; cached in DIR
function nodeBinary(target, version, cache) {
  fs.mkdirSync(cache, { recursive: true });
  const base = 'https://nodejs.org/dist/' + version + '/';
  const asc = path.join(cache, 'SHASUMS256-' + version + '.txt.asc');
  if (!fs.existsSync(asc)) curl(base + 'SHASUMS256.txt.asc', asc);
  let list = fs.readFileSync(asc, 'utf8'), signed = false;
  const gpgv = spawnSync('gpgv', ['--version'], { stdio: 'ignore' });
  if (!gpgv.error && gpgv.status === 0) {
    const ring = path.join(cache, 'node-release-keys.kbx');
    if (!fs.existsSync(ring)) curl('https://raw.githubusercontent.com/nodejs/release-keys/main/gpg/pubring.kbx', ring);
    const v = spawnSync('gpgv', ['--keyring', ring, '--output', '-', asc], { encoding: 'utf8', maxBuffer: 1 << 24 });
    if (v.status !== 0) throw new Error('SHASUMS256.txt.asc: the signature does not verify against the Node release keys\n' + v.stderr);
    list = v.stdout; signed = true;   // only the signed text is used from here on
    console.log('SHASUMS256.txt.asc: good signature from a Node release key');
  } else console.warn('gpgv is not installed: SHASUMS256 is used as fetched over HTTPS from nodejs.org (signature not checked)');
  const want = target === 'win-x64' ? 'win-x64/node.exe' : 'node-' + version + '-' + target + '.tar.gz';
  const line = list.split('\n').find(l => l.trim().endsWith('  ' + want)); if (!line) throw new Error(want + ' is not in SHASUMS256 for ' + version);
  const hash = line.trim().split(/\s+/)[0];
  const file = path.join(cache, version + '-' + want.replace(/\//g, '-'));
  if (!fs.existsSync(file) || sha(fs.readFileSync(file)) !== hash) curl(base + want, file);
  if (sha(fs.readFileSync(file)) !== hash) { fs.rmSync(file, { force: true }); throw new Error(want + ': SHA-256 mismatch'); }
  console.log(want + ': SHA-256 ' + hash + ' matches' + (signed ? ' the signed list' : ''));
  if (target === 'win-x64') return file;
  const dir = fs.mkdtempSync(path.join(cache, 'x-'));
  run('tar', ['-xzf', file, '-C', dir, 'node-' + version + '-' + target + '/bin/node']);
  return path.join(dir, 'node-' + version + '-' + target, 'bin', 'node');
}

// PE optional header Subsystem: 3 = console, 2 = Windows GUI (no console window opens; stdout goes nowhere, the app logs to a file)
function setWindowsGui(buf) {
  if (buf.readUInt16LE(0) !== 0x5a4d) throw new Error('not a PE file');
  const pe = buf.readUInt32LE(0x3c); if (buf.readUInt32LE(pe) !== 0x4550) throw new Error('no PE header');
  const opt = pe + 24, magic = buf.readUInt16LE(opt); if (magic !== 0x20b && magic !== 0x10b) throw new Error('unknown optional header');
  const at = opt + 68, was = buf.readUInt16LE(at);
  if (was !== 3 && was !== 2) throw new Error('unexpected subsystem ' + was);
  buf.writeUInt16LE(2, at); return was;
}

function updateSums(dir, file, hash) {
  const f = path.join(dir, 'SHA256SUMS.txt'), name = path.basename(file);
  const old = fs.existsSync(f) ? fs.readFileSync(f, 'utf8').split('\n').filter(l => l.trim() && !l.trim().endsWith('  ' + name)) : [];
  fs.writeFileSync(f, [...old, hash + '  ' + name].join('\n') + '\n');
}

function build(argv) {
  const target = arg(argv, '--target', 'win-x64');
  if (!['win-x64', 'darwin-arm64', 'darwin-x64'].includes(target)) throw new Error('--target must be win-x64, darwin-arm64 or darwin-x64');
  const mac = target.startsWith('darwin');
  if (mac && process.platform !== 'darwin') throw new Error('the macOS app needs codesign: build it on a Mac (tools/key-manager/macos/make-app.sh)');
  const version = require(path.join(ROOT, 'package.json')).version;
  const work = fs.mkdtempSync(path.join(arg(argv, '--work', os.tmpdir()), 'amfx1-km-'));
  const cache = path.resolve(arg(argv, '--cache', path.join(os.homedir(), '.cache', 'amfx1-key-manager-build')));
  const outDir = path.resolve(arg(argv, '--out', path.join(ROOT, 'releases', mac ? 'macos' : 'windows')));
  try {
    const app = path.join(work, 'app'), man = stage(app);
    console.log('staged ' + Object.keys(man.files).length + ' files (id ' + man.id + ')');
    const assets = { 'manifest.json': path.join(app, 'manifest.json') };
    for (const rel of Object.keys(man.files)) assets[rel] = path.join(app, rel);
    const blob = path.join(work, 'sea.blob');
    fs.writeFileSync(path.join(work, 'sea-config.json'), JSON.stringify({ main: path.join(__dirname, 'sea-entry.js'), output: blob, disableExperimentalSEAWarning: true, useSnapshot: false, useCodeCache: false, assets }));
    run(process.execPath, ['--experimental-sea-config', path.join(work, 'sea-config.json')]);
    const node = nodeBinary(target, process.version, cache);
    const exe = path.join(work, mac ? 'AmF X1 Key Manager' : NAME + '.exe');
    fs.copyFileSync(node, exe); fs.chmodSync(exe, 0o755);
    if (mac) run('codesign', ['--remove-signature', exe]);
    run(process.platform === 'win32' ? 'npx.cmd' : 'npx', ['--yes', POSTJECT, exe, 'NODE_SEA_BLOB', blob, '--sentinel-fuse', FUSE, ...(mac ? ['--macho-segment-name', 'NODE_SEA'] : [])], { shell: process.platform === 'win32' });
    fs.rmSync(blob, { force: true });
    fs.mkdirSync(outDir, { recursive: true });
    let zip;
    if (!mac) {
      const b = fs.readFileSync(exe); setWindowsGui(b); fs.writeFileSync(exe, b);
      const pack = path.join(work, 'pack', NAME); fs.mkdirSync(pack, { recursive: true });
      fs.renameSync(exe, path.join(pack, NAME + '.exe'));
      fs.writeFileSync(path.join(pack, 'READ ME.txt'), readme(version).replace(/\n/g, '\r\n'));
      zip = path.join(outDir, 'AmF-X1-Key-Manager-' + version + '-win-x64.zip'); fs.rmSync(zip, { force: true });
      if (process.platform === 'win32') run('powershell.exe', ['-NoProfile', '-Command', 'Compress-Archive -Path $env:KM_SRC -DestinationPath $env:KM_ZIP'], { env: { ...process.env, KM_SRC: pack, KM_ZIP: zip } });
      else run('zip', ['-q', '-9', '-r', '-X', zip, NAME], { cwd: path.dirname(pack) });
      if (argv.includes('--keep-exe')) fs.copyFileSync(path.join(pack, NAME + '.exe'), path.join(outDir, NAME + '.exe'));
    } else {
      run('codesign', ['--sign', '-', exe]);
      const appDir = path.join(work, 'pack', NAME + '.app', 'Contents');
      fs.mkdirSync(path.join(appDir, 'MacOS'), { recursive: true });
      fs.renameSync(exe, path.join(appDir, 'MacOS', NAME));
      fs.writeFileSync(path.join(appDir, 'Info.plist'), plist(version));
      run('codesign', ['--force', '--deep', '--sign', '-', path.join(work, 'pack', NAME + '.app')]);
      zip = path.join(outDir, 'AmF-X1-Key-Manager-' + version + '-macos-' + target.slice(7) + '.zip'); fs.rmSync(zip, { force: true });
      run('ditto', ['-c', '-k', '--keepParent', path.join(work, 'pack', NAME + '.app'), zip]);
    }
    const hash = sha(fs.readFileSync(zip));
    fs.writeFileSync(zip + '.sha256', hash + '  ' + path.basename(zip) + '\n');
    updateSums(outDir, zip, hash);
    console.log(path.relative(ROOT, zip) + '  ' + (fs.statSync(zip).size / 1048576).toFixed(1) + ' MB  sha256 ' + hash);
    return { zip, hash };
  } finally { fs.rmSync(work, { recursive: true, force: true }); }
}

const readme = v => `AmF X1 Key Manager ${v} (Windows x64)

Your licence key generator as a desktop app. Nothing to install, Node.js is not needed.

START   double-click "AmF X1 Key Manager.exe". It opens its own window (Microsoft Edge app window).
        Windows SmartScreen may warn about an unknown publisher the first time: More info -> Run anyway.
STOP    close the window. The local server stops and the unlocked key is dropped from memory.

KEY FOLDER  %USERPROFILE%\\.audio-mixer-vendor  (private.pem.enc + public.jwk.json + sales/issued logs)
            The private key never leaves that folder and is not inside this program.
            BACK IT UP (USB drive or password manager), and remember the PIN: without both no more keys can be signed.

The server answers on 127.0.0.1 only (this computer). No telemetry.
Log: %LOCALAPPDATA%\\AmF X1 Key Manager\\key-manager.log (start/stop only, never a key or PIN).
PUBLISH UPDATE looks for installers in a "releases" folder next to the .exe.
Guide: docs/key-manager.md in the AmF X1 repository.
`;
const plist = v => `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>${NAME}</string><key>CFBundleDisplayName</key><string>${NAME}</string>
<key>CFBundleIdentifier</key><string>audio.amfb.amfx1.keymanager</string><key>CFBundleVersion</key><string>${v}</string>
<key>CFBundleShortVersionString</key><string>${v}</string><key>CFBundleExecutable</key><string>${NAME}</string>
<key>CFBundlePackageType</key><string>APPL</string><key>LSMinimumSystemVersion</key><string>11.0</string>
</dict></plist>
`;

module.exports = { closure, stage, setWindowsGui, nodeBinary, build };
if (require.main === module) {
  const argv = process.argv.slice(2);
  try {
    if (argv.includes('--stage')) { const d = path.resolve(arg(argv, '--stage')); const m = stage(d); console.log('staged ' + Object.keys(m.files).length + ' files in ' + d); }
    else build(argv);
  } catch (e) { console.error('build failed: ' + e.message); process.exit(1); }
}
