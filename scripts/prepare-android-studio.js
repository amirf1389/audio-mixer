#!/usr/bin/env node
'use strict';
// Prepares the generated inputs of the Android Studio project (platforms/android/studio): the same page assets, launcher icons and manifest that
// scripts/build-apk.js packs, written to platforms/android/studio/app/src/generated/ (git-ignored). Android Studio runs this before every build
// (the app module's preBuild depends on it); it can also be run by hand:
//   node scripts/prepare-android-studio.js
// The Java sources and res/ are not copied: the Gradle project reads platforms/android/src and platforms/android/res directly.
const fs = require('node:fs');
const path = require('node:path');
const { prepareWeb, png, iconPixel, which } = require('./build-apk');

const ROOT = path.resolve(__dirname, '..');
const OUT = path.join(ROOT, 'platforms', 'android', 'studio', 'app', 'src', 'generated');

// AGP 8 takes the package name from the Gradle namespace and refuses a package="" attribute in the manifest, so the copy drops it.
function stripPackage(xml) { return xml.replace(/(<manifest\b[^>]*?)\s+package="[^"]*"/, '$1'); }

async function prepare({ out = OUT } = {}) {
  fs.rmSync(out, { recursive: true, force: true });
  const www = path.join(out, 'assets', 'www');
  fs.mkdirSync(www, { recursive: true });
  const web = await prepareWeb(www, { npm: which('npm') });
  for (const [dir, size] of [['mipmap-mdpi', 48], ['mipmap-hdpi', 72], ['mipmap-xhdpi', 96], ['mipmap-xxhdpi', 144], ['mipmap-xxxhdpi', 192]]) {
    fs.mkdirSync(path.join(out, 'res', dir), { recursive: true });
    fs.writeFileSync(path.join(out, 'res', dir, 'ic_launcher.png'), png(size, iconPixel));
  }
  const manifest = fs.readFileSync(path.join(ROOT, 'platforms', 'android', 'AndroidManifest.xml'), 'utf8');
  fs.writeFileSync(path.join(out, 'AndroidManifest.xml'), stripPackage(manifest));
  return { out, web };
}

module.exports = { prepare, stripPackage, OUT };

if (require.main === module) {
  prepare().then(r => console.log('Android Studio inputs ready in ' + path.relative(ROOT, r.out)), e => { console.error(e.message); process.exit(1); });
}
