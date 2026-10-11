'use strict';
// Builds every Windows installer from the staged install trees and signs them:
//   dist/AudioMixer-<version>-x64.msi   Windows Installer package, 64-bit, installs to C:\Program Files\Audio Mixer (administrator)
//   dist/AudioMixer-<version>-x86.msi   Windows Installer package, 32-bit, installs to C:\Program Files (x86)\Audio Mixer (administrator)
//   dist/Audio Mixer-<version>.exe      signed setup program (64-bit Windows): checks the package SHA-256, then installs it; /quiet /uninstall
//   dist/AudioMixer-signing.cer         the public certificate the files are signed with (see scripts/sign.js)
// Each installer contains the mixer page, the Node.js server and client, the bundled official Node.js runtime, Audify (ASIO / WASAPI / DirectSound),
// the Audio Mixer ASIO driver files (asio-driver\; registering them is the optional AsioDriver feature, off by default),
// the verify scan, Start Menu entries and an uninstall entry in Settings > Apps. Linux (.deb) and macOS (.app) packages: build-unix.js.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { buildInstaller } = require('./build-installer');
const { buildMsi } = require('./build-msi');
const { buildExe, compileLauncher, compileCli } = require('./build-exe');
const { buildAsio } = require('./build-drivers');
const { ensureSigningCert, signFile } = require('./sign');

const sha = f => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');

// The native launcher (starts the bundled Node.js server without a console window, opens the plugin folder) goes into the install tree,
// signed, and is listed in MANIFEST.sha256 like every other installed file.
function addLauncher(staged, { out, id, env }) {
  const exe = compileLauncher({ work: path.join(out, 'launcher', staged.arch), version: staged.version, arch: staged.arch });
  const target = path.join(staged.stage, 'AudioMixerServer.exe');
  fs.copyFileSync(exe, target);
  if (id) signFile(target, id, { env });
  fs.appendFileSync(path.join(staged.stage, 'MANIFEST.sha256'), `${sha(target)}  AudioMixerServer.exe\n`);
  // the command line: audio-mixer.exe (console program, put on PATH by the installer's CommandLine feature)
  const cli = compileCli({ work: path.join(out, 'cli', staged.arch), version: staged.version, arch: staged.arch });
  const cliTarget = path.join(staged.stage, 'audio-mixer.exe');
  fs.copyFileSync(cli, cliTarget);
  if (id) signFile(cliTarget, id, { env });
  fs.appendFileSync(path.join(staged.stage, 'MANIFEST.sha256'), `${sha(cliTarget)}  audio-mixer.exe\n`);
}

// The ASIO driver goes into the install tree as files (asio-driver\): AudioMixerASIO32.dll, and for the 64-bit package AudioMixerASIO64.dll too (64-bit Windows runs
// 32-bit and 64-bit hosts), signed and listed in MANIFEST.sha256. Registering it is the optional MSI feature AsioDriver (see build-msi.js).
function addAsioDriver(staged, { out, id, env }) {
  const dir = path.join(staged.stage, 'asio-driver'), work = path.join(out, 'asio', staged.arch);
  fs.rmSync(dir, { recursive: true, force: true }); fs.mkdirSync(dir, { recursive: true }); fs.mkdirSync(work, { recursive: true });
  const files = [];
  for (const arch of staged.arch === 'x64' ? ['x64', 'x86'] : ['x86']) files.push(path.basename(buildAsio(arch, work)));
  for (const f of files) { fs.copyFileSync(path.join(work, f), path.join(dir, f)); if (id) signFile(path.join(dir, f), id, { env }); }
  fs.appendFileSync(path.join(staged.stage, 'MANIFEST.sha256'), files.map(f => `${sha(path.join(dir, f))}  asio-driver/${f}\n`).join(''));
  return files;
}

async function buildInstallers({ out = path.join(__dirname, '..', 'dist'), archs = ['x64', 'x86'], sign = true, env = process.env } = {}) {
  const outAbs = path.resolve(out);
  const id = sign ? ensureSigningCert({ dir: path.join(outAbs, 'cache', 'signing'), env }) : null;
  const result = { msi: {}, exe: null, signing: id };
  for (const arch of archs) {
    const staged = await buildInstaller({ out: outAbs, arch });
    addLauncher(staged, { out: outAbs, id, env });
    addAsioDriver(staged, { out: outAbs, id, env });
    const m = await buildMsi({ out: outAbs, arch, scope: 'machine', staged });
    if (id) signFile(m.msi, id, { env });
    m.sha256 = sha(m.msi);
    result.msi[arch] = m;
  }
  if (result.msi.x64) {
    const m = result.msi.x64;
    const exe = path.join(outAbs, `Audio Mixer-${m.version}.exe`);
    const e = buildExe({ msi: m.msi, productCode: m.productCode, out: exe, version: m.version, arch: 'x64', work: path.join(outAbs, 'exe'), id });
    result.exe = { ...e, version: m.version };
  }
  if (id && id.cer) { fs.copyFileSync(id.cer, path.join(outAbs, 'AudioMixer-signing.cer')); }
  return result;
}

if (require.main === module) {
  buildInstallers({ sign: !process.argv.includes('--no-sign') }).then(r => {
    for (const [arch, m] of Object.entries(r.msi)) console.log(`MSI ${arch}: ${m.msi}\n     SHA-256 ${m.sha256}\n     product code ${m.productCode}`);
    if (r.exe) console.log(`EXE: ${r.exe.exe}\n     SHA-256 ${r.exe.sha256}`);
    if (r.signing) console.log(r.signing.selfSigned
      ? `Signed with a self-signed certificate (thumbprint ${r.signing.thumbprint}): Windows shows "unknown publisher" until AudioMixer-signing.cer is trusted. Set SIGN_PFX for a commercial certificate.`
      : 'Signed with the certificate from SIGN_PFX.');
  }).catch(e => { console.error('Installer build failed: ' + e.message); process.exit(1); });
}
module.exports = { buildInstallers, addAsioDriver };
