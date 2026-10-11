'use strict';
// The signed release manifest (releases/update.json of the project, or the OTA server): fetched, verified with the pinned Audio Mixer public key, and the chosen file downloaded
// and checked against the SHA-256 of the manifest. A manifest or file that does not verify is refused; nothing is run before that.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const http = require('node:http');
const https = require('node:https');
const JWK = require('./public-key.json');

const DEFAULT_URL = 'https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json';
const HOSTS = ['raw.githubusercontent.com', 'github.com', 'objects.githubusercontent.com', 'release-assets.githubusercontent.com', 'githubusercontent.com'];
const MAX_BYTES = 400 * 1024 * 1024;

const unb64u = s => Buffer.from(String(s).replace(/-/g, '+').replace(/_/g, '/'), 'base64');
function verifyManifest(envelope, jwk = JWK) {
  try {
    if (!envelope || typeof envelope.payload !== 'string' || typeof envelope.signature !== 'string') throw new Error('shape');
    const key = crypto.createPublicKey({ key: jwk, format: 'jwk' });
    if (!crypto.verify('sha256', Buffer.from(envelope.payload), { key, dsaEncoding: 'ieee-p1363' }, unb64u(envelope.signature))) throw new Error('signature');
    const m = JSON.parse(envelope.payload);
    if (m.product !== 'audio-mixer' || typeof m.version !== 'string' || !m.files || typeof m.files !== 'object') throw new Error('content');
    return { ok: true, manifest: m };
  } catch (_) { return { ok: false, error: 'the release information is not signed by the Audio Mixer publisher' }; }
}

// one GET with redirects (https only, or http to localhost with allowHttp); the hosts a redirect may go to are limited to `hosts`
function get(url, { hosts, allowHttp = false, redirects = 5, maxBytes = MAX_BYTES, onData } = {}) {
  return new Promise((resolve, reject) => {
    let u; try { u = new URL(url); } catch (_) { return reject(new Error('bad url: ' + url)); }
    const local = /^(localhost|127\.0\.0\.1|\[::1\])$/.test(u.hostname);
    if (u.protocol !== 'https:' && !(allowHttp && u.protocol === 'http:' && local)) return reject(new Error('only https is accepted: ' + url));
    if (!local && !hosts.some(h => u.hostname === h || u.hostname.endsWith('.' + h))) return reject(new Error('host not allowed: ' + u.hostname + ' (add it with --host)'));
    const req = (u.protocol === 'https:' ? https : http).get(u, { headers: { 'User-Agent': 'audio-mixer-installer', Accept: '*/*' } }, res => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        res.resume();
        if (redirects <= 0) return reject(new Error('too many redirects'));
        return resolve(get(new URL(res.headers.location, u).href, { hosts, allowHttp, redirects: redirects - 1, maxBytes, onData }));
      }
      if (res.statusCode !== 200) { res.resume(); return reject(new Error(url + ' answered ' + res.statusCode)); }
      let n = 0; const chunks = [];
      res.on('data', c => { n += c.length; if (n > maxBytes) { req.destroy(); reject(new Error('the file is larger than ' + maxBytes + ' bytes')); } else if (onData) onData(c); else chunks.push(c); });
      res.on('end', () => resolve(onData ? n : Buffer.concat(chunks)));
      res.on('error', reject);
    });
    req.on('error', reject); req.setTimeout(30000, () => req.destroy(new Error('timeout')));
  });
}

async function fetchManifest(url, opts) {
  const v = verifyManifest(JSON.parse((await get(url, { ...opts, maxBytes: 1024 * 1024 })).toString('utf8')), opts && opts.jwk);   // opts.jwk: tests only
  if (!v.ok) throw new Error(v.error);
  return v.manifest;
}

// the first key of `keys` that the manifest lists
const pickFile = (manifest, keys) => { for (const k of keys) if (manifest.files && manifest.files[k]) return { key: k, ...manifest.files[k] }; return null; };

// downloads `file` ({ name, url, size, sha256 }) into a new folder; deletes it and throws when size or SHA-256 differ
async function download(file, dir, opts) {
  const safe = path.basename(String(file.name || '')).replace(/[^A-Za-z0-9._ \-]/g, '_');
  if (!safe || !/^[a-f0-9]{64}$/i.test(String(file.sha256 || ''))) throw new Error('the manifest entry has no usable name or SHA-256');
  fs.mkdirSync(dir, { recursive: true });
  const to = path.join(dir, safe), out = fs.createWriteStream(to), h = crypto.createHash('sha256');
  let n = 0;
  try {
    await get(file.url, { ...opts, onData: c => { n += c.length; h.update(c); out.write(c); } });
    await new Promise((res, rej) => out.end(err => (err ? rej(err) : res())));
  } catch (e) { out.destroy(); fs.rmSync(to, { force: true }); throw e; }
  if ((file.size != null && n !== file.size) || h.digest('hex') !== String(file.sha256).toLowerCase()) { fs.rmSync(to, { force: true }); throw new Error(safe + ' does not match the signed SHA-256 / size: the download is damaged or was changed; nothing was run'); }
  return to;
}

// a releases folder of a checkout (<os>/releases): the newest file of this system, without a manifest (offline); still needs --yes to run like a download
function fromFolder(dir, keys) {
  const NAMES = { 'win-x64-exe': /^Audio Mixer-(\d+(?:\.\d+){1,3})\.exe$/, 'win-x64-msi': /^AudioMixer-(\d+(?:\.\d+){1,3})-x64\.msi$/, 'win-x86-msi': /^AudioMixer-(\d+(?:\.\d+){1,3})-x86\.msi$/,
    'linux-deb': /^audio-mixer_(\d+(?:\.\d+){1,3})_all\.deb$/, 'linux-tar': /^AudioMixer-(\d+(?:\.\d+){1,3})-linux\.tar\.gz$/, 'macos': /^AudioMixer-(\d+(?:\.\d+){1,3})-macos\.tar\.gz$/,
    'android-apk': /^AudioMixer-(\d+(?:\.\d+){1,3})-android\.apk$/, 'ios-project': /^AudioMixer-(\d+(?:\.\d+){1,3})-ios-xcode-project\.tar\.gz$/ };
  const cmp = (a, b) => { const x = a.split('.').map(Number), y = b.split('.').map(Number); for (let i = 0; i < 4; i++) { const d = (x[i] || 0) - (y[i] || 0); if (d) return d; } return 0; };
  const names = fs.readdirSync(dir);
  for (const k of keys) {
    const found = names.map(n => { const m = NAMES[k] && NAMES[k].exec(n); return m ? { name: n, version: m[1] } : null; }).filter(Boolean).sort((a, b) => cmp(b.version, a.version))[0];
    if (found) return { key: k, name: found.name, version: found.version, path: path.join(dir, found.name) };
  }
  return null;
}

module.exports = { DEFAULT_URL, HOSTS, JWK, verifyManifest, get, fetchManifest, pickFile, download, fromFolder, tmpDir: () => fs.mkdtempSync(path.join(os.tmpdir(), 'audio-mixer-install-')) };
