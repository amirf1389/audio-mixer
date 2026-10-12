#!/usr/bin/env node
'use strict';
// Fills in the false-positive report from android/PLAY-PROTECT.md with this build's real numbers, so reporting a Play Protect / antivirus warning to Google (or an
// antivirus vendor) is copy-paste instead of hand-editing a template. This does not change what the app does or try to affect a scanner's verdict; it only prepares the
// facts (version, file hash, signing certificate) a human sends to Google through the official appeal form.
//   node scripts/play-protect-appeal.js <path/to/app.apk> [--out path.txt]
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');

function certSha256(apk) {
  const apksigner = process.env.APKSIGNER || 'apksigner';
  const r = spawnSync(apksigner, ['verify', '--print-certs', apk], { encoding: 'utf8' });
  const m = r.status === 0 && /certificate SHA-256 digest:\s*([0-9a-f]{64})/i.exec(r.stdout);
  return m ? m[1].toUpperCase().match(/.{2}/g).join(':') : null;
}

function build(apkPath) {
  const pkg = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
  const apk = fs.readFileSync(apkPath);
  const published = (() => { try { return fs.readFileSync(path.join(ROOT, 'android', 'signing-fingerprint.txt'), 'utf8').trim(); } catch (e) { return null; } })();
  const signed = certSha256(apkPath);
  if (published && signed && published !== signed) throw new Error('this APK is not signed with the published key (android/signing-fingerprint.txt): ' + signed + ' != ' + published);
  const fp = signed || published;
  if (!fp) throw new Error('no signing certificate fingerprint: run `apksigner verify --print-certs` on the APK, or check android/signing-fingerprint.txt');
  return [
    `App: Audio Mixer (com.audiomixer.app), version ${pkg.version}.`,
    `APK file: ${path.basename(apkPath)}, SHA-256 ${crypto.createHash('sha256').update(apk).digest('hex')}.`,
    `Signing certificate SHA-256: ${fp}.`,
    'Open-source: https://github.com/amirf1389/audio-mixer. It is a virtual mixing console: a WebView shows a bundled web page; a small engine inside the app serves the page on',
    '127.0.0.1 and reads / writes the phone\'s audio interfaces (microphone, USB audio, Bluetooth) for the mixer. Permissions: INTERNET, RECORD_AUDIO, WAKE_LOCK,',
    'FOREGROUND_SERVICE (see APP SAFETY in the app and scripts/apk-audit.js). No SMS, contacts, location, camera, storage, overlay, accessibility, installer or boot access; no',
    'receivers, no providers, no native code, no code loaded at run time (classes.dex holds no dynamic class loader).',
    'Please remove the Play Protect warning for this file.',
  ].join('\n');
}

function main(argv) {
  const apkPath = argv.find(a => !a.startsWith('--'));
  if (!apkPath) { console.error('usage: node scripts/play-protect-appeal.js <path/to/app.apk> [--out path.txt]'); return 2; }
  const text = build(path.resolve(apkPath));
  const outIdx = argv.indexOf('--out');
  if (outIdx !== -1 && argv[outIdx + 1]) { fs.writeFileSync(argv[outIdx + 1], text + '\n'); console.log('wrote ' + argv[outIdx + 1]); }
  else console.log(text);
  console.log('\nSubmit this at https://support.google.com/googleplay/android-developer/contact/protect_appeals (Play Console help > Appeal Play Protect warning),');
  console.log('or to the antivirus vendor\'s own false-positive form. See android/PLAY-PROTECT.md for the full steps.');
  return 0;
}

if (require.main === module) process.exit(main(process.argv.slice(2)));
module.exports = { build, certSha256 };
