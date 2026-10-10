'use strict';
// Catalog of official audio drivers / audio stacks per OS, with install detection and a safe downloader.
// Only the project's own GitHub release (FlexASIO) can be downloaded automatically; everything else links to the
// vendor's official site or shows the package-manager command. Nothing is ever executed by this module.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { fetchChecked } = require('./security');

const MAX_BYTES = 200 * 1024 * 1024;
const ALLOWED_DOWNLOAD_HOSTS = ['api.github.com', 'github.com', 'objects.githubusercontent.com', 'release-assets.githubusercontent.com', 'github-releases.githubusercontent.com', 'www.asio4all.org', 'asio4all.org'];

// kind: built-in | asio | sdk | vendor | virtual | package | native
const CATALOG = [
  // ── Windows ──
  { id: 'wasapi', os: ['win32'], kind: 'built-in', name: 'WASAPI (Shared / Exclusive / Loopback)', vendor: 'Microsoft Windows Core Audio',
    note: 'Part of Windows. Exclusive mode and event-driven buffers give low latency without extra drivers.',
    url: 'https://learn.microsoft.com/windows/win32/coreaudio/wasapi', detect: { drivers: ['wasapi-shared', 'wasapi-excl'] } },
  { id: 'directsound', os: ['win32'], kind: 'built-in', name: 'DirectSound / WDM-KS / MME', vendor: 'Microsoft Windows',
    note: 'Legacy and kernel-streaming host APIs, installed with Windows.',
    url: 'https://learn.microsoft.com/windows-hardware/drivers/audio/', detect: { drivers: ['directsound', 'wdmks', 'mme'] } },
  { id: 'flexasio', os: ['win32'], kind: 'asio', name: 'FlexASIO', vendor: 'Etienne Dechamps (open source)',
    note: 'ASIO driver on top of WASAPI / PortAudio: ASIO for any sound card. The installer is downloaded from the project\'s official GitHub release.',
    url: 'https://github.com/dechamps/FlexASIO', detect: { drivers: ['flexasio'], asio: /flexasio/i },
    download: { kind: 'github-release', repo: 'dechamps/FlexASIO', pick: /^FlexASIO-[\w.\-]*\.exe$/i, skip: /debug|symbol|pdb/i } },
  { id: 'asio4all', os: ['win32'], kind: 'asio', name: 'ASIO4ALL', vendor: 'Michael Tippach',
    note: 'Universal low-latency ASIO driver for WDM audio hardware. The newest installer is looked up on the official site (asio4all.org) and saved, never run.',
    url: 'https://www.asio4all.org/', detect: { drivers: ['asio4all'], asio: /asio4all/i },
    download: { kind: 'site-link', page: 'https://www.asio4all.org/', pick: /ASIO4ALL[_\w.\-]*\.(?:exe|zip)$/i } },
  { id: 'steinberg-asio', os: ['win32', 'darwin'], kind: 'sdk', name: 'Steinberg ASIO (SDK and generic driver)', vendor: 'Steinberg Media Technologies',
    note: 'ASIO is a Steinberg specification; drivers are written by the hardware vendor.',
    url: 'https://www.steinberg.net/developers/', detect: { asioAny: true } },
  { id: 'focusrite', os: ['win32', 'darwin'], kind: 'vendor', name: 'Focusrite USB ASIO / Control', vendor: 'Focusrite',
    note: 'Scarlett / Clarett / Vocaster drivers.', url: 'https://focusrite.com/downloads', detect: { asio: /focusrite|scarlett|clarett/i } },
  { id: 'rme', os: ['win32', 'darwin'], kind: 'vendor', name: 'RME Fireface / Babyface / HDSP drivers', vendor: 'RME Audio',
    note: 'ASIO and Core Audio drivers.', url: 'https://rme-audio.de/downloads.html', detect: { asio: /rme|fireface|babyface|hdsp|hammerfall/i } },
  { id: 'motu', os: ['win32', 'darwin'], kind: 'vendor', name: 'MOTU Pro Audio drivers', vendor: 'MOTU',
    note: 'Interface installer (ASIO and Core Audio).', url: 'https://motu.com/download', detect: { asio: /motu/i } },
  { id: 'uad', os: ['win32', 'darwin'], kind: 'vendor', name: 'Universal Audio Apollo / Volt', vendor: 'Universal Audio',
    note: 'Console / UAD software including drivers.', url: 'https://www.uaudio.com/downloads', detect: { asio: /apollo|universal audio|uad/i } },
  { id: 'behringer', os: ['win32', 'darwin'], kind: 'vendor', name: 'Behringer / Music Tribe WING and X-USB', vendor: 'Music Tribe',
    note: 'WING / X-USB / UMC drivers. Search the product on the site.', url: 'https://www.behringer.com/', detect: { asio: /wing|behringer|x-?usb|umc/i } },
  { id: 'ssl', os: ['win32', 'darwin'], kind: 'vendor', name: 'Solid State Logic SSL 2 / SSL 12', vendor: 'Solid State Logic',
    note: 'USB audio interface drivers.', url: 'https://solidstatelogic.com/', detect: { asio: /ssl|solid state/i } },
  { id: 'vb-cable', os: ['win32'], kind: 'virtual', name: 'VB-CABLE virtual audio device', vendor: 'VB-Audio',
    note: 'Virtual cable for routing between applications.', url: 'https://vb-audio.com/Cable/', detect: {} },
  { id: 'voicemeeter', os: ['win32'], kind: 'virtual', name: 'Voicemeeter (virtual mixer, ASIO)', vendor: 'VB-Audio',
    note: 'Virtual mixer with ASIO and WDM devices.', url: 'https://vb-audio.com/Voicemeeter/', detect: { asio: /voicemeeter/i } },
  // ── macOS ──
  { id: 'coreaudio', os: ['darwin'], kind: 'built-in', name: 'Core Audio HAL', vendor: 'Apple',
    note: 'Part of macOS. Class-compliant interfaces need no driver; AudioDriverKit drivers come from the hardware vendor.',
    url: 'https://developer.apple.com/documentation/audiodriverkit', detect: { drivers: ['coreaudio'] } },
  { id: 'blackhole', os: ['darwin'], kind: 'virtual', name: 'BlackHole virtual audio loopback', vendor: 'Existential Audio',
    note: 'Open source virtual driver for routing audio between applications.', url: 'https://existential.audio/blackhole/', detect: {} },
  // ── Linux ──
  { id: 'alsa', os: ['linux'], kind: 'package', name: 'ALSA (kernel drivers and utilities)', vendor: 'ALSA Project',
    note: 'The Linux kernel audio layer; install the utilities to inspect devices.', url: 'https://www.alsa-project.org/',
    detect: { drivers: ['alsa'] }, install: { apt: 'sudo apt install alsa-utils', dnf: 'sudo dnf install alsa-utils', pacman: 'sudo pacman -S alsa-utils' } },
  { id: 'pipewire', os: ['linux'], kind: 'package', name: 'PipeWire (PulseAudio and JACK compatible)', vendor: 'PipeWire Project',
    note: 'Modern low-latency audio server with PulseAudio, JACK and ALSA client compatibility.', url: 'https://pipewire.org/',
    detect: { drivers: ['pipewire'] }, install: { apt: 'sudo apt install pipewire pipewire-pulse pipewire-jack wireplumber', dnf: 'sudo dnf install pipewire pipewire-pulseaudio pipewire-jack-audio-connection-kit wireplumber', pacman: 'sudo pacman -S pipewire pipewire-pulse pipewire-jack wireplumber' } },
  { id: 'jack', os: ['linux', 'darwin'], kind: 'package', name: 'JACK Audio Connection Kit', vendor: 'JACK Audio',
    note: 'Professional low-latency audio server.', url: 'https://jackaudio.org/',
    detect: { drivers: ['jack'] }, install: { apt: 'sudo apt install jackd2', dnf: 'sudo dnf install jack-audio-connection-kit', pacman: 'sudo pacman -S jack2' } },
  // ── Android / iOS ──
  { id: 'aaudio', os: ['android'], kind: 'built-in', name: 'AAudio / Oboe', vendor: 'Google Android',
    note: 'Part of Android (NDK). Low-latency "exclusive" streams where the device supports them.',
    url: 'https://developer.android.com/ndk/guides/audio/aaudio/aaudio', detect: {} },
  { id: 'avaudiosession', os: ['ios'], kind: 'built-in', name: 'AVAudioSession / Core Audio', vendor: 'Apple',
    note: 'Part of iOS. No third-party audio drivers.', url: 'https://developer.apple.com/documentation/avfaudio/avaudiosession', detect: {} },
  // ── The bridge's own native audio module ──
  { id: 'naudiodon2', os: ['win32', 'darwin', 'linux'], kind: 'native', name: 'PortAudio native module (naudiodon2)', vendor: 'npm',
    note: 'Lets the local server open ASIO / WASAPI / Core Audio / ALSA devices. Install it once in the bridge folder (needs a C++ build toolchain).',
    url: 'https://www.npmjs.com/package/naudiodon2', detect: { portaudio: true }, install: { npm: 'cd bridge && npm install' } },
];

const PUBLIC_FIELDS = ['id', 'os', 'kind', 'name', 'vendor', 'note', 'url', 'install'];

function isInstalled(item, info) {
  const d = item.detect || {};
  if (!info) return null;
  if (d.portaudio) return !!info.portaudio;
  if (d.asioAny) return info.platform === 'win32' ? (info.asio || []).length > 0 : null;
  if (d.drivers && d.drivers.some(x => (info.drivers || []).includes(x))) return true;
  if (d.asio && (info.asio || []).some(n => d.asio.test(n))) return true;
  if (item.kind === 'built-in' && item.os.includes(info.platform)) return true;
  return (d.drivers || d.asio) ? false : null;   // null = cannot be detected
}

function listCatalog(info, dir) {
  const platform = info && info.platform;
  return CATALOG.map(it => {
    const out = {};
    PUBLIC_FIELDS.forEach(k => { if (it[k] !== undefined) out[k] = it[k]; });
    out.forThisPc = !platform || it.os.includes(platform);
    out.installed = out.forThisPc ? isInstalled(it, info) : null;   // drivers for other systems cannot be detected here
    out.downloadable = !!it.download;
    return out;
  });
}

function downloadDir() { return process.env.BRIDGE_DOWNLOAD_DIR || path.join(os.homedir(), 'AudioMixerDrivers'); }

function hostOk(u, hosts) {
  try { const x = new URL(u); return x.protocol === 'https:' && hosts.includes(x.hostname); } catch (_) { return false; }
}

// Downloads one catalog item. `fetchImpl` and `dir` are injectable for tests. Returns { ok, file, bytes, sha256, verified, source }.
async function resolveGithubRelease(dl, fetchImpl, hosts, item) {
  const api = `https://api.github.com/repos/${dl.repo}/releases/latest`;
  const rel = await fetchImpl(api, { headers: { Accept: 'application/vnd.github+json', 'User-Agent': 'audio-mixer-bridge' } });
  if (!rel.ok) throw Object.assign(new Error('release lookup failed (' + rel.status + ')'), { status: 502 });
  const j = await rel.json();
  const asset = (Array.isArray(j.assets) ? j.assets : []).find(a => a && typeof a.name === 'string' && dl.pick.test(a.name) && !(dl.skip && dl.skip.test(a.name)));
  if (!asset || !hostOk(asset.browser_download_url, hosts)) throw Object.assign(new Error('no matching installer in the latest release; open ' + item.url), { status: 502 });
  return { url: asset.browser_download_url, name: asset.name, digest: asset.digest };
}

// "ASIO4ALL_2_15_English.exe" -> [2, 15]; the highest version wins, English preferred on a tie.
function versionOf(name) { const m = /(\d+)[_.](\d+)(?:[_.](\d+))?/.exec(name); return m ? [Number(m[1]), Number(m[2]), Number(m[3] || 0)] : [0, 0, 0]; }
function cmpVersion(a, b) { for (let i = 0; i < 3; i++) if (a[i] !== b[i]) return a[i] - b[i]; return 0; }

// Finds the newest installer linked from the vendor's own page. Only https links on the allowed vendor hosts are accepted.
async function resolveSiteLink(dl, fetchImpl, hosts, item) {
  const page = await fetchImpl(dl.page, { headers: { 'User-Agent': 'audio-mixer-bridge' } });
  if (!page.ok) throw Object.assign(new Error('vendor page lookup failed (' + page.status + '); open ' + item.url), { status: 502 });
  const html = String(await page.text()).slice(0, 2 * 1024 * 1024);
  const found = [];
  for (const m of html.matchAll(/href\s*=\s*["']([^"']+)["']/gi)) {
    let u; try { u = new URL(m[1].replace(/&amp;/g, '&'), dl.page); } catch (_) { continue; }
    const file = decodeURIComponent(u.pathname.split('/').pop() || '');
    if (u.protocol === 'https:' && hostOk(u.href, hosts) && dl.pick.test(file) && !found.some(f => f.url === u.href)) found.push({ url: u.href, name: file });
  }
  if (!found.length) throw Object.assign(new Error('no installer link found on the vendor page; open ' + item.url), { status: 502 });
  found.sort((a, b) => cmpVersion(versionOf(b.name), versionOf(a.name)) || (/english/i.test(b.name) - /english/i.test(a.name)));
  return found[0];
}

async function downloadDriver(id, { fetchImpl = globalThis.fetch, dir = downloadDir(), hosts = ALLOWED_DOWNLOAD_HOSTS } = {}) {
  const item = CATALOG.find(x => x.id === id);
  if (!item || !item.download) throw Object.assign(new Error('this driver has no automatic download; use the official site'), { status: 400 });
  const dl = item.download;
  const asset = dl.kind === 'site-link' ? await resolveSiteLink(dl, fetchImpl, hosts, item) : await resolveGithubRelease(dl, fetchImpl, hosts, item);
  const name = path.basename(asset.name).replace(/[^\w.\-]/g, '_');
  if (!name || name.startsWith('.')) throw Object.assign(new Error('unsafe file name'), { status: 502 });

  const res = await fetchChecked(fetchImpl, asset.url, { headers: { 'User-Agent': 'audio-mixer-bridge' } }, u => hostOk(u, hosts));
  if (!res.ok || !res.body) throw Object.assign(new Error('download failed (' + res.status + ')'), { status: 502 });
  if (res.url && !hostOk(res.url, hosts)) throw Object.assign(new Error('download redirected to an untrusted host'), { status: 502 });
  const len = Number(res.headers.get('content-length') || 0);
  if (len > MAX_BYTES) throw Object.assign(new Error('file too large'), { status: 502 });

  fs.mkdirSync(dir, { recursive: true });
  const target = path.join(dir, name), part = target + '.part';
  const hash = crypto.createHash('sha256');
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
  } catch (e) {
    out.destroy(); try { fs.unlinkSync(part); } catch (_) { /* gone */ }
    throw Object.assign(new Error(e.message || 'download failed'), { status: 502 });
  }
  const sha256 = hash.digest('hex');
  const expected = typeof asset.digest === 'string' && asset.digest.startsWith('sha256:') ? asset.digest.slice(7).toLowerCase() : null;
  if (expected && expected !== sha256) { try { fs.unlinkSync(part); } catch (_) { /* gone */ } throw Object.assign(new Error('checksum mismatch: download discarded'), { status: 502 }); }
  fs.renameSync(part, target);
  return { ok: true, id, file: target, bytes, sha256, verified: !!expected, source: asset.url, note: 'Saved only. Run the installer yourself after checking it.' };
}

module.exports = { CATALOG, listCatalog, downloadDriver, downloadDir, isInstalled, ALLOWED_DOWNLOAD_HOSTS };
