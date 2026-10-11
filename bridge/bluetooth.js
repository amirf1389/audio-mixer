'use strict';
// Bluetooth of this computer, for the BLUETOOTH page: paired / connected devices, airwaves discovery (devices in range) and connect / pair / disconnect.
//   Linux:   bluetoothctl (BlueZ): devices, info, show, scan, connect, pair
//   Windows: PowerShell Get-PnpDevice -Class Bluetooth (paired devices and whether they are connected); settings page for pairing
//   macOS:   system_profiler SPBluetoothDataType (connected / not connected devices); Bluetooth settings for pairing
// Fixed commands only: the one thing taken from a request is a device address, and it must be a MAC address. Nothing is installed or changed on the PC other than
// what the user asks for on the page (scan, connect, pair, disconnect).
const { execFile } = require('node:child_process');

const MAC = /^[0-9A-F]{2}(:[0-9A-F]{2}){5}$/i;
const AUDIO_NAME = /head(phone|set)|ear(bud|phone|pod)|buds|airpods|speaker|soundbar|sound ?bar|boom|\bWH-|\bWF-|\bLE-|\bJBL\b|\bBose\b|\bSony\b|beats|\bQC\b|sennheiser|hands-?free|a2dp|\baudio\b|\bIEM\b|stereo/i;

function run(cmd, args, timeout = 8000) {
  return new Promise(resolve => {
    execFile(cmd, args, { timeout, windowsHide: true, maxBuffer: 2 * 1024 * 1024 }, (err, stdout) => resolve(err && !stdout ? '' : String(stdout || '')));
  });
}

// ── Linux ──
function parseDevices(text) {
  const out = [];
  for (const line of String(text).split(/\r?\n/)) { const m = /^Device\s+([0-9A-F:]{17})\s+(.+)$/i.exec(line.trim()); if (m) out.push({ address: m[1].toUpperCase(), name: m[2].trim() }); }
  return out;
}
function parseInfo(text) {
  const t = String(text), yes = k => new RegExp('^\\s*' + k + ':\\s*yes', 'mi').test(t);
  const rssi = /RSSI:\s*(?:0x[0-9a-f]+\s*)?\(?(-?\d+)\)?/i.exec(t), batt = /Battery Percentage:\s*0x[0-9a-f]+\s*\((\d+)\)/i.exec(t), icon = /Icon:\s*(\S+)/i.exec(t);
  return { paired: yes('Paired'), connected: yes('Connected'), trusted: yes('Trusted'), rssi: rssi ? Number(rssi[1]) : null, battery: batt ? Number(batt[1]) : null, icon: icon ? icon[1] : '', audioProfile: /Audio Sink|Handsfree|Headset|A\/V Remote|Advanced Audio/i.test(t) };
}
async function listLinux(runner) {
  const show = await runner('bluetoothctl', ['show']);
  const adapter = { present: /Controller\s+[0-9A-F:]{17}/i.test(show), powered: /Powered:\s*yes/i.test(show), discovering: /Discovering:\s*yes/i.test(show) };
  if (!adapter.present) return { adapter, devices: [], method: 'bluetoothctl' };
  const devices = [];
  for (const d of parseDevices(await runner('bluetoothctl', ['devices'])).slice(0, 40)) {
    const info = parseInfo(await runner('bluetoothctl', ['info', d.address]));
    devices.push({ name: d.name, address: d.address, paired: info.paired, connected: info.connected, rssi: info.rssi, battery: info.battery, audio: info.audioProfile || /^audio-/.test(info.icon) || AUDIO_NAME.test(d.name), type: info.icon || '' });
  }
  return { adapter, devices, method: 'bluetoothctl' };
}

// ── Windows ──
function parsePnp(text) {
  let j; try { j = JSON.parse(String(text).replace(/^﻿/, '').trim() || '[]'); } catch (_) { return []; }
  if (!Array.isArray(j)) j = j ? [j] : [];
  const out = [], seen = new Set();
  for (const d of j) {
    const m = /BTHENUM\\DEV_([0-9A-F]{12})/i.exec(String(d.InstanceId || '')) || /BTHLE\\DEV_([0-9A-F]{12})/i.exec(String(d.InstanceId || ''));
    if (!m || !d.FriendlyName) continue;
    const a = m[1].toUpperCase().match(/.{2}/g).join(':');
    if (seen.has(a)) { const o = out.find(x => x.address === a); if (o && String(d.Status).toUpperCase() === 'OK') o.connected = true; continue; }
    seen.add(a);
    out.push({ name: String(d.FriendlyName), address: a, paired: true, connected: String(d.Status).toUpperCase() === 'OK', rssi: null, battery: null, audio: AUDIO_NAME.test(String(d.FriendlyName)), type: '' });
  }
  return out;
}
async function listWindows(runner) {
  const ps = "Get-PnpDevice -Class Bluetooth -ErrorAction SilentlyContinue | Select-Object FriendlyName,Status,InstanceId | ConvertTo-Json -Compress";
  const text = await runner('powershell.exe', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', ps], 12000);
  const all = (() => { try { const j = JSON.parse(String(text).replace(/^﻿/, '').trim() || '[]'); return Array.isArray(j) ? j : j ? [j] : []; } catch (_) { return []; } })();
  const radio = all.some(d => /BTH\\MS_BTHBRB|USB\\VID|Bluetooth Adapter|Radio|Intel.*Bluetooth|Wireless Bluetooth/i.test(String(d.InstanceId) + ' ' + String(d.FriendlyName)) && String(d.Status).toUpperCase() === 'OK');
  return { adapter: { present: all.length > 0, powered: radio || all.some(d => String(d.Status).toUpperCase() === 'OK'), discovering: false }, devices: parsePnp(text), method: 'Get-PnpDevice' };
}

// ── macOS ──
function parseMacBt(text) {
  let j; try { j = JSON.parse(text); } catch (_) { return null; }
  const sec = ((j && j.SPBluetoothDataType) || [])[0]; if (!sec) return null;
  const devices = [];
  const add = (list, connected) => { for (const it of list || []) for (const [name, v] of Object.entries(it || {})) {
    devices.push({ name, address: String(v.device_address || '').replace(/-/g, ':').toUpperCase(), paired: true, connected, rssi: v.device_rssi ? Number(v.device_rssi) : null, battery: v.device_batteryLevelMain ? parseInt(v.device_batteryLevelMain, 10) : null, audio: /audio|headphone|speaker|headset/i.test(String(v.device_minorType || '')) || AUDIO_NAME.test(name), type: String(v.device_minorType || '') });
  } };
  add(sec.device_connected, true); add(sec.device_not_connected, false);
  const st = sec.controller_properties || {};
  return { adapter: { present: true, powered: /on/i.test(String(st.controller_state || 'on')), discovering: false }, devices, method: 'system_profiler' };
}
async function listMac(runner) {
  const r = parseMacBt(await runner('system_profiler', ['SPBluetoothDataType', '-json'], 15000));
  return r || { adapter: { present: false, powered: false, discovering: false }, devices: [], method: 'system_profiler' };
}

async function list({ run: runner = run, platform = process.platform } = {}) {
  let r;
  try { r = platform === 'linux' ? await listLinux(runner) : platform === 'win32' ? await listWindows(runner) : platform === 'darwin' ? await listMac(runner) : { adapter: { present: false, powered: false, discovering: false }, devices: [], method: 'none' }; }
  catch (_) { r = { adapter: { present: false, powered: false, discovering: false }, devices: [], method: 'error' }; }
  r.devices.sort((a, b) => (b.connected - a.connected) || (b.audio - a.audio) || a.name.localeCompare(b.name));
  return { ok: true, platform, scanSupported: platform === 'linux', pairSupported: platform === 'linux', settingsSupported: platform === 'win32' || platform === 'darwin', ...r };
}

// Airwaves discovery: devices in range, paired or not. Only BlueZ can scan from a program; elsewhere the page uses the browser's Bluetooth picker.
async function scan({ seconds = 6, run: runner = run, platform = process.platform } = {}) {
  if (platform !== 'linux') return { ok: true, supported: false, hint: 'This system cannot scan from the local server: use "DISCOVER IN BROWSER" (Web Bluetooth) or the system Bluetooth settings.' };
  const s = Math.max(2, Math.min(15, parseInt(seconds, 10) || 6));
  await runner('bluetoothctl', ['--timeout', String(s), 'scan', 'on'], (s + 4) * 1000);
  const r = await list({ run: runner, platform });
  return { ...r, supported: true, scannedSeconds: s, nearby: r.devices.filter(d => !d.paired).length };
}

async function act(action, address, { run: runner = run, platform = process.platform } = {}) {
  if (!['connect', 'disconnect', 'pair'].includes(action)) throw Object.assign(new Error('unknown action'), { status: 400 });
  if (!MAC.test(String(address || ''))) throw Object.assign(new Error('a Bluetooth address (AA:BB:CC:DD:EE:FF) is needed'), { status: 400 });
  if (platform !== 'linux') throw Object.assign(new Error('pairing and connecting are done in the system Bluetooth settings on this system'), { status: 501 });
  const a = address.toUpperCase();
  if (action === 'pair') { await runner('bluetoothctl', ['pair', a], 25000); await runner('bluetoothctl', ['trust', a], 8000); }
  const out = await runner('bluetoothctl', [action === 'disconnect' ? 'disconnect' : 'connect', a], 20000);
  const info = parseInfo(await runner('bluetoothctl', ['info', a]));
  return { ok: true, action, address: a, paired: info.paired, connected: info.connected, output: out.split(/\r?\n/).filter(Boolean).slice(-2).join(' ').slice(0, 200) };
}

// The system Bluetooth settings page (pairing on Windows and macOS).
function openSettings({ platform = process.platform, spawn = execFile } = {}) {
  const cmd = platform === 'win32' ? ['cmd', ['/c', 'start', '', 'ms-settings:bluetooth']] : platform === 'darwin' ? ['open', ['x-apple.systempreferences:com.apple.BluetoothSettings']] : null;
  if (!cmd) return { ok: false, error: 'no settings page for this system' };
  try { spawn(cmd[0], cmd[1], { windowsHide: true }, () => {}); return { ok: true }; } catch (e) { return { ok: false, error: String(e.message || e) }; }
}

module.exports = { list, scan, act, openSettings, parseDevices, parseInfo, parsePnp, parseMacBt, MAC };
