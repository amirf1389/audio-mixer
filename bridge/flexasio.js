'use strict';
// FlexASIO (https://github.com/dechamps/FlexASIO): an ASIO driver on top of WASAPI / DirectSound / WDM-KS, so any sound card has an ASIO driver the mixer can open.
// install() downloads the pinned installer (drivers/flexasio.json: version, https URL on github.com/dechamps/FlexASIO, SHA-256, silent switches), checks the SHA-256, and runs it
// silently (Inno Setup switches), Windows only. An installer whose hash is not pinned is never run. Nothing is installed unless the person asks (node scripts/install-flexasio.js).
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');
const { fetchChecked } = require('./security');

const PIN_FILE = path.join(__dirname, '..', 'drivers', 'flexasio.json');
const MAX_BYTES = 200 * 1024 * 1024;
// the release assets are served from github.com (redirecting to the objects host); nothing else is followed
const hostOk = u => { try { const x = new URL(u); return x.protocol === 'https:' && (x.hostname === 'github.com' || x.hostname === 'objects.githubusercontent.com' || x.hostname === 'release-assets.githubusercontent.com'); } catch (_) { return false; } };
const urlOk = u => { try { const x = new URL(u); return hostOk(u) && x.hostname === 'github.com' && /^\/dechamps\/FlexASIO\/releases\/download\//i.test(x.pathname); } catch (_) { return false; } };

function loadPin(file = PIN_FILE) { return JSON.parse(fs.readFileSync(file, 'utf8')); }
function pinProblem(pin) {
  if (!pin || !pin.url || !pin.sha256) return 'no FlexASIO installer is pinned yet: fill in url and sha256 in drivers/flexasio.json from https://github.com/dechamps/FlexASIO/releases (or pass --url and --sha256)';
  if (!urlOk(pin.url)) return 'the pinned URL must be a github.com/dechamps/FlexASIO/releases/download/... address';
  if (!/^[0-9a-f]{64}$/i.test(pin.sha256)) return 'the pinned sha256 is not a SHA-256 (64 hex characters)';
  if (!Array.isArray(pin.args) || !pin.args.every(a => typeof a === 'string' && /^[\w/=:.\-]{1,40}$/.test(a))) return 'the silent-install switches in the pin are not valid';
  return null;
}

async function install({ pin = loadPin(), fetchImpl = globalThis.fetch, dir = path.join(os.tmpdir(), 'audio-mixer-flexasio'), platform = process.platform, spawnImpl = spawn, dryRun = false } = {}) {
  if (platform !== 'win32') throw Object.assign(new Error('FlexASIO is a Windows driver'), { status: 409 });
  const bad = pinProblem(pin); if (bad) throw Object.assign(new Error(bad), { status: 400 });
  const name = path.basename(new URL(pin.url).pathname).replace(/[^\w.\- ]/g, '_');
  if (!/\.exe$/i.test(name)) throw Object.assign(new Error('the pinned file is not an installer .exe'), { status: 400 });
  const res = await fetchChecked(fetchImpl, pin.url, { headers: { 'User-Agent': 'audio-mixer-flexasio' } }, hostOk);
  if (!res.ok || !res.body) throw Object.assign(new Error('download failed (' + res.status + ')'), { status: 502 });
  fs.mkdirSync(dir, { recursive: true, mode: 0o700 });
  const target = path.join(dir, name), part = target + '.part', hash = crypto.createHash('sha256'); let bytes = 0;
  const out = fs.createWriteStream(part, { flags: 'w', mode: 0o600 });
  try {
    for await (const chunk of res.body) { bytes += chunk.length; if (bytes > MAX_BYTES) throw new Error('file too large'); hash.update(chunk); if (!out.write(chunk)) await new Promise(r => out.once('drain', r)); }
    await new Promise((r, e) => out.end(err => (err ? e(err) : r())));
  } catch (e) { out.destroy(); try { fs.unlinkSync(part); } catch (_) { /* gone */ } throw Object.assign(new Error(e.message || 'download failed'), { status: 502 }); }
  const sha256 = hash.digest('hex');
  if (sha256 !== pin.sha256.toLowerCase()) { try { fs.unlinkSync(part); } catch (_) { /* gone */ } throw Object.assign(new Error('checksum mismatch: the download was discarded and nothing was run'), { status: 502 }); }
  fs.renameSync(part, target);
  if (dryRun) return { ok: true, file: target, bytes, sha256, installing: false, note: 'Downloaded and verified; not run (dry run).' };
  const child = spawnImpl(target, pin.args, { detached: true, stdio: 'ignore', windowsHide: true }); child.unref();
  return { ok: true, file: target, bytes, sha256, installing: true, version: pin.version || null, note: 'FlexASIO is installing silently. It shows up as an ASIO driver (FlexASIO) in the mixer when it is done; Audio Mixer detects it by itself.' };
}

module.exports = { install, loadPin, pinProblem, urlOk, hostOk, PIN_FILE };
