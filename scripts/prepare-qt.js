#!/usr/bin/env node
'use strict';
// Stages the mixer page for the Qt Android project (android-qt/): the same offline page the Java APK carries (Tailwind CSS built from the page,
// Font Awesome, fonts) goes to android-qt/assets/www, and android-qt/version.txt gets the version of package.json.
//   node scripts/prepare-qt.js          then build android-qt/ with Qt Creator or CMake (see android-qt/README.md)
// The Qt project itself is NOT built by this repository's tests: it needs the Qt 6.5+ SDK and the Android NDK.
const fs = require('node:fs');
const path = require('node:path');
const { prepareWeb, findTools, which } = require('./build-apk');

const ROOT = path.resolve(__dirname, '..'), QT = path.join(ROOT, 'android-qt');

async function main() {
  const version = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8')).version;
  const www = path.join(QT, 'assets', 'www');
  fs.rmSync(path.join(QT, 'assets'), { recursive: true, force: true });
  fs.mkdirSync(www, { recursive: true });
  const t = { ...findTools().tools, npm: which('npm') };
  const r = await prepareWeb(www, t);
  fs.writeFileSync(path.join(QT, 'version.txt'), version + '\n');
  console.log(`staged ${path.relative(ROOT, www)} (version ${version})` + (r && r.warning ? ' - ' + r.warning : ''));
}

if (require.main === module) main().catch(e => { console.error('Error: ' + e.message); process.exit(1); });
module.exports = { main };
