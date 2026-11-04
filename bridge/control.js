'use strict';
// Remote control of the bridge's own audio: which ASIO driver an output stream opens when the page names none, and a 32-channel fader board
// (volume, mute, linked pairs) applied to what the bridge sends to the driver, with the meters of the open streams.
//   GET  /api/audio-devices                         the ASIO drivers on this PC and the selected one
//   POST /api/select-device  { deviceName }         (X-Mixer-Action: select-device) the ASIO driver new output streams open
//   WebSocket /ws/control (this PC's pages only)
//     in:  { type: 'SET_VOLUME',   data: { channel: 0-31, value: 0-1 } }       linear gain, 1 = unity
//          { type: 'FADER_CHANGE', channel: 0-31, value: -90 (off) to +10 }    the same fader in dB
//          { type: 'SET_MUTE',     data: { channel, muted: true | false } }
//          { type: 'TOGGLE_LOCK',  data: { ch1, ch2 } }                        links two channels (a stereo pair): moving one moves the other; again unlinks
//     out: { type: 'INIT', data: { volumes, mutes, locks } } on connect, then to every connected page:
//          VOLUME_CHANGED { channel, value }, MUTE_CHANGED { channel, muted }, LOCK_CHANGED { locks }, METERS [ { id, device, peak, rms, ... } ] about 12 times a second
// The faders apply to output streams (/ws/output), to the output of duplex streams (/ws/duplex), and to a duplex stream's monitor (input straight to output).
// The selection and the board live in this process only: a restart starts at unity gain, unmuted, unlinked, with no selection.
const levels = require('./levels');

const MAX_CH = 32, MIN_DB = -90, MAX_DB = 10, MAX_GAIN = Math.pow(10, MAX_DB / 20);
let selected = null;
const volumes = new Float64Array(MAX_CH), mutes = new Uint8Array(MAX_CH), locks = new Int32Array(MAX_CH);
let unity = true;
const sessions = new Set();

function select(name, available) {
  if (typeof name !== 'string' || !name.trim()) throw Object.assign(new Error('deviceName is required'), { status: 400 });
  const want = name.trim(), hit = (available || []).find(n => String(n).toLowerCase() === want.toLowerCase());
  if (!hit) throw Object.assign(new Error(`no ASIO driver named "${want}" on this PC`), { status: 404 });
  selected = hit;
  return selected;
}

const checkCh = (ch, what = 'channel') => { if (!Number.isInteger(ch) || ch < 0 || ch >= MAX_CH) throw new Error(`${what} must be an integer from 0 to ${MAX_CH - 1}`); };
const refresh = () => { unity = volumes.every(v => v === 1) && mutes.every(m => !m); };
const broadcast = msg => { const s = JSON.stringify(msg); for (const c of sessions) { try { c.send(s); } catch (_) { /* connection closing */ } } };

// Sets a channel and its linked partner; returns the channels that changed
function setVolume(channel, value) {
  checkCh(channel);
  const v = Number(value);
  if (!Number.isFinite(v) || v < 0 || v > MAX_GAIN) throw new Error(`value must be a gain from 0 to ${MAX_GAIN.toFixed(2)} (1 = unity)`);
  const changed = [channel]; volumes[channel] = v;
  if (locks[channel] >= 0) { volumes[locks[channel]] = v; changed.push(locks[channel]); }
  refresh();
  changed.forEach(ch => broadcast({ type: 'VOLUME_CHANGED', data: { channel: ch, value: v } }));
  return changed;
}
function setFader(channel, db) {
  const v = Number(db);
  if (!Number.isFinite(v) || v < MIN_DB || v > MAX_DB) throw new Error(`value must be a level in dB from ${MIN_DB} to ${MAX_DB}`);
  return setVolume(channel, v <= MIN_DB ? 0 : Math.pow(10, v / 20));
}
function setMute(channel, muted) {
  checkCh(channel);
  if (typeof muted !== 'boolean') throw new Error('muted must be true or false');
  const changed = [channel]; mutes[channel] = muted ? 1 : 0;
  if (locks[channel] >= 0) { mutes[locks[channel]] = mutes[channel]; changed.push(locks[channel]); }
  refresh();
  changed.forEach(ch => broadcast({ type: 'MUTE_CHANGED', data: { channel: ch, muted } }));
  return changed;
}
// Links ch1 and ch2 (ch2 takes ch1's volume and mute), or unlinks them when they are linked to each other. A channel linked elsewhere is unlinked from it first.
function toggleLock(ch1, ch2) {
  checkCh(ch1, 'ch1'); checkCh(ch2, 'ch2');
  if (ch1 === ch2) throw new Error('a channel cannot be linked to itself');
  if (locks[ch1] === ch2) { locks[ch1] = -1; locks[ch2] = -1; }
  else {
    for (const c of [ch1, ch2]) if (locks[c] >= 0) { locks[locks[c]] = -1; locks[c] = -1; }
    locks[ch1] = ch2; locks[ch2] = ch1;
    volumes[ch2] = volumes[ch1]; mutes[ch2] = mutes[ch1]; refresh();
    broadcast({ type: 'VOLUME_CHANGED', data: { channel: ch2, value: volumes[ch2] } });
    broadcast({ type: 'MUTE_CHANGED', data: { channel: ch2, muted: !!mutes[ch2] } });
  }
  broadcast({ type: 'LOCK_CHANGED', data: { locks: Array.from(locks) } });
  return Array.from(locks);
}

const state = () => ({ volumes: Array.from(volumes), mutes: Array.from(mutes, m => !!m), locks: Array.from(locks) });
const gain = ch => (mutes[ch] ? 0 : volumes[ch]);

// Interleaved Int16 PCM with `channels` channels: a scaled copy, or the same buffer when every channel is at unity and unmuted
function apply(buf, channels) {
  if (unity) return buf;
  const out = Buffer.allocUnsafe(buf.length), n = buf.length >> 1;
  for (let i = 0, c = 0; i < n; i++) {
    const v = Math.round(buf.readInt16LE(i * 2) * (c < MAX_CH ? gain(c) : 1));
    out.writeInt16LE(v > 32767 ? 32767 : v < -32768 ? -32768 : v, i * 2);
    if (++c === channels) c = 0;
  }
  return out;
}

// One /ws/control connection: the board's state first, then commands in and every change and the meters out
function session(conn, { interval = 80 } = {}) {
  const latest = new Map();
  const off = levels.onSnapshot(s => latest.set(s.id, s));
  const timer = setInterval(() => {
    if (!latest.size) return;
    try { conn.send(JSON.stringify({ type: 'METERS', data: [...latest.values()] })); } catch (_) { /* connection closing */ }
    latest.clear();
  }, interval);
  if (timer.unref) timer.unref();
  sessions.add(conn);
  conn.send(JSON.stringify({ type: 'INIT', data: state() }));
  const fail = message => conn.send(JSON.stringify({ type: 'error', message }));
  return {
    onText(text) {
      let m; try { m = JSON.parse(text); } catch (_) { return fail('invalid JSON'); }
      if (!m || typeof m !== 'object') return fail('a command must be a JSON object');
      const d = m.data && typeof m.data === 'object' ? m.data : {};
      try {
        if (m.type === 'SET_VOLUME') setVolume(d.channel, d.value);
        else if (m.type === 'FADER_CHANGE') setFader(m.channel, m.value);
        else if (m.type === 'SET_MUTE') setMute(d.channel, d.muted);
        else if (m.type === 'TOGGLE_LOCK') toggleLock(d.ch1, d.ch2);
        else fail('unknown command: use SET_VOLUME, FADER_CHANGE, SET_MUTE or TOGGLE_LOCK');
      } catch (e) { fail(e.message); }
    },
    onBinary() {},
    onClose() { clearInterval(timer); off(); sessions.delete(conn); },
  };
}

function reset() { selected = null; volumes.fill(1); mutes.fill(0); locks.fill(-1); unity = true; }
reset();

module.exports = { select, selected: () => selected, setVolume, setFader, setMute, toggleLock, state, gain, apply, session, reset, MAX_CH, MIN_DB, MAX_DB };
