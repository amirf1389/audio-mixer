'use strict';
// ASIO / WASAPI / host-API audio INPUT capture through PortAudio (naudiodon2), streamed to the page as Int16 PCM.
function loadPortAudio() { return require('naudiodon2'); }

function pickInput(pa, wantedId, channels) {
  const devices = pa.getDevices();
  if (Number.isInteger(wantedId)) return devices.find(d => d.id === wantedId) || null;
  return devices.find(d => /asio/i.test(d.hostAPIName) && d.maxInputChannels >= channels) || null;
}

const CHUNK = 16384; // bytes per WebSocket frame

function createInputSession(conn, load = loadPortAudio) {
  let io = null;

  const stop = () => {
    if (io) { try { io.quit(); } catch (_) { /* already closed */ } io = null; }
  };

  function start(opts) {
    stop();
    let pa;
    try { pa = load(); } catch (_) {
      return conn.send(JSON.stringify({ type: 'error', message: 'PortAudio not installed: run "npm install" in bridge/ (needs naudiodon2)' }));
    }
    const channels = Math.min(Math.max(parseInt(opts.channels, 10) || 2, 1), 32);
    const sampleRate = parseInt(opts.sampleRate, 10) || 48000;
    const dev = pickInput(pa, Number.isInteger(opts.deviceId) ? opts.deviceId : null, channels);
    if (!dev && Number.isInteger(opts.deviceId)) return conn.send(JSON.stringify({ type: 'error', message: 'device not found' }));
    try {
      io = new pa.AudioIO({ inOptions: {
        channelCount: channels, sampleFormat: pa.SampleFormat16Bit, sampleRate,
        deviceId: dev ? dev.id : -1, closeOnError: true,
      } });
      io.on('error', e => { conn.send(JSON.stringify({ type: 'error', message: String(e && e.message || e) })); stop(); });
      io.on('data', chunk => { for (let i = 0; i < chunk.length; i += CHUNK) conn.sendBinary(chunk.subarray(i, i + CHUNK)); });
      io.start();
      conn.send(JSON.stringify({ type: 'started', device: dev ? dev.name : 'default', hostApi: dev ? dev.hostAPIName : 'default', sampleRate, channels }));
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
