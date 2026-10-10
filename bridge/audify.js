'use strict';
// Audify (RtAudio) audio engine: device listing for every compiled host API, automatic frame-size allocation, and stream open/close.
// `audify` is an optional dependency (native module). Everything takes an injectable `load` so it is testable without hardware.
function loadAudify() { return require('audify'); }

// RtAudio API enum name -> host API name used everywhere else in the bridge (matches the interface grouping ranks).
const API_NAMES = {
  WINDOWS_ASIO: 'ASIO', WINDOWS_WASAPI: 'Windows WASAPI', WINDOWS_DS: 'Windows DirectSound',
  MACOSX_CORE: 'Core Audio', UNIX_JACK: 'JACK', LINUX_ALSA: 'ALSA', LINUX_PULSE: 'PulseAudio', LINUX_OSS: 'OSS',
};
// Latency each host API can usually sustain; the frame size is derived from it.
const TARGET_MS = { WINDOWS_ASIO: 4, MACOSX_CORE: 5, UNIX_JACK: 5, WINDOWS_WASAPI: 10, LINUX_ALSA: 10, LINUX_PULSE: 15, LINUX_OSS: 15, WINDOWS_DS: 20 };
const MIN_FRAMES = 32, MAX_FRAMES = 4096;
// Audify device ids start here so a deviceId alone tells the bridge which engine owns the device (PortAudio ids are small).
const AUDIFY_BASE = 1000;

const pow2 = n => { let p = 1; while (p < n) p *= 2; return p; };
const clampFrames = (n, min = MIN_FRAMES, max = MAX_FRAMES) => Math.min(Math.max(n, min), max);
const isPow2 = n => Number.isInteger(n) && n > 0 && (n & (n - 1)) === 0;

// API name ("ASIO", "WINDOWS_ASIO", "wasapi" ...) -> enum name, or null.
function apiKey(api) {
  const s = String(api || '').toUpperCase();
  if (API_NAMES[s]) return s;
  if (/ASIO/.test(s)) return 'WINDOWS_ASIO';
  if (/WASAPI/.test(s)) return 'WINDOWS_WASAPI';
  if (/DIRECT ?SOUND|^DS$/.test(s)) return 'WINDOWS_DS';
  if (/CORE/.test(s)) return 'MACOSX_CORE';
  if (/JACK/.test(s)) return 'UNIX_JACK';
  if (/ALSA/.test(s)) return 'LINUX_ALSA';
  if (/PULSE/.test(s)) return 'LINUX_PULSE';
  if (/OSS/.test(s)) return 'LINUX_OSS';
  return null;
}

// Automatic frame-size allocation: latency target of the host API -> nearest power of two (ASIO driver buffers are powers of two),
// doubled for wide interfaces (>16 / >32 channels) whose USB / PCIe transfers need bigger blocks to avoid dropouts.
function recommendFrameSize({ api, sampleRate = 48000, channels = 2, latencyMs, min = MIN_FRAMES, max = MAX_FRAMES } = {}) {
  const rate = Number(sampleRate) > 0 ? Number(sampleRate) : 48000;
  const key = apiKey(api);
  const ms = Number(latencyMs) > 0 ? Number(latencyMs) : (TARGET_MS[key] || 10);
  let raw = rate * ms / 1000;
  const ch = Number(channels) || 2;
  if (ch > 16) raw *= 2;
  if (ch > 32) raw *= 2;
  const frames = clampFrames(pow2(Math.ceil(raw)), min, max);
  return { frames, latencyMs: Math.round(frames / rate * 10000) / 10, sampleRate: rate, api: key, target: ms };
}

// Sizes tried in order when a driver refuses one: the recommendation, then larger (safer), then smaller.
function frameCandidates(first, { min = MIN_FRAMES, max = MAX_FRAMES } = {}) {
  const out = [];
  const add = n => { if (n >= min && n <= max && !out.includes(n)) out.push(n); };
  add(first);
  for (let n = first * 2; n <= max; n *= 2) add(n);
  for (let n = first / 2; n >= min; n /= 2) add(n);
  return out;
}

// What the caller asked for ("auto" | undefined | a number) -> list of sizes to try.
function plan(want, ctx) {
  if (want !== undefined && want !== null && want !== 'auto') {
    const n = Number(want);
    if (!isPow2(n) || n < MIN_FRAMES || n > MAX_FRAMES) throw new Error(`frameSize must be a power of two between ${MIN_FRAMES} and ${MAX_FRAMES}, or "auto"`);
    return { auto: false, candidates: [n] };
  }
  const rec = recommendFrameSize(ctx);
  // ASIO and JACK have one global buffer size set in the driver's own control panel: 0 asks the driver for it and returns the real value.
  const driverOwned = rec.api === 'WINDOWS_ASIO' || rec.api === 'UNIX_JACK';
  return { auto: true, recommended: rec, candidates: (driverOwned ? [0] : []).concat(frameCandidates(rec.frames)) };
}

function apiEnum(mod) { return mod.RtAudioApi || mod.RtAudioApis || {}; }

// Every compiled API (ASIO, WASAPI, ...) with its devices. Device ids are made unique across APIs (the bridge's `deviceId`).
function listDevices(load = loadAudify) {
  let mod;
  try { mod = load(); } catch (_) { return null; }
  const RtAudio = mod.RtAudio, Api = apiEnum(mod);
  const byNumber = new Map(Object.entries(Api).filter(([, v]) => typeof v === 'number').map(([k, v]) => [v, k]));
  let apis = [];
  try {
    const compiled = typeof RtAudio.getCompiledApi === 'function' ? RtAudio.getCompiledApi() : typeof RtAudio.getApis === 'function' ? RtAudio.getApis() : null;
    if (Array.isArray(compiled)) apis = compiled.map(a => typeof a === 'number' ? byNumber.get(a) : String(a)).filter(Boolean);
  } catch (_) { /* fall back to probing */ }
  if (!apis.length) apis = Object.keys(API_NAMES).filter(k => typeof Api[k] === 'number');
  const devices = [], hostApis = [];
  let next = AUDIFY_BASE;
  for (const key of apis) {
    if (!API_NAMES[key]) continue;                      // skips the dummy API and unknown ones
    let rt;
    try { rt = new RtAudio(Api[key]); } catch (_) { continue; }
    // RtAudio silently substitutes another API when the requested one is not compiled in: skip it instead of listing duplicates.
    try { if (typeof rt.getApi === 'function' && apiKey(rt.getApi()) !== key) continue; } catch (_) { /* cannot tell: keep it */ }
    let list = [];
    try { list = rt.getDevices() || []; } catch (_) { /* API present but no devices */ }
    if (!list.length) { hostApis.push(API_NAMES[key]); continue; }
    hostApis.push(API_NAMES[key]);
    for (const d of list) {
      devices.push({
        id: next++, rtId: d.id, api: key, hostAPIName: API_NAMES[key], name: String(d.name || ''),
        maxInputChannels: d.inputChannels || 0, maxOutputChannels: d.outputChannels || 0,
        defaultSampleRate: d.preferredSampleRate || (d.sampleRates && d.sampleRates[0]) || 48000,
        sampleRates: Array.isArray(d.sampleRates) ? d.sampleRates : [],
        isDefaultInput: !!d.isDefaultInput, isDefaultOutput: !!d.isDefaultOutput,
      });
    }
    try { if (typeof rt.closeStream === 'function') rt.closeStream(); } catch (_) { /* nothing open */ }
  }
  return { hostApis, devices };
}

// Same shape as the PortAudio detection so the page, interface grouping and ASIO lock work with either engine.
function detectAudify(load = loadAudify) {
  const r = listDevices(load);
  if (!r) return null;
  return {
    engine: 'audify', hostApis: r.hostApis,
    devices: r.devices.map(d => ({ id: d.id, name: d.name, hostApi: d.hostAPIName, inputs: d.maxInputChannels, outputs: d.maxOutputChannels, sampleRate: d.defaultSampleRate })),
  };
}

// Opens one RtAudio stream (output or input). Returns { frameSize, sampleRate, channels, write, close } or throws.
function openStream({ mod, dev, direction, channels, sampleRate, frameSize, onData, onError }) {
  const { RtAudio, RtAudioFormat = {}, RtAudioStreamFlags = {} } = mod, Api = apiEnum(mod);
  const out = direction === 'output';
  const maxCh = out ? dev.maxOutputChannels : dev.maxInputChannels;
  if (!maxCh) throw new Error(`${dev.name} has no ${out ? 'output' : 'input'} channels`);
  if (out && channels > maxCh) throw new Error(`${dev.name} has only ${maxCh} output channel(s)`);
  const ch = out ? channels : Math.min(channels, maxCh);   // a capture simply delivers fewer channels
  if (dev.sampleRates && dev.sampleRates.length && !dev.sampleRates.includes(sampleRate)) {
    throw new Error(`${dev.name} does not support ${sampleRate} Hz (supports ${dev.sampleRates.join(', ')})`);
  }
  const p = plan(frameSize, { api: dev.api, sampleRate, channels: ch });
  const fmt = RtAudioFormat.RTAUDIO_SINT16 !== undefined ? RtAudioFormat.RTAUDIO_SINT16 : 2;
  const flags = RtAudioStreamFlags.RTAUDIO_MINIMIZE_LATENCY || 0;
  let lastErr = null;
  for (const fs of p.candidates) {
    const rt = new RtAudio(Api[dev.api]);
    try {
      const params = { deviceId: dev.rtId, nChannels: ch, firstChannel: 0 };
      const actual = rt.openStream(out ? params : null, out ? null : params, fmt, sampleRate, fs, 'Audio Mixer',
        out ? null : (pcm => onData && onData(Buffer.from(pcm))), null, flags, (type, msg) => onError && onError(new Error(String(msg || type))));
      rt.start();
      const used = Number.isInteger(actual) && actual > 0 ? actual : fs;
      if (!(used > 0)) throw new Error('the driver did not report its buffer size');
      const frameBytes = used * ch * 2;
      let pending = Buffer.alloc(0), dropped = 0;
      return {
        frameSize: used, sampleRate, channels: ch, auto: p.auto, tried: p.candidates.slice(0, p.candidates.indexOf(fs) + 1),
        latencyMs: Math.round(used / sampleRate * 10000) / 10,
        get dropped() { return dropped; },
        // RtAudio needs whole blocks of exactly frameSize frames.
        write(buf) {
          pending = pending.length ? Buffer.concat([pending, buf]) : buf;
          if (pending.length > frameBytes * 16) { dropped += pending.length - frameBytes * 16; pending = pending.subarray(pending.length - frameBytes * 16); }
          while (pending.length >= frameBytes) { rt.write(Buffer.from(pending.subarray(0, frameBytes))); pending = pending.subarray(frameBytes); }
        },
        close() {
          try { if (typeof rt.isStreamRunning === 'function' ? rt.isStreamRunning() : true) rt.stop(); } catch (_) { /* already stopped */ }
          try { rt.closeStream(); } catch (_) { /* already closed */ }
        },
      };
    } catch (e) {
      lastErr = e;
      try { rt.closeStream(); } catch (_) { /* nothing open */ }
    }
  }
  throw new Error(`could not open ${dev.name} (${dev.hostAPIName}) with frame sizes ${p.candidates.map(n => n || 'driver').join(', ')}: ${lastErr && lastErr.message || lastErr}`);
}

// Opens ONE RtAudio stream that reads and writes the same device (ASIO drivers are single-client: one duplex stream, not two).
// Returns { frameSize, sampleRate, inChannels, outChannels, write, close } or throws.
function openDuplex({ mod, dev, inChannels, outChannels, sampleRate, frameSize, onData, onError }) {
  const { RtAudio, RtAudioFormat = {}, RtAudioStreamFlags = {} } = mod, Api = apiEnum(mod);
  if (!dev.maxInputChannels || !dev.maxOutputChannels) throw new Error(`${dev.name} cannot read and write at once`);
  if (outChannels > dev.maxOutputChannels) throw new Error(`${dev.name} has only ${dev.maxOutputChannels} output channel(s)`);
  const ci = Math.min(inChannels, dev.maxInputChannels);
  if (dev.sampleRates && dev.sampleRates.length && !dev.sampleRates.includes(sampleRate)) {
    throw new Error(`${dev.name} does not support ${sampleRate} Hz (supports ${dev.sampleRates.join(', ')})`);
  }
  const p = plan(frameSize, { api: dev.api, sampleRate, channels: Math.max(ci, outChannels) });
  const fmt = RtAudioFormat.RTAUDIO_SINT16 !== undefined ? RtAudioFormat.RTAUDIO_SINT16 : 2;
  const flags = RtAudioStreamFlags.RTAUDIO_MINIMIZE_LATENCY || 0;
  let lastErr = null;
  for (const fs of p.candidates) {
    const rt = new RtAudio(Api[dev.api]);
    try {
      const actual = rt.openStream({ deviceId: dev.rtId, nChannels: outChannels, firstChannel: 0 }, { deviceId: dev.rtId, nChannels: ci, firstChannel: 0 },
        fmt, sampleRate, fs, 'Audio Mixer', pcm => onData && onData(Buffer.from(pcm)), null, flags, (type, msg) => onError && onError(new Error(String(msg || type))));
      rt.start();
      const used = Number.isInteger(actual) && actual > 0 ? actual : fs;
      if (!(used > 0)) throw new Error('the driver did not report its buffer size');
      const frameBytes = used * outChannels * 2;
      let pending = Buffer.alloc(0);
      return {
        frameSize: used, sampleRate, inChannels: ci, outChannels, latencyMs: Math.round(used / sampleRate * 10000) / 10,
        write(buf) {
          pending = pending.length ? Buffer.concat([pending, buf]) : buf;
          if (pending.length > frameBytes * 16) pending = pending.subarray(pending.length - frameBytes * 16);
          while (pending.length >= frameBytes) { rt.write(Buffer.from(pending.subarray(0, frameBytes))); pending = pending.subarray(frameBytes); }
        },
        close() {
          try { if (typeof rt.isStreamRunning === 'function' ? rt.isStreamRunning() : true) rt.stop(); } catch (_) { /* already stopped */ }
          try { rt.closeStream(); } catch (_) { /* already closed */ }
        },
      };
    } catch (e) {
      lastErr = e;
      try { rt.closeStream(); } catch (_) { /* nothing open */ }
    }
  }
  throw new Error(`could not open ${dev.name} (${dev.hostAPIName}) for reading and writing: ${lastErr && lastErr.message || lastErr}`);
}

function pickAudifyDevice(devices, wantedId, direction, channels) {
  if (Number.isInteger(wantedId)) return devices.find(d => d.id === wantedId) || null;
  const key = direction === 'output' ? 'maxOutputChannels' : 'maxInputChannels';
  return devices.find(d => /asio/i.test(d.hostAPIName) && d[key] >= channels) || null;
}

// Why the module did not load, with the fix: on Windows a "module could not be found" error nearly always means the Microsoft Visual C++ runtime is missing.
function loadProblem(load = loadAudify, platform = process.platform) {
  try { load(); return null; } catch (e) {
    const msg = String(e && e.message || e).split('\n')[0].slice(0, 300);
    let hint = 'Install it with: cd bridge && npm install audify  (or run the launcher again, it installs it for you)';
    if (/Cannot find module/i.test(msg)) hint = 'The Audify module is not installed. Run the PC mode launcher again (it installs it), or: cd bridge && npm install audify';
    else if (platform === 'win32' && /specified module could not be found|ERR_DLOPEN_FAILED|vcruntime|msvcp/i.test(msg)) hint = 'Install the Microsoft Visual C++ runtime: winget install Microsoft.VCRedist.2015+.x64  (https://aka.ms/vs/17/release/vc_redist.x64.exe)';
    return { error: msg, hint };
  }
}

// Data for GET /api/audify
function describe(load = loadAudify) {
  const r = listDevices(load);
  if (!r) return { installed: false, hostApis: [], devices: [], ...(loadProblem(load) || {}) };
  return {
    installed: true, hostApis: r.hostApis,
    devices: r.devices.map(d => ({ ...d, recommended: ['input', 'output'].reduce((o, k) => {
      const ch = k === 'input' ? d.maxInputChannels : d.maxOutputChannels;
      if (ch) o[k] = recommendFrameSize({ api: d.api, sampleRate: d.defaultSampleRate, channels: ch });
      return o;
    }, {}) })),
  };
}

module.exports = { loadAudify, loadProblem, API_NAMES, apiKey, recommendFrameSize, frameCandidates, plan, listDevices, detectAudify, openStream, openDuplex, pickAudifyDevice, describe, isPow2, AUDIFY_BASE, MIN_FRAMES, MAX_FRAMES };
