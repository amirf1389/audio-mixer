#!/usr/bin/env node
'use strict';
// Builds the iOS app: Swift sources (ios/), the mixer page bundled offline (same bundle as the Android app), the app icon and an XcodeGen spec.
//   node scripts/build-ios.js [--out ios/releases] [--ipa]
// Everywhere:  writes ios/releases/AudioMixer-<version>-ios-xcode-project.tar.gz  (open it on a Mac: brew install xcodegen; xcodegen generate; open AudioMixer.xcodeproj)
// On a Mac with Xcode + xcodegen, --ipa also archives the app without code signing and packs ios/releases/AudioMixer-<version>-ios-unsigned.ipa
//   (an unsigned .ipa installs only after it is re-signed with your Apple ID or developer certificate, e.g. with AltStore / Sideloadly / Xcode).
// An iOS app can only be compiled and signed with Xcode on macOS: this script cannot make a signed .ipa on Linux or Windows.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const apk = require('./build-apk');

const ROOT = path.resolve(__dirname, '..');
const IOS = path.join(ROOT, 'ios');

// App Store icons must not have transparency: the rounded tile of the Android icon becomes a full square
function iconPixelOpaque(u, v) { const p = apk.iconPixel(u, v); return p[3] === 0 ? [15, 18, 26, 255] : p; }

function projectYml(template, version) { return template.replace(/@VERSION@/g, version).replace(/@BUILD@/g, String(apk.versionCode(version))); }

function stage(dir, version, web) {
  const www = path.join(dir, 'www'); fs.rmSync(www, { recursive: true, force: true }); fs.mkdirSync(www, { recursive: true });
  return apk.prepareWeb(www, { npm: apk.which('npm') }).then(w => {
    fs.writeFileSync(path.join(dir, 'project.yml'), projectYml(fs.readFileSync(path.join(IOS, 'project.yml.template'), 'utf8'), version));
    const icons = path.join(dir, 'Assets.xcassets', 'AppIcon.appiconset'); fs.mkdirSync(icons, { recursive: true });
    fs.writeFileSync(path.join(dir, 'Assets.xcassets', 'Contents.json'), JSON.stringify({ info: { author: 'xcode', version: 1 } }, null, 2));
    fs.writeFileSync(path.join(icons, 'icon-1024.png'), apk.png(1024, iconPixelOpaque));
    fs.writeFileSync(path.join(icons, 'Contents.json'), JSON.stringify({ images: [{ filename: 'icon-1024.png', idiom: 'universal', platform: 'ios', size: '1024x1024' }], info: { author: 'xcode', version: 1 } }, null, 2));
    return w;
  });
}

async function build({ out = path.join(ROOT, 'ios', 'releases'), ipa = false } = {}) {
  const pkg = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'ios-')), proj = path.join(work, 'AudioMixer-ios');
  fs.cpSync(IOS, proj, { recursive: true, filter: s => !/[\\/]www($|[\\/])/.test(s) && !s.endsWith('project.yml') });
  const web = await stage(proj, pkg.version);
  fs.rmSync(path.join(proj, 'project.yml.template'), { force: true });
  fs.writeFileSync(path.join(proj, 'README.txt'), `Audio Mixer ${pkg.version} for iOS (Xcode project)\n\n1. On a Mac with Xcode 14 or newer: brew install xcodegen\n2. In this folder: xcodegen generate && open AudioMixer.xcodeproj\n3. Xcode > AudioMixer target > Signing & Capabilities: choose your team, then Run on your iPhone / iPad (iOS 15+).\n\nThe app shows the mixer page bundled in www/ (served on 127.0.0.1 inside the app). PC-mode features (ASIO / WASAPI, plugins) need the PC.\n`);
  fs.mkdirSync(path.resolve(out), { recursive: true });
  const dest = path.join(path.resolve(out), `AudioMixer-${pkg.version}-ios-xcode-project.tar.gz`);
  const t = spawnSync('tar', ['-czf', dest, '-C', work, 'AudioMixer-ios'], { encoding: 'utf8' });
  if (t.status !== 0) throw new Error('tar failed: ' + (t.stderr || t.error));
  fs.writeFileSync(dest + '.sha256', `${crypto.createHash('sha256').update(fs.readFileSync(dest)).digest('hex')}  ${path.basename(dest)}\n`);
  const res = { project: dest, version: pkg.version, fonts: web.fonts, ipa: null };
  if (ipa) {
    if (process.platform !== 'darwin' || !apk.which('xcodebuild') || !apk.which('xcodegen')) throw new Error('--ipa needs a Mac with Xcode and xcodegen (brew install xcodegen)');
    const run = (c, a, cwd) => { const r = spawnSync(c, a, { cwd, encoding: 'utf8', maxBuffer: 64 << 20 }); if (r.status !== 0) throw new Error(`${c} failed: ${(r.stderr || r.stdout).slice(-1500)}`); };
    run('xcodegen', ['generate'], proj);
    const archive = path.join(work, 'AudioMixer.xcarchive');
    run('xcodebuild', ['-project', 'AudioMixer.xcodeproj', '-scheme', 'AudioMixer', '-configuration', 'Release', '-sdk', 'iphoneos', '-archivePath', archive, 'archive', 'CODE_SIGNING_ALLOWED=NO', 'CODE_SIGNING_REQUIRED=NO', 'CODE_SIGN_IDENTITY='], proj);
    const payload = path.join(work, 'Payload'); fs.mkdirSync(payload);
    fs.cpSync(path.join(archive, 'Products', 'Applications', 'AudioMixer.app'), path.join(payload, 'AudioMixer.app'), { recursive: true });
    const file = path.join(path.resolve(out), `AudioMixer-${pkg.version}-ios-unsigned.ipa`);
    run('zip', ['-qry', file, 'Payload'], work);
    fs.writeFileSync(file + '.sha256', `${crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')}  ${path.basename(file)}\n`);
    res.ipa = file;
  }
  fs.rmSync(work, { recursive: true, force: true });
  return res;
}

if (require.main === module) {
  const a = process.argv.slice(2), oi = a.indexOf('--out');
  build({ out: oi >= 0 ? a[oi + 1] : undefined, ipa: a.includes('--ipa') }).then(r => {
    console.log('iOS Xcode project: ' + r.project);
    if (r.ipa) console.log('Unsigned IPA: ' + r.ipa + ' (re-sign it to install)');
    else console.log('No .ipa: iOS apps are compiled and signed with Xcode on a Mac (see README.txt in the project, or run this script with --ipa on a Mac).');
    if (!r.fonts) console.log('Warning: the web fonts could not be bundled; the app uses system fonts.');
  }).catch(e => { console.error('iOS build failed: ' + e.message); process.exit(1); });
}
module.exports = { build, projectYml, iconPixelOpaque };
