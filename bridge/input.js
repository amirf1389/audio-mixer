'use strict';
// ASIO / WASAPI / host-API audio INPUT capture through PortAudio (naudiodon2), streamed to the page as Int16 PCM.
const { claim } = require('./asio-lock');
const audify = require('./audify');
const streams = require('./streams');
const levels = require('./levels');
const universal = require('./universal');
function loadPortAudio() { return require('naudiodon2'); }

function pickInput(pa, wantedId, channels) {
  const devices = pa.getDevices();
  if (Number.isInteger(wantedId)) return devices.find(d => d.id === wantedId) || null;
  return devices.find(d => /asio/i.test(d.hostAPIName) && d.maxInputChannels >= channels) || null;
}

const CHUNK = 16384; // bytes per WebSocket frame

function createInputSession(conn, load = loadPortAudio, loadA = audify.loadAudify) {
  let io = null;
  let lock = null;   // ASIO is single-client: see asio-lock.js
  let meter = null;   // level metering of this stream
  let unreg = null;  // live status registry

  const stop = () => {
    if (io) { try { io.quit(); } catch (_) { /* already closed */ } io = null; }
    if (lock) { lock.release(); lock = null; }
    if (unreg) { unreg(); unreg = null; }
    if (meter) { meter.stop(); meter = null; }
  };

  // Audify (RtAudio) engine: chosen by an Audify device id (>= 1000), engine: "audify", or when PortAudio is not installed.
  function startAudify(opts) {
    let list;
    try { list = audify.listDevices(loadA); } catch (_) { list = null; }
    if (!list) return conn.send(JSON.stringify({ type: 'error', message: 'PortAudio not installed: run "npm install" in bridge/ (needs naudiodon2 or audify)' }));
    const channels = Math.min(Math.max(parseInt(opts.channels, 10) || 2, 1), 32);
    const sampleRate = parseInt(opts.sampleRate, 10) || 48000;
    const wanted = Number.isInteger(opts.deviceId) ? opts.deviceId : null;
    const dev = audify.pickAudifyDevice(list.devices, wanted, 'input', channels) || (wanted === null ? list.devices.find(d => d.isDefaultInput) : null);
    if (!dev) return conn.send(JSON.stringify({ type: 'error', message: wanted !== null ? 'device not found' : 'no input device found' }));
    lock = claim({ id: dev.id, name: dev.name, hostAPIName: dev.hostAPIName });
    if (!lock.ok) { const msg = lock.message; lock = null; return conn.send(JSON.stringify({ type: 'error', message: msg })); }
    try {
      const st = audify.openStream({ mod: loadA(), dev, direction: 'input', channels, sampleRate, frameSize: opts.frameSize,
        onData: chunk => { if (meter) meter.push(chunk); for (let i = 0; i < chunk.length; i += CHUNK) conn.sendBinary(chunk.subarray(i, i + CHUNK)); },
        onError: e => { conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) })); stop(); } });
      io = { quit: () => st.close() };
      const si = { direction: 'input', engine: 'audify', device: dev.name, hostApi: dev.hostAPIName, sampleRate, channels: st.channels, frameSize: st.frameSize, latencyMs: st.latencyMs };
      unreg = streams.add(si); meter = levels.attach(conn, { ...si, sid: unreg.id });
      conn.send(JSON.stringify({ type: 'started', ...(opts.universal ? { universal: true } : {}), engine: 'audify', device: dev.name, hostApi: dev.hostAPIName, sampleRate, channels: st.channels, frameSize: st.frameSize, latencyMs: st.latencyMs, autoFrameSize: st.auto }));
    } catch (e) {
      stop();
      conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) }));
    }
  }

  function start(opts) {
    stop();
    // "Universal ASIO driver": detect every device on both engines and open the best one (ASIO first), see universal.js
    if (opts.universal === true || opts.deviceId === 'universal') {
      const choice = universal.resolve({ direction: 'input', channels: opts.channels, engine: opts.engine }, load, loadA, audify);
      if (!choice) return conn.send(JSON.stringify({ type: 'error', message: 'Universal ASIO driver: no input device was found' }));
      opts = { ...opts, deviceId: choice.id, engine: choice.engine, universal: true };
    }
    const id = Number.isInteger(opts.deviceId) ? opts.deviceId : null;
    const forceAudify = opts.engine === 'audify' || (id !== null && id >= audify.AUDIFY_BASE);
    let pa = null;
    if (!forceAudify) { try { pa = load(); } catch (_) { pa = null; } }
    if (!pa && opts.engine !== 'naudiodon' && (id === null || id >= audify.AUDIFY_BASE)) return startAudify(opts);
    if (!pa) {
      return conn.send(JSON.stringify({ type: 'error', message: 'PortAudio not installed: run "npm install" in bridge/ (needs naudiodon2)' }));
    }
    const channels = Math.min(Math.max(parseInt(opts.channels, 10) || 2, 1), 32);
    const sampleRate = parseInt(opts.sampleRate, 10) || 48000;
    const dev = pickInput(pa, Number.isInteger(opts.deviceId) ? opts.deviceId : null, channels);
    if (!dev && Number.isInteger(opts.deviceId)) return conn.send(JSON.stringify({ type: 'error', message: 'device not found' }));
    if (dev) {
      lock = claim(dev);
      if (!lock.ok) { const msg = lock.message; lock = null; return conn.send(JSON.stringify({ type: 'error', message: msg })); }
    }
    try {
      io = new pa.AudioIO({ inOptions: {
        channelCount: channels, sampleFormat: pa.SampleFormat16Bit, sampleRate,
        deviceId: dev ? dev.id : -1, closeOnError: true,
      } });
      io.on('error', e => { conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) })); stop(); });
      io.on('data', chunk => { if (meter) meter.push(chunk); for (let i = 0; i < chunk.length; i += CHUNK) conn.sendBinary(chunk.subarray(i, i + CHUNK)); });
      io.start();
      const si = { direction: 'input', engine: 'naudiodon', device: dev ? dev.name : 'default', hostApi: dev ? dev.hostAPIName : 'default', sampleRate, channels };
      unreg = streams.add(si); meter = levels.attach(conn, { ...si, sid: unreg.id });
      conn.send(JSON.stringify({ type: 'started', ...(opts.universal ? { universal: true } : {}), engine: 'naudiodon', device: dev ? dev.name : 'default', hostApi: dev ? dev.hostAPIName : 'default', sampleRate, channels }));
    } catch (e) {
      stop();
      conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) }));
    }
  }

  return {
    onText(text) {
      let m; try { m = JSON.parse(text); } catch (_) { return; }
      if (m.type === 'start') start(m); else if (m.type === 'stop') { stop(); conn.send(JSON.stringify({ type: 'stopped' })); }
    },
    onClose: stop,
  };
}

module.exports = { createInputSession };
