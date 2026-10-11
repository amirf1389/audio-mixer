'use strict';
// Virtual audio devices: the meeting point between an operating-system audio driver (drivers/: ASIO driver on Windows, ALSA plugin on Linux, Core Audio
// plug-in on macOS, audio HAL on Android) and the mixer.
//
//   WS /ws/virtual            the DRIVER connects here (a native program on this computer, no browser):
//        driver -> bridge   text  {"type":"hello","name":"Audio Mixer","channels":2,"rate":48000}   (first message, once)
//        driver -> bridge   binary interleaved Int16 LE PCM = what applications PLAYED to the virtual device  -> the mixer reads it as an input source
//        bridge -> driver   binary interleaved Int16 LE PCM = what the mixer sends to the virtual device       -> applications record it from the virtual device
//        bridge -> driver   text  {"type":"ready","id":9000} after the hello, {"type":"error","message":...}
//   GET /api/virtual         the devices that are connected right now
//   The page opens a virtual device like any other interface: /ws/input and /ws/output with its deviceId (>= 9000, from /api/interfaces).
//
// Nothing is stored and no audio is kept: frames are forwarded as they arrive and dropped when nobody listens.
const BASE = 9000;
const MAX_NAME = 48;

function createHub() {
  const ids = new Map();            // name -> stable id (for the life of the process)
  const devices = new Map();        // name -> { name, id, channels, rate, conn, listeners:Set, frames, bytes, since }
  const idOf = name => { if (!ids.has(name)) ids.set(name, BASE + ids.size); return ids.get(name); };
  const nameOf = id => { for (const [n, i] of ids) if (i === id) return n; return null; };
  const clean = n => String(n || '').replace(/[\u0000-\u001f\u007f<>"'`&]/g, '').trim().slice(0, MAX_NAME);

  return {
    BASE,
    isVirtualId: id => Number.isInteger(id) && id >= BASE && id < BASE + 1000,
    get: id => { const n = nameOf(id); return n ? devices.get(n) || null : null; },
    list: () => [...devices.values()].map(d => ({ id: d.id, name: d.name, channels: d.channels, rate: d.rate, capturing: d.listeners.size > 0, since: d.since, frames: d.frames })),

    // the driver side of one WebSocket: returns the handlers for it
    session(conn) {
      let dev = null;
      return {
        onText(text) {
          let m; try { m = JSON.parse(text); } catch (_) { return; }
          if (!m || m.type !== 'hello' || dev) return;
          const name = clean(m.name), channels = Math.floor(Number(m.channels)), rate = Math.floor(Number(m.rate));
          if (!name) return conn.send(JSON.stringify({ type: 'error', message: 'name is required' }));
          if (!(channels >= 1 && channels <= 32)) return conn.send(JSON.stringify({ type: 'error', message: 'channels must be 1 to 32' }));
          if (!(rate >= 8000 && rate <= 384000)) return conn.send(JSON.stringify({ type: 'error', message: 'rate must be 8000 to 384000' }));
          if (devices.has(name)) return conn.send(JSON.stringify({ type: 'error', message: 'a driver named "' + name + '" is already connected' }));
          dev = { name, id: idOf(name), channels, rate, conn, listeners: new Set(), frames: 0, bytes: 0, since: Date.now() };
          devices.set(name, dev);
          conn.send(JSON.stringify({ type: 'ready', id: dev.id, name }));
        },
        onBinary(buf) {
          if (!dev) return;
          const fb = 2 * dev.channels;
          if (buf.length % fb !== 0) return;                       // a partial frame would shift every later sample
          dev.frames += buf.length / fb; dev.bytes += buf.length;
          dev.listeners.forEach(f => { try { f(buf); } catch (_) { /* a listener that fails does not stop the others */ } });
        },
        onClose() { if (dev && devices.get(dev.name) === dev) { devices.delete(dev.name); dev.listeners.forEach(f => { try { f(null); } catch (_) { /* gone */ } }); dev.listeners.clear(); } dev = null; },
      };
    },

    // the mixer reads the device (what applications play): f(buf) for every block, f(null) when the driver goes away. Returns stop().
    capture(id, f) {
      const d = this.get(id); if (!d) return null;
      d.listeners.add(f);
      return () => d.listeners.delete(f);
    },
    // the mixer writes the device (what applications record): returns write(buf) -> false when the driver is gone
    render(id) {
      const d = this.get(id); if (!d) return null;
      return buf => { if (devices.get(d.name) !== d) return false; d.conn.sendBinary(buf); return true; };
    },
  };
}

const hub = createHub();
module.exports = { hub, createHub, BASE };
