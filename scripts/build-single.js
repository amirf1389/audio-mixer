#!/usr/bin/env node
'use strict';
// The single-file server: the local server, the command line and the mixer page in ONE file, with nothing to install next to it.
//   node scripts/build-single.js [--targets windows-x64,windows-x86,linux-x64,linux-arm64,macos-x64,macos-arm64,portable] [--out dist/single]
//
//   portable              AudioMixerServer-<version>.js          one JavaScript file (about 300 KB, the page inside, compressed): `node AudioMixerServer-<version>.js`
//                                                                 runs anywhere Node.js 18+ runs: Android (Termux, nodejs-mobile), a Raspberry Pi, any PC
//   windows-x64 / -x86    AudioMixerServer-<version>-windows-<arch>.exe      Node.js single-executable application: that file IS the program
//                                                                 (plus node_modules/npm next to it: client/cli.js's "npm" command finds it there,
//                                                                 so a PC with no Node.js install still gets npm; see stageNpm below)
//   linux-x64 / -arm64    AudioMixerServer-<version>-linux-<arch>.tar.gz     (the executable inside; 100 MB would not fit a repository file)
//   macos-x64 / -arm64    AudioMixerServer-<version>-macos-<arch>.tar.gz     the executable inside; not signed (see below)
//
// How: client/cli.js (which carries bridge/server.js and everything it needs) is bundled with esbuild into one script, the page (index.html, boot.html) is
// gzip-compressed into it, and Node's single-executable support (postject) injects that script into the official Node.js binary of the same version. The
// native audio module (Audify / PortAudio) cannot live inside a file: it is looked up in `node_modules` next to the executable, as the server always did.
// Optional pieces (plugin host, Windows device helper, Core Audio helper) are found in `native/` next to the executable, the layout of an installed copy.
// Windows: the executable is not signed here (build-installers.js signs the copy that goes into the installers). macOS: the injection invalidates Apple's
// signature on the Node binary and Apple Silicon refuses to run an unsigned binary, so on the Mac run once:  codesign --force --sign - AudioMixerServer
// Needs internet once (esbuild, postject, the official Node.js binaries checked against nodejs.org's SHASUMS256.txt), cached in dist/cache.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const zlib = require('node:zlib');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const NODE_VERSION = process.env.SINGLE_NODE_VERSION || 'v22.23.3';          // one version for every target: the blob must match the binary it is injected into
const ESBUILD_VERSION = '0.28.2', POSTJECT_VERSION = '1.0.0-alpha.6';
const FUSE = 'NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2';
const TARGETS = {
  'windows-x64': { os: 'windows', arch: 'x64', node: 'win-x64' },
  'windows-x86': { os: 'windows', arch: 'x86', node: 'win-x86' },
  'linux-x64': { os: 'linux', arch: 'x64', node: 'linux-x64' },
  'linux-arm64': { os: 'linux', arch: 'arm64', node: 'linux-arm64' },
  'macos-x64': { os: 'macos', arch: 'x64', node: 'darwin-x64' },
  'macos-arm64': { os: 'macos', arch: 'arm64', node: 'darwin-arm64' },
};

const sha = b => crypto.createHash('sha256').update(b).digest('hex');
function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { encoding: 'utf8', maxBuffer: 1 << 28, ...opts });
  if (r.error) throw new Error(cmd + ': ' + r.error.message);
  if (r.status !== 0) throw new Error(`${path.basename(cmd)} ${args.slice(0, 3).join(' ')} failed:\n${r.stdout || ''}${r.stderr || ''}`);
  return r;
}
const versionOf = () => JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8')).version;

// esbuild and postject are build tools, not dependencies of the product: installed once into dist/cache/single-tools
function ensureTools(cache) {
  const dir = path.join(cache, 'single-tools'), mark = path.join(dir, 'node_modules', '.complete');
  if (!fs.existsSync(mark)) {
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'package.json'), JSON.stringify({ name: 'audio-mixer-single-tools', private: true, dependencies: { esbuild: ESBUILD_VERSION, postject: POSTJECT_VERSION } }));
    run('npm', ['install', '--no-audit', '--no-fund', '--no-package-lock'], { cwd: dir });
    fs.writeFileSync(mark, '1');
  }
  return { esbuild: require(path.join(dir, 'node_modules', 'esbuild')), postject: path.join(dir, 'node_modules', 'postject', 'dist', 'cli.js') };
}

// ── the script ──
// The prelude runs first: it hands the page to the server (global.__AM_ASSETS), tells the command line that it is the single-file build (global.__AM_SELF) and gives
// the bundle a `require` that also finds the optional native audio module next to the executable (Node's own `require` in a single executable knows built-in modules only).
function prelude({ sea }) {
  const assets = {};
  for (const f of ['index.html', 'boot.html']) assets['/' + f] = zlib.gzipSync(fs.readFileSync(path.join(ROOT, f)), { level: 9 }).toString('base64');
  const help = fs.readFileSync(path.join(ROOT, 'client', 'cli.js'), 'utf8').split('\n').slice(0, 40);   // the header comment of cli.js: `--help` prints it
  return `const __nodeRequire = require, __path = require('node:path'), __zlib = require('node:zlib');
const __self = ${sea ? 'process.execPath' : '(process.argv[1] ? __path.resolve(process.argv[1]) : process.execPath)'};
const __base = __path.dirname(__self);
const __ext = require('node:module').createRequire(__path.join(__base, 'AudioMixerServer'));
const __req = Object.assign(function (name) { try { return __nodeRequire(name); } catch (e) { if (e && (e.code === 'MODULE_NOT_FOUND' || e.code === 'ERR_UNKNOWN_BUILTIN_MODULE')) return __ext(name); throw e; } }, __nodeRequire);
global.__AM_SELF = { sea: ${sea ? 'true' : 'false'}, script: ${sea ? 'null' : '__self'} };
global.__AM_CLI_HELP = ${JSON.stringify(help.join('\n'))};
global.__AM_ASSETS = {};
for (const [k, v] of Object.entries(${JSON.stringify(assets)})) global.__AM_ASSETS[k] = __zlib.gunzipSync(Buffer.from(v, 'base64'));
`;
}

function bundle({ cache, sea }) {
  const { esbuild } = ensureTools(cache);
  const entry = path.join(os.tmpdir(), `am-single-entry-${process.pid}.js`);
  fs.writeFileSync(entry, `require(${JSON.stringify(path.join(ROOT, 'client', 'cli.js'))}).main(process.argv.slice(2)).then(code => process.exit(code), e => { console.error(e.message); process.exit(1); });\n`);
  let out;
  try {
    out = esbuild.buildSync({ entryPoints: [entry], bundle: true, platform: 'node', target: 'node18', format: 'cjs', minify: true, legalComments: 'none', write: false,
      external: ['audify', 'naudiodon2'], logLevel: 'warning', absWorkingDir: ROOT });
  } finally { fs.rmSync(entry, { force: true }); }
  const code = Buffer.from(out.outputFiles[0].contents).toString('utf8');
  // __dirname / __filename of the bundled files: bridge/ and client/ of an installed copy, so `..` is the folder of the executable (native/, runtime pieces)
  return `'use strict';\n${prelude({ sea })}(function (require, __dirname, __filename) {\n${code}\n})(__req, __path.join(__base, 'bridge'), __self);\n`;
}

// ── the official Node.js binaries ──
function fetchBinary(target, cache) {
  const t = TARGETS[target]; if (!t) throw new Error('unknown target ' + target);
  const exe = path.join(cache, `single-node-${NODE_VERSION}-${t.node}${t.os === 'windows' ? '.exe' : ''}`);
  if (fs.existsSync(exe)) return exe;
  const ext = t.os === 'windows' ? 'zip' : t.os === 'macos' ? 'tar.gz' : 'tar.xz', name = `node-${NODE_VERSION}-${t.node}.${ext}`, url = `https://nodejs.org/dist/${NODE_VERSION}/${name}`;
  const sums = run('curl', ['-fsSL', `https://nodejs.org/dist/${NODE_VERSION}/SHASUMS256.txt`]).stdout;
  const want = (sums.split('\n').map(l => l.trim().split(/\s+/)).find(p => p[1] === name) || [])[0];
  if (!want) throw new Error('no checksum published for ' + name);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'am-node-'));
  try {
    const arc = path.join(tmp, name);
    run('curl', ['-fsSL', '-o', arc, url]);
    const got = sha(fs.readFileSync(arc));
    if (got !== want) throw new Error(`checksum mismatch for ${name} (expected ${want}, got ${got})`);
    fs.mkdirSync(cache, { recursive: true });
    if (t.os === 'windows') {
      const { extractFromZip } = require('./build-installer');
      const data = extractFromZip(fs.readFileSync(arc), n => n === `node-${NODE_VERSION}-${t.node}/node.exe`);
      if (!data) throw new Error('node.exe not found in ' + name);
      fs.writeFileSync(exe, data);
    } else {
      run('tar', ['-xf', arc, '-C', tmp, `node-${NODE_VERSION}-${t.node}/bin/node`]);
      fs.copyFileSync(path.join(tmp, `node-${NODE_VERSION}-${t.node}`, 'bin', 'node'), exe); fs.chmodSync(exe, 0o755);
    }
  } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
  return exe;
}

// the executable of one target (the file only; packing is done by buildTarget)
function buildExecutable({ target, cache, outFile, script }) {
  const t = TARGETS[target], { postject } = ensureTools(cache);
  const gen = fetchBinary('linux-x64', cache);                              // runs on this machine; it writes the blob for every target (same Node.js version)
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'am-sea-'));
  try {
    const main = path.join(tmp, 'main.js'), blob = path.join(tmp, 'app.blob');
    fs.writeFileSync(main, script);
    // a code cache is specific to the machine type that wrote it: only linux-x64 (this generator) can use it
    fs.writeFileSync(path.join(tmp, 'sea-config.json'), JSON.stringify({ main, output: blob, disableExperimentalSEAWarning: true, useCodeCache: target === 'linux-x64' }));
    run(gen, ['--experimental-sea-config', path.join(tmp, 'sea-config.json')]);
    fs.copyFileSync(fetchBinary(target, cache), outFile); fs.chmodSync(outFile, 0o755);
    run(process.execPath, [postject, outFile, 'NODE_SEA_BLOB', blob, '--sentinel-fuse', FUSE, ...(t.os === 'macos' ? ['--macho-segment-name', 'NODE_SEA'] : [])]);
    if (t.os === 'macos') adhocSign(outFile);
  } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
  return outFile;
}

// macOS: injecting the script invalidates Apple's signature on the Node binary, and Apple Silicon will not run a binary without a valid one. An ad-hoc signature
// (no certificate, no identity) is enough to run it; `rcodesign` (apple-codesign, runs anywhere) writes it. Without rcodesign the binary stays unsigned
// and has to be signed on the Mac:  codesign --force --sign - AudioMixerServer
function adhocSign(file) {
  const rc = process.env.RCODESIGN || (spawnSync('rcodesign', ['--version']).status === 0 ? 'rcodesign' : null);
  if (!rc) return false;
  run(rc, ['sign', file]);   // no certificate given: an ad-hoc signature, in place
  return true;
}

// deterministic .tar.gz with one executable inside
// npm next to the Windows single-file executable (node_modules/npm, the exact lookup client/cli.js's npm command already does: a bundled npm
// is found at path.dirname(process.execPath)/node_modules/npm/bin/npm-cli.js). Not embedded in the executable itself - a single-executable
// blob carries one script, not a filesystem, and npm is a few thousand files - so it travels as the folder next to it instead, fetched and
// SHA-256-checked the same way build-installer.js already bundles npm into the Windows .msi/.exe installer.
async function stageNpm({ out, cache, arch, fetchImpl, version = NODE_VERSION }) {
  const { fetchNpm } = require('./build-installer');
  const npm = await fetchNpm({ cache, version, arch, fetchImpl });
  const dest = path.join(out, 'node_modules', 'npm');
  fs.rmSync(dest, { recursive: true, force: true });
  fs.cpSync(npm.dir, dest, { recursive: true, filter: src => path.basename(src) !== '.complete' });
  return dest;
}

function pack(dir, file, outTar) {
  run('tar', ['--sort=name', '--owner=0', '--group=0', '--numeric-owner', '--mtime=@0', '-cf', outTar.replace(/\.gz$/, ''), '-C', dir, file]);
  const tar = outTar.replace(/\.gz$/, '');
  fs.writeFileSync(outTar, zlib.gzipSync(fs.readFileSync(tar), { level: 9, mtime: 0 })); fs.rmSync(tar);
}

async function build({ targets = ['portable', ...Object.keys(TARGETS)], out = path.join(ROOT, 'dist', 'single'), cache = path.join(ROOT, 'dist', 'cache'), fetchImpl } = {}) {
  const version = versionOf(), made = [];
  fs.mkdirSync(out, { recursive: true });
  let portable = null, windowsArch = null;
  for (const target of targets) {
    if (target === 'portable') {
      const file = path.join(out, `AudioMixerServer-${version}.js`);
      fs.writeFileSync(file, bundle({ cache, sea: false })); made.push({ target, file }); portable = file; continue;
    }
    const t = TARGETS[target]; if (!t) throw new Error('unknown target ' + target);
    const script = bundle({ cache, sea: true });
    if (t.os === 'windows') {
      const file = path.join(out, `AudioMixerServer-${version}-${target}.exe`);
      buildExecutable({ target, cache, outFile: file, script }); made.push({ target, file }); windowsArch = t.arch;
    } else {
      const work = fs.mkdtempSync(path.join(os.tmpdir(), 'am-pack-'));
      try {
        buildExecutable({ target, cache, outFile: path.join(work, 'AudioMixerServer'), script });
        const file = path.join(out, `AudioMixerServer-${version}-${target}.tar.gz`);
        pack(work, 'AudioMixerServer', file); made.push({ target, file });
      } finally { fs.rmSync(work, { recursive: true, force: true }); }
    }
  }
  if (windowsArch) { const npmDir = await stageNpm({ out, cache, arch: windowsArch, fetchImpl }); made.push({ target: 'npm', file: npmDir }); }
  return made;
}

if (require.main === module) {
  const a = process.argv.slice(2), arg = (n, d) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : d; };
  build({ targets: arg('--targets', '') ? arg('--targets').split(',') : undefined, out: arg('--out') ? path.resolve(arg('--out')) : undefined }).then(r => {
    for (const m of r) console.log(fs.statSync(m.file).isDirectory() ? `${m.target.padEnd(12)} ${m.file}` : `${m.target.padEnd(12)} ${m.file}  (${(fs.statSync(m.file).size / 1048576).toFixed(1)} MB)`);
  }).catch(e => { console.error('Single-file build failed: ' + e.message); process.exit(1); });
}
module.exports = { build, bundle, buildExecutable, fetchBinary, ensureTools, prelude, TARGETS, NODE_VERSION, stageNpm };
