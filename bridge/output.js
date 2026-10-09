'use strict';
// ASIO / host-API audio output through PortAudio (naudiodon2). Receives interleaved Int16 PCM from the page.
const { claim } = require('./asio-lock');
function loadPortAudio() { return require('naudiodon2'); }
const SAMPLE_RATES = [44100, 48000, 88200, 96000, 176400, 192000];

function pickDevice(pa, wantedId, channels) {
  const devices = pa.getDevices();
  if (Number.isInteger(wantedId)) return devices.find(d => d.id === wantedId) || null;
  return devices.find(d => /asio/i.test(d.hostAPIName) && d.maxOutputChannels >= channels) || null;
}

// One output session per WebSocket connection. `load` is injectable for tests.
function createSession(conn, load = loadPortAudio) {
  let io = null;
  let lock = null;   // ASIO is single-client: see asio-lock.js
  let blocked = false;
  let frameBytes = 4; // Int16 * channels; writes must be whole frames or channels swap

  const stop = () => {
    if (io) { try { io.quit(); } catch (_) { /* already closed */ } io = null; }
    if (lock) { lock.release(); lock = null; }
    blocked = false;
  };

  function start(opts) {
    stop();
    let pa;
    try { pa = load(); } catch (_) {
      return conn.send(JSON.stringify({ type: 'error', message: 'PortAudio not installed: run "npm install" in bridge/ (needs naudiodon2)' }));
    }
    const channels = Math.min(Math.max(parseInt(opts.channels, 10) || 2, 1), 32);
    const sampleRate = SAMPLE_RATES.includes(Number(opts.sampleRate)) ? Number(opts.sampleRate) : 48000;
    const wanted = Number.isInteger(opts.deviceId) && opts.deviceId >= 0 ? opts.deviceId : null;
    const dev = pickDevice(pa, wanted, channels);
    if (!dev && wanted !== null) return conn.send(JSON.stringify({ type: 'error', message: 'device not found' }));
    if (dev) {
      lock = claim(dev);
      if (!lock.ok) { const msg = lock.message; lock = null; return conn.send(JSON.stringify({ type: 'error', message: msg })); }
    }
    try {
      io = new pa.AudioIO({ outOptions: {
        channelCount: channels, sampleFormat: pa.SampleFormat16Bit, sampleRate,
        deviceId: dev ? dev.id : -1, closeOnError: true,
      } });
      io.on('error', e => { conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) })); stop(); });
      io.on('drain', () => { blocked = false; });
      io.start();
      frameBytes = 2 * channels;
      conn.send(JSON.stringify({ type: 'started', device: dev ? dev.name : 'default', hostApi: dev ? dev.hostAPIName : 'default', sampleRate, channels }));
    } catch (e) {
      stop();
      conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) }));
    }
  }

  return {
    onText(text) {
      let m; try { m = JSON.parse(text); } catch (_) { return; }
      if (!m || typeof m !== 'object') return;
      if (m.type === 'start') start(m); else if (m.type === 'stop') { stop(); conn.send(JSON.stringify({ type: 'stopped' })); }
    },
    onBinary(buf) {
      if (!io || blocked) return;               // drop instead of queueing, keeps latency bounded
      if (buf.length % frameBytes !== 0) return; // partial frame would misalign every later sample
      if (io.write(buf) === false) blocked = true;
    },
    onClose: stop,
  };
}

module.exports = { createSession };
