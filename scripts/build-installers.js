'use strict';
// Builds BOTH Windows installers from one staging folder:
//   dist/AudioMixer-Setup-<version>.exe   (NSIS, per-user, wizard with options)
//   dist/AudioMixer-<version>.msi         (Windows Installer package, per-user, for managed installs / msiexec)
// Both contain the server, client, bundled Node.js, Audify (ASIO / WASAPI), the plugin folder setup and the verify scan.
const path = require('node:path');
const { buildInstaller } = require('./build-installer');
const { buildMsi } = require('./build-msi');

async function buildInstallers({ out = path.join(__dirname, '..', 'dist') } = {}) {
  const exe = await buildInstaller({ out });
  const msi = await buildMsi({ out, staged: exe });
  return { exe, msi };
}

if (require.main === module) {
  buildInstallers().then(({ exe, msi }) => {
    console.log(`EXE: ${exe.installer}\n     SHA-256 ${exe.sha256}\nMSI: ${msi.msi}\n     SHA-256 ${msi.sha256}\nUnsigned: Windows SmartScreen will warn until they are code-signed.`);
  }).catch(e => { console.error('Installer build failed: ' + e.message); process.exit(1); });
}
module.exports = { buildInstallers };
