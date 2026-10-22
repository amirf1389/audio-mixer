#!/usr/bin/env node
'use strict';
// Builds the Android App Bundle (.aab) that Google Play Console takes, from the same sources as the .apk (scripts/build-apk.js):
//   node scripts/build-aab.js [--out platforms/android/releases]   -> AudioMixer-<version>-android.aab
// Needs what build-apk.js needs plus aapt2 (Android SDK build-tools or Debian / Ubuntu `aapt`) and bundletool (BUNDLETOOL=<jar>, or the pinned 1.18.1 release is
// downloaded once into dist/cache). The bundle is signed with the upload key (the same keystore as the apk: ANDROID_KEYSTORE / dist/cache/signing); Play re-signs
// the installable APKs with its own app-signing key once the app is enrolled in Play App Signing. See platforms/android/PLAY-PROTECT.md.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const apk = require('./build-apk');

const ROOT = path.resolve(__dirname, '..');
const BUNDLETOOL = { url: 'https://github.com/google/bundletool/releases/download/1.18.1/bundletool-all-1.18.1.jar', name: 'bundletool-all-1.18.1.jar' };

function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, ...opts });
  if (r.error) throw new Error(`cannot run ${cmd}: ${r.error.message}`);
  if (r.status !== 0) throw new Error(`${path.basename(cmd)} failed: ${(r.stderr || r.stdout || '').replace(/^Picked up JAVA_TOOL_OPTIONS.*\n/m, '').trim().slice(0, 1500)}`);
  return r.stdout;
}
function bundletool() {
  if (process.env.BUNDLETOOL) return process.env.BUNDLETOOL;
  const f = path.join(ROOT, 'dist', 'cache', BUNDLETOOL.name);
  if (!fs.existsSync(f)) { fs.mkdirSync(path.dirname(f), { recursive: true }); run('curl', ['-fsSL', '-o', f, BUNDLETOOL.url]); }
  return f;
}
function keystore() {
  if (process.env.ANDROID_KEYSTORE) return { file: process.env.ANDROID_KEYSTORE, pass: process.env.ANDROID_KEYSTORE_PASS || '', alias: process.env.ANDROID_KEY_ALIAS || 'audiomixer' };
  const dir = path.join(ROOT, 'dist', 'cache', 'signing');
  return { file: path.join(dir, 'audiomixer-android.jks'), pass: fs.readFileSync(path.join(dir, 'audiomixer-android.pass'), 'utf8').trim(), alias: 'audiomixer' };
}

async function build({ out = path.join(ROOT, 'platforms', 'android', 'releases') } = {}) {
  const pkg = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
  const { tools: t, missing } = apk.findTools();
  const aapt2 = apk.which('aapt2'), jarsigner = apk.which('jarsigner'), java = apk.which('java'), unzip = apk.which('unzip');
  const lack = missing.filter(m => m !== 'aapt' && m !== 'zipalign' && m !== 'apksigner').concat([['aapt2', aapt2], ['jarsigner', jarsigner], ['java', java], ['unzip', unzip]].filter(x => !x[1]).map(x => x[0]));
  if (lack.length) throw new Error('missing tools: ' + lack.join(', '));
  const audit = require('./apk-audit').auditManifest(fs.readFileSync(path.join(ROOT, 'platforms', 'android', 'AndroidManifest.xml'), 'utf8'));
  if (audit.length) throw new Error('the manifest fails the APK audit (scripts/apk-audit.js): ' + audit.join('; '));
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'aab-')), www = path.join(work, 'assets', 'www');
  fs.mkdirSync(www, { recursive: true });
  await apk.prepareWeb(www, t);
  for (const [dir, size] of [['mipmap-mdpi', 48], ['mipmap-hdpi', 72], ['mipmap-xhdpi', 96], ['mipmap-xxhdpi', 144], ['mipmap-xxxhdpi', 192]]) {
    fs.mkdirSync(path.join(work, 'res', dir), { recursive: true }); fs.writeFileSync(path.join(work, 'res', dir, 'ic_launcher.png'), apk.png(size, apk.iconPixel));
  }
  fs.mkdirSync(path.join(work, 'res', 'xml'), { recursive: true });
  fs.copyFileSync(path.join(ROOT, 'platforms', 'android', 'res', 'xml', 'network_security_config.xml'), path.join(work, 'res', 'xml', 'network_security_config.xml'));
  // Java -> classes -> dex, exactly as build-apk.js does it
  const cls = path.join(work, 'classes'), stubs = path.join(work, 'stubs'); fs.mkdirSync(cls); fs.mkdirSync(stubs);
  run(t.javac, ['--release', '8', '-Xlint:-options', '-cp', t.jar, '-d', stubs, ...apk.listFiles(path.join(ROOT, 'platforms', 'android', 'stubs'), '.java')]);
  run(t.javac, ['--release', '8', '-Xlint:-options', '-cp', [t.jar, stubs].join(path.delimiter), '-d', cls, ...apk.listFiles(path.join(ROOT, 'platforms', 'android', 'src'), '.java')]);
  const classFiles = apk.listFiles(cls, '.class');
  if (t.d8) run(t.d8, ['--min-api', '24', '--lib', t.jar, '--classpath', stubs, '--output', work, ...classFiles]);
  else run(t.dx, ['--dex', '--output=' + path.join(work, 'classes.dex'), cls]);
  // resources in protobuf format (what bundletool wants) -> base module layout: manifest/, dex/, res/, assets/, resources.pb
  run(aapt2, ['compile', '--dir', path.join(work, 'res'), '-o', path.join(work, 'res.zip')]);
  const proto = path.join(work, 'proto.apk');
  run(aapt2, ['link', '--proto-format', '-o', proto, '-I', t.frameworkRes, '--manifest', path.join(ROOT, 'platforms', 'android', 'AndroidManifest.xml'), '-A', path.join(work, 'assets'),
    '--min-sdk-version', '24', '--target-sdk-version', '35', '--version-code', String(apk.versionCode(pkg.version)), '--version-name', pkg.version, '--auto-add-overlay', path.join(work, 'res.zip')]);
  const base = path.join(work, 'base'); fs.mkdirSync(base);
  run(unzip, ['-q', proto, '-d', base]);
  fs.mkdirSync(path.join(base, 'manifest')); fs.renameSync(path.join(base, 'AndroidManifest.xml'), path.join(base, 'manifest', 'AndroidManifest.xml'));
  fs.mkdirSync(path.join(base, 'dex')); fs.copyFileSync(path.join(work, 'classes.dex'), path.join(base, 'dex', 'classes.dex'));
  const baseZip = path.join(work, 'base.zip');
  run('jar', ['--create', '--file', baseZip, '--no-manifest', '-C', base, '.']);
  fs.mkdirSync(path.resolve(out), { recursive: true });
  const dest = path.join(path.resolve(out), `AudioMixer-${pkg.version}-android.aab`);
  fs.rmSync(dest, { force: true });
  run(java, ['-jar', bundletool(), 'build-bundle', '--modules=' + baseZip, '--output=' + dest]);
  const ks = keystore();
  run(jarsigner, ['-keystore', ks.file, '-storepass', ks.pass, '-keypass', ks.pass, '-sigalg', 'SHA256withRSA', '-digestalg', 'SHA-256', dest, ks.alias]);
  const verify = run(jarsigner, ['-verify', dest]);
  run(java, ['-jar', bundletool(), 'validate', '--bundle=' + dest]);
  fs.rmSync(work, { recursive: true, force: true });
  return { dest, size: fs.statSync(dest).size, sha256: crypto.createHash('sha256').update(fs.readFileSync(dest)).digest('hex'), verified: /jar verified/.test(verify), versionCode: apk.versionCode(pkg.version) };
}

if (require.main === module) {
  const a = process.argv.slice(2), oi = a.indexOf('--out');
  build({ out: oi >= 0 ? a[oi + 1] : undefined }).then(r => {
    console.log(`AAB: ${r.dest}  (${(r.size / 1048576).toFixed(1)} MB, versionCode ${r.versionCode}, signature ${r.verified ? 'verified' : 'NOT verified'})\n     SHA-256 ${r.sha256}`);
    console.log('Upload it in Play Console > Release > Testing > Internal testing. Target SDK 35; opt in to Play App Signing and use this key as the upload key.');
  }).catch(e => { console.error('AAB build failed: ' + e.message); process.exit(1); });
}
module.exports = { build };
