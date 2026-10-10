'use strict';
// OTA updates: the app asks an update server for a SIGNED manifest (releases/update.json by default), compares versions, and downloads the
// installer for this computer into the downloads folder, verifying its SHA-256 from the manifest. It never runs an installer by itself.
//   BRIDGE_UPDATE_URL     manifest location (default: the project's releases/update.json on GitHub)
//   BRIDGE_UPDATE_HOSTS   extra comma-separated hosts that may serve the manifest / files
// The manifest is signed with the vendor key (scripts/make-update.js); the app verifies it with the embedded public key, so a hijacked
// server or a tampered file cannot offer a fake update.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const lic = require('./license');
const { fetchChecked } = require('./security');

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
function platformFile(manifest, { platform = process.platform, arch = process.arch, debian = fs.existsSync('/etc/debian_version') } = {}) {
  const f = manifest.files || {};
  let key = null, how = '';
  if (platform === 'win32') {
    key = arch === 'ia32' ? 'win-x86-msi' : 'win-x64-exe';
    how = 'Close Audio Mixer, then double-click the downloaded file. Windows asks for administrator permission, and the installer replaces the old version.';
  } else if (platform === 'linux') {
    key = debian ? 'linux-deb' : null;
    how = debian ? 'Install it with: sudo apt install ./<file>' : 'No package for this Linux distribution: download the new project files instead.';
  } else if (platform === 'darwin') {
    key = 'macos';
    how = 'Unpack the archive and double-click install.command.';
  }
  const e = key && f[key];
  return e ? { key, ...e, how } : null;
}

async function fetchJson(fetchImpl, url) {
  const r = await fetchChecked(fetchImpl, url, { headers: { 'User-Agent': 'audio-mixer-update', Accept: 'application/json', 'Cache-Control': 'no-cache' } }, u => hostOk(u));
  if (!r.ok) throw Object.assign(new Error('update server answered ' + r.status), { status: 502 });
  return r.json();
}

// Is a newer version published?
async function check({ current, fetchImpl = globalThis.fetch, url = process.env.BRIDGE_UPDATE_URL || DEFAULT_URL, platform, arch } = {}) {
  if (!hostOk(url)) throw Object.assign(new Error('the update server host is not allowed'), { status: 400 });
  const env = await fetchJson(fetchImpl, url);
  const v = verifyManifest(env);
  if (!v.ok) throw Object.assign(new Error(v.error), { status: 502 });
  const m = v.manifest, file = platformFile(m, { platform, arch });
  return {
    ok: true, current, latest: m.version, released: m.released || null, channel: m.channel || 'stable', notes: Array.isArray(m.notes) ? m.notes.slice(0, 12).map(String) : [],
    updateAvailable: cmpVersion(m.version, current) > 0, signed: true,
    file: file ? { key: file.key, name: file.name, size: file.size || null, sha256: file.sha256, how: file.how } : null, source: url,
  };
}

const updateDir = () => path.join(process.env.BRIDGE_DOWNLOAD_DIR || path.join(os.homedir(), 'AudioMixerDrivers'), 'updates');

// Saves the installer for this computer and checks its SHA-256 against the signed manifest. Nothing is run.
async function download({ current, fetchImpl = globalThis.fetch, url, dir = updateDir(), platform, arch } = {}) {
  const info = await check({ current, fetchImpl, url, platform, arch });
  if (!info.updateAvailable) throw Object.assign(new Error('Audio Mixer is up to date (' + current + ')'), { status: 409 });
  if (!info.file) throw Object.assign(new Error('no update file for this system'), { status: 404 });
  const env = await fetchJson(fetchImpl, url || process.env.BRIDGE_UPDATE_URL || DEFAULT_URL);
  const entry = verifyManifest(env).manifest.files[info.file.key];
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

module.exports = { check, download, verifyManifest, signManifest, platformFile, cmpVersion, hostOk, updateDir, DEFAULT_URL };
