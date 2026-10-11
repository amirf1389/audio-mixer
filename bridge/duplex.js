'use strict';
// READ + WRITE on one interface through ONE native stream (ASIO drivers are single-client, so two separate streams on the same driver fail).
// Page -> bridge binary frames are the output PCM, bridge -> page binary frames are the captured input PCM (both interleaved Int16).
const { claim } = require('./asio-lock');
const audify = require('./audify');
const streams = require('./streams');
const levels = require('./levels');
function loadPortAudio() { return require('naudiodon2'); }
const SAMPLE_RATES = [44100, 48000, 88200, 96000, 176400, 192000];
const CHUNK = 16384;

function createDuplexSession(conn, load = loadPortAudio, loadA = audify.loadAudify) {
  let io = null, lock = null, blocked = false, frameBytes = 4;
  let mIn = null, mOut = null, uIn = null, uOut = null;
  const err = message => conn.send(JSON.stringify({ type: 'error', message }));
  const stop = () => {
    if (io) { try { io.quit(); } catch (_) { /* already closed */ } io = null; }
    if (lock) { lock.release(); lock = null; }
    [uIn, uOut].forEach(u => u && u()); uIn = uOut = null;
    [mIn, mOut].forEach(m => m && m.stop()); mIn = mOut = null;
    blocked = false;
  };
  const announce = (engine, dev, hostApi, sampleRate, inCh, outCh, extra) => {
    const base = { engine, device: dev, hostApi, sampleRate, ...extra };
    uIn = streams.add({ ...base, direction: 'input', channels: inCh }); uOut = streams.add({ ...base, direction: 'output', channels: outCh });
    mIn = levels.attach(conn, { ...base, direction: 'input', channels: inCh, sid: uIn.id });
    mOut = levels.attach(conn, { ...base, direction: 'output', channels: outCh, sid: uOut.id });
    conn.send(JSON.stringify({ type: 'started', duplex: true, ...base, channels: outCh, inChannels: inCh, ...extra }));
  };
  const send = chunk => { if (mIn) mIn.push(chunk); for (let i = 0; i < chunk.length; i += CHUNK) conn.sendBinary(chunk.subarray(i, i + CHUNK)); };

  function start(opts) {
    stop();
    const id = Number.isInteger(opts.deviceId) ? opts.deviceId : null;
    if (id === null) return err('duplex needs a device id');
    const inCh = Math.min(Math.max(parseInt(opts.inChannels, 10) || 2, 1), 32), outCh = Math.min(Math.max(parseInt(opts.channels, 10) || 2, 1), 32);
    const sampleRate = SAMPLE_RATES.includes(Number(opts.sampleRate)) ? Number(opts.sampleRate) : 48000;
    const forceAudify = opts.engine === 'audify' || id >= audify.AUDIFY_BASE;
    let pa = null;
    if (!forceAudify) { try { pa = load(); } catch (_) { pa = null; } }
    try {
      if (!pa) {
        if (opts.engine === 'naudiodon') return err('PortAudio not installed: run "npm install" in bridge/ (needs naudiodon2)');
        let list; try { list = audify.listDevices(loadA); } catch (_) { list = null; }
        if (!list) return err('PortAudio not installed: run "npm install" in bridge/ (needs naudiodon2 or audify)');
        const dev = list.devices.find(d => d.id === id);
        if (!dev) return err('device not found');
        lock = claim({ id: dev.id, name: dev.name, hostAPIName: dev.hostAPIName });
        if (!lock.ok) { const m = lock.message; lock = null; return err(m); }
        const st = audify.openDuplex({ mod: loadA(), dev, inChannels: inCh, outChannels: outCh, sampleRate, frameSize: opts.frameSize,
          onData: send, onError: e => { err(String(e && e.message || e)); stop(); } });
        io = { quit: () => st.close(), write: b => st.write(b) }; frameBytes = 2 * outCh;
        return announce('audify', dev.name, dev.hostAPIName, sampleRate, st.inChannels, outCh, { frameSize: st.frameSize, latencyMs: st.latencyMs });
      }
      const dev = pa.getDevices().find(d => d.id === id);
      if (!dev) return err('device not found');
      if (!dev.maxInputChannels || !dev.maxOutputChannels) return err(`${dev.name} cannot read and write at once`);
      lock = claim(dev);
      if (!lock.ok) { const m = lock.message; lock = null; return err(m); }
      const ci = Math.min(inCh, dev.maxInputChannels);
      const a = new pa.AudioIO({ inOptions: { channelCount: ci, sampleFormat: pa.SampleFormat16Bit, sampleRate, deviceId: dev.id, closeOnError: true },
        outOptions: { channelCount: outCh, sampleFormat: pa.SampleFormat16Bit, sampleRate, deviceId: dev.id, closeOnError: true } });
      a.on('error', e => { err(String(e && e.message || e)); stop(); });
      a.on('data', send); a.on('drain', () => { blocked = false; });
      io = a; frameBytes = 2 * outCh; a.start();
      announce('naudiodon', dev.name, dev.hostAPIName, sampleRate, ci, outCh, {});
    } catch (e) { stop(); err(String(e && e.message || e)); }
  }

  return {
    onText(text) {
      let m; try { m = JSON.parse(text); } catch (_) { return; }
      if (!m || typeof m !== 'object') return;
      if (m.type === 'start') start(m); else if (m.type === 'stop') { stop(); conn.send(JSON.stringify({ type: 'stopped' })); }
    },
    onBinary(buf) {
      if (!io || blocked || buf.length % frameBytes !== 0) return;
      if (mOut) mOut.push(buf);
      if (io.write(buf) === false) blocked = true;
    },
    onClose: stop,
  };
}

module.exports = { createDuplexSession };
