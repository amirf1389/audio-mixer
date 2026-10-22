#!/usr/bin/env node
'use strict';
// Builds and tests the native engine (engine/): the same numbers in every language, held to engine/tests/vectors.txt.
//   node scripts/build-engine.js [--c] [--cpp] [--rust] [--wasm] [--node] [--apple] [--io]    (no option = all of them that this machine can do)
//     --c      embedded C core: build with -std=c99 -Werror, run the vectors with libm and with the built-in math
//     --cpp    C++17 engine: build with -Werror, run the vectors
//     --rust   Rust crate: cargo test (offline, no dependencies)
//     --wasm   the embedded C core as WebAssembly (clang + wasm-ld): writes engine/wasm/am_core.wasm and embeds it in index.html (script id "engine-wasm")
//     --node   the Node.js addon engine/node (g++ and the Node.js headers): build/Release/am_engine.node, optional for the bridge
//     --io     the device runtime (engine/cpp/src/runtime.cpp): the null and ALSA backends, the WASAPI one cross-compiled with mingw-w64
//     --apple  nothing to build here (needs Xcode); checks that the Swift package points at the C core
// Each step reports what it did, or why it could not (a missing compiler is not an error).
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..'), ENGINE = path.join(ROOT, 'engine'), VECTORS = path.join(ENGINE, 'tests', 'vectors.txt');
const which = n => { for (const d of (process.env.PATH || '').split(path.delimiter)) { const p = path.join(d, n); if (fs.existsSync(p)) return p; } return null; };
const run = (cmd, args, opts = {}) => { const r = spawnSync(cmd, args, { encoding: 'utf8', maxBuffer: 64 << 20, ...opts }); return { ok: r.status === 0, out: ((r.stdout || '') + (r.stderr || '')).trim(), status: r.status }; };
const need = (...tools) => tools.filter(t => !which(t));

function buildC() {
  if (need('make', 'cc').length) return { skipped: 'make / cc not found' };
  const r = run('make', ['-C', path.join(ENGINE, 'embedded-c'), 'test', 'VECTORS=' + VECTORS]);
  return { ok: r.ok, detail: r.out.split('\n').filter(l => /cases|FAIL|error/.test(l)).join('\n') || r.out.slice(-600) };
}
function buildCpp({ out = path.join(ENGINE, 'cpp', 'build') } = {}) {
  const cxx = which('g++') || which('clang++'); if (!cxx) return { skipped: 'no C++ compiler' };
  fs.mkdirSync(out, { recursive: true });
  const exe = path.join(out, 'engine_test'), c = path.join(ENGINE, 'cpp');
  let r = run(cxx, ['-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-I' + path.join(c, 'include'), '-o', exe, path.join(c, 'src', 'engine.cpp'), path.join(c, 'test', 'engine_test.cpp')]);
  if (!r.ok) return { ok: false, detail: r.out.slice(-800) };
  r = run(exe, [VECTORS]);
  return { ok: r.ok, detail: r.out };
}
function buildRust() {
  if (need('cargo').length) return { skipped: 'cargo not found' };
  const r = run('cargo', ['test', '--offline', '--release', '--', '--nocapture'], { cwd: path.join(ENGINE, 'rust') });
  return { ok: r.ok, detail: r.out.split('\n').filter(l => /Rust crate|test result: (ok|FAILED)\. [1-9]|error/.test(l)).join('\n') || r.out.slice(-600) };
}

// WebAssembly: freestanding (no C library, no imports), math built in, the page's 6 hours of loudness blocks
function wasmArgs(out) {
  return ['--target=wasm32', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin', '-nostdlib', '-DAMC_NO_LIBM', '-DAMC_LUFS_MAX_BLOCKS=216000', '-fuse-ld=lld',
    '-Wl,--no-entry', '-Wl,--initial-memory=6815744', '-Wl,-z,stack-size=65536', '-o', out, path.join(ENGINE, 'embedded-c', 'src', 'am_core.c'), path.join(ENGINE, 'wasm', 'am_wasm.c')];
}
function buildWasm({ embed = true } = {}) {
  if (need('clang', 'wasm-ld').length) return { skipped: 'clang / wasm-ld not found' };
  const out = path.join(ENGINE, 'wasm', 'am_core.wasm'), r = run('clang', wasmArgs(out));
  if (!r.ok) return { ok: false, detail: r.out.slice(-800) };
  const bytes = fs.readFileSync(out), mod = new WebAssembly.Module(bytes);
  if (WebAssembly.Module.imports(mod).length) return { ok: false, detail: 'the module has imports: ' + JSON.stringify(WebAssembly.Module.imports(mod)) };
  if (embed) {
    const page = path.join(ROOT, 'index.html'), html = fs.readFileSync(page, 'utf8'), a = html.indexOf('/*AM_WASM_B64:BEGIN*/'), b = html.indexOf('/*AM_WASM_B64:END*/');
    if (a < 0 || b < a) return { ok: false, detail: 'the markers of the engine-wasm script are not in index.html' };
    fs.writeFileSync(page, html.slice(0, a) + "/*AM_WASM_B64:BEGIN*/'" + bytes.toString('base64') + "'" + html.slice(b));
  }
  return { ok: true, detail: `${bytes.length} bytes, no imports, ${WebAssembly.Module.exports(mod).length} exports` + (embed ? ', embedded in index.html' : '') };
}

function nodeInclude() {
  const cands = [path.join(path.dirname(process.execPath), '..', 'include', 'node'), path.join(process.env.NODE_INCLUDE || '', '')];
  return cands.find(d => d && fs.existsSync(path.join(d, 'node_api.h'))) || null;
}
function buildNode({ out = path.join(ENGINE, 'node', 'build', 'Release') } = {}) {
  const cxx = which('g++') || which('clang++'), inc = nodeInclude();
  if (!cxx) return { skipped: 'no C++ compiler' };
  if (!inc) return { skipped: 'the Node.js headers (node_api.h) were not found: set NODE_INCLUDE to <node>/include/node' };
  fs.mkdirSync(out, { recursive: true });
  const so = path.join(out, 'am_engine.node'), n = path.join(ENGINE, 'node'), c = path.join(ENGINE, 'cpp');
  const r = run(cxx, ['-std=c++17', '-O2', '-Wall', '-Wextra', '-fPIC', '-shared', '-I' + inc, '-I' + path.join(c, 'include'), '-o', so, path.join(n, 'am_engine_node.cc'), path.join(c, 'src', 'engine.cpp')].concat(process.platform === 'darwin' ? ['-undefined', 'dynamic_lookup'] : []));
  return r.ok ? { ok: true, detail: so, file: so } : { ok: false, detail: r.out.slice(-800) };
}

function buildIo({ out = path.join(ENGINE, 'cpp', 'build') } = {}) {
  const cxx = which('g++') || which('clang++'); if (!cxx) return { skipped: 'no C++ compiler' };
  fs.mkdirSync(out, { recursive: true });
  const c = path.join(ENGINE, 'cpp'), inc = '-I' + path.join(c, 'include'), exe = path.join(out, 'runtime_test'), steps = [];
  const hasAlsa = fs.existsSync('/usr/include/alsa/asoundlib.h');
  const srcs = [path.join(c, 'src', 'engine.cpp'), path.join(c, 'src', 'runtime.cpp'), path.join(c, 'src', 'backend_null.cpp'), path.join(c, 'test', 'runtime_test.cpp')];
  if (hasAlsa) srcs.push(path.join(c, 'src', 'backend_alsa.cpp'));
  let r = run(cxx, ['-std=c++17', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-pthread', inc, ...(hasAlsa ? ['-DAM_HAVE_ALSA'] : []), '-o', exe, ...srcs, ...(hasAlsa ? ['-lasound'] : [])]);
  if (!r.ok) return { ok: false, detail: r.out.slice(-1200) };
  r = run(exe, []); steps.push(r.out);
  if (!r.ok) return { ok: false, detail: steps.join('\n') };
  const mg = which('x86_64-w64-mingw32-g++');                     // the Windows backend: compiled (not run) with mingw-w64
  if (mg) { const o = path.join(out, 'backend_wasapi.o'), w = run(mg, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', inc, '-c', '-o', o, path.join(c, 'src', 'backend_wasapi.cpp')]); steps.push(w.ok ? 'WASAPI backend: compiles with mingw-w64 (not run: no Windows here)' : 'WASAPI backend FAILED to compile:\n' + w.out.slice(-800)); if (!w.ok) return { ok: false, detail: steps.join('\n') }; }
  for (const t of ['x86_64-w64-mingw32', 'i686-w64-mingw32']) {   // the ASIO host backend (Steinberg ASIO SDK headers, platforms/windows/asio-driver): compiled for 64- and 32-bit Windows
    const g = which(t + '-g++'); if (!g) continue;
    const w = run(g, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', inc, '-I' + path.join(c, 'src'), '-I' + path.join(ROOT, 'platforms', 'windows', 'asio-driver'), '-c', '-o', path.join(out, `backend_asio_${t}.o`), path.join(c, 'src', 'backend_asio.cpp')]);
    steps.push(w.ok ? `ASIO backend (${t}): compiles with mingw-w64 (not run: no Windows here)` : `ASIO backend (${t}) FAILED to compile:\n` + w.out.slice(-800)); if (!w.ok) return { ok: false, detail: steps.join('\n') };
  }
  return { ok: true, detail: steps.join('\n') };
}

function checkApple() {
  const pkg = path.join(ENGINE, 'apple', 'Package.swift'); if (!fs.existsSync(pkg)) return { skipped: 'engine/apple is not there' };
  const link = path.join(ENGINE, 'apple', 'Sources', 'CAudioMixerCore', 'src', 'am_core.c');
  return { ok: fs.existsSync(link), detail: 'Swift package + Objective-C wrapper (' + (fs.existsSync(link) ? 'points at the C core' : 'MISSING the link to the C core') + '); not compiled here (needs Xcode)' };
}

const STEPS = { c: ['embedded C core', buildC], cpp: ['C++ engine', buildCpp], rust: ['Rust crate', buildRust], wasm: ['WebAssembly', buildWasm], node: ['Node.js addon', buildNode], io: ['device runtime', buildIo], apple: ['Apple (Swift / Objective-C)', checkApple] };

if (require.main === module) {
  const a = process.argv.slice(2).filter(x => x.startsWith('--')).map(x => x.slice(2)), pick = a.length ? a : Object.keys(STEPS);
  let bad = 0;
  for (const k of pick) {
    if (!STEPS[k]) { console.error('unknown option --' + k); process.exit(2); }
    const [name, fn] = STEPS[k]; let r; try { r = fn(); } catch (e) { r = { ok: false, detail: e.message }; }
    console.log(`${r.skipped ? 'SKIP' : r.ok ? 'OK  ' : 'FAIL'} ${name}${r.skipped ? ': ' + r.skipped : ''}${r.detail ? '\n     ' + String(r.detail).split('\n').join('\n     ') : ''}`);
    if (r.ok === false) bad++;
  }
  process.exit(bad ? 1 : 0);
}
module.exports = { buildC, buildCpp, buildRust, buildWasm, buildNode, buildIo, checkApple, wasmArgs, VECTORS };
