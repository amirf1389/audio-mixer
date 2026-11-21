#!/usr/bin/env node
'use strict';
// Builds the native program (engine/cpp/app/main.cpp: the C++ engine on the operating system's own audio layer, no browser and no Node.js) for the systems this machine can build:
//   node scripts/build-native.js [--out <dir>]
//   AudioMixer-<version>-native-linux-x64.tar.gz     audiomixer-native (ALSA + null), built with the C++ compiler and CMake here, run on the null device as a check
//   AmF X1.exe  (+ AudioMixerASIO64.dll beside it)    ASIO + WASAPI + DirectSound + null, static, cross-compiled with mingw-w64 (compiled, not run: no Windows here); version resource + manifest, nothing embedded
//   AmF X1 (32-bit).exe (+ AudioMixerASIO32.dll)        the same for 32-bit Windows. Sign both with scripts/sign-windows.js (your certificate) before release: see docs/windows-defender.md
// macOS (Core Audio), iOS and Android need Xcode / the NDK and are not built here: engine/cpp/CMakeLists.txt builds the same program on a Mac (cmake -S engine/cpp -B build).
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const version = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8')).version;
const sha = f => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, ...opts });
  if (r.error) throw new Error(`cannot run ${cmd}: ${r.error.message}`);
  if (r.status !== 0) throw new Error(`${path.basename(cmd)} failed: ${(r.stderr || r.stdout || '').trim().slice(-1500)}`);
  return r.stdout;
}
const has = n => spawnSync('which', [n]).status === 0;

// One Windows program (arch 'x64' | 'x86') with mingw-w64 into `exe`; used by main() and by the installer build (scripts/build-installers.js puts it into the install folder)
function compileWindows(arch, exe, opts = {}) {
  const t = arch === 'x64' ? 'x86_64-w64-mingw32' : 'i686-w64-mingw32';
  if (!has(t + '-g++')) throw new Error(`mingw-w64 (${t}-g++) is needed to build the native program (Debian / Ubuntu: apt install mingw-w64)`);
  const src = ['engine.cpp', 'simd.cpp', 'simd_base.cpp', 'runtime.cpp', 'backend_null.cpp', 'devices.cpp', 'backend_wasapi.cpp', 'backend_asio.cpp', 'backend_dsound.cpp'].map(f => path.join(ROOT, 'engine/cpp/src', f));
  fs.mkdirSync(path.dirname(exe), { recursive: true });
  // A clean program for Windows Defender / SmartScreen: nothing packed, nothing embedded that is written out and registered at run time, a
  // version resource (company, product, description, version) and a manifest. The ASIO driver DLL is built next to the program as its own
  // file (the installer puts both in Program Files; --install-driver registers the DLL that sits next to the .exe). `embedDriver: true` keeps
  // the old single-file behaviour (the DLL as a byte array inside the .exe, written out on --install-driver), which heuristic scanners dislike.
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'native-win-'));
  try {
    const dll = require('./build-drivers').buildAsio(arch, work), dllOut = path.join(path.dirname(exe), path.basename(dll)); fs.copyFileSync(dll, dllOut);
    const defs = ['-DAM_VERSION="' + version + '"', '-DAM_EMBED_UPDATE_KEY', '-DAM_DRIVER_DLL="' + path.basename(dll) + '"'];
    if (opts.embedDriver) {
      const bytes = fs.readFileSync(dll), rows = []; for (let i = 0; i < bytes.length; i += 16) rows.push('  ' + Array.from(bytes.subarray(i, i + 16), b => '0x' + b.toString(16).padStart(2, '0')).join(','));
      fs.writeFileSync(path.join(work, 'asio_blob.inc'), `static const unsigned char kAsioDll[] = {\n${rows.join(',\n')}\n};\nstatic const unsigned kAsioDllSize = ${bytes.length}u;\nstatic const char kAsioDllName[] = ${JSON.stringify(path.basename(dll))};\n`);
      defs.push('-DAM_EMBED_ASIO');
    }
    // the project's public key (the one the apps verify update manifests with) for the self-update: --update checks the signature inside the program
    const jwk = require('../bridge/license-public.json'), raw = k => Array.from(Buffer.from(jwk[k], 'base64url')).join(',');
    fs.writeFileSync(path.join(work, 'update_key.inc'), `static const unsigned char kUpdateKeyX[32] = {${raw('x')}};\nstatic const unsigned char kUpdateKeyY[32] = {${raw('y')}};\n`);
    // the version resource + manifest (install/windows/amfx1.rc)
    const v4 = (version.split('.').concat(['0', '0', '0', '0']).slice(0, 4).map(n => parseInt(n, 10) || 0)).join(',');
    fs.writeFileSync(path.join(work, 'amfx1.rc'), fs.readFileSync(path.join(ROOT, 'install/windows/amfx1.rc'), 'utf8').replace(/@VERSION4@/g, v4).replace(/@VERSION@/g, version));
    fs.copyFileSync(path.join(ROOT, 'install/windows/amfx1.manifest'), path.join(work, 'amfx1.manifest'));
    const res = path.join(work, 'amfx1.res.o'); run(t + '-windres', ['-i', path.join(work, 'amfx1.rc'), '-o', res, '-O', 'coff']);
    run(t + '-g++', ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-O2', '-static', '-static-libgcc', '-static-libstdc++', ...defs, '-I' + work, '-I' + path.join(ROOT, 'engine/cpp/app'),
      '-I' + path.join(ROOT, 'engine/cpp/include'), '-I' + path.join(ROOT, 'engine/cpp/src'), '-I' + path.join(ROOT, 'drivers/asio'),
      path.join(ROOT, 'engine/cpp/app/main.cpp'), path.join(ROOT, 'engine/cpp/app/stream.cpp'), path.join(ROOT, 'engine/cpp/app/paths/paths.cpp'), path.join(ROOT, 'engine/cpp/app/win_tools.cpp'), ...src, res, '-o', exe, '-lole32', '-luuid', '-ladvapi32', '-lavrt', '-ldsound', '-lwinmm', '-lwinhttp', '-lbcrypt']);
  } finally { fs.rmSync(work, { recursive: true, force: true }); }
  return exe;
}

function main() {
  const oi = process.argv.indexOf('--out'), out = oi > 0 ? path.resolve(process.argv[oi + 1]) : null;
  const results = [], work = fs.mkdtempSync(path.join(os.tmpdir(), 'native-'));
  try {
    if (has('cmake') && has('g++')) {                                                      // Linux (this machine)
      run('cmake', ['-S', path.join(ROOT, 'engine/cpp'), '-B', work, '-DCMAKE_BUILD_TYPE=Release']); run('cmake', ['--build', work, '--target', 'audiomixer-native', '-j', '4']);
      const exe = path.join(work, 'audiomixer-native'), t = run(exe, ['--backend', 'null', '--seconds', '0.6']);
      if (!/audiomixer-native: null/.test(t)) throw new Error('the Linux program did not run on the null device');
      const dir = path.join(work, 'linux'); fs.mkdirSync(dir); fs.copyFileSync(exe, path.join(dir, 'audiomixer-native'));
      const tar = path.join(out || path.join(ROOT, 'releases/linux'), `AudioMixer-${version}-native-linux-x64.tar.gz`); fs.mkdirSync(path.dirname(tar), { recursive: true });
      run('tar', ['-czf', tar, '-C', dir, 'audiomixer-native']); results.push(tar);
    }
    for (const [t, arch] of [['x86_64-w64-mingw32', 'x64'], ['i686-w64-mingw32', 'x86']]) {   // Windows (cross-compiled)
      if (!has(t + '-g++')) continue;
      const dir = out || path.join(ROOT, 'releases/windows'); const exe = compileWindows(arch, path.join(dir, arch === 'x64' ? 'AmF X1.exe' : 'AmF X1 (32-bit).exe'));   // the program is "AmF X1.exe"; the ASIO driver DLL lands next to it
      results.push(exe); for (const f of fs.readdirSync(dir)) if (/AudioMixerASIO(64|32)\.dll$/.test(f) && ((arch === 'x64') === /64/.test(f))) results.push(path.join(dir, f));
    }
  } finally { fs.rmSync(work, { recursive: true, force: true }); }
  if (!results.length) throw new Error('nothing could be built: needs cmake + g++ (Linux) and / or mingw-w64 (Windows)');
  for (const f of results) console.log(`${f}  (${(fs.statSync(f).size / 1024).toFixed(0)} KB)\n     SHA-256 ${sha(f)}`);
}
if (require.main === module) { try { main(); } catch (e) { console.error('native build failed: ' + e.message); process.exit(1); } }
module.exports = { compileWindows };
