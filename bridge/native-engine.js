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
const LIST_TTL_MS = 30000;       // a full probe loads every ASIO driver (seconds, and it can disturb another program holding a single-client driver):
                                 // at most every 30 s; SCAN NOW / a device change (detect force) asks for a fresh one at once
const { execFile } = require('node:child_process');
const DEFAULT_FRAMES = 256;

// The engine program: AUDIO_MIXER_ENGINE (a development build), else audiomixer-native(.exe) in the install root, where the installers put it.
function enginePath({ env = process.env, platform = process.platform, root = path.join(__dirname, '..') } = {}) {
  if (env.AUDIO_MIXER_ENGINE) return fs.existsSync(env.AUDIO_MIXER_ENGINE) ? env.AUDIO_MIXER_ENGINE : null;
  const p = path.join(root, platform === 'win32' ? 'audiomixer-native.exe' : 'audiomixer-native');
  return fs.existsSync(p) ? p : null;
}

const state = { open: new Set(), list: null, at: 0, refreshing: null, stale: false };

// Onboard codecs (Realtek and the like) are served by two stacks at once: Windows' own (WASAPI, which system sound and music players use) and the
// vendor's ASIO driver (Realtek ASIO) or a generic ASIO wrapper (ASIO4ALL, FlexASIO) that drives the same chip. Both on one chip conflict: an
// exclusive WASAPI stream locks ASIO out, and an ASIO driver that grabs the chip silences music. They are kept apart: ASIO carries the mixer, WASAPI
// stays in SHARED mode so music and system sound keep playing through Windows' mixer next to it.
const FAMILIES = [['realtek', /realtek/i], ['conexant', /conexant|synaptics/i], ['idt', /\bidt\b|sigmatel|tempo/i], ['via', /\bvia\b|vinyl/i], ['cirrus', /cirrus/i], ['intel', /intel.*(smart sound|display audio)/i], ['amd', /\bamd\b.*(high definition|audio)/i]];
const WRAPPERS = /asio4all|flexasio|generic low latency|asio2wasapi/i;   // ASIO drivers that open a Windows endpoint themselves
function hardwareFamily(name) { for (const [f, re] of FAMILIES) if (re.test(String(name))) return f; return null; }
// the hardware an open engine stream sits on: an onboard family, or 'wdm' for a wrapper (it takes whichever Windows endpoint it is set to)
function hwOf(dev) { const f = hardwareFamily(dev.name); return f || (dev.engineBackend === 'asio' && WRAPPERS.test(dev.name) ? 'wdm' : null); }
function separation(dev, exclusive) {
  const hw = hwOf(dev);
  if (!hw) return { exclusive };
  const open = [...state.open].filter(r => r.hw && (r.hw === hw || r.hw === 'wdm' || hw === 'wdm'));
  if (dev.engineBackend === 'asio') {
    const lock = open.find(r => r.backend === 'wasapi' && r.exclusive);
    if (lock) throw new Error(`${dev.name} shares its chip with a Windows device this mixer holds in exclusive mode: close it, or use it in shared mode next to ASIO`);
    return { exclusive };
  }
  // WASAPI next to an ASIO driver on the same chip: shared mode only, so ASIO keeps the chip and music / system sound keep playing
  if (dev.engineBackend === 'wasapi' && exclusive && open.some(r => r.backend === 'asio')) return { exclusive: false, separated: true };
  return { exclusive };
}

function toDevices(raw, base) {
  return raw.map((d, i) => {
    const api = API[d.backend] || 'ENGINE_' + String(d.backend).toUpperCase();
    return {
      id: base + i, engineBackend: d.backend, engineId: String(d.id || ''), api, hostAPIName: HOST_API[api] || String(d.backend), name: String(d.name || d.id || ''),
      maxInputChannels: d.inChannels | 0, maxOutputChannels: d.outChannels | 0, defaultSampleRate: d.defaultRate || 48000,
      sampleRates: Array.isArray(d.sampleRates) ? d.sampleRates : [], isDefaultInput: !!d.defaultIn, isDefaultOutput: !!d.defaultOut,
      hardware: hardwareFamily(d.name || d.id) || (d.backend === 'asio' && WRAPPERS.test(String(d.name || d.id)) ? 'wdm' : null),
    };
  });
}

// Synchronous, like audify.listDevices(). The null device is left out unless includeNull (tests); it plays to nothing.
// While an ASIO stream of the engine is open, the last list is reused: probing an ASIO driver loads it, and ASIO drivers serve one client.
// The first list (nothing known yet) or an invalidated one is probed synchronously, as callers expect a list back; every later refresh runs in
// the background (async spawn) while the last list is answered at once, so a scan never stalls the server or the audio sockets again.
function listDevices({ exe = enginePath(), base = 1000, run = execFileSync, runAsync = execFile, now = Date.now(), includeNull = process.env.AUDIO_MIXER_ENGINE_NULL === '1' } = {}) {
  if (!exe) return null;
  const asioOpen = [...state.open].some(s => s.backend === 'asio');
  const build = raw => { const devices = toDevices(includeNull ? raw : raw.filter(d => d.backend !== 'null'), base); return { provider: 'native', hostApis: [...new Set(devices.map(d => d.hostAPIName))], devices, problems: [] }; };
  // the raw engine answer is cached; each call shapes it with its own options (null device or not, id base)
  const shaped = () => (state.raw ? (state.list = build(state.raw)) : state.list);
  if (state.list && !state.stale) {
    if (asioOpen || now - state.at < LIST_TTL_MS || state.refreshing) return shaped();
    state.at = now;   // the 30 s count from the start of a refresh: a slow or failed probe is never started again on the next scan
    state.refreshing = new Promise(done => {
      try {
        runAsync(exe, ['--list-devices'], { timeout: 20000, windowsHide: true, encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 }, (err, out) => {
          if (!err && !(state.open.size && [...state.open].some(s => s.backend === 'asio'))) {
            try { const raw = JSON.parse(String(out)); state.list = build(raw); state.raw = raw; } catch (_) { /* keep the last list */ }
          }
          state.refreshing = null; done();
        });
      } catch (_) { state.refreshing = null; done(); }
    });
    return shaped();
  }
  let raw;
  try { raw = JSON.parse(String(run(exe, ['--list-devices'], { timeout: 20000, windowsHide: true, encoding: 'utf8' }))); }
  catch (e) { state.stale = false; return state.list || { provider: 'native', hostApis: [], devices: [], problems: [{ api: 'engine', message: String(e && e.message || e).split('\n')[0] }] }; }
  state.list = build(raw); state.raw = raw; state.at = now; state.stale = false;
  return state.list;
}
// the next listDevices() probes afresh (SCAN NOW, a device-change event); not while an ASIO stream of ours is open (that driver would be reloaded)
function invalidate() { if (![...state.open].some(s => s.backend === 'asio')) state.stale = true; }

function launch({ dev, inCh, outCh, sampleRate, frameSize, exclusive, onData, onError, exe = enginePath(), spawnImpl = spawn, shared = false }) {
  if (!exe) throw new Error('the audio engine (audiomixer-native) is not installed');
  if (!shared && dev.engineBackend === 'asio' && [...state.open].some(s => s.backend === 'asio' && s.device === dev.engineId))
    throw new Error(`${dev.name} is already open: an ASIO driver serves one stream, so read and write it together (duplex)`);
  const sep = separation(dev, !!exclusive);
  exclusive = sep.exclusive;
  const rate = Number(sampleRate) || 48000;
  const frames = Number.isInteger(frameSize) && frameSize > 0 ? frameSize : DEFAULT_FRAMES;
  const args = ['--stream', '--backend', dev.engineBackend, '--rate', String(rate), '--frames', String(frames), '--in-ch', String(inCh), '--out-ch', String(outCh)];
  if (inCh) args.push('--in', dev.engineId);
  if (outCh) args.push('--out', dev.engineId);
  if (exclusive) args.push('--exclusive');
  const child = spawnImpl(exe, args, { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });
  const rec = { backend: dev.engineBackend, device: dev.engineId, hw: hwOf(dev), exclusive: !!exclusive };
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
  let ended;
  const whenClosed = new Promise(res => { ended = res; });
  child.on('error', e => { fail(e); ended(); });
  child.on('exit', code => { state.open.delete(rec); if (code) fail(new Error('the audio engine stopped (exit code ' + code + ')')); ended(); });
  child.stdin.on('error', () => { /* the engine ended: exit reports why */ });
  return {
    inChannels: inCh, outChannels: outCh, channels: outCh || inCh, frameSize: frames, auto: !(Number.isInteger(frameSize) && frameSize > 0),
    latencyMs: Math.round(frames / rate * 10000) / 10, sampleRate: rate, deviceRate: rate, resampled: false, exclusive: !!exclusive, separated: !!sep.separated, duplex: !!(inCh && outCh), engine: 'native', whenClosed,
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

// Single-client drivers (ASIO): one engine process per device, shared by an input user and an output user. When the second direction opens (or one
// closes), the process is restarted with both (or the remaining) directions, after the old one has let go of the driver: ASIO cannot be opened twice.
const SHARED = new Set(['asio']);
const groups = new Map();   // backend + device id -> { dev, sampleRate, frameSize, exclusive, exe, spawnImpl, users: { input, output }, duplex, proc, gen }

function startGroup(key, g) {
  const inU = g.users.input, outU = g.users.output;
  if (!inU && !outU) { groups.delete(key); return; }
  g.proc = launch({
    dev: g.dev, inCh: inU ? inU.channels : 0, outCh: outU ? outU.channels : 0, sampleRate: g.sampleRate, frameSize: g.frameSize, exclusive: g.exclusive,
    exe: g.exe, spawnImpl: g.spawnImpl, shared: true,
    onData: b => { const u = g.users.input; if (u && u.onData) u.onData(b); },
    onError: e => { for (const u of [g.users.input, g.users.output]) if (u && u.onError) u.onError(e); },
  });
}
function restartGroup(key, g) {
  const gen = ++g.gen, old = g.proc;
  g.proc = null;
  if (!old) { startGroup(key, g); return; }
  old.close();
  old.whenClosed.then(() => { if (g.gen === gen && groups.get(key) === g) startGroup(key, g); });
}

function openShared(o, direction, channels) {
  const key = o.dev.engineBackend + '\u0000' + o.dev.engineId, rate = Number(o.sampleRate) || 48000;
  let g = groups.get(key);
  if (g && g.duplex) throw new Error(`${o.dev.name} is already open for reading and writing`);
  if (g && g.users[direction]) throw new Error(`${o.dev.name} is already open for ${direction === 'output' ? 'writing' : 'reading'}`);
  if (!g) separation(o.dev, o.exclusive === true);   // refuse before claiming the device, so a refused open leaves nothing behind
  if (g && rate !== g.sampleRate) throw new Error(`${o.dev.name} is already open at ${g.sampleRate} Hz: reading and writing it together needs the same sample rate`);
  if (!g) {
    if (!(o.exe || enginePath())) throw new Error('the audio engine (audiomixer-native) is not installed');
    g = { dev: o.dev, sampleRate: rate, frameSize: o.frameSize, exclusive: o.exclusive === true, exe: o.exe, spawnImpl: o.spawnImpl, users: {}, duplex: false, proc: null, gen: 0 };
    groups.set(key, g);
  }
  const user = { channels, onData: o.onData, onError: o.onError };
  g.users[direction] = user;
  restartGroup(key, g);
  const frames = Number.isInteger(g.frameSize) && g.frameSize > 0 ? g.frameSize : DEFAULT_FRAMES;
  return {
    inChannels: direction === 'input' ? channels : 0, outChannels: direction === 'output' ? channels : 0, channels, frameSize: frames, auto: !(Number.isInteger(g.frameSize) && g.frameSize > 0),
    latencyMs: Math.round(frames / g.sampleRate * 10000) / 10, sampleRate: g.sampleRate, deviceRate: g.sampleRate, resampled: false, exclusive: g.exclusive, duplex: false, engine: 'native', shared: true,
    write(buf) { if (direction === 'output' && g.users.output === user && g.proc) g.proc.write(buf); },
    close() { if (g.users[direction] !== user) return; delete g.users[direction]; restartGroup(key, g); },
  };
}

// The same arguments as audify.openStream / openDuplex. A device with fewer channels than asked delivers / takes its own count.
// ASIO -> WASAPI fallback. An ASIO driver can show no inputs or outputs (its probe failed, another program holds it, it was just plugged in)
// or fail when it starts. Then the same hardware is used through its Windows (WASAPI, shared) endpoint instead, so there is sound anyway.
// The endpoint is chosen by: the same onboard chip family (Realtek ...), then the most name words in common with the ASIO driver
// ("Focusrite USB ASIO" -> "Speakers (Focusrite USB Audio)"), then the Windows default device for that direction.
const GENERIC = new Set(['asio', 'audio', 'driver', 'usb', 'device', 'speakers', 'microphone', 'line', 'input', 'output', 'high', 'definition', 'the', 'and', 'for', 'generic', 'low', 'latency']);
const words = n => new Set(String(n || '').toLowerCase().split(/[^a-z0-9]+/).filter(w => w.length >= 3 && !GENERIC.has(w)));
function wasapiFallback(dev, direction, list = state.list) {
  const out = direction === 'output', has = d => (out ? d.maxOutputChannels : d.maxInputChannels) > 0;
  const pool = ((list && list.devices) || []).filter(d => d.engineBackend === 'wasapi' && has(d));
  if (!pool.length) return null;
  const fam = hardwareFamily(dev.name), w = words(dev.name);
  const score = d => (fam && hardwareFamily(d.name) === fam ? 100 : 0) + [...words(d.name)].filter(x => w.has(x)).length * 10 + ((out ? d.isDefaultOutput : d.isDefaultInput) ? 1 : 0);
  const best = pool.map(d => ({ d, s: score(d) })).sort((a, b) => b.s - a.s)[0];
  return best ? best.d : null;
}
// A stream that can change engine underneath: the caller keeps one object while an ASIO open that fails early is replaced by WASAPI.
function withFallback(o, direction, openAsio) {
  const fb = () => wasapiFallback(o.dev, direction);
  const openWasapi = (why) => {
    const alt = fb(); if (!alt) return null;
    const s = openStream({ ...o, dev: alt, exclusive: false });   // shared: music and the rest of Windows keep playing
    s.fallback = { from: o.dev.name, to: alt.name, via: 'wasapi', reason: why };
    return s;
  };
  let cur, started = Date.now(), gotData = false;
  const proxy = {};
  const adopt = s => { cur = s; Object.assign(proxy, { inChannels: s.inChannels, outChannels: s.outChannels, channels: s.channels, frameSize: s.frameSize, auto: s.auto, latencyMs: s.latencyMs, sampleRate: s.sampleRate, deviceRate: s.deviceRate, exclusive: s.exclusive, engine: s.engine, shared: s.shared, fallback: s.fallback || null, whenClosed: s.whenClosed }); };
  const onData = b => { gotData = true; if (o.onData) o.onData(b); };
  const onError = e => {
    // an ASIO failure in the first seconds, before any sound came through: switch to WASAPI once
    if (!proxy.fallback && !gotData && Date.now() - started < 4000) {
      try { const old = cur; const s = openWasapi('the ASIO driver failed: ' + String(e && e.message || e)); if (s) { adopt(s); try { old.close(); } catch (_) { /* gone */ } if (o.onFallback) o.onFallback(proxy.fallback); return; } } catch (_) { /* report the original error */ }
    }
    if (o.onError) o.onError(e);
  };
  try { adopt(openAsio({ ...o, onData, onError })); }
  catch (e) { const s = openWasapi('the ASIO driver could not be opened: ' + String(e && e.message || e)); if (!s) throw e; adopt(s); }
  proxy.write = buf => cur.write(buf);
  proxy.close = () => cur.close();
  return proxy;
}

function openStream(o) {
  const out = o.direction === 'output', max = out ? o.dev.maxOutputChannels : o.dev.maxInputChannels;
  if (!max) {
    // an ASIO driver with no channels this way: the same hardware through WASAPI
    if (o.dev.engineBackend === 'asio' && o.wasapiFallback !== false) {
      const alt = wasapiFallback(o.dev, out ? 'output' : 'input');
      if (alt) { const s = openStream({ ...o, dev: alt, exclusive: false }); s.fallback = { from: o.dev.name, to: alt.name, via: 'wasapi', reason: `the ASIO driver shows no ${out ? 'outputs' : 'inputs'}` }; return s; }
    }
    throw new Error(`${o.dev.name} has no ${out ? 'output' : 'input'} channels`);
  }
  const ch = Math.min(Math.max(1, Number(o.channels) || 2), max);
  if (SHARED.has(o.dev.engineBackend)) {
    if (o.dev.engineBackend === 'asio' && o.wasapiFallback !== false) return withFallback(o, out ? 'output' : 'input', oo => openShared(oo, out ? 'output' : 'input', ch));
    return openShared(o, out ? 'output' : 'input', ch);
  }
  return launch({ ...o, inCh: out ? 0 : ch, outCh: out ? ch : 0 });
}
function openDuplex(o) {
  const inCh = Math.min(Math.max(1, Number(o.inChannels) || 2), o.dev.maxInputChannels), outCh = Math.min(Math.max(1, Number(o.outChannels) || 2), o.dev.maxOutputChannels);
  if (!inCh || !outCh) throw new Error(`${o.dev.name} cannot read and write at once`);
  if (!SHARED.has(o.dev.engineBackend)) return launch({ ...o, inCh, outCh });
  // a duplex session owns a single-client driver: it opens only when nothing else uses it, and blocks the device for others until it closes
  const key = o.dev.engineBackend + '\u0000' + o.dev.engineId;
  if (groups.has(key)) throw new Error(`${o.dev.name} is already open: close it first, or read and write it together from the same page`);
  const st = launch({ ...o, inCh, outCh, shared: true }), mark = { duplex: true, users: {} };
  groups.set(key, mark);
  st.whenClosed.then(() => { if (groups.get(key) === mark) groups.delete(key); });   // the device is free once the process has let go of the driver
  return st;
}

function _reset() { state.open.clear(); state.list = null; state.at = 0; state.refreshing = null; state.stale = false; state.raw = null; groups.clear(); }

module.exports = { wasapiFallback, invalidate, hardwareFamily, enginePath, listDevices, openStream, openDuplex, toDevices, _reset, _state: state, _shared: SHARED, _groups: groups };
