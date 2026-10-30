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
  if (/OPEN ?SOUND ?SYSTEM|\bOSS\b/.test(s)) return 'LINUX_OSS';   // RtAudio::getApiDisplayName() returns "OpenSoundSystem" (no literal "OSS"), not just an OSS acronym
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

// An RtAudio instance of ONE named host API. `new RtAudio()` with no argument (what `new RtAudio(Api[key])` turns into when the module has no such API entry) lets RtAudio pick any API
// it was built with, so a stream or probe meant for ASIO ran on WASAPI / DirectSound and its device ids belonged to another API. No number = the API is not available here; an instance that
// reports another API than the one asked for is refused too. Errors carry `notCompiled` so a scan can skip the API quietly.
function openRtAudio(RtAudio, Api, key) {
  const id = Api[key];
  if (typeof id !== 'number') throw Object.assign(new Error(`the ${API_NAMES[key] || key} host API is not available in this audio engine`), { notCompiled: true });
  // Ask the binary first: constructing RtAudio with an API it was not built with prints "no compiled support for specified API argument" and
  // then falls back to another API. Refuse here instead, so nothing is constructed and no other API is ever used.
  if (typeof RtAudio.getCompiledApi === 'function') {
    let compiled = null; try { compiled = RtAudio.getCompiledApi(); } catch (_) { compiled = null; }
    // the binary may report its APIs as numbers (the ids) or as names: accept either form
    const built = Array.isArray(compiled) && compiled.some(a => a === id || (typeof a === 'string' && apiKey(a) === key));
    if (Array.isArray(compiled) && !built) throw Object.assign(new Error(`the ${API_NAMES[key] || key} host API is not compiled into this audio engine`), { notCompiled: true });
  }
  const rt = new RtAudio(id);
  let other = null;
  try { if (typeof rt.getApi === 'function') { const got = apiKey(rt.getApi()); if (got !== key) other = got || String(rt.getApi()); } } catch (_) { /* cannot tell: keep it */ }
  if (other) {
    try { if (typeof rt.closeStream === 'function') rt.closeStream(); } catch (_) { /* nothing open */ }
    // A genuinely missing API (not a bug in apiKey()'s matching: see the 'apiKey() matches RtAudio::getApiDisplayName()' test, which pins it against
    // RtAudio's real display strings) means the installed audify binary was built without it - reinstalling gets the prebuilt that matches this Node /
    // OS / arch; only a from-source build (no prebuilt for this platform) needs the Windows SDK's Media Foundation libs (mfplat, mfuuid, ksuser) for WASAPI.
    const reinstall = ' Reinstall it with: cd bridge && npm uninstall audify && npm install audify  (gets the prebuilt binary for this Node / OS / CPU; if it then builds from source, WASAPI needs the Windows SDK installed).';
    throw Object.assign(new Error(`the ${API_NAMES[key] || key} host API is not compiled into this audio engine (RtAudio offered ${other} instead).${/^WINDOWS_/.test(key) ? reinstall : ''}`), { notCompiled: true });
  }
  return rt;
}

// Open ASIO devices (device id -> entry). ASIO drivers are single-client: one RtAudio stream per driver, so reading AND writing the same
// device share one duplex stream (see openStream / openDuplex); the same registry tells listDevices not to probe a driver that is in use.
const registries = new WeakMap();   // per loaded module (in production there is exactly one)
const asioOf = mod => { let m = registries.get(mod); if (!m) { m = new Map(); registries.set(mod, m); } return m; };
// The single-client registry is keyed on the driver NAME, not dev.id. Reading and writing a device arrive as two separate sessions, each of which
// enumerates the device afresh, so the same ASIO driver carries a different dev.id in each (dev.id is a per-enumeration sequential integer). Keying on
// id made the two sessions miss that they are the same driver, so the second direction asked the single-client driver for a second stream (a collision
// that fails or stalls the first) instead of re-opening it as the one shared duplex stream. The name is the stable per-driver identity (see asio-lock.js).
const asioRegKey = dev => String((dev && dev.name) || '').trim().toLowerCase();

const inCh = d => (d && d.inputChannels) || 0, outCh = d => (d && d.outputChannels) || 0;
// A driver that failed to probe on the first try (waking up, the control panel just closed) often answers on a fresh RtAudio instance.
const probed = d => inCh(d) + outCh(d) > 0;
// An ASIO interface is duplex hardware: a reading with channels on EXACTLY ONE side (e.g. 18 in / 0 out, or 0 in / 20 out) is the signature of a
// partial probe - RtApiAsio::probeDeviceInfo read one direction before the driver was fully awake - not a genuinely one-sided device. Such a reading
// is what made an ASIO interface show up with no inputs, so the mixer had nothing to read from it. It triggers a re-probe and never overwrites a
// fuller reading (see mergeDev / betterOf / the listDevices retry).
const partialAsio = d => (inCh(d) === 0) !== (outCh(d) === 0);
// Merge two readings of the same device: keep the most channels seen on each side, so a partial probe can only ADD a direction, never drop one.
function mergeDev(a, b) {
  if (!b) return a; if (!a) return b;
  const ai = inCh(a), ao = outCh(a), bi = inCh(b), bo = outCh(b);
  if (bi <= ai && bo <= ao) return a;               // b adds nothing
  if (ai <= bi && ao <= bo) return b;               // b dominates
  const base = bi + bo >= ai + ao ? b : a;          // each side richer in a different reading: keep the fuller object, lift both sides to the max
  return { ...base, inputChannels: Math.max(ai, bi), outputChannels: Math.max(ao, bo) };
}

// ASIO probe results are merged with what the last probes saw: a device that probes with channels is remembered (name -> info, order kept so the ids stay
// the same); one that fails to probe is kept for ASIO_GRACE_MS (busy for a moment, driver panel open), then dropped (really unplugged).
const ASIO_GRACE_MS = 20000;
function mergeAsio(asio, fresh, now) {
  const seen = asio.seen || (asio.seen = new Map()), order = asio.order || [];
  const good = new Set(), unprobed = [];
  for (const d of fresh) {
    const name = String(d.name || '');
    if (probed(d)) {
      const prev = seen.get(name);
      seen.set(name, { d: prev ? mergeDev(prev.d, d) : d, at: now });   // never downgrade a device that a previous probe saw with more channels (e.g. a partial read that lost the inputs)
      good.add(name);
    } else unprobed.push(name);
  }
  const names = order.filter(n => seen.has(n) && (good.has(n) || now - seen.get(n).at < ASIO_GRACE_MS));
  for (const n of good) if (!names.includes(n)) names.push(n);
  for (const n of [...seen.keys()]) if (!names.includes(n)) seen.delete(n);
  asio.order = names;
  return { list: names.map(n => seen.get(n).d), unprobed, stale: names.filter(n => !good.has(n)) };
}

// Take the probed answer of the second try for every driver the first one could not read, lift each device to the most channels either try saw (so a
// partial first read gains the direction the retry found), and add the drivers the first one did not list at all.
function betterOf(first, again) {
  const byName = new Map(again.map(d => [String(d.name || ''), d]));
  const out = first.map(d => mergeDev(d, byName.get(String(d.name || ''))));
  const have = new Set(first.map(d => String(d.name || '')));
  for (const d of again) if (!have.has(String(d.name || ''))) out.push(d);
  return out;
}

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
  const devices = [], hostApis = [], problems = [];
  let next = AUDIFY_BASE;
  for (const key of apis) {
    if (!API_NAMES[key]) continue;                      // skips the dummy API and unknown ones
    // An ASIO driver is single-client and process-wide: probing it (new RtAudio + getDevices) while a stream is open can stall or kill that stream,
    // and the page scans every few seconds. While an ASIO stream is open the last probed list is reused.
    const asio = asioOf(mod);
    const isAsio = key === 'WINDOWS_ASIO';
    // a driver that is being opened right now counts as in use too: a scan in that moment probes the same single-client driver and makes BOTH fail
    const inUse = isAsio && (asio.size > 0 || asio.opening > 0);
    const cached = inUse && asio.list ? asio.list : null;
    if (inUse && !cached) { hostApis.push(API_NAMES[key]); continue; }          // nothing known yet and the driver is busy: no probe now
    let rt = null;
    let list = [], probeError = '', retried = false;
    if (cached) list = cached;
    else {
      try { rt = openRtAudio(RtAudio, Api, key); } catch (e) { if (!isAsio || e.notCompiled) continue; probeError = String(e && e.message || e); }   // an API that is not built in is skipped, never replaced by another one
      if (rt) try { list = rt.getDevices() || []; } catch (e) { probeError = String(e && e.message || e); /* API present but no devices */ }
      // Re-probe not only when a driver gave no channels at all, but also when an ASIO driver came back with channels on one side only (a partial probe
      // that lost the inputs - the reason an ASIO interface was detected with no input to the mixer). Skip a driver already confirmed one-sided by an
      // earlier retry, so a genuinely output-only (or input-only) ASIO driver is not reloaded on every scan.
      const confirmed = asio.confirmed || (asio.confirmed = new Set());
      if (isAsio && (probeError || list.some(d => !probed(d) || (partialAsio(d) && !confirmed.has(String(d.name || '')))))) {
        // one more try on a fresh instance (a driver that was waking up or just released by another program answers the second time)
        try { const again = openRtAudio(RtAudio, Api, key).getDevices() || []; list = betterOf(list, again); if (list.length) probeError = ''; retried = true; }
        catch (e) { if (!probeError) probeError = String(e && e.message || e); retried = true; }
        // whatever is still one-sided after the retry is taken as genuinely one-sided: remember it so later scans do not keep reloading it
        for (const d of list) { const nm = String(d.name || ''); if (partialAsio(d)) confirmed.add(nm); else confirmed.delete(nm); }
      }
      if (isAsio) {
        // RtApiAsio::probeDeviceInfo fails for a driver that another program has open or whose interface is unplugged: the driver then shows up with
        // no channels (or the whole probe throws). Such a failure must not make the interface vanish and come back on every scan.
        const m = mergeAsio(asio, list, Date.now());
        list = m.list; asio.list = list.length ? list : asio.list;
        if (probeError || m.unprobed.length || m.stale.length) problems.push({ api: 'ASIO', message: probeError || '', unprobed: m.unprobed, kept: m.stale, retried });
      }
    }
    if (!list.length) { hostApis.push(API_NAMES[key]); continue; }
    hostApis.push(API_NAMES[key]);
    // A WASAPI / DirectSound / Core Audio device that RtAudio could not probe (in use by another program, exclusive mode, just plugged in) comes back
    // with no channels at all, so it showed up as "no input, no output". One more probe on a fresh instance often succeeds; if not, the device is
    // flagged so that the page can take its mode from the operating system's own endpoint list (interfaces.js) and a stream can still be tried.
    if (!isAsio && !cached && list.some(d => !((d.inputChannels || 0) + (d.outputChannels || 0)))) {
      try {
        const again = openRtAudio(RtAudio, Api, key).getDevices() || [];
        list = list.map(d => { if ((d.inputChannels || 0) + (d.outputChannels || 0)) return d; const n = again.find(x => x.id === d.id && String(x.name || '') === String(d.name || '')); return n && (n.inputChannels || 0) + (n.outputChannels || 0) ? n : d; });
      } catch (_) { /* keep the first answer */ }
    }
    for (const d of list) {
      devices.push({
        id: next++, rtId: d.id, api: key, hostAPIName: API_NAMES[key], name: String(d.name || ''),
        ...(!isAsio && !((d.inputChannels || 0) + (d.outputChannels || 0)) ? { probeFailed: true } : {}),
        maxInputChannels: d.inputChannels || 0, maxOutputChannels: d.outputChannels || 0,
        defaultSampleRate: d.preferredSampleRate || (d.sampleRates && d.sampleRates[0]) || 48000,
        sampleRates: Array.isArray(d.sampleRates) ? d.sampleRates : [],
        isDefaultInput: !!d.isDefaultInput, isDefaultOutput: !!d.isDefaultOutput,
      });
    }
    try { if (rt && typeof rt.closeStream === 'function') rt.closeStream(); } catch (_) { /* nothing open */ }
  }
  return { hostApis, devices, problems };
}

// Same shape as the PortAudio detection so the page, interface grouping and ASIO lock work with either engine.
function detectAudify(load = loadAudify) {
  const r = listDevices(load);
  if (!r) return null;
  return {
    engine: 'audify', hostApis: r.hostApis, ...(r.problems && r.problems.length ? { problems: r.problems } : {}),
    devices: r.devices.map(d => ({ id: d.id, name: d.name, hostApi: d.hostAPIName, inputs: d.maxInputChannels, outputs: d.maxOutputChannels, sampleRate: d.defaultSampleRate, ...(d.probeFailed ? { probeFailed: true } : {}) })),
  };
}

// Bluetooth hands-free (HFP) endpoints only run at 8 / 16 kHz (and A2DP at 44.1 / 48 kHz): when the mixer's rate is not offered, the device runs at its own
// closest rate and the bridge converts (linear interpolation, state kept across chunks) so the page still gets / sends audio at the mixer's rate.
const { isBluetooth } = require('./interfaces');
function nearestRate(rates, want) { return rates.slice().sort((a, b) => Math.abs(a - want) - Math.abs(b - want) || b - a)[0]; }
// RtAudio reports recoverable trouble (a buffer under- / overrun on DirectSound or WASAPI, a skipped block) through the SAME error callback as
// fatal errors, with a warning type. Closing the stream on the first of them made every glitch end the audio: only real errors stop it.
// RtAudio 5: WARNING = 0, DEBUG_WARNING = 1; RtAudio 6: NO_ERROR = 0, WARNING = 1.
function isWarning(mod, type) {
  if (typeof type === 'string') return /warning/i.test(type);
  const e = (mod && (mod.RtAudioErrorType || mod.RtAudioError)) || {};
  const names = Object.keys(e).filter(k => /WARNING|NO_ERROR/.test(k) && typeof e[k] === 'number').map(k => e[k]);
  return names.length ? names.includes(type) : (type === 0 || type === 1);
}
function errorHandler(mod, onError, onWarning) {
  return (type, msg) => {
    if (isWarning(mod, type)) { if (onWarning) onWarning(new Error(String(msg || 'warning'))); return; }
    if (onError) onError(new Error(String(msg || type)));
  };
}

// Exclusive access to the device. An ASIO driver serves one client, so it is exclusive by nature. For the other host APIs RtAudio offers the "hog device"
// flag, which only Core Audio and OSS honour; WASAPI shared mode (the only mode RtAudio has), DirectSound, ALSA, Pulse and JACK stay shared, and the result
// says so (`exclusive` on the stream, `exclusive` in the "started" message) instead of pretending.
const HOG_APIS = new Set(['MACOSX_CORE', 'LINUX_OSS']);
function streamFlags(mod, dev, exclusive) {
  const f = (mod && mod.RtAudioStreamFlags) || {};
  let flags = f.RTAUDIO_MINIMIZE_LATENCY || 0, applied = !!dev && dev.api === 'WINDOWS_ASIO';
  if (exclusive && dev && HOG_APIS.has(dev.api) && f.RTAUDIO_HOG_DEVICE) { flags |= f.RTAUDIO_HOG_DEVICE; applied = true; }
  return { flags, exclusive: applied };
}

// Interleaved Int16: more channels -> fewer (channel c gets the average of the input channels i with i % to === c, so stereo -> mono is (L + R) / 2).
function remapChannels(buf, from, to) {
  if (from === to) return buf;
  const n = Math.floor(buf.length / 2 / from), out = Buffer.alloc(n * to * 2);
  for (let f = 0; f < n; f++) {
    for (let c = 0; c < to; c++) {
      let sum = 0, cnt = 0;
      for (let i = c; i < from; i += to) { sum += buf.readInt16LE((f * from + i) * 2); cnt++; }
      out.writeInt16LE(Math.round(sum / cnt), (f * to + c) * 2);
    }
  }
  return out;
}
function createResampler(inRate, outRate, channels) {
  if (!(inRate > 0) || !(outRate > 0) || inRate === outRate) return null;
  const step = inRate / outRate;
  let pos = 0, prev = null;
  return function convert(buf) {
    const n = Math.floor(buf.length / 2 / channels);
    if (!n) return Buffer.alloc(0);
    const src = new Int16Array(n * channels);
    for (let i = 0; i < src.length; i++) src[i] = buf.readInt16LE(i * 2);
    if (!prev) prev = src.slice(0, channels);
    const out = [];
    const at = (f, c) => (f < 0 ? prev[c] : src[f * channels + c]);   // frame -1 is the last frame of the previous chunk
    let p = pos;
    while (p < n - 1) {
      const i0 = Math.floor(p), t = p - i0;
      for (let c = 0; c < channels; c++) out.push(Math.round(at(i0, c) * (1 - t) + at(i0 + 1, c) * t));
      p += step;
    }
    pos = p - n;
    prev = src.slice((n - 1) * channels, n * channels);
    const o = Buffer.alloc(out.length * 2);
    for (let i = 0; i < out.length; i++) o.writeInt16LE(Math.max(-32768, Math.min(32767, out[i])), i * 2);
    return o;
  };
}

// Opens one RtAudio stream (output or input). Returns { frameSize, sampleRate, channels, write, close } or throws.
function openStreamRaw({ mod, dev, direction, channels, sampleRate, frameSize, onData, onError, onWarning, queueControl, exclusive }) {
  sampleRate = Number(sampleRate);
  const { RtAudio, RtAudioFormat = {}, RtAudioStreamFlags = {} } = mod, Api = apiEnum(mod);
  const out = direction === 'output';
  // a device whose probe failed has no known channel count: the operating system listed it with this direction, so a stereo stream is tried (RtAudio's own error follows if it cannot open)
  const maxCh = (out ? dev.maxOutputChannels : dev.maxInputChannels) || (dev.probeFailed ? 2 : 0);
  if (!maxCh) throw new Error(`${dev.name} has no ${out ? 'output' : 'input'} channels`);
  const asioDev = dev.api === 'WINDOWS_ASIO';
  // ASIO outputs are fixed hardware channels: refuse. WASAPI / DirectSound / Core Audio / ALSA devices often have fewer channels than the mixer's
  // stereo master (a mono headset, a mono USB speaker): open the device's own count and mix down instead of failing.
  if (out && channels > maxCh && asioDev) throw new Error(`${dev.name} has only ${maxCh} output channel(s)`);
  const ch = Math.min(channels, maxCh);                   // a capture simply delivers fewer channels
  const remap = out && channels > ch ? buf => remapChannels(buf, channels, ch) : null;
  const wantRate = sampleRate;
  if (dev.sampleRates && dev.sampleRates.length && !dev.sampleRates.includes(sampleRate)) {
    // An ASIO driver runs at the clock set in its control panel (or by the DAW / word clock): say so. Every other host API (WASAPI shared mode
    // only offers the Windows mix rate, Bluetooth only 8 / 16 kHz) runs at the device's closest rate and the bridge converts.
    if (asioDev) throw new Error(`${dev.name} does not support ${sampleRate} Hz (supports ${dev.sampleRates.join(', ')}): set the sample rate in the ASIO driver's control panel or change the mixer's rate`);
    sampleRate = nearestRate(dev.sampleRates, sampleRate);
  }
  const conv = createResampler(out ? wantRate : sampleRate, out ? sampleRate : wantRate, ch);   // output: mixer -> device, input: device -> mixer
  const p = plan(frameSize, { api: dev.api, sampleRate, channels: ch });
  const fmt = RtAudioFormat.RTAUDIO_SINT16 !== undefined ? RtAudioFormat.RTAUDIO_SINT16 : 2;
  const sf = streamFlags(mod, dev, exclusive), flags = sf.flags;
  // Audify keeps every block handed to write() in an unbounded queue until the device takes it. A WASAPI stream starts consuming only after the audio
  // client is initialised (tens to hundreds of ms, longer for Bluetooth and USB endpoints), and the mixer's clock and the device's clock never match exactly,
  // so that queue only grows: the sound came out later and later (and the queue used memory). Audify tells when a block is taken (the frame-output
  // callback), so the bridge counts the blocks waiting and, above a limit, drops the backlog and keeps only the newest few blocks (a short gap instead of an
  // ever-growing delay). Used for WASAPI; `queueControl: true / false` forces it on / off.
  const control = out && (queueControl === undefined ? dev.api === 'WINDOWS_WASAPI' : !!queueControl);
  let lastErr = null;
  for (const fs of p.candidates) {
    let rt;
    try { rt = openRtAudio(RtAudio, Api, dev.api); } catch (e) { lastErr = e; break; }      // no other API is ever substituted for the device's own
    let warnings = 0, lastWarning = '', queued = 0;
    try {
      const params = { deviceId: dev.rtId, nChannels: ch, firstChannel: 0 };
      const actual = rt.openStream(out ? params : null, out ? null : params, fmt, sampleRate, fs, 'Audio Mixer',
        out ? null : (pcm => { if (!onData) return; const b = Buffer.from(pcm); onData(conv ? conv(b) : b); }), control ? (() => { if (queued > 0) queued--; }) : null, flags, errorHandler(mod, e => onError && onError(e), e => { warnings++; lastWarning = e.message; if (onWarning) onWarning(e); }));
      rt.start();
      const used = Number.isInteger(actual) && actual > 0 ? actual : fs;
      if (!(used > 0)) throw new Error('the driver did not report its buffer size');
      const frameBytes = used * ch * 2;
      let pending = Buffer.alloc(0), dropped = 0, trimmed = 0;
      const blockMs = used / sampleRate * 1000, keep = Math.max(2, Math.round(30 / blockMs)), limit = Math.max(keep + 4, Math.ceil(150 / blockMs)), recent = [];
      return {
        frameSize: used, exclusive: sf.exclusive, sampleRate: wantRate, deviceRate: sampleRate, resampled: !!conv, channels: remap ? channels : ch, deviceChannels: ch, mixedDown: !!remap, auto: p.auto, tried: p.candidates.slice(0, p.candidates.indexOf(fs) + 1),
        latencyMs: Math.round(used / sampleRate * 10000) / 10,
        get dropped() { return dropped; }, get trimmed() { return trimmed; }, get warnings() { return warnings; }, get lastWarning() { return lastWarning; },
        // RtAudio needs whole blocks of exactly frameSize frames.
        write(buf) {
          if (remap) buf = remap(buf);
          if (conv) buf = conv(buf);
          pending = pending.length ? Buffer.concat([pending, buf]) : buf;
          if (pending.length > frameBytes * 16) { dropped += pending.length - frameBytes * 16; pending = pending.subarray(pending.length - frameBytes * 16); }
          while (pending.length >= frameBytes) {
            const block = Buffer.from(pending.subarray(0, frameBytes));
            pending = pending.subarray(frameBytes);
            if (control && typeof rt.clearOutputQueue === 'function') {
              recent.push(block); if (recent.length > keep) recent.shift();
              if (queued >= limit) {                                   // too late already: drop what waits, play the newest blocks
                rt.clearOutputQueue(); trimmed++; queued = 0;
                for (const b of recent) { rt.write(b); queued++; }
                continue;
              }
              queued++;
            }
            rt.write(block);
          }
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
function openDuplexRaw({ mod, dev, inChannels, outChannels, sampleRate, frameSize, onData, onError, onWarning, exclusive }) {
  const { RtAudio, RtAudioFormat = {}, RtAudioStreamFlags = {} } = mod, Api = apiEnum(mod);
  if (!dev.maxInputChannels || !dev.maxOutputChannels) throw new Error(`${dev.name} cannot read and write at once`);
  if (outChannels > dev.maxOutputChannels) throw new Error(`${dev.name} has only ${dev.maxOutputChannels} output channel(s)`);
  const ci = Math.min(inChannels, dev.maxInputChannels);
  if (dev.sampleRates && dev.sampleRates.length && !dev.sampleRates.includes(sampleRate)) {
    throw new Error(`${dev.name} does not support ${sampleRate} Hz (supports ${dev.sampleRates.join(', ')})`);
  }
  const p = plan(frameSize, { api: dev.api, sampleRate, channels: Math.max(ci, outChannels) });
  const fmt = RtAudioFormat.RTAUDIO_SINT16 !== undefined ? RtAudioFormat.RTAUDIO_SINT16 : 2;
  const sf = streamFlags(mod, dev, exclusive), flags = sf.flags;
  let lastErr = null;
  for (const fs of p.candidates) {
    let rt;
    try { rt = openRtAudio(RtAudio, Api, dev.api); } catch (e) { lastErr = e; break; }
    try {
      const actual = rt.openStream({ deviceId: dev.rtId, nChannels: outChannels, firstChannel: 0 }, { deviceId: dev.rtId, nChannels: ci, firstChannel: 0 },
        fmt, sampleRate, fs, 'Audio Mixer', pcm => onData && onData(Buffer.from(pcm)), null, flags, errorHandler(mod, e => onError && onError(e), e => { if (onWarning) onWarning(e); }));
      rt.start();
      const used = Number.isInteger(actual) && actual > 0 ? actual : fs;
      if (!(used > 0)) throw new Error('the driver did not report its buffer size');
      const frameBytes = used * outChannels * 2;
      let pending = Buffer.alloc(0);
      return {
        frameSize: used, exclusive: sf.exclusive, sampleRate, inChannels: ci, outChannels, latencyMs: Math.round(used / sampleRate * 10000) / 10,
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

// ASIO: one stream per driver. A second direction on a device that is already open (the other direction) re-opens it as ONE duplex stream and
// keeps the first user attached, instead of asking the single-client driver for a second stream (which fails or stalls the first).
function openStream(o) {
  const { dev } = o;
  if (!dev || dev.api !== 'WINDOWS_ASIO') return openStreamRaw(o);
  const asio = asioOf(o.mod);
  asio.opening = (asio.opening || 0) + 1;                 // the scans leave the driver alone while it is being opened (see listDevices)
  try { return openAsioStream(o, asio); } finally { asio.opening--; }
}
function openAsioStream(o, asio) {
  const { dev, direction } = o;
  const k = asioRegKey(dev);
  let e = asio.get(k);
  if (e && e.exclusive) throw new Error(`${dev.name} is already open for reading and writing`);
  if (e && e.dirs[direction]) throw new Error(`${dev.name} is already open for ${direction === 'output' ? 'writing' : 'reading'}`);
  const user = { onData: o.onData, onError: o.onError, channels: Number(o.channels) };
  if (!e) {
    e = { dev, mod: o.mod, sampleRate: Number(o.sampleRate), frameSize: o.frameSize, first: o, dirs: {}, st: null };
    e.onData = b => { const d = e.dirs.input; if (d && d.onData) d.onData(b); };
    e.onError = err => Object.values(e.dirs).forEach(d => d.onError && d.onError(err));
    e.st = openStreamRaw({ ...o, onData: e.onData, onError: e.onError });
    asio.set(k, e);
  } else {
    if (Number(o.sampleRate) !== e.sampleRate) throw new Error(`${dev.name} is already open at ${e.sampleRate} Hz: reading and writing it together needs the same sample rate`);
    const inCh = direction === 'input' ? user.channels : e.dirs.input.channels, outCh = direction === 'output' ? user.channels : e.dirs.output.channels;
    e.st.close();
    try {
      e.st = openDuplexRaw({ mod: o.mod, dev, inChannels: inCh, outChannels: outCh, sampleRate: e.sampleRate, frameSize: o.frameSize !== undefined ? o.frameSize : e.frameSize, onData: e.onData, onError: e.onError });
    } catch (err) {
      try { e.st = openStreamRaw({ ...e.first, onData: e.onData, onError: e.onError }); } catch (_) { asio.delete(k); }   // give the first user its stream back
      throw err;
    }
  }
  e.dirs[direction] = user;
  const entry = e, out = direction === 'output';
  return {
    get exclusive() { return true; }, get frameSize() { return entry.st.frameSize; }, get sampleRate() { return entry.st.sampleRate; }, get latencyMs() { return entry.st.latencyMs; }, get auto() { return entry.st.auto; }, get tried() { return entry.st.tried; }, get deviceRate() { return entry.st.deviceRate; }, get resampled() { return entry.st.resampled; },
    get channels() { return out ? (entry.st.outChannels || entry.st.channels) : (entry.st.inChannels || entry.st.channels); }, duplex: !!entry.st.inChannels,
    write(buf) { return entry.st.write ? entry.st.write(buf) : undefined; },
    close() {
      if (entry.dirs[direction] !== user) return;
      delete entry.dirs[direction];
      if (!Object.keys(entry.dirs).length) { try { entry.st.close(); } finally { if (asio.get(k) === entry) asio.delete(k); } }
    },
  };
}

// One duplex stream (the /ws/duplex endpoint). On ASIO it owns the driver: any other stream on that device is refused until it closes.
function openDuplex(o) {
  const { dev } = o;
  if (!dev || dev.api !== 'WINDOWS_ASIO') return openDuplexRaw(o);
  const asio = asioOf(o.mod);
  const k = asioRegKey(dev);
  if (asio.has(k)) throw new Error(`${dev.name} is already open: close it first, or read and write it together from the same page`);
  asio.opening = (asio.opening || 0) + 1;
  let st; try { st = openDuplexRaw(o); } finally { asio.opening--; }
  const entry = { exclusive: true, dev, st, dirs: {} };
  asio.set(k, entry);
  const close = st.close;
  st.close = () => { try { close(); } finally { if (asio.get(k) === entry) asio.delete(k); } };
  return st;
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
  const problems = (r.problems || []).map(p => ({ ...p, hint: 'An ASIO driver could not be queried (RtApiAsio::probeDeviceInfo): it is open in another program (DAW, the driver\'s control panel) or its interface is unplugged. Close the other program or replug the interface; the last known devices are kept for ' + ASIO_GRACE_MS / 1000 + ' s.' }));
  return {
    installed: true, hostApis: r.hostApis, ...(problems.length ? { problems } : {}),
    devices: r.devices.map(d => ({ ...d, recommended: ['input', 'output'].reduce((o, k) => {
      const ch = k === 'input' ? d.maxInputChannels : d.maxOutputChannels;
      if (ch) o[k] = recommendFrameSize({ api: d.api, sampleRate: d.defaultSampleRate, channels: ch });
      return o;
    }, {}) })),
  };
}

module.exports = { openStreamRaw, openRtAudio, streamFlags, mergeAsio, ASIO_GRACE_MS, isWarning, remapChannels, _asioOf: asioOf, createResampler, nearestRate, loadAudify, loadProblem, API_NAMES, apiKey, recommendFrameSize, frameCandidates, plan, listDevices, detectAudify, openStream, openDuplex, pickAudifyDevice, describe, isPow2, AUDIFY_BASE, MIN_FRAMES, MAX_FRAMES };
