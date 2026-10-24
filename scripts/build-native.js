#!/usr/bin/env node
'use strict';
// Builds the native program (engine/cpp/app/main.cpp: the C++ engine on the operating system's own audio layer, no browser and no Node.js) for the systems this machine can build:
//   node scripts/build-native.js [--out <dir>]
//   AudioMixer-<version>-native-linux-x64.tar.gz     audiomixer-native (ALSA + null), built with the C++ compiler and CMake here, run on the null device as a check
//   AudioMixer-<version>-native-windows-x64.exe      WASAPI + ASIO + null, static, cross-compiled with mingw-w64 (compiled, not run: no Windows here)
//   AudioMixer-<version>-native-windows-x86.exe      the same for 32-bit Windows
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
function compileWindows(arch, exe) {
  const t = arch === 'x64' ? 'x86_64-w64-mingw32' : 'i686-w64-mingw32';
  if (!has(t + '-g++')) throw new Error(`mingw-w64 (${t}-g++) is needed to build the native program (Debian / Ubuntu: apt install mingw-w64)`);
  const src = ['engine.cpp', 'runtime.cpp', 'backend_null.cpp', 'spatial.cpp', 'backend_wasapi.cpp', 'backend_asio.cpp'].map(f => path.join(ROOT, 'engine/cpp/src', f));
  fs.mkdirSync(path.dirname(exe), { recursive: true });
  run(t + '-g++', ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', '-static', '-static-libgcc', '-static-libstdc++', '-I' + path.join(ROOT, 'engine/cpp/include'), '-I' + path.join(ROOT, 'engine/cpp/src'),
    '-I' + path.join(ROOT, 'platforms/windows/asio-driver'), path.join(ROOT, 'engine/cpp/app/main.cpp'), ...src, '-o', exe, '-lole32', '-luuid', '-ladvapi32']);
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
      const tar = path.join(out || path.join(ROOT, 'platforms/linux/releases'), `AudioMixer-${version}-native-linux-x64.tar.gz`); fs.mkdirSync(path.dirname(tar), { recursive: true });
      run('tar', ['-czf', tar, '-C', dir, 'audiomixer-native']); results.push(tar);
    }
    for (const [t, arch] of [['x86_64-w64-mingw32', 'x64'], ['i686-w64-mingw32', 'x86']]) {   // Windows (cross-compiled)
      if (!has(t + '-g++')) continue;
      results.push(compileWindows(arch, path.join(out || path.join(ROOT, 'platforms/windows/releases'), `AudioMixer-${version}-native-windows-${arch}.exe`)));
    }
  } finally { fs.rmSync(work, { recursive: true, force: true }); }
  if (!results.length) throw new Error('nothing could be built: needs cmake + g++ (Linux) and / or mingw-w64 (Windows)');
  for (const f of results) console.log(`${f}  (${(fs.statSync(f).size / 1024).toFixed(0)} KB)\n     SHA-256 ${sha(f)}`);
}
if (require.main === module) { try { main(); } catch (e) { console.error('native build failed: ' + e.message); process.exit(1); } }
module.exports = { compileWindows };
