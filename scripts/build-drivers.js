#!/usr/bin/env node
'use strict';
// Builds the operating-system drivers of drivers/ that this machine can build, and packs them:
//   node scripts/build-drivers.js [--out releases]
//   AudioMixer-<version>-asio-driver-windows.zip     AudioMixerASIO64.dll, AudioMixerASIO32.dll (cross-compiled with mingw-w64), register.bat, unregister.bat, README.txt
//   AudioMixer-<version>-drivers-source.tar.gz       drivers/ complete: ASIO (Windows), ALSA plugin (Linux), Core Audio plug-in (macOS), audio HAL (Android)
// The macOS plug-in and the Android HAL need their own SDKs (Xcode, AOSP) and are shipped as source. The ALSA plugin is built on the target Linux PC (make).
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..'), DRV = path.join(ROOT, 'drivers');
const which = n => { for (const d of (process.env.PATH || '').split(path.delimiter)) { const p = path.join(d, n); if (fs.existsSync(p)) return p; } return null; };
const run = (cmd, args, opts = {}) => { const r = spawnSync(cmd, args, { encoding: 'utf8', ...opts }); if (r.status !== 0) throw new Error(`${path.basename(cmd)} ${args.slice(0, 3).join(' ')} failed: ${(r.stderr || r.error || '').toString().slice(0, 1500)}`); return r; };
const sha = f => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');

// the ASIO driver as a DLL for one architecture; returns the file or throws
function buildAsio(arch, outDir) {
  const t = arch === 'x64' ? 'x86_64-w64-mingw32' : 'i686-w64-mingw32', cc = which(t + '-gcc'), cxx = which(t + '-g++');
  if (!cc || !cxx) throw new Error(`mingw-w64 (${t}-g++) is needed to build the ASIO driver (Debian / Ubuntu: apt install mingw-w64)`);
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'asio-')), dir = path.join(DRV, 'windows-asio');
  const out = path.join(outDir, arch === 'x64' ? 'AudioMixerASIO64.dll' : 'AudioMixerASIO32.dll');
  try {
    run(cc, ['-std=gnu99', '-Wall', '-Wextra', '-Werror', '-O2', '-c', path.join(DRV, 'common', 'am_link.c'), '-o', path.join(work, 'am_link.o')]);
    run(cxx, ['-std=gnu++14', '-Wall', '-Wextra', '-Werror', '-O2', '-c', path.join(dir, 'audiomixer_asio.cpp'), '-o', path.join(work, 'asio.o')]);
    // static runtime: the DLL needs nothing but Windows' own DLLs, so it loads in any host
    run(cxx, ['-shared', '-static', '-static-libgcc', '-static-libstdc++', '-o', out, path.join(work, 'asio.o'), path.join(work, 'am_link.o'), path.join(dir, 'audiomixer_asio.def'),
      '-lws2_32', '-lole32', '-loleaut32', '-luuid', '-ladvapi32', '-lwinmm', '-Wl,--kill-at', '-Wl,--enable-stdcall-fixup']);
    const strip = which(t + '-strip'); if (strip) spawnSync(strip, ['--strip-unneeded', out]);
  } finally { fs.rmSync(work, { recursive: true, force: true }); }
  return out;
}

const README_WINDOWS = version => `Audio Mixer ASIO driver ${version}

An ASIO driver (a DLL) that makes "Audio Mixer" appear in the ASIO driver list of DAWs, OBS and other ASIO programs.
   ASIO outputs (what the program plays)   -> the mixer reads them as an input source  (LIVE SOURCES: READ on the device "Audio Mixer")
   ASIO inputs  (what the program records) <- what the mixer sends to the device       (LIVE SOURCES: WRITE)
2 in / 2 out, 44.1 / 48 / 88.2 / 96 kHz, buffers of 64 to 2048 frames. Audio Mixer must be running (the driver keeps trying to connect every second).

Install (deliberately a separate step: the Audio Mixer setup program never installs a driver):
  1. Unpack this folder somewhere permanent (for example C:\\Program Files\\Audio Mixer ASIO). The DLLs are registered where they are.
  2. Right-click register.bat > Run as administrator. (It runs  regsvr32  on the DLLs: it writes HKLM\\SOFTWARE\\Classes\\CLSID\\{C1893F2F-1AD5-4344-9806-DCFD242C0D48}
     and HKLM\\SOFTWARE\\ASIO\\Audio Mixer, nothing else.)
  3. Start Audio Mixer, then choose "Audio Mixer" as the ASIO driver in your program. Its control panel button shows whether the mixer is connected.
Remove: unregister.bat as administrator, then delete the folder.

The 64-bit DLL is for 64-bit programs, the 32-bit DLL for 32-bit programs; register both if you use both.
Not signed. Windows may warn when a DLL from the internet is registered; check the SHA-256 in the release.
Not tested on a real ASIO host in this release: the DLL was cross-compiled and checked structurally (exports, imports, interface layout).
"ASIO" is a trademark of Steinberg Media Technologies GmbH. The interface was implemented from its published description; no Steinberg SDK file is included.
`;

function buildAll({ out = path.join(ROOT, 'releases') } = {}) {
  const version = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8')).version, made = [];
  fs.mkdirSync(out, { recursive: true });
  // Windows ASIO driver zip
  const stage = fs.mkdtempSync(path.join(os.tmpdir(), 'asiozip-')), pack = path.join(stage, `AudioMixer-ASIO-${version}`);
  fs.mkdirSync(pack);
  try {
    buildAsio('x64', pack); buildAsio('x86', pack);
    for (const f of ['register.bat', 'unregister.bat']) fs.copyFileSync(path.join(DRV, 'windows-asio', f), path.join(pack, f));
    fs.writeFileSync(path.join(pack, 'README.txt'), README_WINDOWS(version).replace(/\r?\n/g, '\r\n'));
    const zip = path.join(out, `AudioMixer-${version}-asio-driver-windows.zip`); fs.rmSync(zip, { force: true });
    run('zip', ['-qr', zip, path.basename(pack)], { cwd: stage });
    fs.writeFileSync(zip + '.sha256', `${sha(zip)}  ${path.basename(zip)}\n`); made.push(zip);
  } finally { fs.rmSync(stage, { recursive: true, force: true }); }
  // all driver sources
  const tar = path.join(out, `AudioMixer-${version}-drivers-source.tar.gz`); fs.rmSync(tar, { force: true });
  run('tar', ['--sort=name', '--mtime=@0', '--owner=0', '--group=0', '--numeric-owner', '--exclude=*.so', '--exclude=*.dll', '--exclude=*.o', '--exclude=*.driver',
    '-czf', tar, '--transform', `s,^drivers,audio-mixer-drivers-${version},`, '-C', ROOT, 'drivers']);
  fs.writeFileSync(tar + '.sha256', `${sha(tar)}  ${path.basename(tar)}\n`); made.push(tar);
  return { version, files: made };
}

if (require.main === module) {
  const a = process.argv.slice(2), i = a.indexOf('--out');
  try { const r = buildAll(i >= 0 ? { out: path.resolve(a[i + 1]) } : {}); r.files.forEach(f => console.log(`${f}  (${(fs.statSync(f).size / 1024).toFixed(0)} KB)`)); }
  catch (e) { console.error('Error: ' + e.message); process.exit(1); }
}
module.exports = { buildAsio, buildAll, README_WINDOWS };
