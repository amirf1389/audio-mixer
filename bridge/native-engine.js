'use strict';
// The bridge's audio I/O through the project's own native engine (engine/cpp: `audiomixer-native --list-devices` and `--stream`), in place of the
// Audify (RtAudio) binding. audify.js hands its listDevices / openStream / openDuplex to this module whenever the engine program is installed.
// Devices come back in the shape audify.listDevices() gives, so input.js, output.js, duplex.js, the ASIO lock and the interface grouping need no change:
//   { id, engineBackend, engineId, api, hostAPIName, name, maxInputChannels, maxOutputChannels, defaultSampleRate, sampleRates, isDefaultInput, isDefaultOutput }
// A stream is one engine process: PCM (interleaved int16) goes to its stdin and comes from its stdout; its stderr carries JSON status lines.
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync, spawn } = require('node:child_process');

const API = { wasapi: 'WINDOWS_WASAPI', asio: 'WINDOWS_ASIO', alsa: 'LINUX_ALSA', coreaudio: 'MACOSX_CORE', null: 'ENGINE_NULL' };
const HOST_API = { WINDOWS_WASAPI: 'Windows WASAPI', WINDOWS_ASIO: 'ASIO', LINUX_ALSA: 'ALSA', MACOSX_CORE: 'Core Audio', ENGINE_NULL: 'Null' };
const LIST_TTL_MS = 5000;        // the page scans every few seconds: one engine run answers them all for a while
const DEFAULT_FRAMES = 256;

// The engine program: AUDIO_MIXER_ENGINE (a development build), else audiomixer-native(.exe) in the install root, where the installers put it.
function enginePath({ env = process.env, platform = process.platform, root = path.join(__dirname, '..') } = {}) {
  if (env.AUDIO_MIXER_ENGINE) return fs.existsSync(env.AUDIO_MIXER_ENGINE) ? env.AUDIO_MIXER_ENGINE : null;
  const p = path.join(root, platform === 'win32' ? 'audiomixer-native.exe' : 'audiomixer-native');
  return fs.existsSync(p) ? p : null;
}

const state = { open: new Set(), list: null, at: 0 };

function toDevices(raw, base) {
  return raw.map((d, i) => {
    const api = API[d.backend] || 'ENGINE_' + String(d.backend).toUpperCase();
    return {
      id: base + i, engineBackend: d.backend, engineId: String(d.id || ''), api, hostAPIName: HOST_API[api] || String(d.backend), name: String(d.name || d.id || ''),
      maxInputChannels: d.inChannels | 0, maxOutputChannels: d.outChannels | 0, defaultSampleRate: d.defaultRate || 48000,
      sampleRates: Array.isArray(d.sampleRates) ? d.sampleRates : [], isDefaultInput: !!d.defaultIn, isDefaultOutput: !!d.defaultOut,
    };
  });
}

// Synchronous, like audify.listDevices(). The null device is left out unless includeNull (tests); it plays to nothing.
// While an ASIO stream of the engine is open, the last list is reused: probing an ASIO driver loads it, and ASIO drivers serve one client.
function listDevices({ exe = enginePath(), base = 1000, run = execFileSync, now = Date.now(), includeNull = process.env.AUDIO_MIXER_ENGINE_NULL === '1' } = {}) {
  if (!exe) return null;
  const asioOpen = [...state.open].some(s => s.backend === 'asio');
  if (state.list && (asioOpen || now - state.at < LIST_TTL_MS)) return state.list;
  let raw;
  try { raw = JSON.parse(String(run(exe, ['--list-devices'], { timeout: 20000, windowsHide: true, encoding: 'utf8' }))); }
  catch (e) { return state.list || { provider: 'native', hostApis: [], devices: [], problems: [{ api: 'engine', message: String(e && e.message || e).split('\n')[0] }] }; }
  const devices = toDevices(includeNull ? raw : raw.filter(d => d.backend !== 'null'), base);
  state.list = { provider: 'native', hostApis: [...new Set(devices.map(d => d.hostAPIName))], devices, problems: [] };
  state.at = now;
  return state.list;
}

function launch({ dev, inCh, outCh, sampleRate, frameSize, exclusive, onData, onError, exe = enginePath(), spawnImpl = spawn }) {
  if (!exe) throw new Error('the audio engine (audiomixer-native) is not installed');
  if (dev.engineBackend === 'asio' && [...state.open].some(s => s.backend === 'asio' && s.device === dev.engineId))
    throw new Error(`${dev.name} is already open: an ASIO driver serves one stream, so read and write it together (duplex)`);
  const rate = Number(sampleRate) || 48000;
  const frames = Number.isInteger(frameSize) && frameSize > 0 ? frameSize : DEFAULT_FRAMES;
  const args = ['--stream', '--backend', dev.engineBackend, '--rate', String(rate), '--frames', String(frames), '--in-ch', String(inCh), '--out-ch', String(outCh)];
  if (inCh) args.push('--in', dev.engineId);
  if (outCh) args.push('--out', dev.engineId);
  if (exclusive) args.push('--exclusive');
  const child = spawnImpl(exe, args, { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });
  const rec = { backend: dev.engineBackend, device: dev.engineId };
  state.open.add(rec);
  let closed = false;
  const fail = e => { if (!closed && onError) onError(e instanceof Error ? e : new Error(String(e))); };
  // stdout chunks can split a frame: pass on whole frames only, so every buffer the page gets is aligned
  const frameBytes = Math.max(1, inCh) * 2;
  let carry = Buffer.alloc(0);
  child.stdout.on('data', chunk => {
    const buf = carry.length ? Buffer.concat([carry, chunk]) : chunk;
    const whole = buf.length - (buf.length % frameBytes);
    carry = whole < buf.length ? Buffer.from(buf.subarray(whole)) : Buffer.alloc(0);
    if (whole && onData) onData(buf.subarray(0, whole));
  });
  let line = '';
  child.stderr.on('data', text => {
    line += text;
    let i;
    while ((i = line.indexOf('\n')) >= 0) {
      const s = line.slice(0, i); line = line.slice(i + 1);
      let j = null; try { j = JSON.parse(s); } catch (_) { /* not a status line */ }
      if (j && j.type === 'error') fail(new Error(j.message || 'the audio engine refused the device'));
    }
  });
  child.on('error', fail);
  child.on('exit', code => { state.open.delete(rec); if (code) fail(new Error('the audio engine stopped (exit code ' + code + ')')); });
  child.stdin.on('error', () => { /* the engine ended: exit reports why */ });
  return {
    inChannels: inCh, outChannels: outCh, channels: outCh || inCh, frameSize: frames, auto: !(Number.isInteger(frameSize) && frameSize > 0),
    latencyMs: Math.round(frames / rate * 10000) / 10, sampleRate: rate, deviceRate: rate, resampled: false, exclusive: !!exclusive, duplex: !!(inCh && outCh), engine: 'native',
    write(buf) { if (!closed && outCh) child.stdin.write(buf); },
    close() {
      if (closed) return;
      closed = true; state.open.delete(rec);
      try { child.stdin.end(); } catch (_) { /* already gone */ }
      const t = setTimeout(() => { try { child.kill(); } catch (_) { /* already gone */ } }, 1500);
      if (t.unref) t.unref();
    },
  };
}

// The same arguments as audify.openStream / openDuplex. A device with fewer channels than asked delivers / takes its own count.
function openStream(o) {
  const out = o.direction === 'output', max = out ? o.dev.maxOutputChannels : o.dev.maxInputChannels;
  if (!max) throw new Error(`${o.dev.name} has no ${out ? 'output' : 'input'} channels`);
  const ch = Math.min(Math.max(1, Number(o.channels) || 2), max);
  return launch({ ...o, inCh: out ? 0 : ch, outCh: out ? ch : 0 });
}
function openDuplex(o) {
  const inCh = Math.min(Math.max(1, Number(o.inChannels) || 2), o.dev.maxInputChannels), outCh = Math.min(Math.max(1, Number(o.outChannels) || 2), o.dev.maxOutputChannels);
  if (!inCh || !outCh) throw new Error(`${o.dev.name} cannot read and write at once`);
  return launch({ ...o, inCh, outCh });
}

function _reset() { state.open.clear(); state.list = null; state.at = 0; }

module.exports = { enginePath, listDevices, openStream, openDuplex, toDevices, _reset, _state: state };
