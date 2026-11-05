'use strict';
// DEVICE SIMULATOR: a software model of real audio interfaces with the same API as naudiodon2 (PortAudio), so the real bridge code
// (input.js, output.js, duplex.js, detect.js) and the real page run against it end to end without hardware. Off unless AMFX_SIM=1 or the
// SIMULATED DEVICES switch (POST /api/sim) turns it on; every simulated device is named "[SIM] ..." in the diagnostics log.
//
// BASE PROFILE: BEHRINGER UM2 (USB, PCM2902 "USB Audio CODEC" chip): 2 in / 2 out, input 1 = mono mic/instrument (signal on channel 1
// only), 44.1 / 48 kHz only, a SINGLE-CLIENT ASIO driver "BEHRINGER USB AUDIO" (a second client gets silence, or the busy error), the same
// hardware also as the Windows (WASAPI) endpoints "Microphone (USB Audio CODEC)" / "Speakers (USB Audio CODEC)". Every other profile is
// the base with parameters changed (derive()).
const { EventEmitter } = require('node:events');

const BASE = {
  key: 'um2', label: 'Behringer UM2',
  asio: { name: 'BEHRINGER USB AUDIO', single: true, busyMode: 'zeros' },   // busyMode: 'zeros' (opens, delivers silence) | 'error' (refuses)
  wasapi: [{ name: 'Microphone (USB Audio CODEC)', in: 2, out: 0 }, { name: 'Speakers (USB Audio CODEC)', in: 0, out: 2 }],
  inputs: 2, outputs: 2, rates: [44100, 48000], defaultRate: 48000,
  signal: [{ wave: 'sine', freq: 220, db: -18 }, null],   // per input channel: null = silence (UM2: the line input is not connected)
  bufferFrames: [64, 128, 256, 512, 1024],
};
function derive(over) { return { ...BASE, ...over, asio: over.asio === null ? null : { ...BASE.asio, ...(over.asio || {}) } }; }

const PROFILES = {
  um2: BASE,
  umc404: derive({ key: 'umc404', label: 'Behringer UMC404HD', asio: { name: 'UMC ASIO Driver' }, inputs: 4, outputs: 4, rates: [44100, 48000, 88200, 96000, 176400, 192000],
    wasapi: [{ name: 'Microphone (BEHRINGER UMC 404HD 192k)', in: 4, out: 0 }, { name: 'Speakers (BEHRINGER UMC 404HD 192k)', in: 0, out: 4 }],
    signal: [{ wave: 'sine', freq: 330, db: -20 }, { wave: 'sine', freq: 440, db: -20 }, null, null] }),
  realtek: derive({ key: 'realtek', label: 'Realtek onboard', asio: null, inputs: 2, outputs: 2, rates: [44100, 48000, 96000],
    wasapi: [{ name: 'Microphone Array (Realtek(R) Audio)', in: 2, out: 0 }, { name: 'Speakers (Realtek(R) Audio)', in: 0, out: 2 }, { name: 'Headphones (Realtek(R) Audio)', in: 0, out: 2 }],
    signal: [{ wave: 'noise', db: -50 }, { wave: 'noise', db: -50 }] }),
  focusrite: derive({ key: 'focusrite', label: 'Focusrite 2i2', asio: { name: 'Focusrite USB ASIO', single: true, busyMode: 'error' },
    wasapi: [{ name: 'Analogue 1 + 2 (Focusrite USB Audio)', in: 2, out: 0 }, { name: 'Speakers (Focusrite USB Audio)', in: 0, out: 2 }],
    rates: [44100, 48000, 88200, 96000, 176400, 192000], signal: [{ wave: 'sine', freq: 261.6, db: -24 }, { wave: 'sine', freq: 392, db: -24 }] }),
  usbdac: derive({ key: 'usbdac', label: 'USB DAC', asio: { name: 'USB DAC ASIO', single: true }, inputs: 0, outputs: 2,
    rates: [44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000], wasapi: [{ name: 'Speakers (USB DAC)', in: 0, out: 2 }], signal: [] }),
  bluetooth: derive({ key: 'bluetooth', label: 'Bluetooth headset', asio: null, inputs: 1, outputs: 2, rates: [16000, 48000],
    wasapi: [{ name: 'Headphones (WH-1000XM4 Stereo)', in: 0, out: 2, rates: [48000] }, { name: 'Headset (WH-1000XM4 Hands-Free AG Audio)', in: 1, out: 1, rates: [16000] }],
    signal: [{ wave: 'noise', db: -40 }] }),
  loopback: derive({ key: 'loopback', label: 'Stereo Mix', asio: null, inputs: 2, outputs: 0, wasapi: [{ name: 'Stereo Mix (Realtek(R) Audio)', in: 2, out: 0 }],
    signal: [{ wave: 'sine', freq: 500, db: -30 }, { wave: 'sine', freq: 500, db: -30 }] }),
  asio4all: derive({ key: 'asio4all', label: 'ASIO4ALL wrapper', asio: { name: 'ASIO4ALL v2', single: true, wrapper: true }, wasapi: [] }),
};

const state = { enabled: false, profiles: ['um2', 'realtek'], faults: {}, open: new Map(), xruns: 0 };
function fromEnv(env = process.env) {
  state.enabled = env.AMFX_SIM === '1';
  if (env.AMFX_SIM_PROFILES) state.profiles = env.AMFX_SIM_PROFILES.split(',').map(s => s.trim()).filter(p => PROFILES[p]);
  if (env.AMFX_SIM_FAULTS) try { state.faults = JSON.parse(env.AMFX_SIM_FAULTS) || {}; } catch (_) { state.faults = {}; }
}
fromEnv();

// faults per profile key: { asioHeldByOtherApp: true (the ASIO driver is held elsewhere: opens deliver silence or the busy error),
//   unplugAfterMs: n, rateChangeAfterMs: n, xrunEvery: n (drop every n-th block), silentInput: true, wrongChannel: true }
function configure({ enabled, profiles, faults } = {}) {
  if (typeof enabled === 'boolean') state.enabled = enabled;
  if (Array.isArray(profiles)) state.profiles = profiles.filter(p => PROFILES[p]);
  if (faults && typeof faults === 'object') state.faults = faults;
  return status();
}
const status = () => ({ enabled: state.enabled, profiles: state.profiles.slice(), faults: JSON.parse(JSON.stringify(state.faults)), available: Object.keys(PROFILES), open: state.open.size, xruns: state.xruns });

// the device list in naudiodon2's shape; ids are stable for a given profile list
function devices() {
  const out = [];
  let id = 0;
  for (const key of state.profiles) {
    const p = PROFILES[key];
    if (p.asio) out.push({ id: id++, name: p.asio.name, hostAPIName: 'ASIO', maxInputChannels: p.inputs, maxOutputChannels: p.outputs, defaultSampleRate: p.defaultRate,
      defaultLowInputLatency: 0.005, defaultLowOutputLatency: 0.005, defaultHighInputLatency: 0.04, defaultHighOutputLatency: 0.04, simulated: true, profile: key, kind: 'asio' });
    for (const w of p.wasapi) out.push({ id: id++, name: w.name, hostAPIName: 'Windows WASAPI', maxInputChannels: w.in, maxOutputChannels: w.out, defaultSampleRate: (w.rates || p.rates).includes(48000) ? 48000 : (w.rates || p.rates)[0],
      defaultLowInputLatency: 0.01, defaultLowOutputLatency: 0.01, defaultHighInputLatency: 0.05, defaultHighOutputLatency: 0.05, simulated: true, profile: key, kind: 'wasapi', rates: w.rates || p.rates });
  }
  return out;
}

function makeError(msg, code) { const e = new Error(msg); e.code = code; return e; }
// one block of interleaved Int16 for `channels` channels from the profile's per-channel signal
function synth(profile, dev, channels, frames, t0, rate, faults) {
  const buf = Buffer.alloc(frames * channels * 2);
  if (faults.silentInput) return buf;
  for (let c = 0; c < channels; c++) {
    let src = (profile.signal || [])[faults.wrongChannel ? (c === 1 ? 0 : 1) : c];
    if (dev.kind === 'wasapi' && dev.maxInputChannels === 1) src = (profile.signal || [])[0];
    if (!src) continue;
    const amp = Math.pow(10, src.db / 20) * 32767;
    for (let i = 0; i < frames; i++) {
      const v = src.wave === 'noise' ? (Math.random() * 2 - 1) * amp : Math.sin(2 * Math.PI * src.freq * (t0 + i) / rate) * amp;
      buf.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(v))), (i * channels + c) * 2);
    }
  }
  return buf;
}

// naudiodon2-compatible AudioIO: { inOptions } (capture), { outOptions } (playback) or both (duplex)
class AudioIO extends EventEmitter {
  constructor(opts = {}) {
    super();
    const io = opts.inOptions || opts.outOptions; if (!io) throw makeError('no inOptions / outOptions', 'EINVAL');
    const list = devices(), dev = list.find(d => d.id === io.deviceId) || (io.deviceId === -1 ? list.find(d => (opts.inOptions ? d.maxInputChannels : d.maxOutputChannels) > 0 && d.kind === 'wasapi') : null);
    if (!dev) throw makeError('PortAudio error -9996: Invalid device', 'paInvalidDevice');
    const p = PROFILES[dev.profile], faults = state.faults[dev.profile] || {}, rate = io.sampleRate || dev.defaultSampleRate;
    const rates = dev.rates || p.rates;
    if (!rates.includes(rate)) throw makeError(`PortAudio error -9997: Invalid sample rate (${rate} Hz; ${dev.name} supports ${rates.join(', ')})`, 'paInvalidSampleRate');
    const want = opts.inOptions ? dev.maxInputChannels : dev.maxOutputChannels;
    if ((io.channelCount || 2) > want) throw makeError(`PortAudio error -9998: Invalid number of channels (${io.channelCount} > ${want})`, 'paInvalidChannelCount');
    this.dev = dev; this.profile = p; this.faults = faults; this.rate = rate; this.channels = io.channelCount || 2; this.capture = !!opts.inOptions; this.t = 0; this.blocks = 0;
    this.silent = false;
    // single-client ASIO: a second client of the same driver (or a driver held by another program) gets silence or the busy error
    if (dev.kind === 'asio' && p.asio && p.asio.single) {
      const holder = [...state.open.values()].find(o => o !== this && o.dev.kind === 'asio' && o.dev.name === dev.name);
      if (holder || faults.asioHeldByOtherApp) {
        if (p.asio.busyMode === 'error') throw makeError(`ASIO error: ${dev.name} is in use by another program (ASE_NotPresent): the driver serves one program at a time`, 'ASE_NotPresent');
        this.silent = true;   // what the real UM2 driver does: the open succeeds, every buffer is zero
      }
    }
    if (p.asio && p.asio.wrapper && dev.kind === 'asio') this.wrapper = true;
  }
  start() {
    state.open.set(this, this);
    const frames = Math.round(this.rate / 50), every = this.faults.xrunEvery || 0, t0 = Date.now();
    this.timer = setInterval(() => {
      this.blocks++;
      if (this.faults.unplugAfterMs && Date.now() - t0 >= this.faults.unplugAfterMs) { this.emit('error', makeError(`${this.dev.name}: device unplugged (PortAudio error -9985: Device unavailable)`, 'paDeviceUnavailable')); this.quit(); return; }
      if (every && this.blocks % every === 0) { state.xruns++; this.t += frames; return; }   // a dropped block (xrun)
      if (this.capture) this.emit('data', this.silent ? Buffer.alloc(frames * this.channels * 2) : synth(this.profile, this.dev, this.channels, frames, this.t, this.rate, this.faults));
      this.t += frames;
    }, 20);
    if (this.timer.unref) this.timer.unref();
  }
  write(chunk, enc, cb) { if (typeof enc === 'function') enc(); else if (cb) cb(); return true; }   // playback: accepted and dropped
  quit(cb) { clearInterval(this.timer); state.open.delete(this); if (typeof cb === 'function') cb(); }
  abort(cb) { this.quit(cb); }
}

function portaudio() {
  return {
    simulated: true, SampleFormat16Bit: 8, SampleFormatFloat32: 1, SampleFormat24Bit: 4, SampleFormat32Bit: 2, AudioIO,
    getDevices: () => devices(),
    getHostAPIs: () => ({ defaultHostAPI: 0, HostAPIs: [{ id: 0, name: 'ASIO', type: 'ASIO' }, { id: 1, name: 'Windows WASAPI', type: 'WASAPI' }] }),
  };
}

module.exports = { PROFILES, BASE, derive, configure, status, devices, portaudio, enabled: () => state.enabled, fromEnv, closeAll: () => { for (const o of [...state.open.values()]) o.quit(); } };
