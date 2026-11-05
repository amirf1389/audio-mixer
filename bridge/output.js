'use strict';
// ASIO / host-API audio output through PortAudio (naudiodon2). Receives interleaved Int16 PCM from the page.
const { claim } = require('./asio-lock');
const audify = require('./audify');
const streams = require('./streams');
const levels = require('./levels');
const universal = require('./universal');
const virtual = require('./virtual');
const control = require('./control');
const diag = require('./diagnostics');
const hw = require('./hw-match');
function loadPortAudio() { return require('./portaudio').load(); }   // naudiodon2, or the device simulator (bridge/sim)
const SAMPLE_RATES = [44100, 48000, 88200, 96000, 176400, 192000];

function pickDevice(pa, wantedId, channels) {
  const devices = pa.getDevices();
  if (Number.isInteger(wantedId)) return devices.find(d => d.id === wantedId) || null;
  const asio = devices.filter(d => /asio/i.test(d.hostAPIName) && d.maxOutputChannels >= channels), sel = control.selected();   // the driver chosen with POST /api/select-device first
  return (sel && asio.find(d => d.name === sel)) || asio[0] || null;
}

// One output session per WebSocket connection. `load` is injectable for tests.
function createSession(conn, load = loadPortAudio, loadA = audify.loadAudify) {
  let pre = null;                                  // converts the page's rate to the device rate when the page's rate is not one the devices are opened at
  let io = null;
  let lock = null;   // ASIO is single-client: see asio-lock.js
  let blocked = false;
  let meter = null;   // level metering of this stream
  let unreg = null;   // live status registry
  let frameBytes = 4; // Int16 * channels; writes must be whole frames or channels swap

  // Another device was asked for (takeover): stop this stream and close the socket, so the page's row goes to stopped
  const takenOver = () => { stop(); conn.close(); };
  const stop = () => {
    if (io) { try { io.quit(); } catch (_) { /* already closed */ } io = null; }
    if (lock) { lock.release(); lock = null; }
    if (unreg) { unreg(); unreg = null; }
    if (meter) { meter.stop(); meter = null; }
    blocked = false; pre = null;
  };

  // Audify (RtAudio) engine: chosen by an Audify device id (>= 1000), engine: "audify", or when PortAudio is not installed.
  function startAudify(opts) {
    let list;
    try { list = audify.listDevices(loadA); } catch (_) { list = null; }
    if (!list) return conn.send(JSON.stringify({ type: 'error', message: 'PortAudio not installed: run "npm install" in bridge/ (needs naudiodon2 or audify)' }));
    const channels = Math.min(Math.max(parseInt(opts.channels, 10) || 2, 1), 32);
    // The page's AudioContext runs at the rate of the system's default output (16 / 24 / 32 kHz on a Bluetooth hands-free headset, 44.1 kHz on A2DP). A rate the device list does not offer
    // used to be replaced by 48000 without converting the audio, so everything played at the wrong speed and pitch (or the ASIO device refused it). It is converted now.
    const pageRate = parseInt(opts.sampleRate, 10) || 48000, sampleRate = SAMPLE_RATES.includes(pageRate) ? pageRate : 48000;
    pre = pageRate !== sampleRate && pageRate >= 8000 && pageRate <= 384000 ? audify.createResampler(pageRate, sampleRate, channels) : null;   // a nonsense rate is just ignored (48000)
    const wanted = Number.isInteger(opts.deviceId) && opts.deviceId >= 0 ? opts.deviceId : null;
    const sel = wanted === null && control.selected(), chosen = sel ? list.devices.find(d => /asio/i.test(d.hostAPIName) && d.name === sel && d.outputChannels >= channels) : null;
    const dev = chosen || audify.pickAudifyDevice(list.devices, wanted, 'output', channels) || (wanted === null ? list.devices.find(d => d.isDefaultOutput) : null);
    if (!dev) return conn.send(JSON.stringify({ type: 'error', message: wanted !== null ? 'device not found' : 'no output device found' }));
    lock = claim({ id: dev.id, name: dev.name, hostAPIName: dev.hostAPIName }, { takeover: opts.takeover === true, preempt: takenOver });
    if (!lock.ok) { const msg = lock.message; lock = null; return conn.send(JSON.stringify({ type: 'error', message: msg })); }
    try {
      const st = audify.openStream({ mod: dev.engineBackend ? null : loadA(), dev, direction: 'output', channels, sampleRate, frameSize: opts.frameSize, exclusive: opts.exclusive === true,
        onFallback: f => { try { conn.send(JSON.stringify({ type: 'fallback', fallback: f })); } catch (_) { /* closed */ } },   // ASIO failed at start: now on WASAPI
        onError: e => { conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) })); stop(); } });
      io = { quit: () => st.close(), write: b => st.write(b) };
      frameBytes = 2 * channels;
      const si = { direction: 'output', engine: dev.engineBackend ? 'native' : 'audify', device: dev.name, hostApi: dev.hostAPIName, sampleRate, channels: channels, frameSize: st.frameSize, latencyMs: st.latencyMs };
      unreg = streams.add(si); meter = levels.attach(conn, { ...si, sid: unreg.id });
      conn.send(JSON.stringify({ type: 'started', ...(opts.universal ? { universal: true } : {}), engine: dev.engineBackend ? 'native' : 'audify', device: dev.name, hostApi: dev.hostAPIName, sampleRate, channels, frameSize: st.frameSize, latencyMs: st.latencyMs, autoFrameSize: st.auto, exclusive: st.exclusive, ...(st.fallback ? { fallback: st.fallback } : {}) }));
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
    // The page's AudioContext runs at the rate of the system's default output (16 / 24 / 32 kHz on a Bluetooth hands-free headset, 44.1 kHz on A2DP). A rate the device list does not offer
    // used to be replaced by 48000 without converting the audio, so everything played at the wrong speed and pitch (or the ASIO device refused it). It is converted now.
    const pageRate = parseInt(opts.sampleRate, 10) || 48000, sampleRate = SAMPLE_RATES.includes(pageRate) ? pageRate : 48000;
    pre = pageRate !== sampleRate && pageRate >= 8000 && pageRate <= 384000 ? audify.createResampler(pageRate, sampleRate, channels) : null;   // a nonsense rate is just ignored (48000)
    const wanted = Number.isInteger(opts.deviceId) && opts.deviceId >= 0 ? opts.deviceId : null;
    let dev = pickDevice(pa, wanted, channels);
    const sim = pa.simulated ? '[SIM] ' : '';
    if (!dev && wanted !== null) { diag.error('device', 'write: device ' + wanted + ' not found (unplugged, or the list changed)'); return conn.send(JSON.stringify({ type: 'error', message: 'device not found' })); }
    if (dev) {
      lock = claim(dev, { takeover: opts.takeover === true, preempt: takenOver });
      if (!lock.ok) {
        const msg = lock.message; lock = null;
        // one ASIO driver per program (PortAudio): the second interface writes through its own Windows endpoint instead of failing
        const twin = /asio/i.test(dev.hostAPIName) && opts.autoFallback !== false ? hw.bestTwin(dev.name, pa.getDevices().filter(d => !/asio/i.test(d.hostAPIName) && d.maxOutputChannels > 0)) : null;
        if (!twin) { diag.error('asio', `write: ${sim}${dev.name}: ${msg}`, { device: dev.name, hostApi: dev.hostAPIName }); return conn.send(JSON.stringify({ type: 'error', message: msg })); }
        const fb = { from: dev.name, to: twin.name, via: 'wasapi', reason: msg };
        diag.warn('asio', `write: ${sim}${dev.name}: ${msg}; using the same hardware through Windows: ${twin.name}`, { device: dev.name, twin: twin.name });
        try { conn.send(JSON.stringify({ type: 'fallback', fallback: fb })); } catch (_) { /* closed */ }
        dev = twin;
        lock = claim(dev, { takeover: false, preempt: takenOver });
        if (!lock.ok) { const m2 = lock.message; lock = null; diag.error('wasapi', `write: ${sim}${dev.name}: ${m2}`); return conn.send(JSON.stringify({ type: 'error', message: m2 })); }
      }
    }
    try {
      io = new pa.AudioIO({ outOptions: {
        channelCount: channels, sampleFormat: pa.SampleFormat16Bit, sampleRate,
        deviceId: dev ? dev.id : -1, closeOnError: true,
      } });
      io.on('error', e => { const m = String(e && e.message || e); diag.error(dev && /asio/i.test(dev.hostAPIName) ? 'asio' : 'device', `write: ${sim}${dev ? dev.name : 'default'}: ${m}`); conn.send(JSON.stringify({ type: 'error', message: m })); stop(); });
      io.on('drain', () => { blocked = false; });
      io.start();
      frameBytes = 2 * channels;
      const si = { direction: 'output', engine: 'naudiodon', device: dev ? dev.name : 'default', hostApi: dev ? dev.hostAPIName : 'default', sampleRate, channels };
      unreg = streams.add(si); meter = levels.attach(conn, { ...si, sid: unreg.id });
      diag.info(dev && /asio/i.test(dev.hostAPIName) ? 'asio' : 'stream', `write: ${sim}${si.device} (${si.hostApi}) open: ${channels} ch at ${sampleRate} Hz`);
      conn.send(JSON.stringify({ type: 'started', ...(opts.universal ? { universal: true } : {}), engine: 'naudiodon', device: dev ? dev.name : 'default', hostApi: dev ? dev.hostAPIName : 'default', sampleRate, channels }));
    } catch (e) {
      stop();
      diag.error('stream', `write: ${sim}${dev ? dev.name : 'default'}: ${String(e && e.message || e)}`);
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
      if (pre) buf = pre(buf);
      if (!buf.length) return;
      buf = control.apply(buf, frameBytes >> 1);    // the faders of /ws/control
      if (meter) meter.push(buf);                  // levels of what is really sent to the driver
      if (io.write(buf) === false) blocked = true;
    },
    onClose: stop,
  };
}

module.exports = { createSession };
