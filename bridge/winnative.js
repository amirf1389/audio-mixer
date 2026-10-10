'use strict';
// Windows native helpers: AudioDevices.exe (C++, WASAPI endpoint list) and audio-devices.vbs (VBScript, WMI sound devices).
// Both are read-only and run without a shell; they are optional and fail soft.
const { execFile } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const DIR = path.resolve(__dirname, '..', 'native', 'win');
const exePath = (arch = process.arch) => path.join(DIR, arch === 'ia32' ? 'x86' : 'x64', 'AudioDevices.exe');   // native/win/<arch>/AudioDevices.exe
const vbsPath = () => path.join(DIR, 'vbs', 'audio-devices.vbs');                                         // native/win/vbs/audio-devices.vbs

function defaultRun(cmd, args, timeout = 6000) {
  return new Promise(resolve => {
    execFile(cmd, args, { timeout, windowsHide: true, maxBuffer: 2 * 1024 * 1024 }, (err, stdout) => resolve(err ? '' : String(stdout)));
  });
}
function parse(text) {
  try { const j = JSON.parse(String(text).replace(/^﻿/, '').trim()); return j && j.ok && Array.isArray(j.devices) ? j.devices : null; } catch (_) { return null; }
}

// Active WASAPI endpoints through the C++ helper -> same shape as the PortAudio / Audify detection (ids are negative: listed, not openable).
async function listEndpoints({ run = defaultRun, platform = process.platform, arch = process.arch, exists = fs.existsSync } = {}) {
  if (platform !== 'win32') return null;
  const exe = exePath(arch);
  if (!exists(exe)) return null;
  const list = parse(await run(exe, []));
  if (!list) return null;
  const devices = list.filter(d => d && d.name).map((d, i) => ({
    id: -(i + 1), name: String(d.name), hostApi: 'Windows WASAPI', native: true, endpointId: String(d.id || ''), isDefault: !!d.default,
    inputs: d.kind === 'input' ? (d.channels | 0) : 0, outputs: d.kind === 'output' ? (d.channels | 0) : 0, sampleRate: d.sampleRate | 0 || 48000,
  }));
  return { engine: 'wasapi-native', hostApis: ['Windows WASAPI'], devices };
}

// Sound devices through VBScript + WMI (names only), for PCs where PowerShell is blocked.
async function listWmi({ run = defaultRun, platform = process.platform, exists = fs.existsSync } = {}) {
  if (platform !== 'win32' || !exists(vbsPath())) return [];
  const list = parse(await run('cscript', ['//nologo', vbsPath()]));
  return (list || []).filter(d => d && d.name).map(d => ({ name: String(d.name), vendor: String(d.vendor || ''), status: String(d.status || '') }));
}

module.exports = { listEndpoints, listWmi, exePath, vbsPath, parse };
