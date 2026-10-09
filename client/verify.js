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
  const [status, subject] = String(out || '').trim().split('|');
  if (!status) return { status: 'Unknown', subject: '' };
  return { status, subject: subject || '' };
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
  for (const dir of ['bridge', 'client']) {
    const d = path.join(root, dir);
    if (!fs.existsSync(d)) continue;
    for (const f of fs.readdirSync(d)) if (/\.(js|json|bat|ps1|cmd|exe|dll|node)$/i.test(f) && !listed.has(`${dir}/${f}`)) extra.push(`${dir}/${f}`);
  }
  if (extra.length) { res.push({ level: WARN, title: 'Files that are not part of the build', detail: extra.join(', ') }); }
  if (!bad) res.unshift({ level: PASS, title: 'File integrity', detail: `${listed.size} files match MANIFEST.sha256` });
  return res;
}

function checkInstallerFile(file) {
  const res = [];
  const buf = fs.readFileSync(file);
  res.push(buf.length > 2 && buf[0] === 0x4d && buf[1] === 0x5a ? { level: PASS, title: 'Windows executable (MZ header)', detail: path.basename(file) } : { level: FAIL, title: 'Not a Windows executable', detail: path.basename(file) });
  res.push(buf.includes(Buffer.from('Nullsoft')) ? { level: PASS, title: 'NSIS installer structure found', detail: '' } : { level: WARN, title: 'Not recognised as an NSIS installer', detail: '' });
  const actual = sha256(file);
  const side = file + '.sha256';
  if (fs.existsSync(side)) {
    const want = (/([0-9a-f]{64})/i.exec(fs.readFileSync(side, 'utf8')) || [])[1];
    res.push(want && want.toLowerCase() === actual ? { level: PASS, title: 'SHA-256 matches the published checksum', detail: actual } : { level: FAIL, title: 'SHA-256 does NOT match the published checksum', detail: `expected ${want || '?'} got ${actual}` });
  } else res.push({ level: INFO, title: 'SHA-256', detail: `${actual} (no ${path.basename(side)} next to it: compare with the release page)` });
  return res;
}

async function checkSignature(file, label, { platform = process.platform, run, expect } = {}) {
  if (platform !== 'win32') return [{ level: INFO, title: `${label} signature`, detail: 'Authenticode is checked on Windows only' }];
  const sig = parseSignature(await ps('$s = Get-AuthenticodeSignature -FilePath $env:VERIFY_PATH; "$($s.Status)|$($s.SignerCertificate.Subject)"', { VERIFY_PATH: file }, run, 60000));
  if (sig.status === 'Valid') {
    if (expect && !expect.test(sig.subject)) return [{ level: WARN, title: `${label} is signed by an unexpected publisher`, detail: sig.subject }];
    return [{ level: PASS, title: `${label} signature is valid`, detail: sig.subject }];
  }
  if (sig.status === 'NotSigned') return [{ level: WARN, title: `${label} is not code-signed`, detail: 'Windows SmartScreen will warn; verify the SHA-256 instead' }];
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
    results.push(...checkInstallerFile(target), ...await checkSignature(target, 'Installer', { platform, run }));
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

module.exports = { verify, format, checkManifest, checkInstallerFile, checkSignature, checkLoopbackOnly, checkDefender, parseSignature, parseDefender, sha256 };
