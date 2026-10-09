#!/usr/bin/env node
'use strict';
// Builds the Windows installer (AudioMixer-Setup-<version>.exe) for the local system server with NSIS.
//   node scripts/build-installer.js            -> dist/AudioMixer-Setup-<version>.exe
//   node scripts/build-installer.js --stage    -> only prepare dist/installer/stage (no makensis needed)
// Needs: NSIS (makensis) on PATH (Windows: https://nsis.sourceforge.io/ , Linux: apt install nsis, macOS: brew install makensis)
// and internet access once, to fetch the official Node.js Windows runtime (checked against nodejs.org's SHASUMS256.txt).
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const { build } = require('./build');

const ROOT = path.resolve(__dirname, '..');
const DIST = 'https://nodejs.org/dist';

// ── small helpers (exported for tests) ──
function parseShasums(text) {
  const out = {};
  for (const line of String(text).split('\n')) { const m = /^([0-9a-f]{64})\s+\*?(\S+)$/i.exec(line.trim()); if (m) out[m[2]] = m[1].toLowerCase(); }
  return out;
}

function version4(v) {
  const p = String(v).split(/[.\-+]/).map(x => parseInt(x, 10)).filter(Number.isFinite).slice(0, 4);
  while (p.length < 4) p.push(0);
  return p.join('.');
}

// Reads one file out of a zip (stored or deflate) without any external tool.
function extractFromZip(buf, wanted) {
  let eocd = -1;
  for (let i = buf.length - 22; i >= Math.max(0, buf.length - 65557); i--) if (buf.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) throw new Error('not a zip file');
  const count = buf.readUInt16LE(eocd + 10);
  let p = buf.readUInt32LE(eocd + 16);
  for (let n = 0; n < count; n++) {
    if (buf.readUInt32LE(p) !== 0x02014b50) throw new Error('corrupt zip directory');
    const method = buf.readUInt16LE(p + 10), csize = buf.readUInt32LE(p + 20), usize = buf.readUInt32LE(p + 24);
    const nlen = buf.readUInt16LE(p + 28), xlen = buf.readUInt16LE(p + 30), clen = buf.readUInt16LE(p + 32), lho = buf.readUInt32LE(p + 42);
    const name = buf.toString('utf8', p + 46, p + 46 + nlen);
    p += 46 + nlen + xlen + clen;
    if (!wanted(name)) continue;
    if (buf.readUInt32LE(lho) !== 0x04034b50) throw new Error('corrupt zip entry');
    const start = lho + 30 + buf.readUInt16LE(lho + 26) + buf.readUInt16LE(lho + 28);
    const raw = buf.subarray(start, start + csize);
    const data = method === 0 ? Buffer.from(raw) : method === 8 ? zlib.inflateRawSync(raw) : null;
    if (!data) throw new Error('unsupported zip compression ' + method);
    if (data.length !== usize) throw new Error('zip entry size mismatch');
    return data;
  }
  return null;
}

async function getText(url, fetchImpl) { const r = await fetchImpl(url, { headers: { 'User-Agent': 'audio-mixer-build' } }); if (!r.ok) throw new Error(url + ' -> ' + r.status); return r.text(); }

// Official Node.js runtime for Windows x64: latest LTS (or NODE_VERSION), verified against SHASUMS256.txt, cached in dist/cache.
async function fetchNodeRuntime({ cache, fetchImpl = globalThis.fetch, version = process.env.NODE_VERSION } = {}) {
  if (!version) {
    try {
      const idx = JSON.parse(await getText(`${DIST}/index.json`, fetchImpl));
      const lts = idx.find(e => e.lts && Array.isArray(e.files) && e.files.includes('win-x64-zip'));
      if (!lts) throw new Error('no LTS Windows build found');
      version = lts.version;
    } catch (e) {
      // Offline: reuse the newest runtime that was downloaded and verified earlier.
      const cached = fs.existsSync(cache) ? fs.readdirSync(cache).map(f => /^node-(v\d+\.\d+\.\d+)-win-x64\.exe$/.exec(f)).filter(Boolean).map(m => m[1]) : [];
      const num = v => v.slice(1).split('.').map(Number);
      cached.sort((a, b) => { const x = num(a), y = num(b); return (y[0] - x[0]) || (y[1] - x[1]) || (y[2] - x[2]); });
      if (!cached.length) throw e;
      version = cached[0];
    }
  }
  if (!/^v\d+\.\d+\.\d+$/.test(version)) throw new Error('bad Node.js version: ' + version);
  const zipName = `node-${version}-win-x64.zip`;
  const exe = path.join(cache, `node-${version}-win-x64.exe`), lic = path.join(cache, `node-${version}-LICENSE.txt`);
  if (fs.existsSync(exe) && fs.existsSync(lic)) return { version, exe, license: lic };
  const sums = parseShasums(await getText(`${DIST}/${version}/SHASUMS256.txt`, fetchImpl));
  if (!sums[zipName]) throw new Error('no checksum published for ' + zipName);
  const r = await fetchImpl(`${DIST}/${version}/${zipName}`, { headers: { 'User-Agent': 'audio-mixer-build' } });
  if (!r.ok) throw new Error(zipName + ' -> ' + r.status);
  const zip = Buffer.from(await r.arrayBuffer());
  const sum = crypto.createHash('sha256').update(zip).digest('hex');
  if (sum !== sums[zipName]) throw new Error('checksum mismatch for ' + zipName + ' (expected ' + sums[zipName] + ', got ' + sum + ')');
  const node = extractFromZip(zip, n => n === `node-${version}-win-x64/node.exe`);
  const license = extractFromZip(zip, n => n === `node-${version}-win-x64/LICENSE`);
  if (!node) throw new Error('node.exe not found in ' + zipName);
  fs.mkdirSync(cache, { recursive: true });
  fs.writeFileSync(exe, node); if (license) fs.writeFileSync(lic, license); else fs.writeFileSync(lic, 'See https://github.com/nodejs/node/blob/main/LICENSE\n');
  return { version, exe, license: lic, sha256: sum };
}

// Windows launchers that use the bundled runtime (no Node.js install needed).
const BAT_PC = '@echo off\r\nrem Audio Mixer PC mode: starts the local system server and opens the mixer (bundled Node.js).\r\ncd /d "%~dp0"\r\n"%~dp0runtime\\node.exe" client\\cli.js %*\r\npause\r\n';
const BAT_SERVER = '@echo off\r\nrem Audio Mixer local system server only (bundled Node.js). Open http://localhost:8765 yourself.\r\ntitle Audio Mixer local server\r\ncd /d "%~dp0"\r\n"%~dp0runtime\\node.exe" bridge\\server.js\r\npause\r\n';

async function buildInstaller({ out = path.join(ROOT, 'dist'), stageOnly = false, fetchImpl, runMakensis = true } = {}) {
  const outAbs = path.resolve(out);
  const app = build({ out: outAbs });
  const stage = path.join(outAbs, 'installer', 'stage');
  fs.rmSync(stage, { recursive: true, force: true });
  fs.cpSync(app.dest, stage, { recursive: true });
  fs.writeFileSync(path.join(stage, 'start-pc-mode.bat'), BAT_PC);
  fs.writeFileSync(path.join(stage, 'start-local-server.bat'), BAT_SERVER);
  fs.rmSync(path.join(stage, 'start-pc-mode.sh'), { force: true });
  // the installer swaps launcher files, so the manifest must describe what is really installed
  const crypto = require('node:crypto');
  const mf = path.join(stage, 'MANIFEST.sha256');
  const lines = fs.readFileSync(mf, 'utf8').split('\n').filter(Boolean).map(l => l.replace(/^([0-9a-f]{64})\s+/, '$1\t').split('\t')).filter(([, rel]) => fs.existsSync(path.join(stage, rel)))
    .map(([, rel]) => `${crypto.createHash('sha256').update(fs.readFileSync(path.join(stage, rel))).digest('hex')}  ${rel}`);
  fs.writeFileSync(mf, lines.join('\n') + '\n');

  const rt = await fetchNodeRuntime({ cache: path.join(outAbs, 'cache'), fetchImpl });
  fs.mkdirSync(path.join(stage, 'runtime'), { recursive: true });
  fs.copyFileSync(rt.exe, path.join(stage, 'runtime', 'node.exe'));
  fs.copyFileSync(rt.license, path.join(stage, 'runtime', 'LICENSE-node.txt'));
  const result = { stage, nodeVersion: rt.version, version: app.version, installer: null };
  if (stageOnly) return result;

  const installer = path.join(outAbs, `AudioMixer-Setup-${app.version}.exe`);
  if (!runMakensis) return result;
  const r = spawnSync('makensis', ['-V2', `-DVERSION=${app.version}`, `-DVERSION4=${version4(app.version)}`, `-DSTAGE=${stage}`, `-DOUTFILE=${installer}`, path.join(ROOT, 'installer', 'audio-mixer.nsi')], { encoding: 'utf8' });
  if (r.error && r.error.code === 'ENOENT') throw new Error('makensis (NSIS) not found. Install NSIS (Windows: https://nsis.sourceforge.io/ , Linux: apt install nsis, macOS: brew install makensis); the staged files are in ' + stage);
  if (r.status !== 0) throw new Error('makensis failed:\n' + r.stdout + r.stderr);
  result.installer = installer;
  result.sha256 = crypto.createHash('sha256').update(fs.readFileSync(installer)).digest('hex');
  return result;
}

if (require.main === module) {
  const a = process.argv.slice(2), oi = a.indexOf('--out');
  buildInstaller({ stageOnly: a.includes('--stage'), out: oi >= 0 ? a[oi + 1] : undefined }).then(r => {
    console.log(`Staged ${r.stage} (app v${r.version}, bundled Node.js ${r.nodeVersion})`);
    if (r.installer) console.log(`Installer: ${r.installer}\nSHA-256:   ${r.sha256}\nUnsigned: Windows SmartScreen will warn until it is code-signed.`);
  }).catch(e => { console.error('Installer build failed: ' + e.message); process.exit(1); });
}
module.exports = { buildInstaller, fetchNodeRuntime, parseShasums, extractFromZip, version4, BAT_PC, BAT_SERVER };
