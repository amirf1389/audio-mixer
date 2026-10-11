'use strict';
// Which system this is, and which release files of a manifest fit it (best first). Self-contained: the package has no dependencies and does not read the rest of the repository.
const fs = require('node:fs');

const OSES = ['windows', 'macos', 'linux', 'android', 'ios'];
const ALIAS = { win: 'windows', win32: 'windows', mac: 'macos', darwin: 'macos', osx: 'macos', apk: 'android', iphone: 'ios', ipad: 'ios' };
const ARCH = { x64: 'x64', amd64: 'x64', x86_64: 'x64', x86: 'x86', ia32: 'x86', i386: 'x86', arm64: 'arm64', aarch64: 'arm64' };

// detect({ os, arch, distro }) overrides; env / platform / archName / debian are injected by tests
function detect(o = {}) {
  const platform = o.platform || process.platform, env = o.env || process.env;
  const forced = String(o.os || '').toLowerCase(), osName = ALIAS[forced] || forced;
  let os = OSES.includes(osName) ? osName : ({ win32: 'windows', darwin: 'macos', linux: 'linux', android: 'android' }[platform] || null);
  let arch = ARCH[String(o.arch || '').toLowerCase()] || null;
  if (!arch) {
    arch = ARCH[o.archName || process.arch] || null;
    // a 32-bit Node.js on 64-bit Windows reports ia32: the machine is still 64-bit (PROCESSOR_ARCHITEW6432 is set for the 32-bit process)
    if (os === 'windows' && arch === 'x86' && (env.PROCESSOR_ARCHITEW6432 || /64/.test(env.PROCESSOR_ARCHITECTURE || ''))) arch = 'x64';
  }
  let distro = null;
  if (os === 'linux') {
    const d = String(o.distro || '').toLowerCase();
    distro = /^(debian|ubuntu|deb|mint)$/.test(d) ? 'debian' : d ? 'other' : (o.debian !== undefined ? o.debian : fs.existsSync('/etc/debian_version')) ? 'debian' : 'other';
  }
  return { os, arch, distro };
}

// manifest keys (see scripts/make-update.js) that fit; macOS takes the archive with install.command (the .dmg is for double-clicking), Debian family the .deb
function keysFor(det, { hasApt = true } = {}) {
  switch (det && det.os) {
    case 'windows': return det.arch === 'x86' ? ['win-x86-msi'] : ['win-x64-exe', 'win-x64-msi'];
    case 'macos': return ['macos'];
    case 'linux': return det.distro === 'debian' && hasApt ? ['linux-deb', 'linux-tar'] : ['linux-tar'];
    case 'android': return ['android-apk'];
    case 'ios': return ['ios-project'];
    default: return [];
  }
}

module.exports = { detect, keysFor, OSES };
