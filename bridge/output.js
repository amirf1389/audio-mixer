'use strict';
// ASIO / host-API audio output through PortAudio (naudiodon2). Receives interleaved Int16 PCM from the page.
const { claim } = require('./asio-lock');
const audify = require('./audify');
function loadPortAudio() { return require('naudiodon2'); }
const SAMPLE_RATES = [44100, 48000, 88200, 96000, 176400, 192000];

function pickDevice(pa, wantedId, channels) {
  const devices = pa.getDevices();
  if (Number.isInteger(wantedId)) return devices.find(d => d.id === wantedId) || null;
  return devices.find(d => /asio/i.test(d.hostAPIName) && d.maxOutputChannels >= channels) || null;
}

// One output session per WebSocket connection. `load` is injectable for tests.
function createSession(conn, load = loadPortAudio, loadA = audify.loadAudify) {
  let io = null;
  let lock = null;   // ASIO is single-client: see asio-lock.js
  let blocked = false;
  let frameBytes = 4; // Int16 * channels; writes must be whole frames or channels swap

  const stop = () => {
    if (io) { try { io.quit(); } catch (_) { /* already closed */ } io = null; }
    if (lock) { lock.release(); lock = null; }
    blocked = false;
  };

  // Audify (RtAudio) engine: chosen by an Audify device id (>= 1000), engine: "audify", or when PortAudio is not installed.
  function startAudify(opts) {
    let list;
    try { list = audify.listDevices(loadA); } catch (_) { list = null; }
    if (!list) return conn.send(JSON.stringify({ type: 'error', message: 'PortAudio not installed: run "npm install" in bridge/ (needs naudiodon2 or audify)' }));
    const channels = Math.min(Math.max(parseInt(opts.channels, 10) || 2, 1), 32);
    const sampleRate = SAMPLE_RATES.includes(Number(opts.sampleRate)) ? Number(opts.sampleRate) : 48000;
    const wanted = Number.isInteger(opts.deviceId) && opts.deviceId >= 0 ? opts.deviceId : null;
    const dev = audify.pickAudifyDevice(list.devices, wanted, 'output', channels) || (wanted === null ? list.devices.find(d => d.isDefaultOutput) : null);
    if (!dev) return conn.send(JSON.stringify({ type: 'error', message: wanted !== null ? 'device not found' : 'no output device found' }));
    lock = claim({ id: dev.id, name: dev.name, hostAPIName: dev.hostAPIName });
    if (!lock.ok) { const msg = lock.message; lock = null; return conn.send(JSON.stringify({ type: 'error', message: msg })); }
    try {
      const st = audify.openStream({ mod: loadA(), dev, direction: 'output', channels, sampleRate, frameSize: opts.frameSize,
        onError: e => { conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) })); stop(); } });
      io = { quit: () => st.close(), write: b => st.write(b) };
      frameBytes = 2 * channels;
      conn.send(JSON.stringify({ type: 'started', engine: 'audify', device: dev.name, hostApi: dev.hostAPIName, sampleRate, channels, frameSize: st.frameSize, latencyMs: st.latencyMs, autoFrameSize: st.auto }));
    } catch (e) {
      stop();
      conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) }));
    }
  }

  function start(opts) {
    stop();
    const id = Number.isInteger(opts.deviceId) ? opts.deviceId : null;
    const forceAudify = opts.engine === 'audify' || (id !== null && id >= audify.AUDIFY_BASE);
    let pa = null;
    if (!forceAudify) { try { pa = load(); } catch (_) { pa = null; } }
    if (!pa && opts.engine !== 'naudiodon' && (id === null || id >= audify.AUDIFY_BASE)) return startAudify(opts);
    if (!pa) {
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
      conn.send(JSON.stringify({ type: 'started', engine: 'naudiodon', device: dev ? dev.name : 'default', hostApi: dev ? dev.hostAPIName : 'default', sampleRate, channels }));
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
