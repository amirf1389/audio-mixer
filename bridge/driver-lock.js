'use strict';
// AI DRIVER LOCK: for each interface, the driver path that actually worked is remembered and used first next time.
//   The ladder, best first: ASIO (the device's own driver, never a wrapper such as ASIO4ALL when the device has one) -> WASAPI EXCLUSIVE
//   (only when native/wasapi-exclusive is built and wired; until then this step is reported as not available, never pretended) ->
//   WASAPI SHARED (the same hardware's Windows endpoint) -> DirectSound -> MME.
//   When a read or write had to fall back (ASIO held by another program, ASIO delivering silence, ASIO refused), the path that worked is
//   LOCKED for that interface: the next start goes straight there instead of fighting the busy driver again. A lock is retried after
//   RETRY_MS (the other program may have closed), and FORGET (DIAGNOSTICS panel, POST /api/driver-lock) clears it at once.
//   A clean ASIO run that delivers signal locks ASIO. Every decision is written to bridge/diagnostics in plain language.
// REALTEK PIN: onboard Realtek audio exposes a "Realtek ASIO" driver that is known to be unreliable (crackles, dropouts, a stuck clock). Its
//   ASIO entry is PINNED to the same chip's Windows (WASAPI shared) endpoint by default: a start goes straight there, the ASIO driver is never
//   opened, and DIAGNOSTICS shows the pin. This is only an in-app routing choice: no driver file, registry key, Windows setting or scheduled
//   task is touched. UNPIN (DIAGNOSTICS panel, POST /api/driver-lock { pin, on:false }) lets the ASIO driver go first again; the choice is saved.
// Locks live in ~/.audio-mixer/driver-lock.json (BRIDGE_DRIVER_LOCK_FILE): device names and host APIs only, nothing personal.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const hw = require('./hw-match');

const RETRY_MS = 6 * 3600 * 1000;
const RANK = [['asio', /asio/i], ['wasapi', /wasapi/i], ['directsound', /directsound|direct sound/i], ['mme', /mme/i]];
const modeOf = api => { for (const [m, re] of RANK) if (re.test(String(api || ''))) return m; return 'other'; };
const rankOf = api => { const i = RANK.findIndex(([, re]) => re.test(String(api || ''))); return i < 0 ? RANK.length : i; };
const file = () => process.env.BRIDGE_DRIVER_LOCK_FILE || path.join(os.homedir(), '.audio-mixer', 'driver-lock.json');
const keyOf = (name, dir) => (dir === 'output' ? 'out:' : 'in:') + String(name || '').trim().toLowerCase().replace(/\s+/g, ' ').slice(0, 120);

let cache = null;
function read() { if (cache) return cache; try { const j = JSON.parse(fs.readFileSync(file(), 'utf8')); cache = j && typeof j === 'object' && j.locks ? j : { v: 1, locks: {} }; } catch (_) { cache = { v: 1, locks: {} }; } if (!cache.pins || typeof cache.pins !== 'object') cache.pins = {}; return cache; }
function write() { try { fs.mkdirSync(path.dirname(file()), { recursive: true }); fs.writeFileSync(file(), JSON.stringify(read()) + '\n', { mode: 0o600 }); } catch (_) { /* read-only disk: kept in memory */ } }
function reset() { cache = null; }   // tests: re-read the file

function exclusiveAvailable() { try { const s = require('./wasapi-exclusive').status(); return !!(s.available && s.wired); } catch (_) { return false; } }

// the same hardware through another host API: the best name match, and among equally good matches WASAPI before DirectSound before MME
function twin(name, pool, min = 10, all = null) {
  // a wrapper (ASIO4ALL, FlexASIO) is not one device: it drives the Windows default endpoint, so that endpoint is its twin (the native engine's
  // wasapiFallback uses the same rule). Default flags first, then WASAPI before DirectSound / MME; never a loopback or a hands-free headset.
  if (hw.isWrapper(name)) {
    let ok = (pool || []).filter(d => !/stereo mix|loopback|what u hear|hands-free|headset/i.test(d.name || ''));
    // never the Windows endpoint of hardware that has its OWN ASIO driver (that driver may be reading it right now: a doubled signal)
    const natives = (all || []).filter(d => /asio/i.test(d.hostAPIName) && !hw.isWrapper(d.name)).map(d => d.name);
    const free = ok.filter(d => !natives.some(n => hw.score(n, d.name) >= 100));
    if (free.length) ok = free;
    const isDef = d => +!!(d.isDefaultInput || d.isDefaultOutput);
    return ok.slice().sort((a, b) => (isDef(b) - isDef(a)) || (rankOf(a.hostAPIName) - rankOf(b.hostAPIName)))[0] || null;
  }
  let best = null, bs = 0, br = 99;
  for (const d of pool || []) {
    const s = hw.score(name, d.name), r = rankOf(d.hostAPIName);
    if (s > bs || (s === bs && s > 0 && r < br)) { best = d; bs = s; br = r; }
  }
  return bs >= min ? best : null;
}

// the ladder for one interface (an ASIO device name) over a PortAudio-style device list: [{ step, mode, device|null, available, why }]
function ladder(name, devices, dir = 'input') {
  const has = d => (dir === 'output' ? d.maxOutputChannels > 0 : d.maxInputChannels > 0);
  const list = (devices || []).filter(has), asio = list.find(d => /asio/i.test(d.hostAPIName) && d.name === name) || null;
  const others = list.filter(d => !/asio/i.test(d.hostAPIName) && !/stereo mix|loopback|what u hear/i.test(d.name || ''));   // never a loopback as a twin
  const win = hw.isWrapper(name) ? others : others.filter(d => hw.score(name, d.name) >= 10);
  const pick = mode => twin(name, win.filter(d => modeOf(d.hostAPIName) === mode), hw.isWrapper(name) ? 0 : 10, list);
  const excl = exclusiveAvailable(), was = pick('wasapi'), pin = asio ? pinOf(name) : null, pinned = !!(pin && pin.on && was);
  // ASIO4ALL is the right first step when it is the only ASIO driver here (onboard Realtek without its own driver, a laptop); it is only held
  // back when a device with its OWN ASIO driver is present (then it would open the same hardware twice)
  const nativeAsio = list.some(d => /asio/i.test(d.hostAPIName) && !hw.isWrapper(d.name));
  const wrapperOk = hw.isWrapper(name) && !nativeAsio;
  return [
    { step: 1, mode: 'asio', device: asio, available: !!asio && !pinned && (!hw.isWrapper(name) || wrapperOk), pinned, why: !asio ? 'no ASIO driver for this interface' : pinned ? 'pinned: ' + PIN_REASON : hw.isWrapper(name) && !wrapperOk ? 'a wrapper (ASIO4ALL-style): a device with its own ASIO driver is present, so that driver is used first' : null },
    { step: 2, mode: 'wasapi-exclusive', device: excl ? was : null, available: excl && !!was, why: !excl ? 'WASAPI exclusive is not built into this copy (native/wasapi-exclusive): skipped' : !was ? 'no Windows endpoint of this hardware' : null },
    { step: 3, mode: 'wasapi-shared', device: was, available: !!was, why: was ? null : 'no WASAPI endpoint of this hardware' },
    { step: 4, mode: 'directsound', device: pick('directsound'), available: !!pick('directsound'), why: pick('directsound') ? null : 'no DirectSound endpoint of this hardware' },
    { step: 5, mode: 'mme', device: pick('mme'), available: !!pick('mme'), why: pick('mme') ? null : 'no MME endpoint of this hardware' },
  ];
}

// REALTEK PIN: is this ASIO driver pinned to its Windows endpoint? { mode, reason, builtIn, on } or null when the rule does not apply
const PIN_REASON = 'Realtek onboard: its ASIO driver is known to be unreliable, so the Windows (WASAPI shared) endpoint of the same chip is used';
const pinKey = name => keyOf(name, 'input').slice(3);
function pinRule(name) { return !!name && !hw.isWrapper(name) && hw.family(name) === 'realtek'; }
function pinOf(name) {
  if (!pinRule(name)) return null;
  const user = read().pins[pinKey(name)];
  return { name: String(name).slice(0, 120), mode: 'wasapi-shared', reason: PIN_REASON, builtIn: true, on: user !== false };
}
function setPin(name, on) {
  if (!pinRule(name)) return null;
  const P = read().pins, k = pinKey(name);
  if (on) delete P[k]; else P[k] = false;   // only the user's opt-out is stored: the default stays on
  write();
  try { require('./diagnostics').info('lock', `Realtek pin ${on ? 'ON' : 'OFF'}: ${name} ${on ? 'uses its Windows (WASAPI) endpoint' : 'tries its own ASIO driver first again'}`, { device: name }); } catch (_) { /* optional */ }
  return pinOf(name);
}
const pins = devices => [...new Set((devices || []).filter(d => /asio/i.test(d.hostAPIName) && pinRule(d.name)).map(d => d.name))].map(pinOf);

// the lock of an interface, or null (none, or due for a retry of the better path)
function get(name, dir = 'input', now = Date.now()) {
  const l = read().locks[keyOf(name, dir)];
  if (!l) return null;
  if (l.mode !== 'asio' && now - l.at > RETRY_MS) return null;
  return l;
}
function learn(name, dir, { mode, device, hostApi, reason = '' }, now = Date.now()) {
  const k = keyOf(name, dir), prev = read().locks[k];
  if (prev && prev.mode === mode && prev.device === device && now - prev.at < 60000) return prev;   // nothing new: no disk write per start
  const l = read().locks[k] = { name: String(name).slice(0, 120), dir, mode, device: String(device || '').slice(0, 120), hostApi: String(hostApi || '').slice(0, 60), reason: String(reason).slice(0, 200), at: now };
  write();
  try { require('./diagnostics').info('lock', `AI driver lock: ${dir === 'output' ? 'write' : 'read'} on ${name} locked to ${mode.toUpperCase()}${device && device !== name ? ' (' + device + ')' : ''}${reason ? ': ' + reason : ''}`, { device: name, mode, via: device }); } catch (_) { /* optional */ }
  return l;
}
function forget(name, dir) {
  const L = read().locks;
  if (name === '*' || name == null) { const n = Object.keys(L).length; read().locks = {}; write(); return n; }
  const k = keyOf(name, dir || 'input'), had = !!L[k]; delete L[k]; if (!dir) delete L[keyOf(name, 'output')]; write(); return had ? 1 : 0;
}
const list = () => Object.values(read().locks).map(l => ({ ...l, retryAt: l.mode === 'asio' ? null : l.at + RETRY_MS }));

// where a start should go: { dev, locked } — the locked Windows endpoint of an ASIO interface when the lock says so and it still exists
function route(dev, devices, dir = 'input', now = Date.now()) {
  if (!dev || !/asio/i.test(dev.hostAPIName)) return { dev, locked: null };
  const has = d => (dir === 'output' ? d.maxOutputChannels > 0 : d.maxInputChannels > 0);
  const pin = pinOf(dev.name);
  if (pin && pin.on) {   // REALTEK PIN: straight to the chip's WASAPI endpoint, before any learned lock (the ASIO driver is not opened at all)
    const step = ladder(dev.name, devices, dir)[2];
    if (step && step.device && has(step.device)) return { dev: step.device, locked: { name: dev.name, dir, mode: 'wasapi-shared', device: step.device.name, hostApi: step.device.hostAPIName, reason: pin.reason, pinned: true, at: now } };
  }
  const l = get(dev.name, dir, now); if (!l || l.mode === 'asio') return { dev, locked: l };
  const to = (devices || []).find(d => d.name === l.device && modeOf(d.hostAPIName) === (l.mode === 'wasapi-shared' || l.mode === 'wasapi-exclusive' ? 'wasapi' : l.mode) && has(d));
  return to ? { dev: to, locked: l } : { dev, locked: null };
}

module.exports = { RETRY_MS, PIN_REASON, ladder, twin, route, get, learn, forget, list, modeOf, keyOf, exclusiveAvailable, file, reset, pinOf, setPin, pins };
