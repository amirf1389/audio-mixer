#!/usr/bin/env node
'use strict';
// ZIP PACKAGES for the website: each release file and each plug-in also as one .zip (it opens in Windows itself, WinZip, WinRAR, 7-Zip),
// with a README.txt and a SHA256SUMS.txt beside the file inside, so a visitor can check what they got.
//   node scripts/make-zips.js [--out releases/zip] [--only windows|android|linux|plugins]
// Written to releases/zip/ (not committed: a release step). Only node's own modules are used; the archive format is plain ZIP with deflate.
// The names that go into an archive are plain file names (no folders, no ".."): the same rule the console applies.
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const crypto = require('node:crypto');

const ROOT = path.resolve(__dirname, '..');
const arg = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 ? process.argv[i + 1] : d; };
const SAFE = /^[A-Za-z0-9][A-Za-z0-9 ._()-]{0,119}$/;

let table = null;
function crc32(buf) { if (!table) { table = new Uint32Array(256); for (let i = 0; i < 256; i++) { let c = i; for (let k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; table[i] = c >>> 0; } }
  let c = 0xFFFFFFFF; for (let i = 0; i < buf.length; i++) c = table[(c ^ buf[i]) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }

// entries: [{name, data: Buffer}] -> Buffer of a ZIP archive (deflate; stored when deflate does not make it smaller)
function zip(entries, when = new Date()) {
  const dosTime = (when.getHours() << 11) | (when.getMinutes() << 5) | (when.getSeconds() >> 1), dosDate = ((when.getFullYear() - 1980) << 9) | ((when.getMonth() + 1) << 5) | when.getDate();
  const parts = [], central = []; let offset = 0; const seen = new Set();
  for (const e of entries) {
    if (!SAFE.test(e.name) || e.name.includes('..') || seen.has(e.name)) throw new Error('a file name is not allowed in the archive: ' + e.name); seen.add(e.name);
    if (e.data.length >= 0xFFFFFFFF) throw new Error(e.name + ' is too large for a plain ZIP archive');
    const name = Buffer.from(e.name, 'utf8'), crc = crc32(e.data); let body = zlib.deflateRawSync(e.data, { level: 9 }), method = 8; if (body.length >= e.data.length) { body = e.data; method = 0; }
    const local = Buffer.alloc(30); local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(20, 4); local.writeUInt16LE(0, 6); local.writeUInt16LE(method, 8); local.writeUInt16LE(dosTime, 10); local.writeUInt16LE(dosDate, 12);
    local.writeUInt32LE(crc, 14); local.writeUInt32LE(body.length, 18); local.writeUInt32LE(e.data.length, 22); local.writeUInt16LE(name.length, 26); local.writeUInt16LE(0, 28);
    const cd = Buffer.alloc(46); cd.writeUInt32LE(0x02014b50, 0); cd.writeUInt16LE(20, 4); cd.writeUInt16LE(20, 6); cd.writeUInt16LE(0, 8); cd.writeUInt16LE(method, 10); cd.writeUInt16LE(dosTime, 12); cd.writeUInt16LE(dosDate, 14);
    cd.writeUInt32LE(crc, 16); cd.writeUInt32LE(body.length, 20); cd.writeUInt32LE(e.data.length, 24); cd.writeUInt16LE(name.length, 28); cd.writeUInt32LE(offset, 42);
    parts.push(local, name, body); central.push(cd, name); offset += 30 + name.length + body.length;
  }
  const cdBuf = Buffer.concat(central), end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10); end.writeUInt32LE(cdBuf.length, 12); end.writeUInt32LE(offset, 16);
  return Buffer.concat([...parts, cdBuf, end]);
}
const sha256 = b => crypto.createHash('sha256').update(b).digest('hex');
function packageOf(file, title, how) {
  const data = fs.readFileSync(file), name = path.basename(file);
  const readme = Buffer.from(`${title}\r\n\r\n${how}\r\n\r\nFile: ${name}\r\nSize: ${data.length} bytes\r\nSHA-256: ${sha256(data)}\r\n\r\nCheck it: the SHA-256 above must be the one shown on the website's download page.\r\n`, 'utf8');
  return zip([{ name, data }, { name: 'README.txt', data: readme }, { name: 'SHA256SUMS.txt', data: Buffer.from(`${sha256(data)}  ${name}\n`, 'utf8') }]);
}
// what gets a package: the newest installer of each system, and every listed plug-in
function plan() {
  const list = [], zipName = f => path.basename(f).replace(/\.(exe|apk|deb)$/i, '').replace(/[^A-Za-z0-9 ._()-]/g, '-') + '.zip';   // named after the file inside, so the version in the name is the version you get
  const newest = (dir, re) => { const d = path.join(ROOT, 'releases', dir); if (!fs.existsSync(d)) return null; const f = fs.readdirSync(d).filter(n => re.test(n)).map(n => ({ n, t: fs.statSync(path.join(d, n)).mtimeMs })).sort((a, b) => b.t - a.t)[0]; return f ? path.join(d, f.n) : null; };
  const win = newest('windows', /setup\.exe$/i) || newest('windows', /\.exe$/i), apk = newest('android', /\.apk$/i), deb = newest('linux', /\.deb$/i);
  if (win) list.push({ kind: 'windows', file: win, out: zipName(win), latest: 'AmF-X1-latest-windows.zip', title: 'AmF X1 for Windows', how: 'Open the setup program inside this archive and follow it.' });
  if (apk) list.push({ kind: 'android', file: apk, out: zipName(apk), latest: 'AmF-X1-latest-android.zip', title: 'AmF X1 for Android', how: 'Copy the .apk to the phone and open it there.' });
  if (deb) list.push({ kind: 'linux', file: deb, out: zipName(deb), latest: 'AmF-X1-latest-linux.zip', title: 'AmF X1 for Linux', how: 'Install the .deb with your package manager.' });
  const pj = path.join(ROOT, 'releases', 'plugins.json');
  if (fs.existsSync(pj)) for (const p of (JSON.parse(fs.readFileSync(pj, 'utf8')).plugins || [])) for (const name of new Set(Object.values(p.files || {}))) { const f = path.join(ROOT, 'releases', 'plugins', name);
    if (SAFE.test(name) && fs.existsSync(f)) list.push({ kind: 'plugins', file: f, out: name.replace(/\.vst3$/i, '') + '.zip', title: `${p.name} ${p.version || ''}`.trim() + ' for AmF X1', how: 'Put the .vst3 file into the console\'s plug-in folder (STORE, ADD VST shows it), or use STORE, DOWNLOADS in the console.' }); }
  return list;
}
if (require.main === module) {
  const out = path.resolve(ROOT, arg('--out', 'releases/zip')), only = arg('--only', '');
  fs.mkdirSync(out, { recursive: true }); let n = 0;
  for (const p of plan()) { if (only && p.kind !== only) continue; const z = packageOf(p.file, p.title, p.how); fs.writeFileSync(path.join(out, p.out), z); n++;
    if (p.latest) fs.writeFileSync(path.join(out, p.latest), z);   // the same archive under a name that does not change: what /get on the website sends a visitor to
    console.log(`${path.relative(ROOT, path.join(out, p.out))}  ${(z.length / 1048576).toFixed(1)} MB  sha256 ${sha256(z)}`); }
  if (!n) console.log('nothing to package: no release files found');
}
module.exports = { zip, crc32, packageOf, plan };
