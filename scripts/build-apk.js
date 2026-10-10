#!/usr/bin/env node
'use strict';
// Builds the Android app (.apk): the mixer page in a full-screen WebView, bundled offline (Tailwind CSS, Font Awesome and the fonts are
// packed into the app instead of loaded from CDNs). Web Audio, the microphone, scenes and the license / plans work as in the browser;
// PC-mode features (ASIO / WASAPI interfaces, plugins, OTA) need the PC.
//   node scripts/build-apk.js [--out releases]      -> AudioMixer-<version>-android.apk (+ .sha256)
// Tools (any Android SDK, or the Debian / Ubuntu packages aapt apksigner zipalign dalvik-exchange libandroid-23-java):
//   javac (JDK 8+), aapt, zipalign, apksigner, d8 or dalvik-exchange (dx), android.jar (ANDROID_JAR, ANDROID_HOME/platforms, or /usr/lib/android-sdk)
// Needs internet once for the cache in dist/cache/android: npm packages tailwindcss 3 and @fortawesome/fontawesome-free (version pinned) and the Google fonts.
// Signing: ANDROID_KEYSTORE / ANDROID_KEYSTORE_PASS / ANDROID_KEY_ALIAS for a real key, otherwise a self-generated key kept in dist/cache/signing
// (the same key must sign every update, or Android refuses to install over the older version).
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const zlib = require('node:zlib');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const CACHE = path.join(ROOT, 'dist', 'cache', 'android');
const TAILWIND = '3.4.17', FA = '6.4.0';
const CHROME_UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36';
const FONT_CSS = 'https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600;800&family=Inter:wght@400;600;800;900&family=Orbitron:wght@600;800;900&display=swap';

// 1.9.0 -> 10900 (Android needs an integer that grows with every release)
function versionCode(v) { const p = String(v).split('.').map(n => parseInt(n, 10) || 0); return p[0] * 10000 + (p[1] || 0) * 100 + (p[2] || 0); }

// The app's copy of the page: CDN links become bundled files. The page in the repository is not touched.
function transformHtml(html) {
  let out = String(html);
  out = out.replace(/<script src="https:\/\/cdn\.tailwindcss\.com"><\/script>/, '<link rel="stylesheet" href="tw.css">');
  out = out.replace(/href="https:\/\/cdnjs\.cloudflare\.com\/ajax\/libs\/font-awesome\/[\d.]+\/css\/all\.min\.css"/, 'href="fa/css/all.min.css"');
  out = out.replace(/@import url\('https:\/\/fonts\.googleapis\.com\/css2\?[^']*'\);/, "@import url('fonts/fonts.css');");
  return out;
}

// ── tiny PNG writer for the launcher icon (no image library needed) ──
function crc32(buf) { let c, crc = ~0; for (let n = 0; n < buf.length; n++) { c = (crc ^ buf[n]) & 255; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crc = (crc >>> 8) ^ c; } return ~crc >>> 0; }
function png(size, pixel) {
  const raw = Buffer.alloc(size * (size * 4 + 1));
  for (let y = 0; y < size; y++) { raw[y * (size * 4 + 1)] = 0; for (let x = 0; x < size; x++) { const [r, g, b, a] = pixel(x / size, y / size); raw.set([r, g, b, a], y * (size * 4 + 1) + 1 + x * 4); } }
  const chunk = (type, data) => { const t = Buffer.from(type), len = Buffer.alloc(4), crc = Buffer.alloc(4); len.writeUInt32BE(data.length); crc.writeUInt32BE(crc32(Buffer.concat([t, data]))); return Buffer.concat([len, t, data, crc]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(size, 0); ihdr.writeUInt32BE(size, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}
// dark rounded tile with three amber faders: the mixer
function iconPixel(u, v) {
  const r = 0.18, dx = Math.max(Math.abs(u - 0.5) - (0.5 - r), 0), dy = Math.max(Math.abs(v - 0.5) - (0.5 - r), 0);
  if (Math.hypot(dx, dy) > r) return [0, 0, 0, 0];
  for (const [cx, cap] of [[0.3, 0.62], [0.5, 0.36], [0.7, 0.5]]) {
    if (Math.abs(u - cx) < 0.018 && v > 0.2 && v < 0.8) return [70, 78, 92, 255];
    if (Math.abs(u - cx) < 0.075 && Math.abs(v - cap) < 0.04) return [251, 191, 36, 255];
  }
  return [15, 18, 26, 255];
}

function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, ...opts });
  if (r.error) throw new Error(`cannot run ${cmd}: ${r.error.message}`);
  if (r.status !== 0) throw new Error(`${path.basename(cmd)} failed: ${(r.stderr || r.stdout || '').replace(/^Picked up JAVA_TOOL_OPTIONS.*\n/m, '').trim().slice(0, 1500)}`);
  return r.stdout;
}
function which(name) {
  const dirs = (process.env.PATH || '').split(path.delimiter);
  for (const d of dirs) { const p = path.join(d, name); if (fs.existsSync(p)) return p; }
  return null;
}
function findTools(env = process.env) {
  const sdk = env.ANDROID_HOME || env.ANDROID_SDK_ROOT || '/usr/lib/android-sdk';
  const bt = (() => { try { const d = path.join(sdk, 'build-tools'); return fs.readdirSync(d).sort().reverse().map(v => path.join(d, v)); } catch (_) { return []; } })();
  const pick = n => [...bt.map(d => path.join(d, n)), which(n)].find(p => p && fs.existsSync(p)) || null;
  const jar = env.ANDROID_JAR || (() => { try { const d = path.join(sdk, 'platforms'); return fs.readdirSync(d).sort((a, b) => parseInt(b.split('-')[1]) - parseInt(a.split('-')[1])).map(v => path.join(d, v, 'android.jar')).find(fs.existsSync); } catch (_) { return null; } })() || '/usr/share/java/com.android.android-23.jar';
  const t = { aapt: pick('aapt'), zipalign: pick('zipalign'), apksigner: pick('apksigner'), d8: pick('d8'), dx: which('dalvik-exchange') || pick('dx'), javac: which('javac'), keytool: which('keytool'), npm: which('npm'), jar: fs.existsSync(jar) ? jar : null };
  const missing = ['aapt', 'zipalign', 'apksigner', 'javac', 'keytool', 'jar'].filter(k => !t[k]).concat(t.d8 || t.dx ? [] : ['d8 / dalvik-exchange']);
  return { tools: t, missing };
}

async function download(url, file, headers = {}) {
  const r = await fetch(url, { headers }); if (!r.ok) throw new Error(url + ' -> ' + r.status);
  fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, Buffer.from(await r.arrayBuffer()));
}

// Offline copies of what the page loads from CDNs: Tailwind CSS built from the page itself, Font Awesome, the Google fonts (latin).
async function prepareWeb(www, t) {
  fs.mkdirSync(CACHE, { recursive: true });
  const nm = path.join(CACHE, 'node_modules');
  if (!fs.existsSync(path.join(nm, 'tailwindcss', 'package.json')) || !fs.existsSync(path.join(nm, '@fortawesome', 'fontawesome-free', 'package.json'))) {
    if (!t.npm) throw new Error('npm is needed once to fetch tailwindcss and font-awesome');
    fs.writeFileSync(path.join(CACHE, 'package.json'), JSON.stringify({ private: true, name: 'apk-cache' }));
    run(t.npm, ['install', '--no-audit', '--no-fund', '--silent', `tailwindcss@${TAILWIND}`, `@fortawesome/fontawesome-free@${FA}`], { cwd: CACHE });
  }
  const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
  fs.writeFileSync(path.join(www, 'index.html'), transformHtml(html));
  // Tailwind: the Play CDN scans the page at runtime; here the same scan happens once, over the page and its scripts
  fs.writeFileSync(path.join(CACHE, 'in.css'), '@tailwind base;\n@tailwind components;\n@tailwind utilities;\n');
  fs.writeFileSync(path.join(CACHE, 'tailwind.config.js'), `module.exports = { content: [${JSON.stringify(path.join(ROOT, 'index.html'))}] };\n`);
  run(process.execPath, [path.join(nm, 'tailwindcss', 'lib', 'cli.js'), '-i', path.join(CACHE, 'in.css'), '-c', path.join(CACHE, 'tailwind.config.js'), '-o', path.join(www, 'tw.css'), '--minify'], { cwd: CACHE });
  const fa = path.join(nm, '@fortawesome', 'fontawesome-free');
  fs.mkdirSync(path.join(www, 'fa', 'css'), { recursive: true }); fs.mkdirSync(path.join(www, 'fa', 'webfonts'), { recursive: true });
  fs.copyFileSync(path.join(fa, 'css', 'all.min.css'), path.join(www, 'fa', 'css', 'all.min.css'));
  for (const f of fs.readdirSync(path.join(fa, 'webfonts'))) if (/\.woff2$/.test(f)) fs.copyFileSync(path.join(fa, 'webfonts', f), path.join(www, 'fa', 'webfonts', f));
  // fonts (best effort: without them the app falls back to the system fonts)
  fs.mkdirSync(path.join(www, 'fonts'), { recursive: true });
  const fontCache = path.join(CACHE, 'fonts');
  try {
    let css = '';
    const cached = path.join(fontCache, 'fonts.css');
    if (fs.existsSync(cached)) css = fs.readFileSync(cached, 'utf8');
    else {
      const r = await fetch(FONT_CSS, { headers: { 'User-Agent': CHROME_UA } }); if (!r.ok) throw new Error('fonts.googleapis.com -> ' + r.status);
      const blocks = (await r.text()).split(/(?=\/\*\s*[\w-]+\s*\*\/)/).filter(b => /\/\*\s*latin\s*\*\//.test(b));   // latin subset only
      for (const b of blocks) {
        const m = /url\((https:\/\/fonts\.gstatic\.com\/[^)]+)\)/.exec(b); if (!m) continue;
        const name = crypto.createHash('sha1').update(m[1]).digest('hex').slice(0, 16) + '.woff2';
        await download(m[1], path.join(fontCache, name)); css += b.replace(m[1], name);
      }
      fs.writeFileSync(cached, css);
    }
    fs.writeFileSync(path.join(www, 'fonts', 'fonts.css'), css);
    for (const f of fs.readdirSync(fontCache)) if (f.endsWith('.woff2')) fs.copyFileSync(path.join(fontCache, f), path.join(www, 'fonts', f));
    return { fonts: true };
  } catch (e) { fs.writeFileSync(path.join(www, 'fonts', 'fonts.css'), '/* fonts could not be downloaded at build time: system fonts are used */\n'); return { fonts: false, warning: e.message }; }
}

function keystore(env = process.env, t) {
  if (env.ANDROID_KEYSTORE) return { file: env.ANDROID_KEYSTORE, pass: env.ANDROID_KEYSTORE_PASS || '', alias: env.ANDROID_KEY_ALIAS || 'audiomixer', real: true };
  const dir = path.join(ROOT, 'dist', 'cache', 'signing'), file = path.join(dir, 'audiomixer-android.jks'), passFile = path.join(dir, 'audiomixer-android.pass');
  fs.mkdirSync(dir, { recursive: true });
  if (!fs.existsSync(file) || !fs.existsSync(passFile)) {
    const pass = crypto.randomBytes(18).toString('base64url');
    fs.rmSync(file, { force: true });
    run(t.keytool, ['-genkeypair', '-keystore', file, '-storetype', 'PKCS12', '-alias', 'audiomixer', '-keyalg', 'RSA', '-keysize', '3072', '-validity', '10950', '-dname', 'CN=Audio Mixer, O=Audio Mixer', '-storepass', pass, '-keypass', pass]);
    fs.writeFileSync(passFile, pass, { mode: 0o600 });
  }
  return { file, pass: fs.readFileSync(passFile, 'utf8').trim(), alias: 'audiomixer', real: false };
}

async function build({ out = path.join(ROOT, 'releases') } = {}) {
  const pkg = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
  const { tools: t, missing } = findTools();
  if (missing.length) throw new Error('missing tools: ' + missing.join(', ') + '. Install the Android SDK build-tools, or on Debian / Ubuntu: apt install aapt apksigner zipalign dalvik-exchange libandroid-23-java default-jdk');
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'apk-')), www = path.join(work, 'assets', 'www');
  fs.mkdirSync(www, { recursive: true });
  const web = await prepareWeb(www, t);
  // launcher icon
  for (const [dir, size] of [['mipmap-mdpi', 48], ['mipmap-hdpi', 72], ['mipmap-xhdpi', 96], ['mipmap-xxhdpi', 144], ['mipmap-xxxhdpi', 192]]) {
    fs.mkdirSync(path.join(work, 'res', dir), { recursive: true }); fs.writeFileSync(path.join(work, 'res', dir, 'ic_launcher.png'), png(size, iconPixel));
  }
  // Java -> classes -> dex
  const cls = path.join(work, 'classes'); fs.mkdirSync(cls);
  run(t.javac, ['--release', '8', '-Xlint:-options', '-cp', t.jar, '-d', cls, path.join(ROOT, 'android', 'src', 'com', 'audiomixer', 'app', 'MainActivity.java')]);
  if (t.d8) run(t.d8, ['--min-api', '24', '--lib', t.jar, '--output', work, ...fs.readdirSync(path.join(cls, 'com', 'audiomixer', 'app')).map(f => path.join(cls, 'com', 'audiomixer', 'app', f))]);
  else run(t.dx, ['--dex', '--output=' + path.join(work, 'classes.dex'), cls]);
  // resources + manifest -> apk (resources.arsc stored uncompressed, as Android 11+ requires), then the dex, alignment, signature
  const unsigned = path.join(work, 'unsigned.apk'), aligned = path.join(work, 'aligned.apk');
  run(t.aapt, ['package', '-f', '-M', path.join(ROOT, 'android', 'AndroidManifest.xml'), '-S', path.join(work, 'res'), '-A', path.join(work, 'assets'), '-I', t.jar,
    '-F', unsigned, '--min-sdk-version', '24', '--target-sdk-version', '33', '--version-code', String(versionCode(pkg.version)), '--version-name', pkg.version, '-0', 'arsc']);
  run(t.aapt, ['add', unsigned, 'classes.dex'], { cwd: work });
  run(t.zipalign, ['-p', '-f', '4', unsigned, aligned]);
  const ks = keystore(process.env, t);
  fs.mkdirSync(path.resolve(out), { recursive: true });
  const dest = path.join(path.resolve(out), `AudioMixer-${pkg.version}-android.apk`);
  run(t.apksigner, ['sign', '--ks', ks.file, '--ks-key-alias', ks.alias, '--ks-pass', 'pass:' + ks.pass, '--key-pass', 'pass:' + ks.pass, '--min-sdk-version', '24', '--v4-signing-enabled', 'false', '--out', dest, aligned]);
  const verify = run(t.apksigner, ['verify', '--verbose', '--min-sdk-version', '24', dest]);
  const sum = crypto.createHash('sha256').update(fs.readFileSync(dest)).digest('hex');
  fs.writeFileSync(dest + '.sha256', `${sum}  ${path.basename(dest)}\n`);
  fs.rmSync(work, { recursive: true, force: true });
  return { dest, sha256: sum, size: fs.statSync(dest).size, version: pkg.version, versionCode: versionCode(pkg.version), selfSigned: !ks.real, fonts: web.fonts, warning: web.warning, verify: verify.trim() };
}

if (require.main === module) {
  const a = process.argv.slice(2), oi = a.indexOf('--out');
  build({ out: oi >= 0 ? a[oi + 1] : undefined }).then(r => {
    console.log(`APK: ${r.dest}  (${(r.size / 1048576).toFixed(1)} MB, versionCode ${r.versionCode})\n     SHA-256 ${r.sha256}`);
    if (r.selfSigned) console.log('Signed with a self-generated key (dist/cache/signing). Set ANDROID_KEYSTORE for your own; keep the key: updates must be signed with the same one.');
    if (!r.fonts) console.log('Warning: the web fonts could not be bundled (' + r.warning + '); the app uses system fonts.');
  }).catch(e => { console.error('APK build failed: ' + e.message); process.exit(1); });
}
module.exports = { build, versionCode, transformHtml, png, iconPixel, findTools, prepareWeb, which };
