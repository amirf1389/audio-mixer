'use strict';
// ASIO / host-API audio output through PortAudio (naudiodon2). Receives interleaved Int16 PCM from the page.
const { claim } = require('./asio-lock');
const audify = require('./audify');
const streams = require('./streams');
const levels = require('./levels');
const universal = require('./universal');
const virtual = require('./virtual');
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
  let meter = null;   // level metering of this stream
  let unreg = null;   // live status registry
  let frameBytes = 4; // Int16 * channels; writes must be whole frames or channels swap

  const stop = () => {
    if (io) { try { io.quit(); } catch (_) { /* already closed */ } io = null; }
    if (lock) { lock.release(); lock = null; }
    if (unreg) { unreg(); unreg = null; }
    if (meter) { meter.stop(); meter = null; }
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
      const si = { direction: 'output', engine: 'audify', device: dev.name, hostApi: dev.hostAPIName, sampleRate, channels: channels, frameSize: st.frameSize, latencyMs: st.latencyMs };
      unreg = streams.add(si); meter = levels.attach(conn, { ...si, sid: unreg.id });
      conn.send(JSON.stringify({ type: 'started', ...(opts.universal ? { universal: true } : {}), engine: 'audify', device: dev.name, hostApi: dev.hostAPIName, sampleRate, channels, frameSize: st.frameSize, latencyMs: st.latencyMs, autoFrameSize: st.auto }));
    } catch (e) {
      stop();
      conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) }));
    }
  }

  // A virtual device of an operating-system driver (drivers/, see virtual.js): what the mixer sends is what applications record from it.
  function startVirtual(opts) {
    const dev = virtual.hub.get(opts.deviceId), write = dev && virtual.hub.render(dev.id);
    if (!dev || !write) return conn.send(JSON.stringify({ type: 'error', message: 'The Audio Mixer driver is not connected: install and enable it (see drivers/README.md).' }));
    const channels = Math.min(Math.max(parseInt(opts.channels, 10) || dev.channels, 1), 32);
    const sampleRate = parseInt(opts.sampleRate, 10) || dev.rate;
    const remap = channels !== dev.channels ? b => audify.remapChannels(b, channels, dev.channels) : null;
    const conv = audify.createResampler(sampleRate, dev.rate, dev.channels);
    frameBytes = 2 * channels;
    const si = { direction: 'output', engine: 'virtual', device: dev.name, hostApi: 'Audio Mixer Virtual', sampleRate, channels };
    unreg = streams.add(si); meter = levels.attach(conn, { ...si, sid: unreg.id });
    io = { write: buf => { if (remap) buf = remap(buf); if (conv) buf = conv(buf); return write(buf) !== false; }, quit: () => {} };
    conn.send(JSON.stringify({ type: 'started', engine: 'virtual', device: dev.name, hostApi: 'Audio Mixer Virtual', sampleRate, channels, frameSize: null, latencyMs: null }));
  }

  function start(opts) {
    stop();
    if (virtual.hub.isVirtualId(opts.deviceId)) return startVirtual(opts);
    // "Universal ASIO driver": detect every device on both engines and open the best one (ASIO first), see universal.js
    if (opts.universal === true || opts.deviceId === 'universal') {
      const choice = universal.resolve({ direction: 'output', channels: opts.channels, engine: opts.engine }, load, loadA, audify);
      if (!choice) return conn.send(JSON.stringify({ type: 'error', message: 'Universal ASIO driver: no output device was found' }));
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
      const si = { direction: 'output', engine: 'naudiodon', device: dev ? dev.name : 'default', hostApi: dev ? dev.hostAPIName : 'default', sampleRate, channels };
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
      if (!m || typeof m !== 'object') return;
      if (m.type === 'start') start(m); else if (m.type === 'stop') { stop(); conn.send(JSON.stringify({ type: 'stopped' })); }
    },
    onBinary(buf) {
      if (!io || blocked) return;               // drop instead of queueing, keeps latency bounded
      if (buf.length % frameBytes !== 0) return; // partial frame would misalign every later sample
      if (meter) meter.push(buf);                  // levels of what is really sent to the driver
      if (io.write(buf) === false) blocked = true;
    },
    onClose: stop,
  };
}

module.exports = { createSession };
