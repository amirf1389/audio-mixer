'use strict';
// Reads the audio interfaces from the operating system itself (no audio engine needed), so the interface list works on every OS:
//   Linux: aplay -l / arecord -l (ALSA cards), pactl (PulseAudio / PipeWire), /proc/asound/cards
//   macOS: system_profiler SPAudioDataType -json (Core Audio)
//   Windows: see winnative.js (WASAPI endpoints) with the WMI names as the last resort
// Everything is read-only, runs without a shell and fails soft. Device ids are negative: listed, not openable (Audify / PortAudio open them).
const { execFile } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

function defaultRun(cmd, args, timeout = 5000) {
  return new Promise(resolve => {
    execFile(cmd, args, { timeout, windowsHide: true, maxBuffer: 4 * 1024 * 1024 }, (err, stdout) => resolve(err ? '' : String(stdout)));
  });
}

// "card 1: USB [Scarlett 2i2 USB], device 0: USB Audio [USB Audio]"
function parseAlsa(text) {
  const out = [];
  for (const line of String(text).split(/\r?\n/)) {
    const m = /^card (\d+): ([^\[]*)\[([^\]]+)\], device (\d+): ([^\[]*)\[([^\]]+)\]/.exec(line);
    if (m) out.push({ card: +m[1], device: +m[4], name: m[3].trim(), detail: m[6].trim() });
  }
  return out;
}

// pactl list short sources|sinks  ->  "index<TAB>name<TAB>driver<TAB>s16le 2ch 44100Hz<TAB>STATE"
function parsePactl(text) {
  const out = [];
  for (const line of String(text).split(/\r?\n/)) {
    const f = line.split('\t');
    if (f.length < 2 || !f[1]) continue;
    const spec = /(\d+)ch\s+(\d+)Hz/.exec(f[3] || '');
    out.push({ name: f[1], channels: spec ? +spec[1] : 2, sampleRate: spec ? +spec[2] : 48000, monitor: /\.monitor$/.test(f[1]) });
  }
  return out;
}

function parseCards(text) {
  const out = [];
  for (const line of String(text).split(/\r?\n/)) { const m = /^\s*(\d+)\s+\[(\S+)\s*\]:\s*(.+)$/.exec(line); if (m) out.push({ card: +m[1], name: m[3].trim() }); }
  return out;
}

function parseMac(text) {
  let j; try { j = JSON.parse(text); } catch (_) { return []; }
  const items = [];
  for (const sec of (j && j.SPAudioDataType) || []) for (const it of sec._items || []) items.push(it);
  return items.filter(it => it && it._name).map(it => ({
    name: String(it._name), inputs: Number(it.coreaudio_device_input) || 0, outputs: Number(it.coreaudio_device_output) || 0,
    sampleRate: Number(it.coreaudio_device_srate) || 48000,
    defaultIn: /yes/i.test(String(it.coreaudio_default_audio_input_device || '')), defaultOut: /yes/i.test(String(it.coreaudio_default_audio_output_device || '')),
  }));
}

async function listLinux({ run = defaultRun, readFile = p => fs.readFileSync(p, 'utf8') } = {}) {
  const devices = [];
  const add = (name, hostApi, inputs, outputs, sampleRate) => devices.push({ id: -(devices.length + 1), name, hostApi, native: true, inputs, outputs, sampleRate: sampleRate || 48000 });
  const rec = parseAlsa(await run('arecord', ['-l'])), play = parseAlsa(await run('aplay', ['-l']));
  for (const d of rec) add(d.name + (d.device ? ' #' + d.device : ''), 'ALSA', 2, 0);
  for (const d of play) add(d.name + (d.device ? ' #' + d.device : ''), 'ALSA', 0, 2);
  if (!rec.length && !play.length) {
    let cards = []; try { cards = parseCards(readFile('/proc/asound/cards')); } catch (_) { /* no ALSA */ }
    for (const c of cards) { add(c.name, 'ALSA', 2, 0); add(c.name, 'ALSA', 0, 2); }
  }
  for (const s of parsePactl(await run('pactl', ['list', 'short', 'sources']))) add(s.monitor ? 'Monitor of ' + s.name.replace(/\.monitor$/, '') : s.name, 'PulseAudio', s.channels, 0, s.sampleRate);
  for (const s of parsePactl(await run('pactl', ['list', 'short', 'sinks']))) add(s.name, 'PulseAudio', 0, s.channels, s.sampleRate);
  return devices;
}

// Core Audio helper written in Swift (native/mac/AudioDevices.swift, built by install.command or `swiftc`): channels, sample rate, default device, transport
const macHelper = () => path.join(__dirname, '..', 'native', 'mac', 'AudioDevices');
function fromHelper(text) {
  let j; try { j = JSON.parse(text); } catch (_) { return null; }
  if (!j || !j.ok || !Array.isArray(j.devices)) return null;
  const devices = [];
  for (const d of j.devices) {
    if (!d || !d.name) continue;
    devices.push({ id: -(devices.length + 1), name: String(d.name), hostApi: 'Core Audio', native: true, isDefault: !!d.default, transport: d.transport || 'unknown',
      inputs: d.kind === 'input' ? ((d.channels | 0) > 0 ? d.channels | 0 : 2) : 0, outputs: d.kind === 'output' ? ((d.channels | 0) > 0 ? d.channels | 0 : 2) : 0, sampleRate: d.sampleRate | 0 || 48000 });   // direction known, channel count unknown: stereo
  }
  return devices;
}

async function listMac({ run = defaultRun, exists = fs.existsSync, helper = macHelper() } = {}) {
  if (exists(helper)) { const h = fromHelper(await run(helper, [])); if (h && h.length) return h; }
  const devices = [];
  for (const d of parseMac(await run('system_profiler', ['SPAudioDataType', '-json'], 15000))) {
    if (d.inputs) devices.push({ id: -(devices.length + 1), name: d.name, hostApi: 'Core Audio', native: true, isDefault: d.defaultIn, inputs: d.inputs, outputs: 0, sampleRate: d.sampleRate });
    if (d.outputs) devices.push({ id: -(devices.length + 1), name: d.name, hostApi: 'Core Audio', native: true, isDefault: d.defaultOut, inputs: 0, outputs: d.outputs, sampleRate: d.sampleRate });
  }
  return devices;
}

// Same shape as the engine detections: { engine, hostApis, devices } or null.
async function list(opts = {}) {
  const platform = opts.platform || process.platform;
  let devices = [], engine = null, hostApis = [];
  if (platform === 'linux') { devices = await listLinux(opts); engine = 'alsa-native'; hostApis = [...new Set(devices.map(d => d.hostApi))]; }
  else if (platform === 'darwin') { devices = await listMac(opts); engine = 'coreaudio-native'; hostApis = devices.length ? ['Core Audio'] : []; }
  else return null;
  return devices.length ? { engine, hostApis, devices } : null;
}

module.exports = { fromHelper, macHelper, list, listLinux, listMac, parseAlsa, parsePactl, parseCards, parseMac };
