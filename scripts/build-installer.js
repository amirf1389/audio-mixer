#!/usr/bin/env node
'use strict';
// Stages the Windows install tree (app files, official Node.js runtime, Audify with its Windows prebuilt binaries) for one architecture.
// The installers themselves are built from that folder: build-msi.js (.msi), build-exe.js (signed setup .exe), see build-installers.js.
//   node scripts/build-installer.js [--arch x64|x86]   -> dist/installer/stage-<arch>
// Needs internet once, to fetch the official Node.js runtime (checked against nodejs.org's SHASUMS256.txt) and the Audify prebuilt binaries.
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

// Official Node.js runtime for Windows (x64, or x86 from the newest LTS line that still ships one): latest LTS (or NODE_VERSION /
// NODE_VERSION_X86), verified against SHASUMS256.txt, cached in dist/cache.
async function fetchNodeRuntime({ cache, fetchImpl = globalThis.fetch, arch = 'x64', version = arch === 'x86' ? process.env.NODE_VERSION_X86 : process.env.NODE_VERSION } = {}) {
  if (arch !== 'x64' && arch !== 'x86') throw new Error('unknown architecture: ' + arch);
  if (!version) {
    try {
      const idx = JSON.parse(await getText(`${DIST}/index.json`, fetchImpl));
      const lts = idx.find(e => e.lts && Array.isArray(e.files) && e.files.includes(`win-${arch}-zip`));
      if (!lts) throw new Error('no LTS Windows build found');
      version = lts.version;
    } catch (e) {
      // Offline: reuse the newest runtime that was downloaded and verified earlier.
      const cached = fs.existsSync(cache) ? fs.readdirSync(cache).map(f => new RegExp(`^node-(v\\d+\\.\\d+\\.\\d+)-win-${arch}\\.exe$`).exec(f)).filter(Boolean).map(m => m[1]) : [];
      const num = v => v.slice(1).split('.').map(Number);
      cached.sort((a, b) => { const x = num(a), y = num(b); return (y[0] - x[0]) || (y[1] - x[1]) || (y[2] - x[2]); });
      if (!cached.length) throw e;
      version = cached[0];
    }
  }
  if (!/^v\d+\.\d+\.\d+$/.test(version)) throw new Error('bad Node.js version: ' + version);
  const zipName = `node-${version}-win-${arch}.zip`;
  const exe = path.join(cache, `node-${version}-win-${arch}.exe`), lic = path.join(cache, `node-${version}-LICENSE.txt`);
  if (fs.existsSync(exe) && fs.existsSync(lic)) return { version, exe, license: lic };
  const sums = parseShasums(await getText(`${DIST}/${version}/SHASUMS256.txt`, fetchImpl));
  if (!sums[zipName]) throw new Error('no checksum published for ' + zipName);
  const r = await fetchImpl(`${DIST}/${version}/${zipName}`, { headers: { 'User-Agent': 'audio-mixer-build' } });
  if (!r.ok) throw new Error(zipName + ' -> ' + r.status);
  const zip = Buffer.from(await r.arrayBuffer());
  const sum = crypto.createHash('sha256').update(zip).digest('hex');
  if (sum !== sums[zipName]) throw new Error('checksum mismatch for ' + zipName + ' (expected ' + sums[zipName] + ', got ' + sum + ')');
  const node = extractFromZip(zip, n => n === `node-${version}-win-${arch}/node.exe`);
  const license = extractFromZip(zip, n => n === `node-${version}-win-${arch}/LICENSE`);
  if (!node) throw new Error('node.exe not found in ' + zipName);
  fs.mkdirSync(cache, { recursive: true });
  fs.writeFileSync(exe, node); if (license) fs.writeFileSync(lic, license); else fs.writeFileSync(lic, 'See https://github.com/nodejs/node/blob/main/LICENSE\n');
  return { version, exe, license: lic, sha256: sum };
}

// Audify (RtAudio, with ASIO / WASAPI / DirectSound) for the bundled Node.js: the npm package plus its official Windows x64 N-API
// prebuilt binary (prebuild-install), so the target PC needs no compiler. Binaries are pinned by SHA-256.
const AUDIFY_VERSION = '1.10.1';
const AUDIFY_WIN_SHA256 = {
  x64: {
    'audify.node': 'ba9be079733bf4fc5958bcc898124757d1342805d43fd9fdf1fdfb57c3bdc2b2',
    'opus.dll': 'f89de06563f996693b3de3185939fb12fa61f02b1d33ab556cd774105d21c13d',
    'rtaudio.dll': '0d5b3cf7c40dbcc6d4688977ed3e3f57b255a71e590fe750759516c7563d9b1d',
  },
  x86: {
    'audify.node': '4a20a710a8afe56d216f9b0b96c3c1b05006f39ebdbe383c1bffe24459f71b40',
    'opus.dll': '510228086eb8d1abaed014b439b4e8b3d6a2840c3b6af3060a80f812cc8fb68d',
    'rtaudio.dll': 'dcd94f75fca0f30beffdf462bf147fb54e9f938ec2f1214e2794d35099be5f7c',
  },
};
function fetchAudify({ cache, version = AUDIFY_VERSION, arch = 'x64', run = spawnSync } = {}) {
  const pins = AUDIFY_WIN_SHA256[arch];
  if (!pins) throw new Error('unknown architecture: ' + arch);
  const work = path.join(cache, arch === 'x64' ? `audify-${version}` : `audify-${version}-${arch}`), nm = path.join(work, 'node_modules');
  const rel = path.join(nm, 'audify', 'build', 'Release');
  const sha = f => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
  const verified = () => Object.entries(pins).every(([f, h]) => fs.existsSync(path.join(rel, f)) && sha(path.join(rel, f)) === h);
  if (!verified()) {
    fs.mkdirSync(work, { recursive: true });
    fs.writeFileSync(path.join(work, 'package.json'), JSON.stringify({ name: 'audify-bundle', private: true }));
    const npm = process.platform === 'win32' ? 'npm.cmd' : 'npm';
    let r = run(npm, ['install', `audify@${version}`, '--ignore-scripts', '--no-audit', '--no-fund'], { cwd: work, encoding: 'utf8', shell: process.platform === 'win32' });
    if (r.status !== 0) throw new Error('npm install audify failed: ' + (r.stderr || r.error || ''));
    r = run(process.execPath, [path.join(nm, 'prebuild-install', 'bin.js'), '--platform', 'win32', '--arch', arch === 'x86' ? 'ia32' : 'x64', '--runtime', 'napi'], { cwd: path.join(nm, 'audify'), encoding: 'utf8' });
    if (r.status !== 0) throw new Error('downloading the Windows Audify binaries failed: ' + (r.stderr || r.error || ''));
    for (const [f, h] of Object.entries(pins)) {
      if (!fs.existsSync(path.join(rel, f))) throw new Error('missing Audify binary ' + f);
      if (sha(path.join(rel, f)) !== h) { fs.rmSync(path.join(rel, f), { force: true }); throw new Error('checksum mismatch for Audify ' + f + ' (not the pinned build)'); }
    }
  }
  return { version, nm, dirs: [['audify', ['index.js', 'index.d.ts', 'package.json', 'LICENSE', 'build/Release']], ['bindings', ['bindings.js', 'package.json', 'LICENSE.md']], ['file-uri-to-path', ['index.js', 'package.json', 'History.md']]] };
}
function stageAudify(a, stage) {
  for (const [mod, items] of a.dirs) {
    for (const it of items) {
      const from = path.join(a.nm, mod, it);
      if (!fs.existsSync(from)) continue;
      const to = path.join(stage, 'bridge', 'node_modules', mod, it);
      fs.mkdirSync(path.dirname(to), { recursive: true });
      fs.cpSync(from, to, { recursive: true });
    }
  }
}

// Windows launchers that use the bundled runtime (no Node.js install needed).
const BAT_PC = '@echo off\r\nrem Audio Mixer PC mode: starts the local system server and opens the mixer (bundled Node.js).\r\ncd /d "%~dp0"\r\n"%~dp0runtime\\node.exe" client\\cli.js %*\r\npause\r\n';
const BAT_SERVER = '@echo off\r\nrem Audio Mixer local system server only (bundled Node.js). Open http://localhost:8765 yourself.\r\ntitle Audio Mixer local server\r\ncd /d "%~dp0"\r\n"%~dp0runtime\\node.exe" bridge\\server.js\r\npause\r\n';

async function buildInstaller({ out = path.join(ROOT, 'dist'), arch = 'x64', fetchImpl, bundleAudify = true, audifyRun } = {}) {
  const outAbs = path.resolve(out);
  const app = build({ out: outAbs });
  const stage = path.join(outAbs, 'installer', `stage-${arch}`);
  fs.rmSync(stage, { recursive: true, force: true });
  fs.cpSync(app.dest, stage, { recursive: true });
  fs.writeFileSync(path.join(stage, 'start-pc-mode.bat'), BAT_PC);
  fs.writeFileSync(path.join(stage, 'start-local-server.bat'), BAT_SERVER);
  fs.rmSync(path.join(stage, 'start-pc-mode.sh'), { force: true });
  fs.rmSync(path.join(stage, 'ensure-node.sh'), { force: true });   // Windows packages carry their own Node.js
  // the installer swaps launcher files, so the manifest must describe what is really installed
  const crypto = require('node:crypto');
  const mf = path.join(stage, 'MANIFEST.sha256');
  if (bundleAudify) stageAudify(fetchAudify({ cache: path.join(outAbs, 'cache'), arch, run: audifyRun }), stage);
  const lines = fs.readFileSync(mf, 'utf8').split('\n').filter(Boolean).map(l => l.replace(/^([0-9a-f]{64})\s+/, '$1\t').split('\t')).filter(([, rel]) => fs.existsSync(path.join(stage, rel)))
    .map(([, rel]) => `${crypto.createHash('sha256').update(fs.readFileSync(path.join(stage, rel))).digest('hex')}  ${rel}`);
  // the bundled Audify module is part of what is installed: list it so the verification scan covers it
  const bundled = [];
  const walkNm = dir => { for (const e of fs.readdirSync(dir, { withFileTypes: true })) { const f = path.join(dir, e.name); if (e.isDirectory()) walkNm(f); else bundled.push(path.relative(stage, f).split(path.sep).join('/')); } };
  if (fs.existsSync(path.join(stage, 'bridge', 'node_modules'))) walkNm(path.join(stage, 'bridge', 'node_modules'));
  for (const rel of bundled) lines.push(`${crypto.createHash('sha256').update(fs.readFileSync(path.join(stage, rel))).digest('hex')}  ${rel}`);
  fs.writeFileSync(mf, lines.join('\n') + '\n');

  const rt = await fetchNodeRuntime({ cache: path.join(outAbs, 'cache'), arch, fetchImpl });
  fs.mkdirSync(path.join(stage, 'runtime'), { recursive: true });
  fs.copyFileSync(rt.exe, path.join(stage, 'runtime', 'node.exe'));
  fs.copyFileSync(rt.license, path.join(stage, 'runtime', 'LICENSE-node.txt'));
  return { stage, arch, nodeVersion: rt.version, version: app.version };
}

if (require.main === module) {
  const a = process.argv.slice(2), oi = a.indexOf('--out'), ai = a.indexOf('--arch');
  buildInstaller({ out: oi >= 0 ? a[oi + 1] : undefined, arch: ai >= 0 ? a[ai + 1] : 'x64' }).then(r => {
    console.log(`Staged ${r.stage} (${r.arch}, app v${r.version}, bundled Node.js ${r.nodeVersion})`);
  }).catch(e => { console.error('Staging failed: ' + e.message); process.exit(1); });
}
module.exports = { fetchAudify, stageAudify, AUDIFY_WIN_SHA256, buildInstaller, fetchNodeRuntime, parseShasums, extractFromZip, version4, BAT_PC, BAT_SERVER };
