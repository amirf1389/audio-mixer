'use strict';
// Which operating system / app is asking, and which release file of an update manifest belongs to it.
// Used by the OTA server (GET /latest, GET /api/latest) and by bridge/update.js. The page (index.html, "rt-ota") carries a copy of detectOs for the apps;
// bridge/test.js runs both on the same table of user agents.
//   os:   windows | macos | linux | android | ios
//   arch: x64 | x86 | arm64 | null     distro (Linux only): debian (Debian, Ubuntu, Mint ...: the .deb fits) | other (Fedora, Arch, SUSE ...: it does not) | null (not known)
// Order of trust: an explicit choice (?os=android) > Client Hints (Sec-CH-UA-Platform) > the user agent > nothing (os: null).
const FILES = {
  'win-x64-exe': 'Windows 64-bit setup (.exe)', 'win-x64-msi': 'Windows 64-bit package (.msi)', 'win-x86-msi': 'Windows 32-bit package (.msi)',
  'linux-deb': 'Debian / Ubuntu package (.deb)', 'linux-tar': 'Linux archive (.tar.gz)', 'macos-dmg': 'macOS disk image (.dmg)', 'macos': 'macOS archive (.tar.gz)',
  'android-apk': 'Android app (.apk)', 'ios-project': 'iOS Xcode project (.tar.gz)',
};
const OSES = ['windows', 'macos', 'linux', 'android', 'ios'];
const NODE_OS = { win32: 'windows', darwin: 'macos', linux: 'linux', android: 'android' };
const NODE_ARCH = { x64: 'x64', ia32: 'x86', arm64: 'arm64' };

const clientHint = v => String(v == null ? '' : v).replace(/^"|"$/g, '').toLowerCase();
function fromHint(p) {
  p = clientHint(p);
  return p === 'windows' ? 'windows' : p === 'macos' ? 'macos' : p === 'android' ? 'android' : p === 'ios' ? 'ios' : (p === 'linux' || p === 'chrome os' || p === 'chromeos') ? (p === 'linux' ? 'linux' : null) : null;
}
function fromUserAgent(ua) {
  ua = String(ua || '');
  if (/Android/i.test(ua)) return { os: 'android', arch: /arm64|aarch64|armv8/i.test(ua) ? 'arm64' : null, distro: null };
  if (/iPhone|iPad|iPod/i.test(ua)) return { os: 'ios', arch: 'arm64', distro: null };
  if (/Windows NT|Win64|WOW64|Windows/i.test(ua)) return { os: 'windows', arch: /Win64|x64|WOW64|ARM64|amd64/i.test(ua) ? 'x64' : 'x86', distro: null };
  if (/CrOS/.test(ua)) return { os: null, arch: null, distro: null };                                                     // Chrome OS: no package
  if (/Macintosh|Mac OS X|Darwin/i.test(ua)) return { os: 'macos', arch: /arm64|aarch64/i.test(ua) ? 'arm64' : null, distro: null };
  if (/Linux|X11|Ubuntu|Debian|Fedora/i.test(ua)) return { os: 'linux', arch: /aarch64|arm64/i.test(ua) ? 'arm64' : /x86_64|amd64|x64/i.test(ua) ? 'x64' : null, distro: /Ubuntu|Debian|Mint|Pop!_OS|Kali/i.test(ua) ? 'debian' : /Fedora|Red Hat|CentOS|SUSE|Arch|Gentoo|Manjaro|NixOS/i.test(ua) ? 'other' : null };
  return { os: null, arch: null, distro: null };
}

// detectOs({ os, arch, distro, userAgent, platformHint, archHint, node: { platform, arch, debian } }) -> { os, arch, distro, source }
function detectOs(o = {}) {
  const choice = String(o.os || '').toLowerCase();
  const alias = { win: 'windows', win32: 'windows', mac: 'macos', darwin: 'macos', osx: 'macos', apk: 'android', iphone: 'ios', ipad: 'ios' }[choice] || choice;
  const arch = a => ({ x64: 'x64', amd64: 'x64', x86_64: 'x64', x86: 'x86', ia32: 'x86', i386: 'x86', arm64: 'arm64', aarch64: 'arm64' }[String(a || '').toLowerCase()] || null);
  const distro = d => (/^(debian|ubuntu|deb|mint)$/i.test(String(d || '')) ? 'debian' : /^(other|fedora|rpm|arch|suse|redhat|rhel|centos)$/i.test(String(d || '')) ? 'other' : null);
  if (OSES.includes(alias)) return { os: alias, arch: arch(o.arch), distro: distro(o.distro), source: 'param' };
  if (o.node && NODE_OS[o.node.platform]) return { os: NODE_OS[o.node.platform], arch: NODE_ARCH[o.node.arch] || null, distro: o.node.debian ? 'debian' : 'other', source: 'system' };
  const hint = fromHint(o.platformHint), ua = fromUserAgent(o.userAgent);
  if (hint) {                                                                                                              // Client Hints are exact; the user agent adds the details
    const same = ua.os === hint;
    const h = clientHint(o.archHint);
    return { os: hint, arch: arch(o.arch) || (same && ua.arch) || (h === 'arm' ? 'arm64' : h === 'x86' ? 'x64' : null), distro: same ? ua.distro : null, source: 'client-hints' };
  }
  if (ua.os) return { ...ua, arch: arch(o.arch) || ua.arch, distro: distro(o.distro) || ua.distro, source: 'user-agent' };
  return { os: null, arch: null, distro: null, source: 'none' };
}

// the manifest keys that fit, best first
function keysFor(det) {
  if (!det || !det.os) return [];
  switch (det.os) {
    case 'windows': return det.arch === 'x86' ? ['win-x86-msi'] : ['win-x64-exe', 'win-x64-msi'];
    case 'macos': return ['macos-dmg', 'macos'];
    case 'linux': return det.distro === 'debian' ? ['linux-deb', 'linux-tar'] : det.distro === 'other' ? ['linux-tar'] : ['linux-tar', 'linux-deb'];            // an unknown distribution may still take the .deb
    case 'android': return ['android-apk'];
    case 'ios': return ['ios-project'];
    default: return [];
  }
}

const HOW = {
  windows: 'Close Audio Mixer, then double-click the downloaded file. Windows asks for administrator permission, and the installer replaces the old version.',
  macos: 'Open the disk image (or unpack the archive) and double-click install.command.',
  linux: 'Debian / Ubuntu: sudo apt install ./<file>. Other systems: unpack the archive and run install.sh.',
  android: 'Open the downloaded .apk on the phone (allow installing apps from this source when Android asks). It updates the installed app when it is signed with the same key.',
  ios: 'iOS apps are built with Xcode on a Mac: unpack the archive, run xcodegen generate, open the project, choose your team and run it on the device.',
};

// the file of a manifest for this system: { key, label, how, ...entry } or null
function pick(manifest, det) {
  const files = (manifest && manifest.files) || {};
  for (const key of keysFor(det)) if (files[key]) return { key, label: FILES[key] || key, how: HOW[det.os] || '', ...files[key] };
  return null;
}
// every other file of the manifest, for "other systems"
function others(manifest, exceptKey) {
  return Object.entries((manifest && manifest.files) || {}).filter(([k]) => k !== exceptKey).map(([key, f]) => ({ key, label: FILES[key] || key, name: f.name, url: f.url, size: f.size || null, sha256: f.sha256 }));
}
// request headers (lower-case names, as Node gives them) -> detection
const fromHeaders = (h, q = {}) => detectOs({ os: q.os, arch: q.arch, distro: q.distro, userAgent: h['user-agent'], platformHint: h['sec-ch-ua-platform'], archHint: h['sec-ch-ua-arch'] });

module.exports = { detectOs, keysFor, pick, others, fromHeaders, FILES, HOW, OSES };
