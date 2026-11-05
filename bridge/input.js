'use strict';
// ASIO / WASAPI / host-API audio INPUT capture through PortAudio (naudiodon2), streamed to the page as Int16 PCM.
const { claim } = require('./asio-lock');
const audify = require('./audify');
const streams = require('./streams');
const levels = require('./levels');
const universal = require('./universal');
const virtual = require('./virtual');
const diag = require('./diagnostics');
const guard = require('./input-guard');
const hw = require('./hw-match');
function loadPortAudio() { return require('./portaudio').load(); }   // naudiodon2, or the device simulator (bridge/sim)

function pickInput(pa, wantedId, channels) {
  const devices = pa.getDevices();
  if (Number.isInteger(wantedId)) return devices.find(d => d.id === wantedId) || null;
  const asio = devices.filter(d => /asio/i.test(d.hostAPIName) && d.maxInputChannels >= channels);
  return asio.find(d => !hw.isWrapper(d.name)) || asio[0] || null;   // ASIO4ALL only when no device has its own driver
}

const CHUNK = 16384; // bytes per WebSocket frame

function createInputSession(conn, load = loadPortAudio, loadA = audify.loadAudify) {
  let io = null;
  let lock = null;   // ASIO is single-client: see asio-lock.js
  let meter = null;   // level metering of this stream
  let unreg = null;  // live status registry

  // a takeover by another device: stop this read and close its socket, so the page's READ goes to idle instead of holding the driver
  const takenOver = () => { stop(); conn.close(); };
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
    lock = claim({ id: dev.id, name: dev.name, hostAPIName: dev.hostAPIName }, { takeover: opts.takeover === true, preempt: takenOver });
    if (!lock.ok) { const msg = lock.message; lock = null; return conn.send(JSON.stringify({ type: 'error', message: msg })); }
    try {
      const st = audify.openStream({ mod: dev.engineBackend ? null : loadA(), dev, direction: 'input', channels, sampleRate, frameSize: opts.frameSize, exclusive: opts.exclusive === true,
        onData: chunk => { if (meter) meter.push(chunk); for (let i = 0; i < chunk.length; i += CHUNK) conn.sendBinary(chunk.subarray(i, i + CHUNK)); },
        onFallback: f => { try { conn.send(JSON.stringify({ type: 'fallback', fallback: f })); } catch (_) { /* closed */ } },   // ASIO failed at start: now on WASAPI
        onError: e => { conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) })); stop(); } });
      io = { quit: () => st.close() };
      const si = { direction: 'input', engine: dev.engineBackend ? 'native' : 'audify', device: dev.name, hostApi: dev.hostAPIName, sampleRate, channels: st.channels, frameSize: st.frameSize, latencyMs: st.latencyMs };
      unreg = streams.add(si); meter = levels.attach(conn, { ...si, sid: unreg.id });
      conn.send(JSON.stringify({ type: 'started', ...(opts.universal ? { universal: true } : {}), engine: dev.engineBackend ? 'native' : 'audify', device: dev.name, hostApi: dev.hostAPIName, sampleRate, channels: st.channels, frameSize: st.frameSize, latencyMs: st.latencyMs, autoFrameSize: st.auto, exclusive: st.exclusive, ...(st.fallback ? { fallback: st.fallback } : {}) }));
    } catch (e) {
      stop();
      conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) }));
    }
  }

  // A virtual device of an operating-system driver (drivers/, see virtual.js): the audio that applications play to it is the input of the mixer.
  function startVirtual(opts) {
    const dev = virtual.hub.get(opts.deviceId);
    if (!dev) return conn.send(JSON.stringify({ type: 'error', message: 'The Audio Mixer driver is not connected: install and enable it (see drivers/README.md) and start an application that uses it.' }));
    const channels = Math.min(Math.max(parseInt(opts.channels, 10) || dev.channels, 1), 32);
    const sampleRate = parseInt(opts.sampleRate, 10) || dev.rate;
    const remap = channels !== dev.channels ? b => audify.remapChannels(b, dev.channels, channels) : null;
    const conv = audify.createResampler(dev.rate, sampleRate, channels);
    const si = { direction: 'input', engine: 'virtual', device: dev.name, hostApi: 'Audio Mixer Virtual', sampleRate, channels };
    unreg = streams.add(si); meter = levels.attach(conn, { ...si, sid: unreg.id });
    const off = virtual.hub.capture(dev.id, buf => {
      if (buf === null) { conn.send(JSON.stringify({ type: 'error', message: 'The Audio Mixer driver disconnected' })); stop(); return; }
      let chunk = Buffer.from(buf);
      if (remap) chunk = remap(chunk);
      if (conv) chunk = conv(chunk);
      if (meter) meter.push(chunk);
      for (let i = 0; i < chunk.length; i += CHUNK) conn.sendBinary(chunk.subarray(i, i + CHUNK));
    });
    io = { quit: off };
    conn.send(JSON.stringify({ type: 'started', engine: 'virtual', device: dev.name, hostApi: 'Audio Mixer Virtual', sampleRate, channels, frameSize: null, latencyMs: null }));
  }

  function start(opts) {
    stop();
    if (virtual.hub.isVirtualId(opts.deviceId)) return startVirtual(opts);
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
    if (!dev && Number.isInteger(opts.deviceId)) { diag.error('device', 'read: device ' + opts.deviceId + ' not found (unplugged, or the list changed)'); return conn.send(JSON.stringify({ type: 'error', message: 'device not found' })); }
    openPa(pa, dev, { channels, sampleRate, opts, universal: opts.universal });
  }

  // Opens a PortAudio input and runs it through the guards. On an ASIO device that delivers exact silence (driver held by another client) it
  // switches, once, to the same hardware's Windows (WASAPI) endpoint and tells the page with a "fallback" message.
  function openPa(pa, dev, { channels, sampleRate, opts, universal, fallbackFrom }) {
    const sim = pa.simulated ? '[SIM] ' : '', api = dev ? dev.hostAPIName : 'default', name = dev ? dev.name : 'default';
    if (dev) {
      if (hw.isWrapper(dev.name) && /asio/i.test(api)) diag.warn('asio', `read: ${sim}${dev.name} is an ASIO wrapper: it may open a device that is also open through its own driver`, { device: dev.name });
      lock = claim(dev, { takeover: opts.takeover === true, preempt: takenOver });
      if (!lock.ok) {
        const msg = lock.message; lock = null;
        // PortAudio loads ONE ASIO driver per program: a second interface (or a second reader of a busy driver) runs through its own
        // Windows endpoint instead, so every interface can run at the same time ("all audio interfaces run")
        const twin = /asio/i.test(api) && !fallbackFrom && opts.autoFallback !== false ? hw.bestTwin(name, pa.getDevices().filter(d => !/asio/i.test(d.hostAPIName) && d.maxInputChannels > 0)) : null;
        if (twin) {
          const fb = { from: name, to: twin.name, via: 'wasapi', reason: msg };
          diag.warn('asio', `read: ${sim}${name}: ${msg}; using the same hardware through Windows: ${twin.name}`, { device: name, hostApi: api, twin: twin.name });
          try { conn.send(JSON.stringify({ type: 'fallback', fallback: fb })); } catch (_) { /* closed */ }
          return openPa(pa, twin, { channels, sampleRate, opts: { ...opts, takeover: false }, universal, fallbackFrom: name });
        }
        diag.error('asio', `read: ${sim}${name}: ${msg}`, { device: name, hostApi: api });
        return conn.send(JSON.stringify({ type: 'error', message: msg }));
      }
    }
    const devCh = dev && dev.maxInputChannels ? Math.min(channels, dev.maxInputChannels) : channels;
    if (devCh < channels) diag.info('stream', `read: ${sim}${name} has ${dev.maxInputChannels} input channel(s): opening ${devCh}, delivering ${channels}`, { device: name });
    // the asked rate first, then the device's own rate (converted to the asked one): "open at the device's native rate"
    const rates = [sampleRate]; if (dev && dev.defaultSampleRate && !rates.includes(dev.defaultSampleRate)) rates.push(dev.defaultSampleRate); for (const r of [48000, 44100]) if (!rates.includes(r)) rates.push(r);
    let devRate = null, lastErr = null;
    for (const r of rates) {
      try {
        io = new pa.AudioIO({ inOptions: { channelCount: devCh, sampleFormat: pa.SampleFormat16Bit, sampleRate: r, deviceId: dev ? dev.id : -1, closeOnError: true } });
        devRate = r; break;
      } catch (e) {
        lastErr = e; io = null;
        const m = String(e && e.message || e);
        if (!/sample ?rate/i.test(m)) break;
        diag.warn('stream', `read: ${sim}${name} refused ${r} Hz: ${m}`, { device: name, hostApi: api, rate: r });
      }
    }
    if (!io) { const m = String(lastErr && lastErr.message || lastErr || 'could not open the device'); diag.error(/asio/i.test(api) ? 'asio' : 'stream', `read: ${sim}${name} (${api}) did not open: ${m}`, { device: name, hostApi: api, code: lastErr && lastErr.code }); stop(); return conn.send(JSON.stringify({ type: 'error', message: m })); }
    if (devRate !== sampleRate) diag.info('stream', `read: ${sim}${name} runs at its own rate ${devRate} Hz, converted to ${sampleRate} Hz`, { device: name });
    const remap = devCh !== channels ? b => audify.remapChannels(b, devCh, channels) : null;
    const conv = audify.createResampler(devRate, sampleRate, channels);
    const mapper = guard.createChannelMap(channels, opts.inputMap || 'auto', d => {
      if (d.source >= 0) { diag.info('signal', `read: ${sim}${name}: input ${d.source + 1} carries the signal, the other channel is silent: copied to both sides (mono)`, { device: name }); try { conn.send(JSON.stringify({ type: 'mapped', inputMap: d.mode, source: d.source + 1 })); } catch (_) { /* closed */ } }
    });
    const isAsio = /asio/i.test(api);
    let switching = false;
    const watch = guard.createSignalWatch({ rate: sampleRate, channels, isAsio,
      onSignal: s => diag.info('signal', `read: ${sim}${name}: signal (${s.peakDb.toFixed(1)} dBFS)`, { device: name }),
      onNoSignal: s => { diag.warn('signal', `read: ${sim}${name} (${api}): no signal for ${s.seconds} s${s.exactZero ? ' (exact digital silence)' : ''}`, { device: name, hostApi: api }); try { conn.send(JSON.stringify({ type: 'signal', state: 'none', seconds: s.seconds, exactZero: s.exactZero })); } catch (_) { /* closed */ } },
      onSilentAsio: () => {
        if (fallbackFrom || opts.autoFallback === false || switching) return;
        const twin = hw.bestTwin(name, pa.getDevices().filter(d => !/asio/i.test(d.hostAPIName) && d.maxInputChannels > 0));
        diag.warn('asio', `read: ${sim}${name} delivered silence for 3 s: the ASIO driver is held by another client` + (twin ? `; switching to the same hardware through Windows: ${twin.name}` : '; no Windows twin of this hardware was found'), { device: name, hostApi: api, twin: twin ? twin.name : null });
        if (!twin) { try { conn.send(JSON.stringify({ type: 'signal', state: 'asio-silent', seconds: 3 })); } catch (_) { /* closed */ } return; }
        switching = true;
        setImmediate(() => {
          stop();
          const fb = { from: name, to: twin.name, via: 'wasapi', reason: 'the ASIO driver delivered silence (held by another client)' };
          try { conn.send(JSON.stringify({ type: 'fallback', fallback: fb })); } catch (_) { /* closed */ }
          diag.info('fallback', `read: ${sim}${name} -> ${twin.name} (WASAPI, shared)`, fb);
          openPa(pa, twin, { channels, sampleRate, opts: { ...opts, takeover: false }, universal, fallbackFrom: name });
        });
      } });
    try {
      io.on('error', e => { const m = String(e && e.message || e); diag.error(isAsio ? 'asio' : 'device', `read: ${sim}${name}: ${m}`, { device: name, hostApi: api }); conn.send(JSON.stringify({ type: 'error', message: m })); stop(); });
      io.on('data', chunk => {
        if (remap) chunk = remap(chunk);
        if (conv) chunk = conv(chunk);
        watch(chunk);
        chunk = mapper(chunk);
        if (meter) meter.push(chunk);
        for (let i = 0; i < chunk.length; i += CHUNK) conn.sendBinary(chunk.subarray(i, i + CHUNK));
      });
      io.start();
      const si = { direction: 'input', engine: pa.simulated ? 'simulator' : 'naudiodon', device: name, hostApi: api, sampleRate, channels, ...(devRate !== sampleRate ? { deviceRate: devRate } : {}), ...(pa.simulated ? { simulated: true } : {}) };
      unreg = streams.add(si); meter = levels.attach(conn, { ...si, sid: unreg.id });
      diag.info(isAsio ? 'asio' : 'stream', `read: ${sim}${name} (${api}) open: ${devCh} ch at ${devRate} Hz` + (fallbackFrom ? ` (fallback from ${fallbackFrom})` : ''), { device: name, hostApi: api, rate: devRate, channels: devCh });
      conn.send(JSON.stringify({ type: 'started', ...(universal ? { universal: true } : {}), engine: si.engine, device: name, hostApi: api, sampleRate, channels, ...(devRate !== sampleRate ? { deviceRate: devRate } : {}), ...(fallbackFrom ? { fallbackFrom } : {}), ...(pa.simulated ? { simulated: true } : {}) }));
    } catch (e) {
      stop();
      diag.error('stream', `read: ${sim}${name}: ${String(e && e.message || e)}`, { device: name, hostApi: api });
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
