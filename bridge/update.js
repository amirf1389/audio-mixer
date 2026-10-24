'use strict';
// OTA updates: the app asks an update server for a SIGNED manifest (releases/update.json by default), compares versions, and downloads the
// installer for this computer into the downloads folder, verifying its SHA-256 from the manifest. download() never runs an installer by
// itself; install() (below) does, but only the signed Windows setup .exe, and only when this computer's own system could actually be
// detected as Windows (never guessed from a user agent or a parameter).
//   BRIDGE_UPDATE_URL     manifest location (default: the project's releases/update.json on GitHub)
//   BRIDGE_UPDATE_HOSTS   extra comma-separated hosts that may serve the manifest / files
// The manifest is signed with the vendor key (scripts/make-update.js); the app verifies it with the embedded public key, so a hijacked
// server or a tampered file cannot offer a fake update.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');
const lic = require('./license');
const { fetchChecked } = require('./security');
const osdetect = require('./osdetect');

const DEFAULT_URL = 'https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json';
const BASE_HOSTS = ['raw.githubusercontent.com', 'github.com', 'objects.githubusercontent.com', 'release-assets.githubusercontent.com', 'githubusercontent.com'];
const MAX_BYTES = 400 * 1024 * 1024;

const allowedHosts = () => BASE_HOSTS.concat(String(process.env.BRIDGE_UPDATE_HOSTS || '').split(',').map(s => s.trim()).filter(Boolean));
const hostOk = (u, hosts = allowedHosts()) => { try { const x = new URL(u); return x.protocol === 'https:' && hosts.some(h => x.hostname === h || x.hostname.endsWith('.' + h)); } catch (_) { return false; } };

// "1.10.2" > "1.9.0"; prerelease suffixes are ignored
function cmpVersion(a, b) {
  const p = v => String(v).split(/[-+]/)[0].split('.').map(n => parseInt(n, 10) || 0);
  const x = p(a), y = p(b);
  for (let i = 0; i < Math.max(x.length, y.length, 3); i++) { const d = (x[i] || 0) - (y[i] || 0); if (d) return d < 0 ? -1 : 1; }
  return 0;
}

// Envelope = { payload: "<manifest JSON text>", signature: base64url }; the signature covers the exact payload text.
function verifyManifest(envelope, jwk = lic.PUBLIC_JWK) {
  try {
    if (!envelope || typeof envelope.payload !== 'string' || typeof envelope.signature !== 'string') throw new Error('shape');
    const ok = crypto.verify('sha256', Buffer.from(envelope.payload), { key: lic.publicKeyOf ? lic.publicKeyOf(jwk) : crypto.createPublicKey({ key: jwk, format: 'jwk' }), dsaEncoding: 'ieee-p1363' }, lic.unb64u(envelope.signature));
    if (!ok) throw new Error('signature');
    const m = JSON.parse(envelope.payload);
    if (m.product !== 'audio-mixer' || typeof m.version !== 'string' || !m.files || typeof m.files !== 'object') throw new Error('content');
    return { ok: true, manifest: m };
  } catch (_) { return { ok: false, error: 'The update information is not signed by the Audio Mixer publisher and was ignored.' }; }
}
function signManifest(manifest, privateKey) {
  const payload = JSON.stringify(manifest);
  return { payload, signature: lic.b64u(crypto.sign('sha256', Buffer.from(payload), { key: privateKey, dsaEncoding: 'ieee-p1363' })) };
}

// Which file of the manifest fits this computer, and how to install it.
function platformFile(manifest, { platform = process.platform, arch = process.arch, debian = platform === 'linux' && fs.existsSync('/etc/debian_version') } = {}) {
  // the system is detected as the OTA server does it (bridge/osdetect.js): Windows setup .exe / .msi, Debian .deb or the Linux archive, macOS .dmg or archive, Android .apk
  const det = osdetect.detectOs({ node: { platform, arch, debian } });
  const p = osdetect.pick(manifest, det);
  if (!p) return null;
  const how = det.os === 'linux' ? (p.key === 'linux-deb' ? 'Install it with: sudo apt install ./<file>' : 'Unpack the archive and run install.sh.') : p.how;
  return { key: p.key, name: p.name, url: p.url, size: p.size, sha256: p.sha256, how };
}

async function fetchJson(fetchImpl, url) {
  const r = await fetchChecked(fetchImpl, url, { headers: { 'User-Agent': 'audio-mixer-update', Accept: 'application/json', 'Cache-Control': 'no-cache' } }, u => hostOk(u));
  if (!r.ok) throw Object.assign(new Error('update server answered ' + r.status), { status: 502 });
  return r.json();
}

// Is a newer version published?
async function check({ current, fetchImpl = globalThis.fetch, url = process.env.BRIDGE_UPDATE_URL || DEFAULT_URL, platform, arch, jwk = lic.PUBLIC_JWK } = {}) {
  if (!hostOk(url)) throw Object.assign(new Error('the update server host is not allowed'), { status: 400 });
  const env = await fetchJson(fetchImpl, url);
  const v = verifyManifest(env, jwk);
  if (!v.ok) throw Object.assign(new Error(v.error), { status: 502 });
  const m = v.manifest, det = osdetect.detectOs({ node: { platform: platform || process.platform, arch: arch || process.arch } });
  // Android: Google Play delivers updates to the store app; an app that downloads and installs its own APK is what Play's policies and Play Protect block, so none is offered
  const playManaged = det.os === 'android', file = playManaged ? null : platformFile(m, { platform, arch });
  return {
    ok: true, current, os: det.os, arch: det.arch, others: osdetect.others(m, [file && file.key, (osdetect.driverFor(m, det) || {}).key, playManaged && 'android-apk']).map(o => ({ key: o.key, label: o.label, name: o.name, url: o.url, size: o.size })), latest: m.version, released: m.released || null, channel: m.channel || 'stable', notes: Array.isArray(m.notes) ? m.notes.slice(0, 12).map(String) : [],
    updateAvailable: cmpVersion(m.version, current) > 0, signed: true, playManaged,
    file: file ? { key: file.key, name: file.name, url: file.url, size: file.size || null, sha256: file.sha256, how: file.how } : null, source: url,
    driver: (d => d ? { key: d.key, label: d.label, name: d.name, url: d.url, size: d.size || null, sha256: d.sha256, how: d.how } : null)(osdetect.driverFor(m, det)),
  };
}

const updateDir = () => path.join(process.env.BRIDGE_DOWNLOAD_DIR || path.join(os.homedir(), 'AudioMixerDrivers'), 'updates');

// Saves the installer for this computer and checks its SHA-256 against the signed manifest. Nothing is run.
async function download({ current, fetchImpl = globalThis.fetch, url, dir = updateDir(), platform, arch, jwk = lic.PUBLIC_JWK } = {}) {
  const info = await check({ current, fetchImpl, url, platform, arch, jwk });
  if (!info.updateAvailable) throw Object.assign(new Error('Audio Mixer is up to date (' + current + ')'), { status: 409 });
  if (info.playManaged) throw Object.assign(new Error('on Android, updates come from Google Play'), { status: 409 });
  if (!info.file) throw Object.assign(new Error('no update file for this system'), { status: 404 });
  const env = await fetchJson(fetchImpl, url || process.env.BRIDGE_UPDATE_URL || DEFAULT_URL);
  const entry = verifyManifest(env, jwk).manifest.files[info.file.key];
  if (!hostOk(entry.url)) throw Object.assign(new Error('the update file host is not allowed'), { status: 502 });
  const name = path.basename(String(entry.name || '')).replace(/[^\w.\- ]/g, '_');
  if (!name || name.startsWith('.')) throw Object.assign(new Error('unsafe file name'), { status: 502 });
  const res = await fetchChecked(fetchImpl, entry.url, { headers: { 'User-Agent': 'audio-mixer-update' } }, u => hostOk(u));
  if (!res.ok || !res.body) throw Object.assign(new Error('download failed (' + res.status + ')'), { status: 502 });
  if (res.url && !hostOk(res.url)) throw Object.assign(new Error('download redirected to an untrusted host'), { status: 502 });
  if (Number(res.headers.get('content-length') || 0) > MAX_BYTES) throw Object.assign(new Error('file too large'), { status: 502 });
  fs.mkdirSync(dir, { recursive: true });
  const target = path.join(dir, name), part = target + '.part', hash = crypto.createHash('sha256');
  let bytes = 0;
  const out = fs.createWriteStream(part, { flags: 'w', mode: 0o600 });
  try {
    for await (const chunk of res.body) {
      bytes += chunk.length;
      if (bytes > MAX_BYTES) throw new Error('file too large');
      hash.update(chunk);
      if (!out.write(chunk)) await new Promise(r => out.once('drain', r));
    }
    await new Promise((r, e) => out.end(err => (err ? e(err) : r())));
  } catch (e) { out.destroy(); try { fs.unlinkSync(part); } catch (_) { /* gone */ } throw Object.assign(new Error(e.message || 'download failed'), { status: 502 }); }
  const sha256 = hash.digest('hex');
  if (sha256 !== String(entry.sha256).toLowerCase()) { try { fs.unlinkSync(part); } catch (_) { /* gone */ } throw Object.assign(new Error('checksum mismatch: the download was discarded'), { status: 502 }); }
  fs.renameSync(part, target);
  return { ok: true, version: info.latest, file: target, bytes, sha256, verified: true, how: info.file.how, note: 'Saved and verified. It is not started automatically.' };
}

// download(), then run the installer: Windows only, and only the signed setup .exe (build-exe.js), which understands /quiet and performs its
// own MSI major upgrade - the same ProductCode/UpgradeCode machinery every release already uses, so it reinstalls into the existing
// Program Files folder, keeps the Start Menu / taskbar icon and file-association registrations, and carries the asio-driver, bridge and
// client/scripts folders forward exactly as a fresh install would. Nothing here chooses or moves any of that.
// Anything install() cannot safely auto-run - another OS, a .msi-only release, or a system this computer's own platform could not identify
// as Windows (process.platform, never a browser user agent) - is left exactly as download() already leaves it: saved, verified, not run.
async function install({ current, fetchImpl = globalThis.fetch, url, dir, platform = process.platform, arch, jwk = lic.PUBLIC_JWK, spawnImpl = spawn } = {}) {
  const dl = await download({ current, fetchImpl, url, dir, platform, arch, jwk });
  if (platform !== 'win32' || !/\.exe$/i.test(dl.file)) return { ...dl, installing: false, note: dl.note + ' Run it yourself: ' + dl.how };
  const child = spawnImpl(dl.file, ['/quiet'], { detached: true, stdio: 'ignore', windowsHide: true });
  child.unref();
  return { ...dl, installing: true, note: `Installing v${dl.version} now (silently, no prompts). Audio Mixer will restart once it finishes.` };
}

// Automatic updates (Windows setup .exe only): off until the user switches it on (the page's switch, or BRIDGE_AUTO_UPDATE=1). When on, the server checks a little after
// it starts and then every 6 hours; a newer signed version is downloaded, checked against the manifest's SHA-256 and installed silently by install() (the signed setup's own
// MSI major upgrade). A version is tried once per run (a failing installer is not retried in a loop), nothing runs without the PRO / STUDIO plan, and every other system
// (Android included: Google Play updates the store app) is left alone.
const autoFile = () => process.env.BRIDGE_AUTO_UPDATE_FILE || path.join(os.homedir(), '.audio-mixer', 'auto-update.json');
function autoEnabled() {
  if (process.env.BRIDGE_AUTO_UPDATE === '1') return true;
  if (process.env.BRIDGE_AUTO_UPDATE === '0') return false;
  try { return JSON.parse(fs.readFileSync(autoFile(), 'utf8')).enabled === true; } catch (_) { return false; }
}
function setAutoEnabled(enabled) {
  fs.mkdirSync(path.dirname(autoFile()), { recursive: true });
  fs.writeFileSync(autoFile(), JSON.stringify({ enabled: enabled === true }) + '\n', { mode: 0o600 });
  return autoEnabled();
}
function createAutoUpdater({ current, allowed = () => true, platform = process.platform, firstAfterMs = 120000, everyMs = 6 * 3600 * 1000, checkFn = check, installFn = install, log = () => {} } = {}) {
  let timer = null, busy = false, last = { at: null, result: 'not run yet' };
  const tried = new Set();
  async function tick() {
    if (busy) return last;
    busy = true;
    try {
      if (platform !== 'win32') last = { at: Date.now(), result: 'automatic updates are for Windows only' };
      else if (!autoEnabled()) last = { at: Date.now(), result: 'off' };
      else if (!allowed()) last = { at: Date.now(), result: 'needs the PRO or STUDIO plan' };
      else {
        const info = await checkFn({ current, platform });
        if (!info.updateAvailable) last = { at: Date.now(), result: 'up to date' };
        else if (!info.file || !/\.exe$/i.test(info.file.name)) last = { at: Date.now(), result: 'v' + info.latest + ' has no setup .exe for this system; install it by hand' };
        else if (tried.has(info.latest)) last = { at: Date.now(), result: 'v' + info.latest + ' was already tried in this run' };
        else {
          tried.add(info.latest);
          const r = await installFn({ current, platform });
          last = { at: Date.now(), result: r.installing ? 'installing v' + r.version : 'v' + r.version + ' saved, not run' };
        }
      }
    } catch (e) { last = { at: Date.now(), result: 'failed: ' + e.message }; }
    finally { busy = false; }
    log('auto update: ' + last.result);
    return last;
  }
  return {
    tick, status: () => ({ enabled: autoEnabled(), supported: platform === 'win32', last }),
    start() { if (timer) return; timer = setTimeout(function again() { tick().then(() => { timer = setTimeout(again, everyMs); if (timer.unref) timer.unref(); }); }, firstAfterMs); if (timer.unref) timer.unref(); },
    stop() { if (timer) clearTimeout(timer); timer = null; },
  };
}

module.exports = { check, download, install, autoEnabled, setAutoEnabled, createAutoUpdater, autoFile, verifyManifest, signManifest, verifyManifest, signManifest, platformFile, cmpVersion, hostOk, updateDir, DEFAULT_URL };
