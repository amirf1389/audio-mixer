'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { server, originAllowed } = require('./server');

test('origin policy', () => {
  assert.ok(originAllowed('http://localhost:3000'));
  assert.ok(originAllowed('null'));
  assert.ok(!originAllowed('https://evil.example'));
});

test('api + traversal', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const s = await (await fetch(base + '/api/status')).json();
  assert.strictEqual(s.ok, true);
  const d = await (await fetch(base + '/api/drivers')).json();
  assert.ok(Array.isArray(d.drivers));
  assert.strictEqual((await fetch(base + '/api/status', { headers: { Origin: 'https://evil.example' } })).status, 403);
  assert.notStrictEqual((await fetch(base + '/%2e%2e/%2e%2e/etc/passwd')).status, 200);
  assert.strictEqual((await fetch(base + '/bridge/server.js')).status, 404);
  server.closeAllConnections();
  server.close();
});

test('output session streams PCM to PortAudio (ASIO preferred)', () => {
  const sent = [], written = [];
  const conn = { send: m => sent.push(JSON.parse(m)) };
  let opened = null, quit = 0;
  const fakePa = {
    SampleFormat16Bit: 8,
    getDevices: () => [{ id: 1, name: 'WASAPI Out', hostAPIName: 'Windows WASAPI', maxOutputChannels: 2 }, { id: 5, name: 'Wing ASIO', hostAPIName: 'ASIO', maxOutputChannels: 32 }],
    AudioIO: class {
      constructor(o) { opened = o; this.h = {}; }
      on(e, f) { this.h[e] = f; }
      start() {}
      write(b) { written.push(b.length); return written.length < 3; }
      quit() { quit++; }
    },
  };
  const s = require('./output').createSession(conn, () => fakePa);
  s.onText(JSON.stringify({ type: 'start', channels: 2, sampleRate: 48000 }));
  assert.strictEqual(opened.outOptions.deviceId, 5);
  assert.strictEqual(sent[0].type, 'started');
  assert.strictEqual(sent[0].hostApi, 'ASIO');
  s.onBinary(Buffer.alloc(8)); s.onBinary(Buffer.alloc(8)); s.onBinary(Buffer.alloc(8)); s.onBinary(Buffer.alloc(8));
  assert.strictEqual(written.length, 3);       // 4th dropped while backpressured
  s.onClose();
  assert.strictEqual(quit, 1);
});

test('websocket upgrade works and reports missing PortAudio', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const ws = new WebSocket(`ws://127.0.0.1:${server.address().port}/ws/output`);
  const msg = await new Promise((res, rej) => { ws.onopen = () => ws.send(JSON.stringify({ type: 'start' })); ws.onmessage = e => res(JSON.parse(e.data)); ws.onerror = rej; });
  assert.ok(msg.type === 'error' || msg.type === 'started');
  ws.close();
  server.closeAllConnections();
  server.close();
});
