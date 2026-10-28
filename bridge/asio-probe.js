'use strict';
// ASIO driver facts read by the native engine through the Steinberg ASIO SDK interface (audiomixer-native --probe-asio), not through RtAudio.
// The probe prints a JSON array: one object per registered driver with its real input / output channel counts and the sample rates it accepts.
// Only Windows has ASIO; elsewhere the probe is not run.
const { execFile } = require('node:child_process');

// A probe reply is trusted only in this shape. Anything else is reported as an error and no driver is listed.
function parseProbe(text) {
  let list;
  try { list = JSON.parse(String(text).trim()); } catch (_) { return { ok: false, error: 'the native probe did not print JSON', drivers: [] }; }
  if (!Array.isArray(list)) return { ok: false, error: 'the native probe did not print a list of drivers', drivers: [] };
  const drivers = [];
  for (const d of list) {
    if (!d || typeof d !== 'object' || typeof d.name !== 'string') continue;
    drivers.push({
      name: d.name,
      clsid: typeof d.clsid === 'string' ? d.clsid : '',
      inChannels: Number.isInteger(d.inChannels) && d.inChannels > 0 ? d.inChannels : 0,
      outChannels: Number.isInteger(d.outChannels) && d.outChannels > 0 ? d.outChannels : 0,
      sampleRates: Array.isArray(d.sampleRates) ? d.sampleRates.filter(r => Number.isFinite(r) && r > 0) : [],
      ...(typeof d.error === 'string' && d.error ? { error: d.error } : {}),
    });
  }
  return { ok: true, drivers };
}

// Runs the native engine's probe. runner(exe, args) -> Promise<stdout>; injectable for tests.
async function probeAsio({ exe = 'audiomixer-native.exe', platform = process.platform, timeout = 20000, runner } = {}) {
  if (platform !== 'win32') return { ok: false, error: 'ASIO is only available on Windows', drivers: [] };
  const run = runner || ((file, args) => new Promise((resolve, reject) => {
    execFile(file, args, { timeout, windowsHide: true, maxBuffer: 1024 * 1024 }, (err, stdout) => (err && !stdout ? reject(err) : resolve(String(stdout || ''))));
  }));
  let text;
  try { text = await run(exe, ['--probe-asio']); } catch (e) { return { ok: false, error: `the native engine could not run: ${String(e && e.message || e).split('\n')[0]}`, drivers: [] }; }
  return parseProbe(text);
}

module.exports = { probeAsio, parseProbe };
