'use strict';
// Input guards shared by the capture paths (input.js; duplex.js can use the same):
//  - createChannelMap: "mono interfaces to channel 1 -> both sides". The Behringer UM2 (and most 2-channel USB mic interfaces) carry the mic on
//    input 1 only; read as stereo, the voice sat hard left and input 2 was silent. mode 'auto' watches the first second of real signal and,
//    when one channel carries sound and the other is dead, copies the live one to both. Explicit modes: 'ch1', 'ch2', 'stereo' (as is).
//  - createSignalWatch: an ASIO stream whose buffers are EXACT zeros for 3 s is a driver held by another client (a real input always has a noise
//    floor), and any stream below -90 dBFS for 5 s is "no signal"; each is reported once, and the first real signal is reported too.

const DEAD = 0.0003, LIVE = 0.002;   // about -70 dBFS (a dead channel) and -54 dBFS (a channel with something on it)

function peaks(buf, channels) {
  const p = new Array(channels).fill(0), n = Math.floor(buf.length / 2 / channels);
  let zero = true;
  for (let f = 0; f < n; f++) for (let c = 0; c < channels; c++) {
    const v = buf.readInt16LE((f * channels + c) * 2);
    if (v !== 0) zero = false;
    const a = Math.abs(v) / 32768; if (a > p[c]) p[c] = a;
  }
  return { p, zero, frames: n };
}

function createChannelMap(channels, mode = 'auto', onDecide) {
  let map = mode === 'ch1' ? 0 : mode === 'ch2' ? 1 : mode === 'stereo' || channels < 2 ? -1 : null;   // null = still deciding (auto)
  let seen = 0;
  if (map !== null && map >= 0 && onDecide) onDecide({ mode, source: map });
  const copy = (buf, src) => {
    const n = Math.floor(buf.length / 2 / channels), out = Buffer.alloc(buf.length);
    for (let f = 0; f < n; f++) { const v = buf.readInt16LE((f * channels + src) * 2); for (let c = 0; c < channels; c++) out.writeInt16LE(v, (f * channels + c) * 2); }
    return out;
  };
  const fn = buf => {
    if (map === -1) return buf;
    if (map === null) {
      const { p, frames } = peaks(buf, channels);
      const live = p.map((v, i) => (v >= LIVE ? i : -1)).filter(i => i >= 0), dead = p.filter(v => v < DEAD).length;
      if (live.length) {
        seen += frames;
        if (live.length === 1 && dead === channels - 1 && seen >= 4800) { map = live[0]; if (onDecide) onDecide({ mode: 'auto', source: map }); }
        else if (live.length > 1) { map = -1; if (onDecide) onDecide({ mode: 'auto', source: -1 }); return buf; }   // real stereo: leave it
      }
      return map >= 0 ? copy(buf, map) : buf;
    }
    return copy(buf, map);
  };
  fn.state = () => ({ mode, source: map });
  return fn;
}

function createSignalWatch({ rate, channels, isAsio, onSilentAsio, onNoSignal, onSignal, zeroMs = 3000, quietMs = 5000 }) {
  let zeroFrames = 0, quietFrames = 0, firedZero = false, firedQuiet = false, hadSignal = false, peak = 0;
  const fn = buf => {
    const { p, zero, frames } = peaks(buf, channels);
    const m = Math.max(0, ...p); peak = m;
    if (zero) zeroFrames += frames; else zeroFrames = 0;
    if (m < 0.00003) quietFrames += frames; else { quietFrames = 0; firedQuiet = false; }   // -90 dBFS
    if (m >= LIVE && !hadSignal) { hadSignal = true; if (onSignal) onSignal({ peakDb: 20 * Math.log10(m) }); }
    if (isAsio && !firedZero && zeroFrames >= rate * zeroMs / 1000) { firedZero = true; if (onSilentAsio) onSilentAsio({ seconds: zeroMs / 1000 }); }
    if (!firedQuiet && quietFrames >= rate * quietMs / 1000) { firedQuiet = true; if (onNoSignal) onNoSignal({ seconds: quietMs / 1000, exactZero: zeroFrames >= quietFrames }); }
  };
  fn.state = () => ({ hadSignal, peakDb: peak > 0 ? 20 * Math.log10(peak) : -Infinity });
  return fn;
}

module.exports = { createChannelMap, createSignalWatch, peaks };
