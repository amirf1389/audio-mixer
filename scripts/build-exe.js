'use strict';
// Builds the signed setup program "Audio Mixer-<version>.exe": a small bootstrapper (installer/setup-stub.c, compiled with MinGW-w64)
// that carries the .msi as payload, checks its SHA-256 before running it, and understands /quiet /passive /scan /uninstall.
//   node scripts/build-exe.js <package.msi> <productCode> <out.exe>
// Signing: see scripts/sign.js. Needs: x86 MinGW-w64 (Linux: apt install gcc-mingw-w64-i686) and osslsigncode.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const MAGIC = Buffer.alloc(16); MAGIC.write('AMIXSETUPv1');
const TRAILER = 96;

function version4(v) {
  const p = String(v).split(/[.\-+]/).map(x => parseInt(x, 10)).filter(Number.isFinite).slice(0, 4);
  while (p.length < 4) p.push(0);
  return p.join(',');
}

// payload + trailer, appended to the compiled stub (pure function, unit-tested)
function packPayload(stub, payload, productCode) {
  if (!/^\{[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}\}$/.test(productCode)) throw new Error('bad product code: ' + productCode);
  const t = Buffer.alloc(TRAILER);
  MAGIC.copy(t, 0);
  t.writeBigUInt64LE(BigInt(payload.length), 16);
  crypto.createHash('sha256').update(payload).digest().copy(t, 24);
  t.write(productCode, 56, 'ascii');
  return Buffer.concat([stub, payload, t]);
}

// reads the trailer back (what the stub does at run time); `end` = where the PE data ends (before any signature)
function readTrailer(file, end) {
  const t = file.subarray(end - TRAILER, end);
  if (!t.subarray(0, 16).equals(MAGIC)) return null;
  const size = Number(t.readBigUInt64LE(16));
  const start = end - TRAILER - size;
  if (start < 0) return null;
  return { size, sha256: t.subarray(24, 56).toString('hex'), productCode: t.subarray(56, 96).toString('ascii').replace(/\0+$/, ''), payload: file.subarray(start, end - TRAILER) };
}

// end of the PE image data = start of the Authenticode certificate table when the file is signed
function peDataEnd(buf) {
  if (buf.length < 0x100 || buf.readUInt16LE(0) !== 0x5a4d) return buf.length;
  const pe = buf.readUInt32LE(0x3c);
  if (buf.readUInt32LE(pe) !== 0x4550) return buf.length;
  const opt = pe + 24, dd = opt + (buf.readUInt16LE(opt) === 0x20b ? 112 : 96);
  const off = buf.readUInt32LE(dd + 32), len = buf.readUInt32LE(dd + 36);
  return len && off && off < buf.length ? off : buf.length;
}

// Compiles one of the small native programs in installer/ (MinGW-w64). `name` = setup | launcher; the launcher is built for the
// architecture of the install it goes into, the setup program is always 32-bit so it runs on both 32- and 64-bit Windows.
function compileNative({ name, work, version, arch, defs = [], libs = [] }) {
  fs.mkdirSync(work, { recursive: true });
  const triple = arch === 'x64' ? 'x86_64-w64-mingw32' : 'i686-w64-mingw32';
  const v4 = version4(version);
  const rc = fs.readFileSync(path.join(ROOT, 'installer', `${name}.rc`), 'utf8').replace(/@VERSION4@/g, v4).replace(/@VERSION@/g, version);
  fs.writeFileSync(path.join(work, `${name}.rc`), rc);
  fs.copyFileSync(path.join(ROOT, 'installer', `${name}.manifest`), path.join(work, `${name}.manifest`));
  const res = path.join(work, `${name}.res.o`), exe = path.join(work, `${name}-${arch}.exe`);
  let r = spawnSync(`${triple}-windres`, ['-i', `${name}.rc`, '-o', res], { cwd: work, encoding: 'utf8' });
  if (r.error && r.error.code === 'ENOENT') throw new Error(`${triple}-windres not found (Linux: apt install mingw-w64)`);
  if (r.status !== 0) throw new Error('windres failed: ' + r.stderr);
  r = spawnSync(`${triple}-gcc`, ['-O2', '-s', '-mwindows', '-municode', '-Wall', ...defs, '-o', exe, path.join(ROOT, 'installer', name === 'setup' ? 'setup-stub.c' : `${name}.c`), res, ...libs, '-static-libgcc'], { encoding: 'utf8' });
  if (r.error && r.error.code === 'ENOENT') throw new Error(`${triple}-gcc not found (Linux: apt install mingw-w64)`);
  if (r.status !== 0) throw new Error(`compiling ${name} failed:\n` + r.stderr);
  return exe;
}
const compileStub = ({ work, version, arch = 'x64' }) => compileNative({ name: 'setup', work, version, arch: 'x86', defs: arch === 'x64' ? ['-DREQUIRE_X64'] : [], libs: ['-ladvapi32', '-lshell32', '-luser32'] });
const compileLauncher = ({ work, version, arch }) => compileNative({ name: 'launcher', work, version, arch, libs: ['-lshell32', '-luser32'] });

function buildExe({ msi, productCode, out, version, arch = 'x64', work, id }) {
  const { signFile } = require('./sign');
  const stub = fs.readFileSync(compileStub({ work, version, arch }));
  const packed = packPayload(stub, fs.readFileSync(msi), productCode);
  fs.writeFileSync(out, packed);
  if (id) signFile(out, id);
  return { exe: out, sha256: crypto.createHash('sha256').update(fs.readFileSync(out)).digest('hex') };
}

module.exports = { buildExe, packPayload, readTrailer, peDataEnd, compileStub, compileLauncher, compileNative, TRAILER, version4 };
