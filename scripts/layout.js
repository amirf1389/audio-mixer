'use strict';
// Where things live in the repository. One folder per operating system, under platforms/, holds everything of that system: its source, its drivers
// and its release files; code shared by all systems stays in bridge/, client/, ota-server/, npm-installer/, engine/, scripts/, deploy/ and drivers/common/
// (the web app is index.html + boot.html at the root, because GitHub Pages serves it from there).
//
//   platforms/android/   app (src, kotlin, stubs), qt/ (Qt project), hal/ (audio HAL), releases/*.apk
//   platforms/ios/       Xcode project template, releases/*.tar.gz
//   platforms/windows/   installer/ (setup, launcher), native/ (device helper), plugin-host/, asio-driver/, start-*.bat, Audio Mixer.vbs, releases/*.exe *.msi *.zip *.cer
//   platforms/macos/     native/ (Core Audio helper), coreaudio-driver/, releases/*.dmg *.zip *.tar.gz
//   platforms/linux/     alsa-plugin/, kernel-module/, start-pc-mode.sh, ensure-node.sh, releases/*.deb *.tar.gz
//   releases/update.json   the signed update manifest that installed apps fetch (its file URLs point into the releases/<os>/ folders)
//
// The PACKAGES built from this repository keep their own layout (native/win, native/host, native/mac, start-pc-mode.bat ... next to bridge/): `repoPath`
// maps a path inside a package to the file in the repository.
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.resolve(__dirname, '..');
const OSES = ['android', 'ios', 'windows', 'macos', 'linux'];
const PLATFORMS = 'platforms';

// package path -> repository path (exact files first, then folder prefixes)
const FILES = {
  'start-pc-mode.bat': 'platforms/windows/start-pc-mode.bat', 'start-local-server.bat': 'platforms/windows/start-local-server.bat', 'Audio Mixer.vbs': 'platforms/windows/Audio Mixer.vbs',
  'start-pc-mode.sh': 'platforms/linux/start-pc-mode.sh', 'ensure-node.sh': 'platforms/linux/ensure-node.sh',
};
const PREFIXES = [['native/win/', 'platforms/windows/native/'], ['native/host/', 'platforms/windows/plugin-host/'], ['native/mac/', 'platforms/macos/native/']];
function repoPath(rel) {
  if (FILES[rel]) return FILES[rel];
  for (const [from, to] of PREFIXES) if (rel.startsWith(from)) return to + rel.slice(from.length);
  return rel;
}

// the operating system a manifest key (win-x64-exe, linux-deb, macos-dmg, android-apk, ios-project ...) belongs to
function osOfKey(key) {
  if (/-native$/.test(key)) return 'native';                 // the native programs (releases/native): the self-update of audiomixer-native.exe
  return /^win/.test(key) ? 'windows' : /^linux/.test(key) ? 'linux' : /^macos/.test(key) ? 'macos' : key === 'android-apk' ? 'android' : key === 'ios-project' ? 'ios' : null;
}
const osDir = os => path.join(ROOT, PLATFORMS, os);
const releasesDir = os => path.join(ROOT, 'releases', os);
// the repository folder (relative, forward slashes) that holds the files of a manifest key
const releasesRel = key => { const os = osOfKey(key); return os ? `releases/${os}` : 'releases'; };
// a release file by name: in the folder of its system, else anywhere in the <os>/releases folders, including releases/native/ (the self-update
// programs: osOfKey() already returns 'native' for their manifest keys, but 'native' is not a platforms/<os> system, so it stays out of OSES itself)
function findRelease(name, os) {
  for (const o of os ? [os] : [...OSES, 'native']) { const f = path.join(releasesDir(o), name); if (fs.existsSync(f)) return f; }
  return null;
}

module.exports = { ROOT, OSES, PLATFORMS, repoPath, osOfKey, osDir, releasesDir, releasesRel, findRelease };
