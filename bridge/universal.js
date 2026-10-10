'use strict';
// Universal ASIO driver: one entry that detects every audio input and output source on the PC, across ASIO, WASAPI, Core Audio, JACK, ALSA ...
// (PortAudio and Audify), ranks them and picks the best driver automatically. A start message with deviceId "universal" opens it.
//   rank: ASIO (lowest latency, direct driver access) > WASAPI > Core Audio > JACK > ALSA > WDM-KS > DirectSound > MME
const crypto = require('node:crypto');
const { keyOf } = require('./interfaces');

const API_RANK = [/asio/i, /wasapi/i, /core ?audio/i, /jack/i, /alsa/i, /wdm|kernel/i, /direct ?sound/i, /mme/i];
const rankOf = api => { const i = API_RANK.findIndex(r => r.test(api || '')); return i < 0 ? API_RANK.length : i; };
const LOOPBACK = /stereo mix|what u hear|wave out mix|monitor of|loopback|virtual cable|cable output|blackhole|soundflower|voicemeeter out/i;

function channelLabels(n, prefix) {
  const names = [], pairs = [];
  for (let i = 1; i <= Math.min(n, 64); i++) names.push(`${prefix} ${i}`);
  for (let i = 1; i < Math.min(n, 64); i += 2) pairs.push(`${prefix} ${i}-${i + 1}`);
  return { names, pairs };
}

// Normalised device: { id, name, hostApi, inputs, outputs, sampleRate, sampleRates? } from either engine's list.
function entry(engine, d, direction) {
  const channels = direction === 'input' ? d.inputs : d.outputs;
  return {
    id: d.id, engine, name: d.name, hostApi: d.hostApi, channels, sampleRate: d.sampleRate || null,
    sampleRates: d.sampleRates || undefined, loopback: LOOPBACK.test(d.name), rank: rankOf(d.hostApi), key: keyOf(d.name),
    ...channelLabels(channels, direction === 'input' ? 'IN' : 'OUT'),
  };
}

const better = (a, b) => (a.loopback - b.loopback) || (a.rank - b.rank) || (b.channels - a.channels) || (a.engine === b.engine ? 0 : a.engine === 'audify' ? 1 : -1) || a.name.localeCompare(b.name);

// lists: [{ engine, devices: [{id,name,hostApi,inputs,outputs,sampleRate}] }]  ->  everything the PC offers, best first
function detectSources(lists) {
  const inputs = [], outputs = [];
  for (const l of lists.filter(Boolean)) {
    for (const d of l.devices || []) {
      if (d.inputs > 0) inputs.push(entry(l.engine, d, 'input'));
      if (d.outputs > 0) outputs.push(entry(l.engine, d, 'output'));
    }
  }
  inputs.sort(better); outputs.sort(better);
  const bestInput = inputs.find(i => !i.loopback) || null;
  // a duplex interface (same card on the same driver) is preferred for the output so that read and write share one ASIO device
  const bestOutput = (bestInput && outputs.find(o => o.key === bestInput.key && o.hostApi === bestInput.hostApi && !o.loopback)) || outputs.find(o => !o.loopback) || null;
  const apis = [...new Set([...inputs, ...outputs].map(d => d.hostApi))].sort((a, b) => rankOf(a) - rankOf(b));
  const sig = crypto.createHash('sha1').update(JSON.stringify([...inputs, ...outputs].map(d => [d.engine, d.id, d.name, d.hostApi, d.channels]).sort())).digest('hex').slice(0, 12);
  return {
    inputs, outputs, apis, best: { input: bestInput, output: bestOutput },
    asio: { inputs: inputs.filter(d => rank0(d)).length, outputs: outputs.filter(d => rank0(d)).length },
    changeId: sig,                                // changes when a device appears / disappears: the page re-scans on a new value
  };
}
const rank0 = d => d.rank === 0;

// For a session: pick the device for "deviceId: universal" out of one engine's raw device list (PortAudio or Audify shape).
function pick(devices, direction, channels) {
  const key = direction === 'output' ? 'maxOutputChannels' : 'maxInputChannels';
  const ok = (devices || []).filter(d => (d[key] || 0) >= 1);
  const full = ok.filter(d => d[key] >= channels && !LOOPBACK.test(d.name));
  const pool = full.length ? full : ok.filter(d => !LOOPBACK.test(d.name));
  pool.sort((a, b) => (rankOf(a.hostAPIName) - rankOf(b.hostAPIName)) || (b[key] - a[key]));
  return pool[0] || null;
}

// Resolves a universal start request across both engines. `loadPa` / `loadAu` are the sessions' injectable loaders.
function resolve(opts, loadPa, loadAu, audifyMod) {
  const direction = opts.direction, channels = Math.min(Math.max(parseInt(opts.channels, 10) || 2, 1), 32);
  const cands = [];
  if (opts.engine !== 'audify') {
    try { const pa = loadPa(); const d = pick(pa.getDevices(), direction, channels); if (d) cands.push({ engine: 'naudiodon', id: d.id, hostAPIName: d.hostAPIName, ch: direction === 'output' ? d.maxOutputChannels : d.maxInputChannels, name: d.name }); } catch (_) { /* engine not installed */ }
  }
  if (opts.engine !== 'naudiodon') {
    try { const r = audifyMod.listDevices(loadAu); const d = r && pick(r.devices, direction, channels); if (d) cands.push({ engine: 'audify', id: d.id, hostAPIName: d.hostAPIName, ch: direction === 'output' ? d.maxOutputChannels : d.maxInputChannels, name: d.name }); } catch (_) { /* engine not installed */ }
  }
  cands.sort((a, b) => (rankOf(a.hostAPIName) - rankOf(b.hostAPIName)) || (b.ch - a.ch) || (a.engine === 'naudiodon' ? -1 : 1));
  return cands[0] || null;
}

module.exports = { detectSources, pick, resolve, rankOf, LOOPBACK, API_RANK };
