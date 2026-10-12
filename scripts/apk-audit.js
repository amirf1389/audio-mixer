#!/usr/bin/env node
'use strict';
// Checks the Android manifest (and optionally a built APK) for everything that makes security scanners and Google Play Protect suspicious of an app, so a change that adds one fails
// the build instead of reaching a phone:
//   - only the permissions the app explains to the person (INTERNET, RECORD_AUDIO, WAKE_LOCK, FOREGROUND_SERVICE; the same table is AppInfo.why in the app)
//   - nothing from the list scanners treat as spyware / dropper material (SMS, contacts, call log, accessibility, overlay, package installer, all-apps query, boot start, device admin ...)
//   - not debuggable, not testOnly, no backup of app data, every component with an intent filter says exported="true" explicitly
//   - the notification listener (the "now playing" access) is shipped switched off: android:enabled="false"
//   - the APK holds no native library, no second dex and no file that is not a resource, the page or classes.dex
//   node scripts/apk-audit.js [path/to/app.apk]
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.resolve(__dirname, '..');
const ALLOWED = ['android.permission.INTERNET', 'android.permission.RECORD_AUDIO', 'android.permission.WAKE_LOCK', 'android.permission.FOREGROUND_SERVICE'];
const SUSPICIOUS = /READ_SMS|SEND_SMS|RECEIVE_SMS|READ_CONTACTS|READ_CALL_LOG|WRITE_CALL_LOG|PROCESS_OUTGOING_CALLS|READ_PHONE_STATE|READ_PHONE_NUMBERS|ACCESS_(FINE|COARSE|BACKGROUND)_LOCATION|CAMERA|SYSTEM_ALERT_WINDOW|REQUEST_INSTALL_PACKAGES|INSTALL_PACKAGES|DELETE_PACKAGES|QUERY_ALL_PACKAGES|RECEIVE_BOOT_COMPLETED|BIND_ACCESSIBILITY_SERVICE|BIND_DEVICE_ADMIN|MANAGE_EXTERNAL_STORAGE|READ_EXTERNAL_STORAGE|WRITE_EXTERNAL_STORAGE|USE_FULL_SCREEN_INTENT|DISABLE_KEYGUARD|GET_ACCOUNTS|PACKAGE_USAGE_STATS|READ_LOGS/;

function auditManifest(xml) {
  const problems = [];
  const perms = [...xml.matchAll(/<uses-permission[^>]*android:name="([^"]+)"/g)].map(m => m[1]);
  for (const p of perms) { if (SUSPICIOUS.test(p)) problems.push('suspicious permission: ' + p); else if (!ALLOWED.includes(p)) problems.push('permission not in the explained list: ' + p); }
  if (/android:debuggable="true"/.test(xml)) problems.push('debuggable');
  if (/android:testOnly="true"/.test(xml)) problems.push('testOnly');
  if (!/android:allowBackup="false"/.test(xml)) problems.push('allowBackup must be false');
  if (/<receiver/.test(xml)) problems.push('a broadcast receiver is declared: none is needed (boot / package receivers look like droppers)');
  if (/<provider/.test(xml)) problems.push('a content provider is declared: none is needed');
  for (const m of xml.matchAll(/<(activity|service)\b([^>]*?)(\/>|>[\s\S]*?<\/\1>)/g)) {
    const attrs = m[2], hasFilter = /<intent-filter/.test(m[3]), name = (/android:name="([^"]+)"/.exec(attrs) || [])[1];
    if (hasFilter && !/android:exported="/.test(attrs)) problems.push(name + ' has an intent filter but no explicit android:exported');
    if (/android:name="\.NowPlayingListener"/.test(attrs) && !/android:enabled="false"/.test(attrs)) problems.push('the notification listener must ship switched off (android:enabled="false")');
  }
  if (!/<uses-feature android:name="android.hardware.microphone" android:required="false"\/>/.test(xml)) problems.push('the microphone feature must be optional');
  return { permissions: perms, problems };
}

// `entries`: names inside the APK (unzip -Z1 / zipinfo)
function auditApk(entries) {
  const problems = [];
  for (const e of entries) {
    if (/\.(so|jar|apk|dex)$/.test(e) && e !== 'classes.dex') problems.push('unexpected file in the APK: ' + e);
    if (/^lib\//.test(e)) problems.push('native library in the APK: ' + e);
    if (!/^(AndroidManifest\.xml|classes\.dex|resources\.arsc|assets\/|res\/|META-INF\/)/.test(e) && !e.endsWith('/')) problems.push('unexpected file in the APK: ' + e);
  }
  if (!entries.includes('classes.dex')) problems.push('no classes.dex');
  return problems;
}

function main(argv) {
  const m = auditManifest(fs.readFileSync(path.join(ROOT, 'android', 'AndroidManifest.xml'), 'utf8'));
  let problems = m.problems.slice();
  if (argv[0]) {
    const r = require('node:child_process').spawnSync('unzip', ['-Z1', argv[0]], { encoding: 'utf8' });
    if (r.status !== 0) { console.error('cannot list ' + argv[0]); return 2; }
    problems = problems.concat(auditApk(r.stdout.split('\n').filter(Boolean)));
  }
  for (const p of problems) console.error('APK AUDIT: ' + p);
  console.log(problems.length ? problems.length + ' problem(s)' : 'APK audit passed: ' + m.permissions.map(p => p.replace('android.permission.', '')).join(', '));
  return problems.length ? 1 : 0;
}

if (require.main === module) process.exit(main(process.argv.slice(2)));
module.exports = { auditManifest, auditApk, ALLOWED, SUSPICIOUS };
