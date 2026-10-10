'use strict';
// Verification scan for the installed app and for the installer .exe.
//   node client/cli.js verify [--scan]            check the installed / built app folder
//   node client/cli.js verify <installer.exe> [--scan]   check a downloaded installer
// Checks: file hashes against MANIFEST.sha256, Authenticode signature of the bundled Node.js runtime and of the installer,
// that the server is not reachable from the network (loopback only), the autostart entry, and (--scan) a Microsoft Defender scan.
const fs = require('node:fs');
const os = require('node:os');
const net = require('node:net');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFile } = require('node:child_process');

const sha256 = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const PASS = 'PASS', WARN = 'WARN', FAIL = 'FAIL', INFO = 'INFO';

function ps(script, env, run = execFile, timeout = 600000) {
  return new Promise(resolve => run('powershell', ['-NoProfile', '-NonInteractive', '-Command', script], { env: { ...process.env, ...env }, timeout, windowsHide: true, maxBuffer: 4 * 1024 * 1024 }, (err, out) => resolve(err ? '' : String(out))));
}

// ── parsers (pure, unit-tested) ──
function parseSignature(out) {
  const parts = String(out || '').trim().split('|');
  if (!parts[0]) return { status: 'Unknown', subject: '', thumbprint: '' };
  // "Status|Subject|Thumbprint" (a subject never ends in a bare hex string, so the last field is the thumbprint when there are three)
  if (parts.length >= 3) return { status: parts[0], subject: parts.slice(1, -1).join('|'), thumbprint: parts[parts.length - 1].toUpperCase() };
  return { status: parts[0], subject: parts[1] || '', thumbprint: '' };
}

// Thumbprint (SHA-1 of the DER certificate) of the published AudioMixer-signing.cer: next to the file, or AUDIO_MIXER_SIGNING_CER.
function pinnedThumbprint(file, env = process.env) {
  const candidates = [env.AUDIO_MIXER_SIGNING_CER, file && path.join(path.dirname(file), 'AudioMixer-signing.cer')].filter(Boolean);
  for (const c of candidates) {
    try {
      let der = fs.readFileSync(c);
      const pem = /-----BEGIN CERTIFICATE-----([\s\S]+?)-----END CERTIFICATE-----/.exec(der.toString('latin1'));
      if (pem) der = Buffer.from(pem[1].replace(/\s+/g, ''), 'base64');
      return crypto.createHash('sha1').update(der).digest('hex').toUpperCase();
    } catch (_) { /* try the next one */ }
  }
  return null;
}
function parseDefender(out) {
  const t = String(out || '').trim();
  if (!t) return { state: 'unavailable' };
  if (/^CLEAN/m.test(t)) return { state: 'clean' };
  const m = /^THREAT\|(.*)$/m.exec(t);
  if (m) return { state: 'threat', ids: m[1] };
  return { state: 'unavailable' };
}

// ── checks ──
function checkManifest(root) {
  const mf = path.join(root, 'MANIFEST.sha256'), res = [];
  if (!fs.existsSync(mf)) return [{ level: INFO, title: 'File integrity', detail: 'no MANIFEST.sha256 here (a source checkout, not an installed build)' }];
  const lines = fs.readFileSync(mf, 'utf8').split('\n').map(l => l.trim()).filter(Boolean);
  const listed = new Set();
  let bad = 0;
  for (const l of lines) {
    const m = /^([0-9a-f]{64})\s+(.+)$/i.exec(l);
    if (!m) { bad++; res.push({ level: FAIL, title: 'Manifest line unreadable', detail: l.slice(0, 80) }); continue; }
    const rel = m[2], file = path.resolve(root, rel);
    if (!file.startsWith(path.resolve(root) + path.sep)) { bad++; res.push({ level: FAIL, title: 'Manifest path escapes the folder', detail: rel }); continue; }
    listed.add(rel.replace(/\\/g, '/'));
    if (!fs.existsSync(file)) { bad++; res.push({ level: FAIL, title: 'File missing', detail: rel }); }
    else if (sha256(file) !== m[1].toLowerCase()) { bad++; res.push({ level: FAIL, title: 'File changed since it was built', detail: rel }); }
  }
  // code that is not in the manifest would run with the server's rights
  const extra = [];
  // native code anywhere under bridge/node_modules (the bundled Audify module) must be listed too
  const nmDir = path.join(root, 'bridge', 'node_modules');
  const walkNm = d => { let es = []; try { es = fs.readdirSync(d, { withFileTypes: true }); } catch (_) { return; } for (const e of es) { const p = path.join(d, e.name); if (e.isDirectory()) walkNm(p); else if (/\.(node|dll|exe)$/i.test(e.name)) { const rel = path.relative(root, p).split(path.sep).join('/'); if (!listed.has(rel)) extra.push(rel); } } };
  walkNm(nmDir);
  for (const dir of ['bridge', 'client']) {
    const d = path.join(root, dir);
    if (!fs.existsSync(d)) continue;
    for (const f of fs.readdirSync(d)) if (/\.(js|json|bat|ps1|cmd|exe|dll|node)$/i.test(f) && !listed.has(`${dir}/${f}`)) extra.push(`${dir}/${f}`);
  }
  if (extra.length) { res.push({ level: WARN, title: 'Files that are not part of the build', detail: extra.join(', ') }); }
  if (!bad) res.unshift({ level: PASS, title: 'File integrity', detail: `${listed.size} files match MANIFEST.sha256` });
  return res;
}

// The setup .exe carries the .msi plus a trailer with the payload SHA-256 (see installer/setup-stub.c). Signatures are appended after
// the PE image, so the trailer is located from the end of the image data, not the end of the file.
function peDataEnd(buf) {
  if (buf.length < 0x100 || buf.readUInt16LE(0) !== 0x5a4d) return buf.length;
  const pe = buf.readUInt32LE(0x3c);
  if (pe + 0x100 > buf.length || buf.readUInt32LE(pe) !== 0x4550) return buf.length;
  const opt = pe + 24, dd = opt + (buf.readUInt16LE(opt) === 0x20b ? 112 : 96);
  const off = buf.readUInt32LE(dd + 32), len = buf.readUInt32LE(dd + 36);
  return len && off && off < buf.length ? off : buf.length;
}
function checkSetupPayload(buf) {
  const end = peDataEnd(buf), magic = Buffer.alloc(16); magic.write('AMIXSETUPv1');
  if (end < 96 || !buf.subarray(end - 96, end - 80).equals(magic)) return [{ level: WARN, title: 'No embedded installer package found', detail: 'not an Audio Mixer setup program' }];
  const size = Number(buf.readBigUInt64LE(end - 96 + 16)), start = end - 96 - size;
  if (start < 0) return [{ level: FAIL, title: 'Embedded installer package is truncated', detail: '' }];
  const payload = buf.subarray(start, end - 96), want = buf.subarray(end - 96 + 24, end - 96 + 56).toString('hex');
  const got = crypto.createHash('sha256').update(payload).digest('hex');
  const ole = payload.subarray(0, 8).equals(Buffer.from([0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1]));
  return [
    ole ? { level: PASS, title: 'Embedded Windows Installer package (.msi)', detail: `${(size / 1048576).toFixed(1)} MB, ${buf.subarray(end - 96 + 56, end - 96 + 96).toString('ascii').replace(/\0+$/, '')}` } : { level: FAIL, title: 'Embedded payload is not an .msi', detail: '' },
    got === want ? { level: PASS, title: 'Embedded package SHA-256 matches the value stored at build time', detail: got } : { level: FAIL, title: 'Embedded package was changed after the build', detail: `expected ${want} got ${got}` },
  ];
}

function checkInstallerFile(file) {
  const res = [];
  const buf = fs.readFileSync(file);
  if (/\.msi$/i.test(file)) {
    // Windows Installer packages are OLE compound files
    res.push(buf.length > 8 && buf.subarray(0, 8).equals(Buffer.from([0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1])) ? { level: PASS, title: 'Windows Installer package (.msi) structure', detail: path.basename(file) } : { level: FAIL, title: 'Not a Windows Installer package', detail: path.basename(file) });
  } else {
    res.push(buf.length > 2 && buf[0] === 0x4d && buf[1] === 0x5a ? { level: PASS, title: 'Windows executable (MZ header)', detail: path.basename(file) } : { level: FAIL, title: 'Not a Windows executable', detail: path.basename(file) });
    res.push(...checkSetupPayload(buf));
  }
  const actual = sha256(file);
  const side = file + '.sha256';
  if (fs.existsSync(side)) {
    const want = (/([0-9a-f]{64})/i.exec(fs.readFileSync(side, 'utf8')) || [])[1];
    res.push(want && want.toLowerCase() === actual ? { level: PASS, title: 'SHA-256 matches the published checksum', detail: actual } : { level: FAIL, title: 'SHA-256 does NOT match the published checksum', detail: `expected ${want || '?'} got ${actual}` });
  } else res.push({ level: INFO, title: 'SHA-256', detail: `${actual} (no ${path.basename(side)} next to it: compare with the release page)` });
  return res;
}

async function checkSignature(file, label, { platform = process.platform, run, expect, pinned } = {}) {
  if (platform !== 'win32') return [{ level: INFO, title: `${label} signature`, detail: 'Authenticode is checked on Windows only' }];
  const sig = parseSignature(await ps('$s = Get-AuthenticodeSignature -FilePath $env:VERIFY_PATH; "$($s.Status)|$($s.SignerCertificate.Subject)|$($s.SignerCertificate.Thumbprint)"', { VERIFY_PATH: file }, run, 60000));
  const ours = pinned && sig.thumbprint && sig.thumbprint === String(pinned).toUpperCase();
  if (sig.status === 'Valid') {
    if (pinned && !ours) return [{ level: WARN, title: `${label} is signed by a different certificate than AudioMixer-signing.cer`, detail: sig.subject }];
    if (!pinned && expect && !expect.test(sig.subject)) return [{ level: WARN, title: `${label} is signed by an unexpected publisher`, detail: sig.subject }];
    return [{ level: PASS, title: `${label} signature is valid`, detail: sig.subject }];
  }
  // A self-signed certificate is reported as not trusted by Windows. It still proves the file is unchanged when it is the published one.
  if (ours && /^(UnknownError|NotTrusted|UntrustedRoot|Incompatible)$/i.test(sig.status)) {
    return [{ level: PASS, title: `${label} is signed with the published Audio Mixer certificate and is unchanged`, detail: `${sig.subject} (self-signed: Windows does not trust the publisher until you import AudioMixer-signing.cer)` }];
  }
  if (sig.status === 'NotSigned') return [{ level: WARN, title: `${label} is not code-signed`, detail: 'Windows SmartScreen will warn; verify the SHA-256 instead' }];
  if (sig.status === 'HashMismatch') return [{ level: FAIL, title: `${label} was changed after it was signed`, detail: 'the signature no longer matches the file' }];
  return [{ level: FAIL, title: `${label} signature problem`, detail: sig.status }];
}

// The server must answer on loopback and must NOT answer on any LAN address of this PC.
async function checkLoopbackOnly(port) {
  const tryConnect = (host) => new Promise(resolve => { const s = net.connect({ host, port, timeout: 1200 }); s.on('connect', () => { s.destroy(); resolve(true); }); s.on('error', () => resolve(false)); s.on('timeout', () => { s.destroy(); resolve(false); }); });
  const up = await tryConnect('127.0.0.1');
  if (!up) return [{ level: INFO, title: 'Network exposure', detail: `the server is not running on port ${port} (start it, then verify again)` }];
  const lan = Object.values(os.networkInterfaces()).flat().filter(a => a && a.family === 'IPv4' && !a.internal).map(a => a.address);
  const open = [];
  for (const ip of lan) if (await tryConnect(ip)) open.push(ip);
  return open.length ? [{ level: FAIL, title: 'The server is reachable from the network', detail: open.join(', ') }] : [{ level: PASS, title: 'The server listens on this PC only (loopback)', detail: `127.0.0.1:${port}${lan.length ? ', not on ' + lan.join(', ') : ''}` }];
}

async function checkDefender(target, { platform = process.platform, run } = {}) {
  if (platform !== 'win32') return [{ level: INFO, title: 'Microsoft Defender scan', detail: 'available on Windows only' }];
  const out = await ps(`$ErrorActionPreference='Stop'; try { Start-MpScan -ScanType CustomScan -ScanPath $env:VERIFY_PATH } catch { return }
$since = (Get-Date).AddMinutes(-15)
$t = Get-MpThreatDetection | Where-Object { $_.InitialDetectionTime -gt $since -and ($_.Resources -join ' ') -like ('*' + $env:VERIFY_PATH + '*') }
if ($t) { 'THREAT|' + (($t | ForEach-Object { $_.ThreatID }) -join ',') } else { 'CLEAN' }`, { VERIFY_PATH: target }, run, 900000);
  const d = parseDefender(out);
  if (d.state === 'clean') return [{ level: PASS, title: 'Microsoft Defender scan: no threats found', detail: target }];
  if (d.state === 'threat') return [{ level: FAIL, title: 'Microsoft Defender reported a threat', detail: 'threat id ' + d.ids }];
  return [{ level: WARN, title: 'Microsoft Defender scan could not run', detail: 'Defender is off, managed by another antivirus, or needs administrator rights' }];
}

async function verify({ target, root, scan = false, port = 8765, platform = process.platform, run } = {}) {
  const results = [];
  if (target && fs.existsSync(target) && fs.statSync(target).isFile()) {
    results.push(...checkInstallerFile(target), ...await checkSignature(target, 'Installer', { platform, run, pinned: pinnedThumbprint(target) }));
    if (scan) results.push(...await checkDefender(target, { platform, run }));
  } else {
    const dir = path.resolve(target || root || path.join(__dirname, '..'));
    results.push(...checkManifest(dir));
    const node = path.join(dir, 'runtime', 'node.exe');
    if (fs.existsSync(node)) results.push(...await checkSignature(node, 'Bundled Node.js runtime', { platform, run, expect: /OpenJS Foundation|Node\.js/i }));
    results.push(...await checkLoopbackOnly(port));
    try { const st = require('./service').status(); results.push({ level: INFO, title: 'Start at login', detail: st.installed ? 'enabled' : 'not enabled' }); } catch (_) { /* unsupported OS */ }
    if (scan) results.push(...await checkDefender(dir, { platform, run }));
  }
  const failed = results.filter(r => r.level === FAIL).length, warned = results.filter(r => r.level === WARN).length;
  return { results, failed, warned, ok: failed === 0 };
}

function format(r) {
  const mark = { PASS: '[ OK ]', WARN: '[WARN]', FAIL: '[FAIL]', INFO: '[info]' };
  return r.results.map(x => `${mark[x.level]} ${x.title}${x.detail ? ': ' + x.detail : ''}`).join('\n') + `\n\nResult: ${r.ok ? 'VERIFIED' : 'NOT VERIFIED'} (${r.failed} failed, ${r.warned} warnings)`;
}

module.exports = { verify, format, checkManifest, checkInstallerFile, checkSetupPayload, pinnedThumbprint, peDataEnd, checkSignature, checkLoopbackOnly, checkDefender, parseSignature, parseDefender, sha256 };
