'use strict';
// Windows native helpers: AudioDevices.exe (C++, WASAPI endpoint list) and audio-devices.vbs (VBScript, WMI sound devices).
// Both are read-only and run without a shell; they are optional and fail soft.
const { execFile } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

// an installed package keeps the helpers in native/win, the repository in windows/native
const DIRS = [path.resolve(__dirname, '..', 'native', 'win'), path.resolve(__dirname, '..', 'windows', 'native')];
const firstOf = (...rel) => { const all = DIRS.map(d => path.join(d, ...rel)); return all.find(p => fs.existsSync(p)) || all[0]; };
const exePath = (arch = process.arch) => firstOf(arch === 'ia32' ? 'x86' : 'x64', 'AudioDevices.exe');   // native/win/<arch>/AudioDevices.exe
const vbsPath = () => firstOf('vbs', 'audio-devices.vbs');                                                // native/win/vbs/audio-devices.vbs

function defaultRun(cmd, args, timeout = 6000) {
  return new Promise(resolve => {
    execFile(cmd, args, { timeout, windowsHide: true, maxBuffer: 2 * 1024 * 1024 }, (err, stdout) => resolve(err ? '' : String(stdout)));
  });
}
// channel count of an endpoint whose direction is known: the reported one, else stereo
const chans = n => ((n | 0) > 0 ? n | 0 : 2);
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
    // The endpoint kind (capture / render) is known even when Windows cannot give its mix format (device busy or in exclusive use, some Bluetooth
    // endpoints): the helper then reports 0 channels, which showed the interface as "no input, no output" with READ and WRITE switched off.
    inputs: d.kind === 'input' ? chans(d.channels) : 0, outputs: d.kind === 'output' ? chans(d.channels) : 0, sampleRate: d.sampleRate | 0 || 48000,
  }));
  return { engine: 'wasapi-native', hostApis: ['Windows WASAPI'], devices };
}

// Sound devices through VBScript + WMI (names only), for PCs where PowerShell is blocked.
async function listWmi({ run = defaultRun, platform = process.platform, exists = fs.existsSync } = {}) {
  if (platform !== 'win32' || !exists(vbsPath())) return [];
  const list = parse(await run('cscript', ['//nologo', vbsPath()]));
  return (list || []).filter(d => d && d.name).map(d => ({ name: String(d.name), vendor: String(d.vendor || ''), status: String(d.status || '') }));
}

module.exports = { listEndpoints, listWmi, exePath, vbsPath, parse, chans };
