'use strict';
// Minimal zero-dependency RFC 6455 WebSocket server side (text + binary, no extensions).
const crypto = require('node:crypto');
const MAX_PAYLOAD = 1 << 20;

function accept(req, socket, handlers) {
  const key = req.headers['sec-websocket-key'];
  if (!key || String(req.headers.upgrade).toLowerCase() !== 'websocket') { socket.destroy(); return null; }
  const hash = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${hash}\r\n\r\n`);
  socket.setNoDelay(true);

  let buf = Buffer.alloc(0);
  let frag = [];
  let fragOp = 0;
  let closed = false;

  const frame = (op, payload) => {
    const n = payload.length;
    let head;
    if (n < 126) head = Buffer.from([0x80 | op, n]);
    else if (n < 65536) head = Buffer.from([0x80 | op, 126, n >> 8, n & 255]);
    else { head = Buffer.alloc(10); head[0] = 0x80 | op; head[1] = 127; head.writeBigUInt64BE(BigInt(n), 2); }
    return Buffer.concat([head, payload]);
  };
  const fail = () => { conn.close(); socket.destroy(); };
  let fragSize = 0;
  const conn = {
    send(text) { if (!closed) socket.write(frame(1, Buffer.from(String(text)))); },
    sendBinary(buf) { if (!closed && socket.writableLength < (1 << 20)) socket.write(frame(2, buf)); },
    close() { if (!closed) { closed = true; try { socket.end(frame(8, Buffer.alloc(0))); } catch (_) { /* gone */ } } },
  };

  socket.on('data', chunk => {
    if (closed) return;
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      if (closed || buf.length < 2) return;
      const fin = (buf[0] & 0x80) !== 0, op = buf[0] & 0x0f, masked = (buf[1] & 0x80) !== 0;
      if (buf[0] & 0x70) { fail(); return; }                         // RSV bits set: no extensions negotiated
      if (op >= 8 && (!fin || (buf[1] & 0x7f) > 125)) { fail(); return; } // control frames: unfragmented, <=125 bytes
      let len = buf[1] & 0x7f, off = 2;
      if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); off = 4; }
      else if (len === 127) { if (buf.length < 10) return; len = Number(buf.readBigUInt64BE(2)); off = 10; }
      if (!masked || len > MAX_PAYLOAD) { fail(); return; }
      if (buf.length < off + 4 + len) return;
      const mask = buf.subarray(off, off + 4);
      const data = Buffer.from(buf.subarray(off + 4, off + 4 + len));
      for (let i = 0; i < data.length; i++) data[i] ^= mask[i & 3];
      buf = buf.subarray(off + 4 + len);
      if (op === 8) { conn.close(); release(); return; }   // close frame: free the stream now
      if (op === 9) { if (!closed) socket.write(frame(10, data)); continue; }
      if (op === 10) continue;
      if (op === 1 || op === 2) {
        if (frag.length) { fail(); return; }                         // new message while one is still open
        frag = [data]; fragOp = op; fragSize = data.length;
      } else if (op === 0) {
        if (!frag.length) { fail(); return; }                        // continuation without a start frame
        frag.push(data); fragSize += data.length;
        if (fragSize > MAX_PAYLOAD) { fail(); return; }               // bound total message size, not just frames
      } else { fail(); return; }                                      // unknown opcode
      if (fin) {
        const all = Buffer.concat(frag);
        frag = []; fragSize = 0;
        if (fragOp === 1) handlers.onText && handlers.onText(all.toString('utf8'));
        else handlers.onBinary && handlers.onBinary(all);
      }
    }
  });
  // The stream behind the connection (an ASIO driver is single-client) must be released as soon as the peer says close or disappears,
  // not only when the TCP connection finally ends: once, whichever comes first.
  let released = false;
  const release = () => { if (released) return; released = true; clearInterval(beat); closed = true; try { handlers.onClose && handlers.onClose(); } catch (_) { /* never leave the socket half open */ } };
  // a peer that vanished without a close (cable, sleep, crashed browser) is detected by a ping every 15 s: no sign of life for 45 s ends the connection
  let seen = Date.now();
  const beat = setInterval(() => {
    if (closed) return;
    if (Date.now() - seen > 45000) { fail(); return; }
    try { socket.write(frame(9, Buffer.alloc(0))); } catch (_) { /* gone */ }
  }, 15000);
  if (beat.unref) beat.unref();
  socket.on('data', () => { seen = Date.now(); });
  conn.release = release;
  socket.on('close', release);
  socket.on('error', () => socket.destroy());
  return conn;
}

module.exports = { accept };
