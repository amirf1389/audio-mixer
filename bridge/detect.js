'use strict';
// Native audio driver / device detection. Read-only; every probe is optional and fails soft.
const { execFile } = require('node:child_process');
const fs = require('node:fs');

function run(cmd, args, timeout = 4000) {
  return new Promise(resolve => {
    execFile(cmd, args, { timeout, windowsHide: true, maxBuffer: 4 * 1024 * 1024 }, (err, stdout) => resolve(err ? '' : String(stdout)));
  });
}

// Keys match the driver ids used by window.universalAsioSdk.drivers in the page.
async function windows() {
  const drivers = new Set(['wasapi-shared', 'wasapi-excl', 'wasapi-loop', 'directsound', 'wdmks', 'mme']);
  const devices = [];
  const asio = [];
  const ps = await run('powershell', ['-NoProfile', '-Command',
    'Get-CimInstance Win32_SoundDevice | Select-Object Name,Manufacturer,Status | ConvertTo-Json -Compress']);
  try {
    const j = JSON.parse(ps || '[]');
    (Array.isArray(j) ? j : [j]).forEach(d => d && d.Name && devices.push({ name: d.Name, vendor: d.Manufacturer || '', status: d.Status || '' }));
  } catch (_) { /* ignore */ }
  const reg = await run('reg', ['query', 'HKLM\\SOFTWARE\\ASIO']);
  reg.split(/\r?\n/).forEach(line => {
    const m = line.match(/HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\(.+)$/i);
    if (m) asio.push(m[1].trim());
  });
  asio.forEach(n => {
    drivers.add('steinberg');
    const l = n.toLowerCase();
    if (l.includes('asio4all')) drivers.add('asio4all');
    if (l.includes('flexasio')) drivers.add('flexasio');
    if (l.includes('focusrite')) drivers.add('focusrite');
    if (l.includes('yamaha') || l.includes('steinberg')) drivers.add('yamaha');
    if (l.includes('rme') || l.includes('fireface') || l.includes('hammerfall')) drivers.add('rme');
    if (l.includes('motu')) drivers.add('motu');
    if (l.includes('wing') || l.includes('behringer')) drivers.add('wing');
    if (l.includes('apollo') || l.includes('universal audio')) drivers.add('uad');
    if (l.includes('ssl')) drivers.add('ssl');
    if (l.includes('cwasio')) drivers.add('cwasio');
  });
  return { drivers: [...drivers], devices, asio, recommended: asio.length ? 'steinberg' : 'wasapi-excl' };
}

async function mac() {
  const out = await run('system_profiler', ['SPAudioDataType']);
  const devices = [];
  out.split('\n').forEach(l => { const m = l.match(/^\s{8}([^\s].*):$/); if (m) devices.push({ name: m[1], vendor: '', status: 'OK' }); });
  return { drivers: ['coreaudio'], devices, asio: [], recommended: 'coreaudio' };
}

async function linux() {
  const drivers = new Set();
  const devices = [];
  let cards = '';
  try { cards = fs.readFileSync('/proc/asound/cards', 'utf8'); } catch (_) { /* no ALSA */ }
  cards.split('\n').forEach(l => { const m = l.match(/^\s*\d+\s+\[(\S+)\s*\]:\s*(.+)$/); if (m) devices.push({ name: m[2].trim(), vendor: '', status: 'OK' }); });
  if (cards.trim()) drivers.add('alsa');
  const pw = await run('pw-cli', ['info', '0']);
  if (pw) drivers.add('pipewire');
  const jack = await run('jack_lsp', []);
  if (jack) drivers.add('jack');
  const rec = drivers.has('pipewire') ? 'pipewire' : drivers.has('jack') ? 'jack' : drivers.has('alsa') ? 'alsa' : null;
  return { drivers: [...drivers], devices, asio: [], recommended: rec };
}

async function detect() {
  const p = process.platform;
  const r = p === 'win32' ? await windows() : p === 'darwin' ? await mac() : p === 'linux' ? await linux() : { drivers: [], devices: [], asio: [], recommended: null };
  return { platform: p, arch: process.arch, node: process.version, ...r };
}

module.exports = { detect };
