'use strict';
// AI driver locks learned during the tests go to a temporary file, never the developer's own ~/.audio-mixer
process.env.BRIDGE_DRIVER_LOCK_FILE = require('node:path').join(require('node:os').tmpdir(), 'amfx-test-driver-lock-' + process.pid + '.json');
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

test('system volume parsers', () => {
  const v = require('./volume');
  assert.strictEqual(v.parsePactlVolume('Volume: front-left: 42000 /  64% / -11.9 dB,   front-right: 42000 /  64% / -11.9 dB'), 0.64);
  assert.strictEqual(v.parsePactlMute('Mute: yes'), true);
  assert.strictEqual(v.parsePactlMute('Mute: no'), false);
  const mac = v.parseMacVolume('output volume:50, input volume:75, alert volume:100, output muted:false');
  assert.deepStrictEqual(mac, { output: { volume: 0.5, muted: false }, input: { volume: 0.75, muted: null } });
  const win = v.parseWinVolume('out 0.65 False\r\nin 0.8 True\r\n');
  assert.deepStrictEqual(win, { output: { volume: 0.65, muted: false }, input: { volume: 0.8, muted: true } });
});

test('input session captures from ASIO and streams chunked PCM', () => {
  const sent = [], bins = [];
  const conn = { send: m => sent.push(JSON.parse(m)), sendBinary: b => bins.push(b.length) };
  let opened = null, quit = 0, onData = null;
  const fakePa = {
    SampleFormat16Bit: 8,
    getDevices: () => [{ id: 2, name: 'Mic WASAPI', hostAPIName: 'Windows WASAPI', maxInputChannels: 2 }, { id: 9, name: 'Wing ASIO', hostAPIName: 'ASIO', maxInputChannels: 32 }],
    AudioIO: class {
      constructor(o) { opened = o; }
      on(e, f) { if (e === 'data') onData = f; }
      start() {}
      quit() { quit++; }
    },
  };
  const s = require('./input').createInputSession(conn, () => fakePa);
  s.onText(JSON.stringify({ type: 'start', channels: 2 }));
  assert.strictEqual(opened.inOptions.deviceId, 9);
  assert.strictEqual(sent[0].hostApi, 'ASIO');
  onData(Buffer.alloc(40000));
  assert.deepStrictEqual(bins, [16384, 16384, 7232]);
  s.onClose();
  assert.strictEqual(quit, 1);
});

test('input websocket and volume endpoint', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const ws = new WebSocket(`ws://127.0.0.1:${server.address().port}/ws/input`);
  ws.binaryType = 'arraybuffer';
  const first = await new Promise((res, rej) => { ws.onopen = () => ws.send(JSON.stringify({ type: 'start' })); ws.onmessage = e => res(JSON.parse(e.data)); ws.onerror = rej; });
  assert.ok(first.type === 'error' || first.type === 'started');
  ws.close();
  const v = await (await fetch(`http://127.0.0.1:${server.address().port}/api/volume`)).json();
  assert.strictEqual(v.ok, true);
  server.closeAllConnections();
  server.close();
});

const http = require('node:http');
const raw = (port, p, headers = {}) => new Promise(resolve => {
  const req = http.request({ host: '127.0.0.1', port, path: p, headers }, res => { res.resume(); res.on('end', () => resolve(res)); });
  req.on('error', () => resolve({ statusCode: 0 })); req.end();
});

test('hardening: rebinding, malformed URLs, dotfiles, headers', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port;
  assert.strictEqual((await raw(port, '/api/drivers', { Host: 'attacker.example' })).statusCode, 403);
  assert.strictEqual((await raw(port, '/%E0%A4%A')).statusCode, 400);
  assert.strictEqual((await raw(port, '/api/status')).statusCode, 200);        // still alive after the bad request
  for (const p of ['/.git/config', '/.gitignore', '/.vscode/launch.json', '/.github/workflows/codeql.yml']) assert.strictEqual((await raw(port, p)).statusCode, 404, p);
  const st = await raw(port, '/api/status');
  assert.strictEqual(st.headers['x-content-type-options'], 'nosniff');
  server.closeAllConnections();
  server.close();
});

test('websocket server frames large binary payloads (64-bit length)', () => {
  const EventEmitter = require('node:events');
  const sock = new EventEmitter();
  const writes = [];
  sock.write = b => { writes.push(Buffer.from(b)); return true; };
  sock.setNoDelay = () => {};
  sock.destroy = () => {};
  sock.writableLength = 0;
  const conn = require('./ws').accept({ headers: { 'sec-websocket-key': 'dGhlIHNhbXBsZSBub25jZQ==', upgrade: 'websocket' } }, sock, {});
  assert.ok(/101 Switching Protocols/.test(writes[0].toString()));
  conn.sendBinary(Buffer.alloc(70000));
  const f = writes[1];
  assert.strictEqual(f[0], 0x82);
  assert.strictEqual(f[1], 127);
  assert.strictEqual(Number(f.readBigUInt64BE(2)), 70000);
  assert.strictEqual(f.length, 10 + 70000);
});

test('websocket: a close frame frees the stream at once, and only once (the TCP close afterwards does not release again)', () => {
  const EventEmitter = require('node:events');
  const sock = new EventEmitter();
  sock.write = () => true; sock.end = () => {}; sock.setNoDelay = () => {}; sock.destroy = () => sock.emit('close'); sock.writableLength = 0;
  let released = 0;
  require('./ws').accept({ headers: { 'sec-websocket-key': 'dGhlIHNhbXBsZSBub25jZQ==', upgrade: 'websocket' } }, sock, { onClose: () => { released++; throw new Error('a failing handler must not break the socket'); } });
  sock.emit('data', Buffer.from([0x88, 0x80, 1, 2, 3, 4]));          // masked, empty close frame from the peer
  assert.strictEqual(released, 1);                                    // the driver is free before the peer closes TCP
  sock.emit('close'); assert.strictEqual(released, 1);
});

test('output session validates sample rate and drops partial frames', () => {
  const written = [];
  let opened = null;
  const fakePa = { SampleFormat16Bit: 8, getDevices: () => [], AudioIO: class { constructor(o) { opened = o; } on() {} start() {} write(b) { written.push(b.length); return true; } quit() {} } };
  const s = require('./output').createSession({ send() {} }, () => fakePa);
  s.onText(JSON.stringify({ type: 'start', channels: 2, sampleRate: 1e9 }));
  assert.strictEqual(opened.outOptions.sampleRate, 48000);
  s.onBinary(Buffer.alloc(6));                 // 1.5 stereo frames: dropped
  s.onBinary(Buffer.alloc(8));
  assert.deepStrictEqual(written, [8]);
  s.onText('null');                            // must not throw
});

test('security audit reports no failures', async () => {
  const { probe, scanPage } = require('./audit');
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const results = [...await probe(`http://127.0.0.1:${server.address().port}`), ...scanPage()];
  server.closeAllConnections();
  server.close();
  assert.deepStrictEqual(results.filter(r => r.level === 'FAIL').map(r => r.title), []);
});

// ── official driver catalog, downloader and PC-mode client ──
const fsx = require('node:fs'), osx = require('node:os'), pathx = require('node:path'), cryptox = require('node:crypto');

test('catalog lists drivers for this OS and detects what is installed', () => {
  const { listCatalog, CATALOG } = require('./catalog');
  const win = listCatalog({ platform: 'win32', drivers: ['wasapi-shared', 'flexasio', 'steinberg'], asio: ['FlexASIO', 'Focusrite USB ASIO'], portaudio: null });
  const by = id => win.find(x => x.id === id);
  assert.strictEqual(by('flexasio').installed, true);
  assert.strictEqual(by('focusrite').installed, true);
  assert.strictEqual(by('asio4all').installed, false);
  assert.strictEqual(by('wasapi').installed, true);
  assert.strictEqual(by('naudiodon2').installed, false);
  assert.strictEqual(by('pipewire').forThisPc, false);
  assert.strictEqual(by('pipewire').installed, null);                     // other system: not detectable here
  assert.ok(CATALOG.every(x => x.url === null || /^https:\/\//.test(x.url)), 'every official link is https, or none (ASIO4ALL is found in the Windows ASIO registry)');
  assert.strictEqual(by('asio4all').url, 'https://www.asio4all.org/', 'ASIO4ALL links to its official site');
  assert.ok(CATALOG.every(x => !('download' in x)) && win.every(x => !('downloadable' in x)), 'no entry offers a download any more');
  const cat = require('./catalog'); assert.deepStrictEqual(Object.keys(cat).sort(), ['CATALOG', 'isInstalled', 'listCatalog']);   // no downloader in the module
});



test('RtAudio: an API the binary was not built with is refused before RtAudio is constructed (no "no compiled support" fallback)', () => {
  const a = require('./audify');
  let made = 0;
  class RtAudio { constructor(id) { made++; this.id = id; } getApi() { return 'WINDOWS_WASAPI'; } closeStream() {} }
  RtAudio.getCompiledApi = () => [7];                     // only WASAPI compiled in
  const Api = { WINDOWS_WASAPI: 7, WINDOWS_DS: 8, WINDOWS_ASIO: 6 };
  assert.throws(() => a.openRtAudio(RtAudio, Api, 'WINDOWS_DS'), e => e.notCompiled === true && /not compiled into this audio engine/.test(e.message));
  assert.strictEqual(made, 0, 'nothing constructed for an uncompiled API');
  assert.ok(a.openRtAudio(RtAudio, Api, 'WINDOWS_WASAPI'), 'a compiled API still opens');
  assert.strictEqual(made, 1);
});

test('RtAudio: the compiled-API check accepts the binary reporting its APIs as names, not only as numbers', () => {
  const a = require('./audify');
  let made = 0;
  class RtAudio { constructor(id) { made++; this.id = id; } getApi() { return 'WINDOWS_WASAPI'; } closeStream() {} }
  RtAudio.getCompiledApi = () => ['WINDOWS_WASAPI'];       // names, as some builds report them
  const Api = { WINDOWS_WASAPI: 7, WINDOWS_DS: 8 };
  assert.ok(a.openRtAudio(RtAudio, Api, 'WINDOWS_WASAPI'), 'a compiled API reported by name still opens');
  assert.throws(() => a.openRtAudio(RtAudio, Api, 'WINDOWS_DS'), e => e.notCompiled === true);
  assert.strictEqual(made, 1);
});


test('ASIO probe (native Steinberg SDK, no RtAudio): the JSON from audiomixer-native --probe-asio is read into real channel counts and rates; bad output is refused', async () => {
  const { probeAsio, parseProbe } = require('./asio-probe');
  const good = JSON.stringify([
    { name: 'Focusrite USB ASIO', clsid: '{AAA}', inChannels: 18, outChannels: 20, sampleRates: [44100, 48000, 96000] },
    { name: 'Broken Driver', clsid: '{BBB}', inChannels: 0, outChannels: 0, sampleRates: [], error: 'did not start: init failed' },
  ]);
  const r = parseProbe(good);
  assert.strictEqual(r.ok, true);
  assert.strictEqual(r.drivers[0].inChannels, 18); assert.strictEqual(r.drivers[0].outChannels, 20);
  assert.deepStrictEqual(r.drivers[0].sampleRates, [44100, 48000, 96000]);
  assert.strictEqual(r.drivers[1].error, 'did not start: init failed');
  assert.strictEqual(parseProbe('not json').ok, false, 'non-JSON output is refused');
  assert.strictEqual(parseProbe('{"a":1}').ok, false, 'a non-list reply is refused');
  assert.deepStrictEqual(parseProbe('[{"name":"X","inChannels":-3,"outChannels":"2","sampleRates":[48000,"x",-1]}]').drivers[0],
    { name: 'X', clsid: '', inChannels: 0, outChannels: 0, sampleRates: [48000] }, 'bad counts and rates are sanitized');
  // the probe is run with the exact flag, through the injected runner
  let called = null;
  const ok = await probeAsio({ platform: 'win32', exe: 'audiomixer-native.exe', runner: async (exe, args) => { called = [exe, args]; return good; } });
  assert.deepStrictEqual(called, ['audiomixer-native.exe', ['--probe-asio']]);
  assert.strictEqual(ok.drivers.length, 2);
  // not Windows: nothing is run
  let ran = false;
  const off = await probeAsio({ platform: 'linux', runner: async () => { ran = true; return good; } });
  assert.strictEqual(off.ok, false); assert.strictEqual(ran, false, 'no probe off Windows');
  // the engine cannot run: reported, not thrown
  const dead = await probeAsio({ platform: 'win32', runner: async () => { throw new Error('ENOENT'); } });
  assert.strictEqual(dead.ok, false); assert.match(dead.error, /could not run/);
});

test('ASIO startup check: every driver gets one console line with its real channel counts, or the reason it was not read', () => {
  const { parseProbe, describeAsio } = require('./asio-probe');
  const r = parseProbe(JSON.stringify([
    { name: 'Focusrite USB ASIO', clsid: '{A}', inChannels: 18, outChannels: 20, sampleRates: [44100, 48000] },
    { name: 'Broken', clsid: '{B}', inChannels: 0, outChannels: 0, sampleRates: [], error: 'did not start' },
  ]));
  assert.deepStrictEqual(describeAsio(r), [
    'ASIO: Focusrite USB ASIO: 18 in / 20 out, 44100, 48000 Hz',
    'ASIO: Broken: did not start',
  ]);
  assert.deepStrictEqual(describeAsio(parseProbe('[]')), ['ASIO: no driver is registered on this PC']);
  assert.match(describeAsio({ ok: false, error: 'ASIO is only available on Windows', drivers: [] })[0], /not checked .*Windows/);
  const serverSrc = fsx.readFileSync(pathx.join(__dirname, 'server.js'), 'utf8');
  assert.ok(serverSrc.includes("asioProbe.probeAsio({ exe: path.join(__dirname, '..', 'audiomixer-native.exe') })"), 'the server runs the ASIO check at startup, on the engine in the install root');
  assert.ok(serverSrc.includes('describeAsio(r).forEach'), 'and logs one line per driver');
});

test('ASIO discovery: an ASIO entry reports the Windows ASIO registry names it matched; ASIO4ALL is found there and links to its official site', () => {
  const { listCatalog } = require('./catalog');
  const info = { platform: 'win32', portaudio: false, drivers: [], asio: ['ASIO4ALL v2', 'Focusrite USB ASIO'] };
  const by = id => listCatalog(info, null).find(x => x.id === id);
  const a4 = by('asio4all');
  assert.strictEqual(a4.installed, true, 'the registered ASIO4ALL is detected');
  assert.deepStrictEqual(a4.registeredAs, ['ASIO4ALL v2'], 'discovery reports the matched registry name');
  assert.strictEqual(a4.url, 'https://www.asio4all.org/');
  assert.deepStrictEqual(by('focusrite').registeredAs, ['Focusrite USB ASIO']);
  assert.strictEqual(by('wasapi').registeredAs, undefined, 'only ASIO drivers carry the discovery field');
  const none = listCatalog({ platform: 'win32', portaudio: false, drivers: [], asio: [] }, null).find(x => x.id === 'asio4all');
  assert.strictEqual(none.installed, false); assert.deepStrictEqual(none.registeredAs, []);
});

test('catalog endpoints: GET list only; the driver download route is gone', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port, base = `http://127.0.0.1:${port}`;
  const cat = await (await fetch(base + '/api/catalog')).json();
  assert.strictEqual(cat.ok, true);
  assert.ok(Array.isArray(cat.items) && cat.items.length > 5 && cat.items.every(i => !('detect' in i) && !('download' in i) && !('downloadable' in i)) && !('downloadDir' in cat));
  const post = (headers, body, origin) => fetch(base + '/api/catalog/download', { method: 'POST', headers: { 'Content-Type': 'application/json', ...(origin ? { Origin: origin } : {}), ...headers }, body });
  for (const id of ['flexasio', 'asio4all', 'focusrite']) assert.strictEqual((await post({ 'X-Mixer-Action': 'download' }, JSON.stringify({ id }))).status, 405, id);   // nothing downloads a driver
  assert.strictEqual((await post({ 'X-Mixer-Action': 'download' }, '{"id":"flexasio"}', 'https://evil.example')).status, 403);
  assert.strictEqual((await fetch(base + '/api/catalog', { method: 'DELETE' })).status, 405);
  server.closeAllConnections();
  server.close();
});

test('third-party SDK plugin slots (AudioShake, GPU Audio, Camb.ai, Clarity Voice Studio): honest configured/wired state, never a fake result', async () => {
  const sdk = require('./sdk');
  for (const id of sdk.ids()) { sdk.unregisterClient(id); delete process.env[sdk.provider(id).envKey]; }
  try {
    let st = sdk.status();
    assert.deepStrictEqual(st.map(s => s.id).sort(), ['audioshake', 'cambai', 'clarity-voice', 'gpu-audio']);
    assert.ok(st.every(s => s.configured === false && s.wired === false && s.ready === false && typeof s.purpose === 'string' && s.purpose.length > 0 && /^[A-Z0-9_]+$/.test(s.envKey)));
    // no API key, no client: process() refuses rather than inventing output
    await assert.rejects(sdk.process('audioshake', {}), /not configured.*AUDIOSHAKE_API_KEY/s);
    assert.throws(() => sdk.provider('nope'), /unknown SDK provider/);
    assert.throws(() => sdk.registerClient('cambai', {}), /client\.process must be a function/);
    // API key present but no client registered: still refuses, names the missing package
    process.env.GPU_AUDIO_API_KEY = 'test-key-123';
    assert.strictEqual(sdk.configured('gpu-audio'), true); assert.strictEqual(sdk.wired('gpu-audio'), false);
    await assert.rejects(sdk.process('gpu-audio', {}), /no SDK client installed.*@gpu-audio\/sdk/s);
    // once both a key and a real client are present, the job reaches the client and nothing else is faked
    const calls = [];
    sdk.registerClient('gpu-audio', { process: async job => { calls.push(job); return { ok: true, from: 'real-client' }; } });
    assert.strictEqual(sdk.wired('gpu-audio'), true);
    const r = await sdk.process('gpu-audio', { op: 'eq' });
    assert.deepStrictEqual(r, { ok: true, from: 'real-client' }); assert.deepStrictEqual(calls, [{ op: 'eq' }]);
    st = sdk.status(); assert.strictEqual(st.find(s => s.id === 'gpu-audio').ready, true);
    assert.ok(st.filter(s => s.id !== 'gpu-audio').every(s => s.ready === false), 'the other three providers are unaffected');
  } finally { for (const id of sdk.ids()) { sdk.unregisterClient(id); delete process.env[sdk.provider(id).envKey]; } }
});

test('GET /api/sdks reports the SDK plugin slots over HTTP', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port, base = `http://127.0.0.1:${port}`;
  const j = await (await fetch(base + '/api/sdks')).json();
  assert.strictEqual(j.ok, true);
  assert.ok(Array.isArray(j.providers) && j.providers.length === 4 && j.providers.every(p => 'configured' in p && 'wired' in p && 'ready' in p));
  assert.strictEqual((await fetch(base + '/api/sdks', { method: 'POST' })).status, 405);
  server.closeAllConnections();
  server.close();
});

test('WASAPI exclusive mode: honest status, never claims availability RtAudio does not have', async () => {
  const wasapiExclusive = require('./wasapi-exclusive');
  const st = wasapiExclusive.status();
  assert.strictEqual(typeof st.available, 'boolean'); assert.strictEqual(typeof st.wired, 'boolean');
  if (process.platform !== 'win32') { assert.deepStrictEqual(st, { available: false, wired: false, reason: 'Windows only' }); assert.throws(() => wasapiExclusive.listEndpoints(), /win32|Windows only|not built/i); }
  assert.ok(!st.available || st.reason === null || typeof st.reason === 'string');
  // bridge/audify.js itself must still say the same thing it always has: RtAudio's WASAPI is shared-mode only
  const audifySrc = fsx.readFileSync(pathx.join(__dirname, 'audify.js'), 'utf8');
  assert.match(audifySrc, /WASAPI shared mode \(the only mode RtAudio has\)/);

  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port, base = `http://127.0.0.1:${port}`;
  const j = await (await fetch(base + '/api/wasapi-exclusive')).json();
  assert.strictEqual(j.ok, true); assert.strictEqual(typeof j.available, 'boolean'); assert.strictEqual(typeof j.wired, 'boolean');
  assert.strictEqual((await fetch(base + '/api/wasapi-exclusive', { method: 'POST' })).status, 405);
  server.closeAllConnections();
  server.close();
});

test('client: argument parsing and the browser opener only accept localhost URLs', () => {
  const { parseArgs, openCommand } = require('../client/cli');
  assert.deepStrictEqual(parseArgs([]), { cmd: 'start', arg: null, port: 8765, open: true, help: false });
  const a = parseArgs(['service', 'status', '--port', '9000', '--no-open']);
  assert.strictEqual(a.cmd, 'service'); assert.strictEqual(a.arg, 'status'); assert.strictEqual(a.port, 9000); assert.strictEqual(a.open, false);
  assert.strictEqual(parseArgs(['--port', '99999']).port, 8765);
  assert.deepStrictEqual(openCommand('win32', 'http://localhost:8765/'), { cmd: 'rundll32', args: ['url.dll,FileProtocolHandler', 'http://localhost:8765/'] });
  assert.strictEqual(openCommand('linux', 'http://localhost:8765/').cmd, 'xdg-open');
  assert.strictEqual(openCommand('darwin', 'http://localhost:8765/').cmd, 'open');
  assert.strictEqual(openCommand('linux', 'https://evil.example/'), null);
  assert.strictEqual(openCommand('win32', 'http://localhost:8765/ & calc'), null);
});

test('autostart generates safe per-OS files and installs / removes them', async () => {
  const svc = require('../client/service');
  const home = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'svc-'));
  const calls = [];
  const exec = (cmd, args, o, cb) => { calls.push([cmd, ...args].join(' ')); cb(null); };
  const base = { home, env: { APPDATA: pathx.join(home, 'AppData') }, node: '/usr/bin/node', server: '/opt/audio mixer/bridge/server.js', exec };

  // Windows: there is no start at login any more: install refuses and writes nothing; an entry left by an older version is still found and removed
  await assert.rejects(svc.install({ ...base, platform: 'win32', launcher: null }), /not offered on Windows any more/);
  assert.ok(!calls.some(c => /^reg add/.test(c)) && !calls.some(c => /wscript/i.test(c)));
  assert.deepStrictEqual(fsx.readdirSync(home), []);                                   // nothing was written into the profile
  assert.strictEqual(svc.status({ ...base, platform: 'win32', reg: () => ({ status: 0 }) }).installed, true);
  assert.strictEqual(svc.status({ ...base, platform: 'win32', reg: () => ({ status: 1 }) }).installed, false);
  assert.strictEqual((await svc.uninstall({ ...base, platform: 'win32', reg: () => ({ status: 0 }) })).removed, true);
  assert.ok(calls.some(c => c.startsWith('reg delete HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run /v AudioMixerServer /f')));

  const m = await svc.install({ ...base, platform: 'darwin' });
  assert.ok(m.file.endsWith('com.audiomixer.bridge.plist'));
  assert.match(fsx.readFileSync(m.file, 'utf8'), /<string>\/usr\/bin\/node<\/string><string>\/opt\/audio mixer\/bridge\/server\.js<\/string>[\s\S]*<key>RunAtLoad<\/key><true\/>/);
  assert.ok(calls.some(c => c.startsWith('launchctl load -w ')));

  const l = await svc.install({ ...base, platform: 'linux' });
  assert.ok(l.file.endsWith(pathx.join('systemd', 'user', 'audio-mixer.service')));
  assert.match(fsx.readFileSync(l.file, 'utf8'), /ExecStart="\/usr\/bin\/node" "\/opt\/audio mixer\/bridge\/server\.js"/);
  assert.ok(calls.includes('systemctl --user enable --now audio-mixer.service'));
  assert.strictEqual(svc.status({ ...base, platform: 'linux' }).installed, true);

  assert.strictEqual((await svc.uninstall({ ...base, platform: 'linux' })).removed, true);
  assert.strictEqual(svc.status({ ...base, platform: 'linux' }).installed, false);
  assert.ok(calls.includes('systemctl --user disable --now audio-mixer.service'));

  await assert.rejects(svc.install({ ...base, platform: 'linux', node: '/usr/bin/node" --evil "' }), /unsupported characters/);
  await assert.rejects(svc.install({ ...base, platform: 'darwin', server: '/x/$(rm -rf ~)/server.js' }), /unsupported characters/);
  await assert.rejects(svc.install({ ...base, platform: 'freebsd' }), /not supported/);
  fsx.rmSync(home, { recursive: true, force: true });
});


// ── Windows installer build ──
function makeZip(entries) {      // minimal zip writer (deflate) for tests
  const zlibx = require('node:zlib'), parts = [], central = [];
  let off = 0;
  for (const [name, data] of entries) {
    const nb = Buffer.from(name), comp = zlibx.deflateRawSync(data);
    const lh = Buffer.alloc(30); lh.writeUInt32LE(0x04034b50, 0); lh.writeUInt16LE(20, 4); lh.writeUInt16LE(8, 8);
    lh.writeUInt32LE(comp.length, 18); lh.writeUInt32LE(data.length, 22); lh.writeUInt16LE(nb.length, 26);
    const ch = Buffer.alloc(46); ch.writeUInt32LE(0x02014b50, 0); ch.writeUInt16LE(20, 4); ch.writeUInt16LE(20, 6); ch.writeUInt16LE(8, 10);
    ch.writeUInt32LE(comp.length, 20); ch.writeUInt32LE(data.length, 24); ch.writeUInt16LE(nb.length, 28); ch.writeUInt32LE(off, 42);
    parts.push(lh, nb, comp); central.push(ch, nb); off += 30 + nb.length + comp.length;
  }
  const cd = Buffer.concat(central), eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0); eocd.writeUInt16LE(entries.length, 8); eocd.writeUInt16LE(entries.length, 10); eocd.writeUInt32LE(cd.length, 12); eocd.writeUInt32LE(off, 16);
  return Buffer.concat([...parts, cd, eocd]);
}

function fakeNodeFetch(zip, { badSum = false, arch = 'x64' } = {}) {
  const sum = cryptox.createHash('sha256').update(zip).digest('hex');
  return async url => {
    if (url.endsWith('/index.json')) return new Response(JSON.stringify([{ version: 'v99.0.0', lts: false, files: [`win-${arch}-zip`] }, { version: 'v98.1.2', lts: 'Fake', files: ['linux-x64', `win-${arch}-zip`] }, { version: 'v97.0.0', lts: 'Old', files: ['linux-x64'] }]));
    if (url.endsWith('/SHASUMS256.txt')) return new Response(`${badSum ? '0'.repeat(64) : sum}  node-v98.1.2-win-${arch}.zip\n${'1'.repeat(64)}  other.tar.gz\n`);
    if (url.endsWith(`node-v98.1.2-win-${arch}.zip`)) return new Response(zip);
    return new Response('', { status: 404 });
  };
}




// ── now playing, interface scan, ASIO arbitration ──
test('now playing: services are recognised from app, URL and window titles', () => {
  const np = require('./nowplaying');
  const id = (...p) => np.classify(p).id;
  assert.strictEqual(id('org.mpris.MediaPlayer2.spotify'), 'spotify');
  assert.strictEqual(id('chromium', 'https://music.youtube.com/watch?v=x'), 'youtube-music');
  assert.strictEqual(id('chromium', 'https://www.youtube.com/watch?v=x'), 'youtube');
  assert.strictEqual(id('chrome', 'Song - YouTube Music - Google Chrome'), 'youtube-music');
  assert.strictEqual(id('TIDAL.exe'), 'tidal');
  assert.strictEqual(id('com.squirrel.TIDAL.TIDAL'), 'tidal');
  assert.strictEqual(id('firefox', 'https://tidal.com/browse/track/1'), 'tidal');
  assert.strictEqual(id('AppleInc.AppleMusicWin'), 'apple-music');
  assert.strictEqual(id('Music'), 'apple-music');
  assert.strictEqual(id('deezer'), 'deezer');
  assert.strictEqual(id('vlc'), 'vlc');
  assert.strictEqual(id('something-else'), 'other');
});

test('now playing: MPRIS, SMTC and osascript output are parsed', async () => {
  const np = require('./nowplaying');
  const meta = JSON.stringify({ type: 'a{sv}', data: { 'xesam:title': { type: 's', data: 'Karma Police' }, 'xesam:artist': { type: 'as', data: ['Radiohead'] }, 'xesam:album': { type: 's', data: 'OK Computer' }, 'xesam:url': { type: 's', data: 'https://music.youtube.com/watch?v=abc' } } });
  const status = JSON.stringify({ type: 's', data: 'Playing' });
  const s = np.mprisSession('org.mpris.MediaPlayer2.chromium.instance123', status, meta);
  assert.deepStrictEqual([s.service, s.serviceName, s.status, s.title, s.artist, s.album, s.app], ['youtube-music', 'YouTube Music', 'playing', 'Karma Police', 'Radiohead', 'OK Computer', 'chromium']);

  const smtc = np.parseSmtc('SESSION|Spotify.exe|Playing|Everlong|Foo Fighters|The Colour and the Shape\nSESSION|msedge|Paused|Lofi mix|Chill Channel|\nSESSION|chrome|Playing|Cool Video|Someone|\nWINDOW|msedge|Lofi mix - YouTube - Microsoft Edge\nWINDOW|chrome|Cool Video - YouTube Music - Google Chrome\nWINDOW|notepad|notes');
  assert.deepStrictEqual(smtc.map(x => [x.service, x.status]), [['spotify', 'playing'], ['youtube', 'paused'], ['youtube-music', 'playing']]);

  const osa = np.parseOsa('APP|Spotify|playing|Song A|Artist A|Album A\nTAB|Google Chrome|https://www.youtube.com/watch?v=1|My Video - YouTube\nTAB|Safari|https://example.com|Nothing');
  assert.deepStrictEqual(osa.map(x => [x.service, x.title]), [['spotify', 'Song A'], ['youtube', 'My Video']]);

  const run = async (cmd, args) => {
    if (args.includes('list')) return 'org.mpris.MediaPlayer2.spotify 123 spotify :1.5\norg.freedesktop.Notifications 5 x :1.2\norg.mpris.MediaPlayer2.vlc 9 vlc :1.7\n';
    if (args.includes('PlaybackStatus')) return args.includes('org.mpris.MediaPlayer2.vlc') ? JSON.stringify({ type: 's', data: 'Paused' }) : status;
    return args.includes('org.mpris.MediaPlayer2.vlc') ? JSON.stringify({ type: 'a{sv}', data: { 'xesam:title': { type: 's', data: 'Track V' } } }) : meta;
  };
  const r = await np.readNowPlaying({ platform: 'linux', run });
  assert.strictEqual(r.method, 'mpris');
  assert.deepStrictEqual(r.sessions.map(x => x.service), ['youtube-music', 'vlc']);       // playing first
  assert.strictEqual(r.playing.title, 'Karma Police');
  assert.deepStrictEqual((await np.readNowPlaying({ platform: 'linux', run: async () => '' })).sessions, []);
});

test('now playing: every OS has a path (playerctl, BSD, Android media sessions, Windows window titles)', async () => {
  const np = require('./nowplaying');
  const pc = await np.readNowPlaying({ platform: 'linux', run: async cmd => cmd === 'playerctl' ? 'spotify|Playing|Song|Artist|Album|https://open.spotify.com/x\nvlc|Paused|Clip|||' : '' });
  assert.strictEqual(pc.method, 'playerctl');
  assert.deepStrictEqual(pc.sessions.map(x => [x.service, x.status, x.title]), [['spotify', 'playing', 'Song'], ['vlc', 'paused', 'Clip']]);
  assert.strictEqual((await np.readNowPlaying({ platform: 'freebsd', run: async () => '' })).method, 'playerctl');
  const dump = 'Sessions Stack - have 2 sessions:\n    package=com.spotify.music\n      state=PlaybackState {state=3, position=10}\n      metadata: size=5, description=Karma Police, Radiohead, OK Computer\n'
    + '    package=com.google.android.apps.youtube.music\n      state=PlaybackState {state=2, position=1}\n      metadata: size=3, description=Lofi, Chill\n';
  const an = await np.readNowPlaying({ platform: 'android', run: async (cmd, args) => (cmd === 'dumpsys' && args[0] === 'media_session' ? dump : '') });
  assert.strictEqual(an.method, 'media-session');
  assert.deepStrictEqual(an.sessions.map(x => [x.service, x.status, x.title, x.artist]), [['spotify', 'playing', 'Karma Police', 'Radiohead'], ['youtube-music', 'paused', 'Lofi', 'Chill']]);
  assert.strictEqual(an.playing.album, 'OK Computer');
  const wins = 'WINDOW|Spotify|Radiohead - Karma Police\nWINDOW|Spotify|Spotify Premium\nWINDOW|chrome|Lofi mix - YouTube - Google Chrome\nWINDOW|notepad|notes';
  const w = await np.readNowPlaying({ platform: 'win32', run: async () => wins });       // no media sessions API (Windows 7 / 8)
  assert.strictEqual(w.method, 'window-titles');
  assert.deepStrictEqual(w.sessions.map(x => [x.service, x.title, x.artist]), [['spotify', 'Karma Police', 'Radiohead'], ['youtube', 'Lofi mix', '']]);
  assert.strictEqual((await np.readNowPlaying({ platform: 'win32', run: async () => 'SMTC|ok\n' + wins })).method, 'smtc');   // sessions API works, nothing playing: no guessing
  assert.deepStrictEqual((await np.readNowPlaying({ platform: 'win32', run: async () => 'SMTC|ok\n' + wins })).sessions, []);
});


test('interfaces: one physical interface across ASIO / WASAPI / DirectSound, loopback flagged, ASIO-only drivers kept', () => {
  const { groupInterfaces } = require('./interfaces');
  const dev = (id, name, hostApi, inputs, outputs) => ({ id, name, hostApi, inputs, outputs, sampleRate: 48000 });
  const list = groupInterfaces([
    dev(0, 'Microphone (Focusrite USB Audio)', 'Windows DirectSound', 2, 0), dev(1, 'Speakers (Focusrite USB Audio)', 'Windows DirectSound', 0, 2),
    dev(2, 'Focusrite USB ASIO', 'ASIO', 18, 20), dev(3, 'Microphone (Focusrite USB Audio)', 'Windows WASAPI', 2, 0), dev(4, 'Speakers (Focusrite USB Audio)', 'Windows WASAPI', 0, 2),
    dev(5, 'Stereo Mix (Realtek Audio)', 'Windows WASAPI', 2, 0), dev(6, 'Speakers (Realtek Audio)', 'Windows WASAPI', 0, 2),
  ], ['Focusrite USB ASIO', 'ASIO4ALL v2']);
  const foc = list.find(i => /focusrite/i.test(i.name));
  assert.ok(foc.asio);
  assert.deepStrictEqual(foc.apis.map(a => a.api), ['ASIO', 'Windows WASAPI', 'Windows WASAPI', 'Windows DirectSound', 'Windows DirectSound']);
  assert.strictEqual(foc.read.deviceId, 2); assert.strictEqual(foc.write.deviceId, 2);          // ASIO preferred for both
  assert.strictEqual(foc.inputs, 18); assert.strictEqual(foc.outputs, 20);
  const mix = list.find(i => i.loopback);
  assert.ok(mix && /realtek/i.test(mix.name) === true || mix.name.length > 0);
  const real = list.find(i => /realtek/i.test(i.name) && !i.loopback);
  assert.ok(real || list.find(i => i.loopback));
  const a4 = list.find(i => /asio4all/i.test(i.name));
  assert.ok(a4 && a4.driverOnly && a4.asio && a4.read === null);
  assert.strictEqual(list.filter(i => /focusrite/i.test(i.name)).length, 1);                      // not split per API
  assert.strictEqual(list[list.length - 1].loopback || list.indexOf(mix) > list.indexOf(foc), true);
});

test('ASIO is single-client: a second ASIO device is refused until the first is closed', () => {
  const { claim, _owners } = require('./asio-lock');
  _owners.clear();
  const a = { id: 1, name: 'Card A', hostAPIName: 'ASIO' }, b = { id: 2, name: 'Card B', hostAPIName: 'ASIO' }, w = { id: 3, name: 'WASAPI X', hostAPIName: 'Windows WASAPI' };
  const l1 = claim(a), l1b = claim(a);                       // read + write on the same device is fine
  assert.ok(l1.ok && l1b.ok);
  const l2 = claim(b);
  assert.strictEqual(l2.ok, false); assert.match(l2.message, /Card A/);
  assert.ok(claim(w).ok);                                    // other host APIs are not limited
  l1.release(); l1.release();                                // double release is harmless
  assert.strictEqual(claim(b).ok, false);                    // still held by the second claim
  l1b.release();
  assert.ok(claim(b).ok);
  _owners.clear();
});

test('ASIO takeover: WRITE on a device takes the single-client slot from the device holding it; without the flag it is still refused', () => {
  const { claim, _owners } = require('./asio-lock');
  _owners.clear();
  const holder = { id: 1, name: 'ASIO4ALL v2', hostAPIName: 'ASIO' }, wanted = { id: 2, name: 'Audio Mixer', hostAPIName: 'ASIO' };
  let stopped = 0;
  const held = claim(holder, { preempt: () => { stopped++; held.release(); } });
  assert.ok(held.ok);
  // no flag: refused as before, the holder keeps running
  const refused = claim(wanted); assert.strictEqual(refused.ok, false); assert.strictEqual(stopped, 0);
  // takeover: the holder is stopped (its preempt releases its claim) and the wanted device gets the slot
  const taken = claim(wanted, { takeover: true, preempt: () => {} });
  assert.ok(taken.ok, 'the requested device takes over'); assert.strictEqual(stopped, 1, 'the holder was stopped exactly once');
  assert.strictEqual(_owners.size, 1); assert.ok([..._owners.keys()][0].includes('audio mixer'));
  taken.release(); assert.strictEqual(_owners.size, 0);
  // a holder that registered no preempt cannot be taken over: the request is refused and nothing is stopped
  const bare = claim(holder); assert.ok(bare.ok);
  const again = claim({ id: 3, name: 'FlexASIO', hostAPIName: 'ASIO' }, { takeover: true });
  assert.strictEqual(again.ok, false, 'no preempt registered: refused');
  bare.release(); assert.strictEqual(_owners.size, 0);
  _owners.clear();
});


test('ASIO lock keys on the driver NAME, not dev.id: the same driver seen with a different id (another engine / a re-scan) is one driver, not two', () => {
  const { claim, keyOf, _owners } = require('./asio-lock');
  _owners.clear();
  // dev.id is a per-enumeration sequential integer (listDevices: `id: next++`); it shifts across engines and re-scans.
  // The single-client limit is per physical driver, so the output side (one scan) and the input side (another scan) of the
  // SAME driver must share the lock even though their ids differ - otherwise the read+write pair is wrongly refused.
  const outSide = { id: 1000, name: 'Audio Mixer ASIO', hostAPIName: 'ASIO' };
  const inSide = { id: 7, name: 'Audio Mixer ASIO', hostAPIName: 'ASIO' };   // same driver, different engine/scan -> different id
  assert.strictEqual(keyOf(outSide), keyOf(inSide), 'the key ignores id and normalizes on name');
  const w = claim(outSide); assert.ok(w.ok);
  const r = claim(inSide);  assert.ok(r.ok, 'the same driver under a different id is NOT refused as a second driver');
  assert.strictEqual(_owners.size, 1, 'both handles share one owner slot');
  // a genuinely different driver is still refused while this one is held
  assert.strictEqual(claim({ id: 1, name: 'FlexASIO', hostAPIName: 'ASIO' }).ok, false);
  r.release(); assert.strictEqual(_owners.size, 1, 'still held by the output side');
  w.release(); assert.strictEqual(_owners.size, 0, 'released once both handles are closed');
  // name is compared case/space-insensitively, host API still gates (WASAPI is never limited)
  assert.strictEqual(keyOf({ name: ' Audio Mixer ASIO ', hostAPIName: 'asio' }), keyOf(outSide));
  assert.ok(claim({ id: 2, name: 'Audio Mixer ASIO', hostAPIName: 'Windows WASAPI' }).ok, 'a WASAPI device of the same name is not ASIO-limited');
  _owners.clear();
});

test('input sessions share the ASIO lock', () => {
  const { _owners } = require('./asio-lock');
  _owners.clear();
  const mkPa = () => ({ SampleFormat16Bit: 8, getDevices: () => [{ id: 1, name: 'Card A', hostAPIName: 'ASIO', maxInputChannels: 2 }, { id: 2, name: 'Card B', hostAPIName: 'ASIO', maxInputChannels: 2 }],
    AudioIO: class { constructor() { this.h = {}; } on() {} start() {} quit() {} } });
  const mk = () => { const sent = []; return { sent, s: require('./input').createInputSession({ send: m => sent.push(JSON.parse(m)), sendBinary() {} }, mkPa) }; };
  const one = mk(), two = mk();
  one.s.onText(JSON.stringify({ type: 'start', deviceId: 1 }));
  two.s.onText(JSON.stringify({ type: 'start', deviceId: 2 }));
  assert.strictEqual(one.sent[0].type, 'started');
  assert.strictEqual(two.sent[0].type, 'error'); assert.match(two.sent[0].message, /one driver at a time/);
  one.s.onClose();
  two.s.onText(JSON.stringify({ type: 'start', deviceId: 2 }));
  assert.strictEqual(two.sent[1].type, 'started');
  two.s.onClose();
  assert.strictEqual(_owners.size, 0);
});

test('endpoints: /api/nowplaying and /api/interfaces', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const np = await (await fetch(base + '/api/nowplaying')).json();
  assert.strictEqual(np.ok, true); assert.ok(Array.isArray(np.sessions) && 'platform' in np);
  const itf = await (await fetch(base + '/api/interfaces')).json();
  assert.strictEqual(itf.ok, true); assert.ok(Array.isArray(itf.interfaces) && Array.isArray(itf.asio));
  assert.strictEqual((await fetch(base + '/api/nowplaying', { headers: { Origin: 'https://evil.example' } })).status, 403);
  server.closeAllConnections();
  server.close();
});

test('verify: manifest pass, tamper, missing and extra files', () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path');
  const v = require('../client/verify');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'vfy-'));
  fs.mkdirSync(path.join(root, 'bridge'));
  fs.writeFileSync(path.join(root, 'bridge', 'a.js'), 'x');
  fs.writeFileSync(path.join(root, 'MANIFEST.sha256'), `${v.sha256(path.join(root, 'bridge', 'a.js'))}  bridge/a.js\n`);
  assert.strictEqual(v.checkManifest(root)[0].level, 'PASS');
  fs.writeFileSync(path.join(root, 'bridge', 'b.js'), 'evil');
  assert.ok(v.checkManifest(root).some(r => r.level === 'WARN' && /b\.js/.test(r.detail)));
  fs.writeFileSync(path.join(root, 'bridge', 'a.js'), 'changed');
  assert.ok(v.checkManifest(root).some(r => r.level === 'FAIL' && r.title === 'File changed since it was built'));
  fs.unlinkSync(path.join(root, 'bridge', 'a.js'));
  assert.ok(v.checkManifest(root).some(r => r.title === 'File missing'));
  fs.writeFileSync(path.join(root, 'MANIFEST.sha256'), `${'0'.repeat(64)}  ../x\n`);
  assert.ok(v.checkManifest(root).some(r => /escapes/.test(r.title)));
  fs.rmSync(root, { recursive: true });
});


test('verify: pinned self-signed certificate is accepted only when it is the published one', async () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), crypto = require('node:crypto');
  const v = require('../client/verify');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'vfy-'));
  const der = crypto.randomBytes(300); fs.writeFileSync(path.join(dir, 'AudioMixer-signing.cer'), der);
  const pin = crypto.createHash('sha1').update(der).digest('hex').toUpperCase();
  assert.strictEqual(v.pinnedThumbprint(path.join(dir, 'x.exe'), {}), pin);
  fs.writeFileSync(path.join(dir, 'pem.cer'), '-----BEGIN CERTIFICATE-----\n' + der.toString('base64') + '\n-----END CERTIFICATE-----\n');
  assert.strictEqual(v.pinnedThumbprint('x', { AUDIO_MIXER_SIGNING_CER: path.join(dir, 'pem.cer') }), pin);
  assert.strictEqual(v.pinnedThumbprint(path.join(os.tmpdir(), 'nothing-here', 'x.exe'), {}), null);
  const run = out => (cmd, args, opts, cb) => cb(null, out);
  const sig = (out, pinned) => v.checkSignature('x', 'Installer', { platform: 'win32', run: run(out), pinned });
  assert.strictEqual((await sig(`UnknownError|CN=Audio Mixer (self-signed)|${pin}`, pin))[0].level, 'PASS');       // untrusted root, but the published certificate
  assert.strictEqual((await sig(`UnknownError|CN=Someone|${'AB'.repeat(20)}`, pin))[0].level, 'FAIL');            // some other self-signed certificate
  assert.strictEqual((await sig(`Valid|CN=Someone|${'AB'.repeat(20)}`, pin))[0].level, 'WARN');                   // trusted, but not ours
  assert.strictEqual((await sig(`Valid|CN=Audio Mixer|${pin}`, pin))[0].level, 'PASS');
  assert.strictEqual((await sig(`HashMismatch|CN=Audio Mixer|${pin}`, pin))[0].level, 'FAIL');                    // changed after signing
  assert.strictEqual((await sig('NotSigned||', pin))[0].level, 'WARN');
  fs.rmSync(dir, { recursive: true, force: true });
});

test('verify: parsers and injected PowerShell results', async () => {
  const v = require('../client/verify');
  assert.deepStrictEqual(v.parseSignature('Valid|CN=OpenJS Foundation'), { status: 'Valid', subject: 'CN=OpenJS Foundation', thumbprint: '' });
  assert.deepStrictEqual(v.parseSignature('UnknownError|CN=A, O=B|ab12'), { status: 'UnknownError', subject: 'CN=A, O=B', thumbprint: 'AB12' });
  const run = out => (cmd, args, opts, cb) => cb(null, out);
  assert.strictEqual((await v.checkSignature('x', 'Runtime', { platform: 'win32', run: run('Valid|CN=OpenJS Foundation'), expect: /OpenJS/ }))[0].level, 'PASS');
  assert.strictEqual((await v.checkSignature('x', 'Runtime', { platform: 'win32', run: run('Valid|CN=Someone Else'), expect: /OpenJS/ }))[0].level, 'WARN');
  assert.strictEqual((await v.checkSignature('x', 'Installer', { platform: 'win32', run: run('NotSigned|') }))[0].level, 'WARN');
  assert.strictEqual((await v.checkSignature('x', 'Installer', { platform: 'win32', run: run('HashMismatch|CN=x') }))[0].level, 'FAIL');
});

test('verify: loopback-only check and cli flag parsing', async () => {
  const v = require('../client/verify');
  const srv = require('node:net').createServer().listen(0, '127.0.0.1');
  await new Promise(r => srv.on('listening', r));
  const r = await v.checkLoopbackOnly(srv.address().port);
  assert.strictEqual(r[0].level, 'PASS');
  srv.close();
  const closed = await v.checkLoopbackOnly(1);
  assert.strictEqual(closed[0].level, 'INFO');
});

// ── Audify (RtAudio) engine ──
function fakeAudify({ failSizes = [], driverSize = 192 } = {}) {
  const opened = [], written = [];
  class RtAudio {
    constructor(api) { this.api = api; }
    getApi() { return ({ 1: 'Core Audio', 6: 'ASIO', 7: 'WASAPI', 8: 'DirectSound', 3: 'JACK' })[this.api] || 'Dummy'; }
    getDevices() {
      return this.api === 6 ? [{ id: 0, name: 'Focusrite USB ASIO', inputChannels: 18, outputChannels: 20, sampleRates: [44100, 48000, 96000], preferredSampleRate: 48000, isDefaultInput: 1, isDefaultOutput: 1 }]
        : this.api === 7 ? [{ id: 5, name: 'Speakers (Realtek)', inputChannels: 0, outputChannels: 2, sampleRates: [48000], preferredSampleRate: 48000, isDefaultInput: 0, isDefaultOutput: 1 }] : [];
    }
    openStream(out, inp, fmt, rate, frames, name, cb) {
      if (failSizes.includes(frames)) throw new Error('bad buffer size');
      opened.push({ api: this.api, out, inp, rate, frames }); this.cb = cb;
      return frames === 0 ? driverSize : frames;
    }
    start() {} stop() {} closeStream() {} isStreamRunning() { return true; }
    write(b) { written.push(b.length); }
  }
  return { RtAudio, RtAudioApi: { UNSPECIFIED: 0, MACOSX_CORE: 1, LINUX_ALSA: 2, UNIX_JACK: 3, LINUX_PULSE: 4, LINUX_OSS: 5, WINDOWS_ASIO: 6, WINDOWS_WASAPI: 7, WINDOWS_DS: 8, RTAUDIO_DUMMY: 9 },
    RtAudioFormat: { RTAUDIO_SINT16: 2 }, RtAudioStreamFlags: { RTAUDIO_MINIMIZE_LATENCY: 2 }, opened, written };
}

test('audify: automatic frame size allocation', () => {
  const a = require('./audify');
  assert.strictEqual(a.recommendFrameSize({ api: 'ASIO', sampleRate: 48000 }).frames, 256);
  assert.strictEqual(a.recommendFrameSize({ api: 'ASIO', sampleRate: 96000, channels: 2 }).frames, 512);
  assert.strictEqual(a.recommendFrameSize({ api: 'Windows WASAPI', sampleRate: 48000 }).frames, 512);
  assert.strictEqual(a.recommendFrameSize({ api: 'DirectSound', sampleRate: 48000 }).frames, 1024);
  assert.ok(a.recommendFrameSize({ api: 'ASIO', sampleRate: 48000, channels: 32 }).frames > 256);   // wide interfaces get bigger blocks
  assert.strictEqual(a.recommendFrameSize({ api: 'ASIO', sampleRate: 48000, latencyMs: 0.1 }).frames, 32);
  assert.strictEqual(a.recommendFrameSize({ api: 'ASIO', sampleRate: 384000, latencyMs: 500 }).frames, 4096);
  const c = a.frameCandidates(256);
  assert.deepStrictEqual(c.slice(0, 3), [256, 512, 1024]); assert.ok(c.includes(128) && !c.includes(256 * 2 * 2 * 2 * 2 * 2));
  assert.deepStrictEqual(a.plan('auto', { api: 'ASIO' }).candidates[0], 0);          // ASIO: ask the driver's own buffer size first
  assert.notStrictEqual(a.plan('auto', { api: 'WASAPI' }).candidates[0], 0);
  assert.deepStrictEqual(a.plan(128, { api: 'ASIO' }).candidates, [128]);
  assert.throws(() => a.plan(100, {}), /power of two/);
});

test('audify: devices of every API, unique ids, substituted APIs skipped', () => {
  const a = require('./audify');
  const r = a.listDevices(() => fakeAudify());
  assert.deepStrictEqual(r.devices.map(d => [d.id, d.hostAPIName]), [[1000, 'ASIO'], [1001, 'Windows WASAPI']]);
  assert.strictEqual(a.detectAudify(() => fakeAudify()).devices[0].inputs, 18);
  assert.strictEqual(a.listDevices(() => { throw new Error('missing'); }), null);
  const d = a.describe(() => fakeAudify());
  assert.strictEqual(d.installed, true); assert.strictEqual(d.devices[0].recommended.output.frames, 512);   // 20 ch -> doubled
});

test('audify: opens with the driver buffer, retries other sizes, aligns writes', () => {
  const a = require('./audify');
  const dev = a.listDevices(() => fakeAudify()).devices[0];
  const f1 = fakeAudify();
  const s1 = a.openStream({ mod: f1, dev, direction: 'output', channels: 2, sampleRate: 48000 });
  assert.strictEqual(s1.frameSize, 192); assert.strictEqual(f1.opened[0].frames, 0); assert.strictEqual(s1.auto, true);
  s1.write(Buffer.alloc(192 * 4 + 10)); s1.write(Buffer.alloc(200));                    // 778 bytes then 200 more: one whole block (768) so far
  assert.deepStrictEqual(f1.written, [768]);
  s1.write(Buffer.alloc(768)); assert.deepStrictEqual(f1.written, [768, 768]);
  const f2 = fakeAudify({ failSizes: [0, 256] });
  const s2 = a.openStream({ mod: f2, dev, direction: 'output', channels: 2, sampleRate: 48000 });
  assert.strictEqual(s2.frameSize, 512); assert.deepStrictEqual(s2.tried, [0, 256, 512]);
  assert.throws(() => a.openStream({ mod: fakeAudify({ failSizes: [128] }), dev, direction: 'output', channels: 2, sampleRate: 48000, frameSize: 128 }), /could not open/);
  assert.throws(() => a.openStream({ mod: fakeAudify(), dev, direction: 'output', channels: 2, sampleRate: 88200 }), /does not support 88200/);
  assert.throws(() => a.openStream({ mod: fakeAudify(), dev, direction: 'output', channels: 30, sampleRate: 48000 }), /only 20 output/);
  const cap = a.openStream({ mod: f1, dev, direction: 'input', channels: 40, sampleRate: 48000, onData() {} });
  assert.strictEqual(cap.channels, 18);
});

test('audify: output and input sessions, engine choice, ASIO lock', () => {
  const { _owners } = require('./asio-lock'); _owners.clear();
  const fa = fakeAudify();
  const mk = (kind) => { const sent = [], bin = []; const conn = { send: m => sent.push(JSON.parse(m)), sendBinary: b => bin.push(b) };
    const noPa = () => { throw new Error('no naudiodon2'); };
    return { sent, bin, s: kind === 'out' ? require('./output').createSession(conn, noPa, () => fa) : require('./input').createInputSession(conn, noPa, () => fa) }; };
  const out = mk('out');
  out.s.onText(JSON.stringify({ type: 'start', channels: 2, sampleRate: 48000 }));        // no naudiodon2 -> Audify, ASIO preferred
  assert.strictEqual(out.sent[0].type, 'started'); assert.strictEqual(out.sent[0].engine, 'audify');
  assert.strictEqual(out.sent[0].hostApi, 'ASIO'); assert.strictEqual(out.sent[0].frameSize, 192); assert.strictEqual(out.sent[0].autoFrameSize, true);
  const inp = mk('in');
  inp.s.onText(JSON.stringify({ type: 'start', channels: 2, sampleRate: 48000, deviceId: 1001 }));   // WASAPI is not ASIO: no lock conflict
  assert.strictEqual(inp.sent[0].type, 'error');                                          // speakers have no input channels
  const inp2 = mk('in');
  inp2.s.onText(JSON.stringify({ type: 'start', channels: 2, sampleRate: 48000, deviceId: 1000, frameSize: 'auto' }));
  assert.strictEqual(inp2.sent[0].type, 'started'); assert.strictEqual(inp2.sent[0].channels, 2);
  out.s.onClose(); inp2.s.onClose(); inp.s.onClose();
  assert.strictEqual(_owners.size, 0);
  const bad = mk('out');
  bad.s.onText(JSON.stringify({ type: 'start', engine: 'naudiodon' }));
  assert.match(bad.sent[0].message, /PortAudio not installed/);
  const missing = mk('out');
  missing.s.onText(JSON.stringify({ type: 'start', deviceId: 1099 }));
  assert.strictEqual(missing.sent[0].message, 'device not found');
});

test('endpoints: /api/audify and /api/framesize', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const au = await (await fetch(base + '/api/audify')).json();
  assert.strictEqual(au.ok, true); assert.strictEqual(typeof au.installed, 'boolean'); assert.ok(Array.isArray(au.devices));
  const fs1 = await (await fetch(base + '/api/framesize?api=ASIO&sampleRate=96000&channels=2')).json();
  assert.strictEqual(fs1.ok, true); assert.strictEqual(fs1.frames, 512); assert.strictEqual(fs1.candidates[0], 0);
  assert.strictEqual((await fetch(base + '/api/framesize?api=nope')).status, 400);
  assert.strictEqual((await fetch(base + '/api/framesize?sampleRate=5')).status, 400);
  assert.strictEqual((await fetch(base + '/api/framesize', { headers: { Origin: 'https://evil.example' } })).status, 403);
  server.closeAllConnections(); server.close();
});

// ── plugin system (VST3 / VST2 .vst3 / .dll) ──
function fakePe({ machine = 0x8664, exports = [] } = {}) {
  const b = Buffer.alloc(0x1000);
  b.write('MZ', 0); b.writeUInt32LE(0x80, 0x3c);
  b.write('PE\0\0', 0x80, 'latin1'); b.writeUInt16LE(machine, 0x84); b.writeUInt16LE(1, 0x86); b.writeUInt16LE(0xf0, 0x94); b.writeUInt16LE(0x2022, 0x96);
  b.writeUInt16LE(0x20b, 0x98);                                   // PE32+
  const dd = 0x98 + 112; b.writeUInt32LE(0x2000, dd);             // export directory RVA
  const sec = 0x98 + 0xf0; b.write('.edata', sec); b.writeUInt32LE(0x1000, sec + 8); b.writeUInt32LE(0x2000, sec + 12); b.writeUInt32LE(0x1000, sec + 16); b.writeUInt32LE(0x400, sec + 20);
  // section raw data at file offset 0x400 maps RVA 0x2000
  const ed = 0x400; b.writeUInt32LE(exports.length, ed + 24); b.writeUInt32LE(0x2000 + 0x100, ed + 32);
  let str = 0x400 + 0x200;
  exports.forEach((n, i) => { b.writeUInt32LE(0x2000 + 0x200 + (str - 0x600), 0x400 + 0x100 + i * 4); b.write(n + '\0', str, 'latin1'); str += n.length + 1; });
  return b;
}

test('plugin system: validates VST3 / VST2 binaries and rejects plain DLLs', () => {
  const plugins = require('./plugins');
  const root = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'plg-'));
  const app = pathx.join(root, 'AudioMixerPlugins'); fsx.mkdirSync(pathx.join(app, 'sub'), { recursive: true });
  fsx.writeFileSync(pathx.join(app, 'Comp.vst3'), fakePe({ exports: ['GetPluginFactory', 'InitDll'] }));
  fsx.writeFileSync(pathx.join(app, 'sub', 'Verb.dll'), fakePe({ exports: ['VSTPluginMain'] }));
  fsx.writeFileSync(pathx.join(app, 'Old32.dll'), fakePe({ machine: 0x14c, exports: ['main'] }));
  fsx.writeFileSync(pathx.join(app, 'helper.dll'), fakePe({ exports: ['SomethingElse'] }));
  fsx.writeFileSync(pathx.join(app, 'fake.vst3'), 'text file');
  const r = plugins.scan({ platform: 'win32', env: { AUDIO_MIXER_PLUGINS: app, ProgramFiles: pathx.join(root, 'pf'), CommonProgramFiles: pathx.join(root, 'cf') }, home: root, hostArch: 'x64', hash: true });
  const by = n => r.plugins.find(p => p.name === n);
  assert.deepStrictEqual([by('Comp').format, by('Comp').valid, by('Comp').compatible, by('Comp').arch, by('Comp').entry], ['VST3', true, true, 'x64', 'GetPluginFactory']);
  assert.deepStrictEqual([by('Verb').format, by('Verb').valid, by('Verb').entry], ['VST2', true, 'VSTPluginMain']);
  assert.strictEqual(by('Old32').compatible, false); assert.match(by('Old32').reason, /x86 plugin cannot be loaded by a x64 host/);
  assert.strictEqual(by('helper').valid, false); assert.match(by('helper').reason, /plain DLL/);
  assert.strictEqual(by('fake').valid, false);
  assert.strictEqual(r.counts.loadable, 2); assert.strictEqual(r.counts.rejected, 3);
  assert.match(by('Comp').sha256, /^[0-9a-f]{64}$/); assert.strictEqual(r.appDirExists, true);
  assert.strictEqual(plugins.inspectPe(pathx.join(root, 'missing.dll')), null);
  fsx.rmSync(root, { recursive: true, force: true });
});

test('endpoint: /api/plugins', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const j = await (await fetch(base + '/api/plugins')).json();
  assert.strictEqual(j.ok, true); assert.ok(Array.isArray(j.plugins) && Array.isArray(j.dirs) && typeof j.counts.total === 'number');
  assert.strictEqual((await fetch(base + '/api/plugins', { headers: { Origin: 'https://evil.example' } })).status, 403);
  server.closeAllConnections(); server.close();
});


test('verify: native files under bridge/node_modules must be in the manifest', () => {
  const v = require('../client/verify');
  const root = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'vfy-'));
  fsx.mkdirSync(pathx.join(root, 'bridge', 'node_modules', 'audify', 'build', 'Release'), { recursive: true });
  const f = pathx.join(root, 'bridge', 'node_modules', 'audify', 'build', 'Release', 'audify.node'); fsx.writeFileSync(f, 'x');
  fsx.writeFileSync(pathx.join(root, 'MANIFEST.sha256'), '\n');
  assert.ok(v.checkManifest(root).some(r => r.level === 'WARN' && /audify\.node/.test(r.detail)));
  fsx.writeFileSync(pathx.join(root, 'MANIFEST.sha256'), `${v.sha256(f)}  bridge/node_modules/audify/build/Release/audify.node\n`);
  assert.strictEqual(v.checkManifest(root)[0].level, 'PASS');
  assert.ok(!v.checkManifest(root).some(r => r.level === 'WARN'));
  fsx.rmSync(root, { recursive: true, force: true });
});



test('cli: license / plugins / update commands exist and the stub has the feature switches', () => {
  const cli = fsx.readFileSync(pathx.join(__dirname, '..', 'client', 'cli.js'), 'utf8');
  for (const c of ["'license'", "'plugins'", "'update'"]) assert.ok(cli.includes(`o.cmd === ${c}`));
  const stub = fsx.readFileSync(pathx.join(__dirname, '..', 'platforms', 'windows', 'installer', 'setup-stub.c'), 'utf8');
  for (const sw of ['/noplugins', '/nohelpers', '/notools', '/noshortcuts', '/desktop', '/nodesktop', '/noautostart']) assert.ok(stub.includes(`L"${sw}"`), sw);
  assert.match(stub, /ADDLOCAL=Main/);
  assert.match(stub, /stop_old_server\(old\)/); assert.match(stub, /_wcsnicmp\(path, dir, n\)/);   // upgrade ends only processes that run from the install folder
});







test('interfaces: an endpoint of known direction whose channel count cannot be read is still READ / WRITE capable (stereo)', async () => {
  const wn = require('./winnative');
  const out = JSON.stringify({ ok: true, devices: [
    { id: 'a', name: 'Speakers (USB DAC)', kind: 'output', channels: 0, sampleRate: 0, default: false },
    { id: 'b', name: 'Microphone (Busy Interface)', kind: 'input', channels: 0, sampleRate: 0, default: false },
    { id: 'c', name: 'Line (Scarlett)', kind: 'output', channels: 8, sampleRate: 48000, default: true } ] });
  const r = await wn.listEndpoints({ platform: 'win32', arch: 'x64', exists: () => true, run: async () => out });
  assert.deepStrictEqual(r.devices.map(d => [d.inputs, d.outputs]), [[0, 2], [2, 0], [0, 8]]);   // 0 channels reported -> stereo, a real count is kept
  const sa = require('./sysaudio');
  const mac = sa.fromHelper ? sa.fromHelper(JSON.stringify({ ok: true, devices: [{ name: 'X', kind: 'output', channels: 0 }] })) : [{ outputs: 2 }];
  assert.strictEqual(mac[0].outputs, 2);
  const { groupInterfaces } = require('./interfaces');
  const g = groupInterfaces(r.devices);
  const dac = g.find(i => /USB DAC/.test(i.name));
  assert.ok(dac.outputs === 2 && dac.inputs === 0 && dac.write && !dac.read);                    // a DAC: write only, detected
});



test('verify: .msi installer file check', () => {
  const v = require('../client/verify');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'vfy-')), f = pathx.join(dir, 'A.msi');
  fsx.writeFileSync(f, Buffer.concat([Buffer.from([0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1]), Buffer.alloc(64)]));
  assert.ok(v.checkInstallerFile(f).every(r => r.level !== 'FAIL' && r.level !== 'WARN'));
  fsx.writeFileSync(f, 'MZ not an msi');
  assert.ok(v.checkInstallerFile(f).some(r => r.level === 'FAIL'));
  fsx.rmSync(dir, { recursive: true, force: true });
});

test('live status: open native streams are listed in /api/status and removed on stop', async () => {
  const fa = fakeAudify();
  const sent = [];
  const out = require('./output').createSession({ send: m => sent.push(JSON.parse(m)), sendBinary() {} }, () => { throw new Error('no pa'); }, () => fa);
  const streams = require('./streams');
  const base = streams.list().length;   // other tests may leave sessions open
  require('./asio-lock')._owners.clear();
  out.onText(JSON.stringify({ type: 'start', channels: 2, sampleRate: 48000 }));
  const l = streams.list().filter(x => x.engine === 'audify');
  assert.strictEqual(streams.list().length, base + 1); assert.strictEqual(l.length, 1); assert.strictEqual(l[0].hostApi, 'ASIO'); assert.strictEqual(l[0].frameSize, 192);
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const st = await (await fetch(`http://127.0.0.1:${server.address().port}/api/status`)).json();
  assert.strictEqual(st.streams.length, base + 1); assert.strictEqual(st.node, process.version); assert.ok(st.uptimeSec >= 0 && st.time > 0);
  server.closeAllConnections(); server.close();
  out.onClose();
  assert.strictEqual(streams.list().length, base);
});

// ── signing and the setup program ──
const toolOk = (cmd, args) => { try { return require('node:child_process').spawnSync(cmd, args, { encoding: 'utf8' }).status !== null; } catch (_) { return false; } };

test('signing: certificate handling and thumbprint', () => {
  const sg = require('../scripts/sign');
  assert.strictEqual(sg.thumbprint(Buffer.from('abc')), 'A9993E364706816ABA3E25717850C26C9CD0D89D');
  assert.throws(() => sg.ensureSigningCert({ dir: osx.tmpdir(), env: { SIGN_PFX: '/nonexistent/file.pfx' } }), /SIGN_PFX file not found/);
  const id = sg.ensureSigningCert({ dir: osx.tmpdir(), env: { SIGN_PFX: __filename, SIGN_PFX_PASSWORD: 'x' } });
  assert.deepStrictEqual([id.mode, id.selfSigned], ['pfx', false]);
});


// ── Linux / macOS packages ──



// ── automatic Node.js install, levels, universal ASIO driver ──


test('audify load problems come with the fix (Visual C++ runtime on Windows)', () => {
  const a = require('./audify');
  assert.strictEqual(a.loadProblem(() => ({})), null);
  const dll = a.loadProblem(() => { throw new Error('The specified module could not be found.\n\\?\\C:\\x\\audify.node'); }, 'win32');
  assert.match(dll.hint, /VCRedist/); assert.ok(!/\n/.test(dll.error));
  assert.match(a.loadProblem(() => { throw new Error("Cannot find module 'audify'"); }, 'linux').hint, /not installed/);
  const d = a.describe(() => { throw new Error("Cannot find module 'audify'"); });
  assert.strictEqual(d.installed, false); assert.ok(d.hint);
});

test('levels: peak / rms per channel in dBFS, clipping, reset after each report', () => {
  const lv = require('./levels');
  const m = new lv.Meter(2);
  const buf = Buffer.alloc(8 * 4);                                   // 8 stereo frames
  for (let i = 0; i < 8; i++) { buf.writeInt16LE(16384, i * 4); buf.writeInt16LE(i === 3 ? 32767 : 0, i * 4 + 2); }
  m.push(buf);
  const s = m.snapshot();
  assert.strictEqual(s.peak[0], -6); assert.strictEqual(s.rms[0], -6);   // half scale = -6.0 dBFS
  assert.ok(s.peak[1] > -0.1); assert.ok(s.rms[1] < -8.9 && s.rms[1] > -9.2); assert.strictEqual(s.clip, 1);
  const again = m.snapshot();
  assert.deepStrictEqual(again.peak, [lv.FLOOR, lv.FLOOR]); assert.strictEqual(again.clip, 0);
  m.push(Buffer.alloc(3)); assert.strictEqual(m.snapshot().frames, 0);  // a partial frame is ignored
  assert.strictEqual(lv.dbfs(0), lv.FLOOR);
});

test('virtual audio devices: a driver connects to /ws/virtual, the mixer reads what applications play and writes what they record; listed as an interface', async () => {
  const { createHub } = require('./virtual');
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port, base = `http://127.0.0.1:${port}`;
  const open = (path) => new Promise((res, rej) => { const ws = new WebSocket(`ws://127.0.0.1:${port}${path}`); ws.binaryType = 'arraybuffer'; ws.msgs = []; ws.bins = []; ws.onmessage = e => (typeof e.data === 'string' ? ws.msgs.push(JSON.parse(e.data)) : ws.bins.push(Buffer.from(e.data))); ws.onopen = () => res(ws); ws.onerror = () => rej(new Error('connect')); });
  const until = async (f, ms = 3000) => { const t0 = Date.now(); while (!f()) { if (Date.now() - t0 > ms) throw new Error('timeout'); await new Promise(r => setTimeout(r, 10)); } };
  const sock = [];
  try {
    assert.deepStrictEqual(await (await fetch(base + '/api/virtual')).json(), { ok: true, devices: [] });
    // the driver says hello (a bad hello is refused, a second driver of the same name too)
    const drv = await open('/ws/virtual'); sock.push(drv);
    drv.send(JSON.stringify({ type: 'hello', name: 'Audio Mixer', channels: 99, rate: 48000 })); await until(() => drv.msgs.length === 1); assert.match(drv.msgs[0].message, /channels/);
    drv.send(JSON.stringify({ type: 'hello', name: 'Audio Mixer', channels: 2, rate: 48000 })); await until(() => drv.msgs.length === 2);
    assert.deepStrictEqual([drv.msgs[1].type, drv.msgs[1].id], ['ready', 9000]);
    const dup = await open('/ws/virtual'); sock.push(dup); dup.send(JSON.stringify({ type: 'hello', name: 'Audio Mixer', channels: 2, rate: 48000 })); await until(() => dup.msgs.length === 1); assert.match(dup.msgs[0].message, /already connected/);
    const list = (await (await fetch(base + '/api/virtual')).json()).devices; assert.deepStrictEqual([list.length, list[0].id, list[0].channels, list[0].rate], [1, 9000, 2, 48000]);
    // it shows up as an interface that can be read and written
    const ifs = (await (await fetch(base + '/api/interfaces')).json()).interfaces.find(i => /Audio Mixer/.test(i.name) && i.apis.some(a => a.api === 'Audio Mixer Virtual'));
    assert.ok(ifs && ifs.inputs === 2 && ifs.outputs === 2 && ifs.read && ifs.write && ifs.apis[0].deviceId === 9000, JSON.stringify(ifs));
    // the mixer reads the device: 2 stereo frames played by an application reach the page; a half frame is ignored
    const inp = await open('/ws/input'); sock.push(inp); inp.send(JSON.stringify({ type: 'start', deviceId: 9000, channels: 2, sampleRate: 48000 }));
    await until(() => inp.msgs.some(m => m.type === 'started')); const st = inp.msgs.find(m => m.type === 'started'); assert.deepStrictEqual([st.engine, st.device, st.hostApi, st.sampleRate, st.channels], ['virtual', 'Audio Mixer', 'Audio Mixer Virtual', 48000, 2]);
    const pcm = Buffer.alloc(16); [100, -100, 200, -200, 300, -300, 400, -400].forEach((v, i) => pcm.writeInt16LE(v, i * 2));
    drv.send(pcm.subarray(0, 6)); drv.send(pcm); await until(() => inp.bins.length >= 1);
    assert.ok(Buffer.concat(inp.bins).equals(pcm), 'the frames arrive unchanged');
    assert.strictEqual((await (await fetch(base + '/api/virtual')).json()).devices[0].capturing, true);
    // the mixer writes the device: what the page sends is delivered to the driver
    const out = await open('/ws/output'); sock.push(out); out.send(JSON.stringify({ type: 'start', deviceId: 9000, channels: 2, sampleRate: 48000 }));
    await until(() => out.msgs.some(m => m.type === 'started')); out.send(pcm); await until(() => drv.bins.length >= 1); assert.ok(drv.bins[0].equals(pcm));
    // channel and rate conversion: a mono page reading a stereo device gets the mix-down
    const mono = await open('/ws/input'); sock.push(mono); mono.send(JSON.stringify({ type: 'start', deviceId: 9000, channels: 1, sampleRate: 48000 }));
    await until(() => mono.msgs.some(m => m.type === 'started')); drv.send(pcm); await until(() => mono.bins.length >= 1); assert.strictEqual(mono.bins[0].readInt16LE(0), 0); assert.strictEqual(mono.bins[0].length, 8);   // (100 - 100) / 2 ...
    // levels of the virtual stream are reported like any other
    await until(() => inp.msgs.some(m => m.type === 'levels' && m.hostApi === 'Audio Mixer Virtual'));
    // the driver goes away: the readers are told, the device disappears from the lists
    drv.close(); await until(() => inp.msgs.some(m => m.type === 'error' && /disconnected/.test(m.message)));
    await until(async () => true); await new Promise(r => setTimeout(r, 100));
    assert.deepStrictEqual((await (await fetch(base + '/api/virtual')).json()).devices, []);
    const gone = await open('/ws/input'); sock.push(gone); gone.send(JSON.stringify({ type: 'start', deviceId: 9000 })); await until(() => gone.msgs.some(m => m.type === 'error')); assert.match(gone.msgs.at(-1).message, /not connected/);
    // a web page cannot pose as a driver: a connection with an Origin header is refused
    const net = require('node:net');
    const refused = await new Promise(res => { const s = net.connect(port, '127.0.0.1', () => s.write(`GET /ws/virtual HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nOrigin: http://localhost:3000\r\n\r\n`)); let got = ''; s.on('data', d => { got += d; }); s.on('close', () => res(!/101/.test(got))); setTimeout(() => { s.destroy(); }, 600); });
    assert.strictEqual(refused, true);
    // the hub on its own: ids are stable per name
    const h = createHub(); assert.strictEqual(h.isVirtualId(9000), true); assert.strictEqual(h.isVirtualId(1000), false); assert.strictEqual(h.get(9000), null);
  } finally { sock.forEach(s => { try { s.close(); } catch (_) { /* closed */ } }); server.closeAllConnections(); server.close(); }
});

test('driver link library (drivers/common/am_link.c, C): a real native client connects to the bridge, plays PCM to the mixer and records what the mixer sends', async (tc) => {
  const cp = require('node:child_process'), root = pathx.join(__dirname, '..', 'drivers', 'common'), exe = pathx.join(osx.tmpdir(), 'am_link_test_' + process.pid);
  const cc = cp.spawnSync('gcc', ['-std=gnu99', '-Wall', '-Wextra', '-Werror', '-O1', '-o', exe, pathx.join(root, 'am_link.c'), pathx.join(root, 'test', 'link_test.c'), '-lpthread'], { encoding: 'utf8' });
  if (cc.error) return tc.skip('no C compiler on this machine');
  assert.strictEqual(cc.status, 0, cc.stderr);                                                           // no warnings either
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port, base = `http://127.0.0.1:${port}`, sock = [];
  const open = (path) => new Promise((res, rej) => { const ws = new WebSocket(`ws://127.0.0.1:${port}${path}`); ws.binaryType = 'arraybuffer'; ws.msgs = []; ws.bins = []; ws.onmessage = e => (typeof e.data === 'string' ? ws.msgs.push(JSON.parse(e.data)) : ws.bins.push(Buffer.from(e.data))); ws.onopen = () => res(ws); ws.onerror = () => rej(new Error('connect')); });
  const until = async (f, ms = 5000) => { const t0 = Date.now(); while (!(await f())) { if (Date.now() - t0 > ms) throw new Error('timeout'); await new Promise(r => setTimeout(r, 10)); } };
  try {
    // 1. the mixer is not there: the library says so instead of hanging
    const t0 = Date.now(), none = cp.spawnSync(exe, [], { encoding: 'utf8', env: { ...process.env, AUDIO_MIXER_PORT: '1' }, timeout: 8000 });
    assert.strictEqual(none.status, 2); assert.match(none.stdout, /open failed: the Audio Mixer program is not running/);
    assert.ok(Date.now() - t0 < 1500, 'a missing mixer is reported at once, not after the 1 to 2 s that Windows takes to refuse a connection: ' + (Date.now() - t0) + ' ms');
    // a program on the mixer's port that never answers the WebSocket handshake does not hold the caller either
    const mute = require('node:net').createServer(() => {}); await new Promise(r => mute.listen(0, '127.0.0.1', r));
    const t1 = Date.now(), stuck = cp.spawnSync(exe, [], { encoding: 'utf8', env: { ...process.env, AUDIO_MIXER_PORT: String(mute.address().port) }, timeout: 8000 });
    mute.close(); assert.strictEqual(stuck.status, 2); assert.ok(Date.now() - t1 < 2500, 'handshake limit: ' + (Date.now() - t1) + ' ms');
    // 2. the mixer runs: the native client says hello, plays 25 blocks of 480 stereo frames, and records what the page writes
    let out = ''; const child = cp.spawn(exe, ['Link Test'], { env: { ...process.env, AUDIO_MIXER_PORT: String(port) } }); child.stdout.on('data', d => { out += d; });
    const done = new Promise(res => child.on('close', code => res(code)));
    await until(async () => (await (await fetch(base + '/api/virtual')).json()).devices.length === 1);
    const dev = (await (await fetch(base + '/api/virtual')).json()).devices[0]; assert.deepStrictEqual([dev.name, dev.id >= 9000, dev.channels, dev.rate], ['Link Test', true, 2, 48000]);       // the hub numbers devices by name for the life of the process
    const inp = await open('/ws/input'); sock.push(inp); inp.send(JSON.stringify({ type: 'start', deviceId: dev.id, channels: 2, sampleRate: 48000 }));
    const outp = await open('/ws/output'); sock.push(outp); outp.send(JSON.stringify({ type: 'start', deviceId: dev.id, channels: 2, sampleRate: 48000 }));
    await until(() => inp.msgs.some(m => m.type === 'started') && outp.msgs.some(m => m.type === 'started'));
    const back = Buffer.alloc(480 * 4); for (let i = 0; i < 480; i++) { back.writeInt16LE(7, i * 4); back.writeInt16LE(-7, i * 4 + 2); }
    const feed = setInterval(() => { if (outp.readyState === 1) outp.send(back); }, 20);
    const code = await done; clearInterval(feed);
    assert.strictEqual(code, 0, out);
    const pcm = Buffer.concat(inp.bins);
    assert.ok(pcm.length >= 480 * 4 * 20, 'the mixer received the played audio: ' + pcm.length);               // 25 blocks sent, the first may have been sent before the reader attached
    assert.strictEqual(pcm.readInt16LE(0), 1000); assert.strictEqual(pcm.readInt16LE(2), -1000); assert.strictEqual(pcm.readInt16LE(4), 1001);   // frames arrive intact and in order
    assert.match(out, new RegExp('ready id=' + dev.id)); assert.match(out, /played=25/);
    const rec = /recorded=(\d+) first=(-?\d+) alive=(\d)/.exec(out); assert.ok(rec && Number(rec[1]) >= 960 && Number(rec[2]) === 7 && rec[3] === '1', out);   // what the mixer sent was recorded by the driver
    assert.match(out, /closed/);
    await until(async () => (await (await fetch(base + '/api/virtual')).json()).devices.length === 0);          // closing the link removes the device
  } finally { sock.forEach(s => { try { s.close(); } catch (_) { /* closed */ } }); server.closeAllConnections(); server.close(); try { fsx.unlinkSync(exe); } catch (_) { /* none */ } }
});

test('Linux ALSA plugin (linux/alsa-plugin): aplay plays into the mixer and arecord records what the mixer sends, in real time', async (tc) => {
  const cp = require('node:child_process'), dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'alsa-')), src = pathx.join(__dirname, '..');
  const so = pathx.join(dir, 'libasound_module_pcm_audiomixer.so');
  const cc = cp.spawnSync('gcc', ['-std=gnu99', '-Wall', '-Wextra', '-Werror', '-O1', '-fPIC', '-DPIC', '-shared', '-o', so, pathx.join(src, 'platforms', 'linux', 'alsa-plugin', 'pcm_audiomixer.c'), pathx.join(src, 'drivers', 'common', 'am_link.c'), '-lasound', '-lpthread'], { encoding: 'utf8' });
  if (cc.error || /asoundlib\.h|pcm_external\.h/.test(cc.stderr)) { fsx.rmSync(dir, { recursive: true, force: true }); return tc.skip('no C compiler or no ALSA development files (libasound2-dev) on this machine'); }
  assert.strictEqual(cc.status, 0, cc.stderr);
  const have = n => !cp.spawnSync('which', [n]).status;
  if (!have('aplay') || !have('arecord')) { fsx.rmSync(dir, { recursive: true, force: true }); return tc.skip('aplay / arecord (alsa-utils) are not installed'); }
  fsx.writeFileSync(pathx.join(dir, '.asoundrc'), `pcm_type.audiomixer { lib "${so}" }\npcm.audiomixer { type audiomixer name "ALSA Test" channels 2 rate 48000 }\n`);
  const ramp = Buffer.alloc(44 + 48000 * 2 * 2 * 2); ramp.write('RIFF', 0); ramp.writeUInt32LE(ramp.length - 8, 4); ramp.write('WAVEfmt ', 8); ramp.writeUInt32LE(16, 16); ramp.writeUInt16LE(1, 20); ramp.writeUInt16LE(2, 22); ramp.writeUInt32LE(48000, 24); ramp.writeUInt32LE(192000, 28); ramp.writeUInt16LE(4, 32); ramp.writeUInt16LE(16, 34); ramp.write('data', 36); ramp.writeUInt32LE(ramp.length - 44, 40);
  for (let i = 0; i < 96000; i++) { ramp.writeInt16LE((i % 2000) + 1, 44 + i * 4); ramp.writeInt16LE(-((i % 2000) + 1), 46 + i * 4); }      // two seconds of a ramp: (v, -v)
  fsx.writeFileSync(pathx.join(dir, 'ramp.wav'), ramp);
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port, base = `http://127.0.0.1:${port}`, env = { ...process.env, HOME: dir, AUDIO_MIXER_PORT: String(port) }, sock = [];
  const open = (path) => new Promise((res, rej) => { const ws = new WebSocket(`ws://127.0.0.1:${port}${path}`); ws.binaryType = 'arraybuffer'; ws.msgs = []; ws.bins = []; ws.onmessage = e => (typeof e.data === 'string' ? ws.msgs.push(JSON.parse(e.data)) : ws.bins.push(Buffer.from(e.data))); ws.onopen = () => res(ws); ws.onerror = () => rej(new Error('connect')); });
  const until = async (f, ms = 8000) => { const t0 = Date.now(); while (!(await f())) { if (Date.now() - t0 > ms) throw new Error('timeout'); await new Promise(r => setTimeout(r, 10)); } };
  const device = async () => (await (await fetch(base + '/api/virtual')).json()).devices[0];
  try {
    // playback: aplay -> the device appears in the mixer, its audio arrives intact and in order, and the stream runs at real time
    const t0 = Date.now(); const ap = cp.spawn('aplay', ['-q', '-D', 'audiomixer', pathx.join(dir, 'ramp.wav')], { env }); let aerr = ''; ap.stderr.on('data', d => { aerr += d; });
    const aDone = new Promise(r => ap.on('close', c => r(c)));
    await until(async () => !!(await device()));
    const dev = await device(); assert.deepStrictEqual([dev.name, dev.channels, dev.rate], ['ALSA Test', 2, 48000]);
    const inp = await open('/ws/input'); sock.push(inp); inp.send(JSON.stringify({ type: 'start', deviceId: dev.id, channels: 2, sampleRate: 48000 }));
    assert.strictEqual(await aDone, 0, aerr);
    const secs = (Date.now() - t0) / 1000; assert.ok(secs > 1.7 && secs < 4.5, 'two seconds of audio take about two seconds: ' + secs);
    await new Promise(r => setTimeout(r, 150));
    const pcm = Buffer.concat(inp.bins); assert.ok(pcm.length / 4 >= 48000, 'the mixer received at least a second: ' + pcm.length / 4);
    let prev = null, gaps = 0, mism = 0; for (let i = 0; i + 4 <= pcm.length; i += 4) { const l = pcm.readInt16LE(i); if (pcm.readInt16LE(i + 2) !== -l) mism++; if (prev !== null && l !== (prev % 2000) + 1) gaps++; prev = l; }
    assert.strictEqual(mism, 0); assert.strictEqual(gaps, 0, 'no gaps or reordering');
    await until(async () => !(await device()));                                                          // aplay ended: the device is gone
    // capture: arecord <- what the page sends to the device
    const wavPath = pathx.join(dir, 'rec.wav'); const ar = cp.spawn('arecord', ['-q', '-D', 'audiomixer', '-d', '2', '-f', 'S16_LE', '-r', '48000', '-c', '2', '-t', 'wav', wavPath], { env }); let rerr = ''; ar.stderr.on('data', d => { rerr += d; });
    const rDone = new Promise(r => ar.on('close', c => r(c)));
    await until(async () => !!(await device()));
    const out = await open('/ws/output'); sock.push(out); out.send(JSON.stringify({ type: 'start', deviceId: (await device()).id, channels: 2, sampleRate: 48000 }));
    const blk = Buffer.alloc(480 * 4); for (let i = 0; i < 480; i++) { blk.writeInt16LE(7, i * 4); blk.writeInt16LE(-7, i * 4 + 2); }
    const feed = setInterval(() => { if (out.readyState === 1) out.send(blk); }, 10);
    const rcode = await rDone; clearInterval(feed); assert.strictEqual(rcode, 0, rerr);
    const rec = fsx.readFileSync(wavPath).subarray(44); let sevens = 0; for (let i = 0; i + 4 <= rec.length; i += 4) if (rec.readInt16LE(i) === 7 && rec.readInt16LE(i + 2) === -7) sevens++;
    assert.ok(rec.length / 4 >= 90000, 'two seconds were recorded: ' + rec.length / 4); assert.ok(sevens >= 60000, 'the mixer\'s audio was recorded: ' + sevens);
  } finally { sock.forEach(s => { try { s.close(); } catch (_) { /* closed */ } }); server.closeAllConnections(); server.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
});

test('ASIO host backend (engine/cpp/src/backend_asio.cpp): any installed ASIO driver through the Steinberg IASIO interface, built into the Windows runtime, compiled for x64 and x86 with -Werror', (tc) => {
  const cp = require('node:child_process'), root = pathx.join(__dirname, '..');
  const src = fsx.readFileSync(pathx.join(root, 'engine', 'cpp', 'src', 'backend_asio.cpp'), 'utf8');
  assert.match(src, /SOFTWARE\\\\ASIO/); assert.match(src, /CoCreateInstance\(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid,/); assert.match(src, /#include "asio_sdk\.h"/);
  assert.match(fsx.readFileSync(pathx.join(root, 'engine', 'cpp', 'src', 'runtime.cpp'), 'utf8'), /name == "asio"\) return makeAsioBackend\(\)/);
  assert.match(fsx.readFileSync(pathx.join(root, 'engine', 'cpp', 'CMakeLists.txt'), 'utf8'), /src\/backend_asio\.cpp/);
  for (const t of ['x86_64-w64-mingw32', 'i686-w64-mingw32']) {
    if (cp.spawnSync('which', [t + '-g++']).status) return tc.skip('mingw-w64 is not installed');
    const o = pathx.join(fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'asiohost-')), 'b.o');
    const r = cp.spawnSync(t + '-g++', ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', '-I' + pathx.join(root, 'engine', 'cpp', 'include'), '-I' + pathx.join(root, 'engine', 'cpp', 'src'), '-I' + pathx.join(root, 'drivers', 'asio'), '-c', '-o', o, pathx.join(root, 'engine', 'cpp', 'src', 'backend_asio.cpp')], { encoding: 'utf8' });
    assert.strictEqual(r.status, 0, t + ': ' + r.stderr);
  }
});

test('Windows ASIO driver (drivers/asio): cross-compiled for x64 and x86 with -Werror, COM exports, system DLLs only, the GUID agrees everywhere', (tc) => {
  const cp = require('node:child_process'), drv = require('../scripts/build-drivers');
  if (!['x86_64-w64-mingw32-g++', 'i686-w64-mingw32-g++'].every(n => !cp.spawnSync('which', [n]).status)) return tc.skip('mingw-w64 is not installed');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'asio-')), root = pathx.join(__dirname, '..', 'drivers', 'asio');
  try {
    for (const [arch, machine] of [['x64', 0x8664], ['x86', 0x14c]]) {
      const dll = drv.buildAsio(arch, dir), b = fsx.readFileSync(dll);
      assert.strictEqual(b.toString('latin1', 0, 2), 'MZ'); const pe = b.readUInt32LE(0x3c); assert.strictEqual(b.toString('latin1', pe, pe + 4), 'PE\0\0'); assert.strictEqual(b.readUInt16LE(pe + 4), machine); assert.ok(b.readUInt16LE(pe + 22) & 0x2000, 'a DLL');
      const dump = cp.spawnSync(arch === 'x64' ? 'x86_64-w64-mingw32-objdump' : 'i686-w64-mingw32-objdump', ['-p', dll], { encoding: 'utf8' }).stdout;
      for (const e of ['DllGetClassObject', 'DllCanUnloadNow', 'DllRegisterServer', 'DllUnregisterServer']) assert.match(dump, new RegExp('\\] ' + e + '\\b'), arch + ' exports ' + e);   // undecorated names on both
      const imports = [...dump.matchAll(/DLL Name: (\S+)/g)].map(m => m[1].toLowerCase());
      assert.ok(imports.length && imports.every(n => ['kernel32.dll', 'user32.dll', 'advapi32.dll', 'ws2_32.dll', 'winmm.dll', 'avrt.dll', 'msvcrt.dll', 'ole32.dll', 'oleaut32.dll'].includes(n)), arch + ' imports only Windows DLLs: ' + imports);   // no libstdc++ / libgcc / winpthread DLL to ship
    }
    // one class id: the source, the README texts and the registration use the same GUID
    const cpp = fsx.readFileSync(pathx.join(root, 'audiomixer_asio.cpp'), 'utf8'), guid = /\{C1893F2F-1AD5-4344-9806-DCFD242C0D48\}/i;
    assert.match(cpp, guid); assert.ok(cpp.includes('0xc1893f2f, 0x1ad5, 0x4344, { 0x98, 0x06, 0xdc, 0xfd, 0x24, 0x2c, 0x0d, 0x48 }')); assert.match(drv.README_WINDOWS('1'), guid);
    // the registration scripts: administrator only, regsvr32 on the DLLs that exist, CRLF; nothing else (no run key, no service, no network)
    assert.ok(!/CurrentVersion\\Run/.test(cpp) && !/CreateService/.test(cpp), 'the driver installs nothing that starts by itself');
    // the interface the hosts call: the method order of IASIO is the binary contract
    const iface = fsx.readFileSync(pathx.join(root, 'sdk', 'iasiodrv.h'), 'utf8'), order = ['init', 'getDriverName', 'getDriverVersion', 'getErrorMessage', 'start', 'stop', 'getChannels', 'getLatencies', 'getBufferSize', 'canSampleRate', 'getSampleRate', 'setSampleRate', 'getClockSources', 'setClockSource', 'getSamplePosition', 'getChannelInfo', 'createBuffers', 'disposeBuffers', 'controlPanel', 'future', 'outputReady'];
    const at = order.map(n => iface.indexOf(' ' + n + '('));
    assert.ok(at.every((x, i) => x > 0 && (i === 0 || x > at[i - 1])), 'IASIO methods in the order of the ASIO interface: ' + at);
    // 32-bit hosts call IASIO with thiscall (the callee removes the arguments): the x86 object must return with `ret $8` from getChannels(two pointers); cdecl would be a plain `ret`
    const o32 = pathx.join(dir, 'asio32.o'), cc32 = cp.spawnSync('i686-w64-mingw32-g++', ['-std=gnu++14', '-O2', '-c', pathx.join(root, 'audiomixer_asio.cpp'), '-o', o32], { encoding: 'utf8' });
    assert.strictEqual(cc32.status, 0, cc32.stderr);
    const asm = cp.spawnSync('i686-w64-mingw32-objdump', ['-d', '-C', o32], { encoding: 'utf8', maxBuffer: 1 << 26 }).stdout;
    const body = asm.split(/\n(?=[0-9a-f]+ <)/).find(x => /^[0-9a-f]+ <AudioMixerAsio::getChannels\(/.test(x)) || ''; assert.match(body, /ret\s+\$0x8\b/, 'getChannels is thiscall on x86');
    assert.match(fsx.readFileSync(pathx.join(root, 'asio_sdk.h'), 'utf8'), /ASIO_CALL __attribute__\(\(thiscall\)\)/); assert.strictEqual((iface.match(/virtual \w+ ASIO_CALL /g) || []).length, order.length);
    // the audio thread never connects, allocates or waits for the network: the connector thread does that
    const cyc = cpp.slice(cpp.indexOf('void cycle()'), cpp.indexOf('void run()'));
    assert.ok(!/am_link_open|am_link_close|alloc\(|Sleep\(/.test(cyc), 'cycle() (the audio thread) has no connect, close, allocation or sleep'); assert.match(cpp, /connectorMain/);
  } finally { fsx.rmSync(dir, { recursive: true, force: true }); }
});

test('driver sources and packaging: macOS plug-in, Android HAL, the build script packs the ASIO zip (and, on request, the source archive)', (tc) => {
  const cp = require('node:child_process'), root = pathx.join(__dirname, '..'), rd = f => fsx.readFileSync(pathx.join(root, f), 'utf8');
  // macOS: the factory id in Info.plist belongs to the factory function and to the type id of Apple's AudioServerPlugIn
  const plist = rd('platforms/macos/coreaudio-driver/Info.plist'), c = rd('platforms/macos/coreaudio-driver/AudioMixerDriver.c');
  assert.match(plist, /443ABAB8-E7B3-491A-B985-BEB9187030DB/); assert.match(plist, /<string>AudioMixerDriverFactory<\/string>/); assert.match(c, /void \*AudioMixerDriverFactory\(/); assert.match(c, /visibility\("default"\)/);
  assert.ok(c.includes('kAudioServerPlugInTypeUUID') && c.includes('am_link_play') && c.includes('on_record = onRecord') || c.includes('c.on_record = onRecord'));
  // every interface function of the driver table is defined (the table is the contract with coreaudiod)
  const table = /gInterface = \{\s*NULL, ([^}]+)\}/.exec(c)[1].split(',').map(s => s.trim()).filter(Boolean);
  assert.strictEqual(table.length, 22); for (const fn of table) assert.match(c, new RegExp('\\b' + fn + '\\(')); 
  // Android HAL: the module symbol, the streams, the build file and the policy
  const hal = rd('drivers/android-hal/audio_hw.c');
  for (const s of ['HAL_MODULE_INFO_SYM', 'AUDIO_HARDWARE_MODULE_ID', 'open_output_stream', 'open_input_stream', 'am_link_open', 'am_link_play', 'out_write', 'in_read']) assert.ok(hal.includes(s), s);
  assert.match(rd('drivers/android-hal/Android.bp'), /audio\.audiomixer\.default/); assert.match(rd('drivers/android-hal/audio_policy_configuration_audiomixer.xml'), /AUDIO_DEVICE_OUT_BUS/); assert.match(rd('drivers/android-hal/sepolicy/hal_audio_audiomixer.te'), /hal_audio_default/);
  for (const f of ['drivers/README.md']) { const s = rd(f); assert.match(s, /Not compiled or run|not compiled or run|NOT compiled/i); assert.match(s, /\/ws\/virtual/); }       // the limits are stated
  assert.ok(/STATUS:[^\n]*NOT compiled/.test(c) && /STATUS:[^\n]*NOT compiled/.test(hal));
  // packaging
  if (!['x86_64-w64-mingw32-g++', 'i686-w64-mingw32-g++', 'zip', 'tar'].every(n => !cp.spawnSync('which', [n]).status)) return tc.skip('mingw-w64 / zip / tar are not installed');
  const out = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'drvpack-'));
  try {
    const r = require('../scripts/build-drivers').buildAll({ out, source: true }), v = r.version;
    const zip = cp.spawnSync('unzip', ['-Z1', pathx.join(out, `AudioMixer-${v}-asio-driver-windows.zip`)], { encoding: 'utf8' }).stdout.split('\n').filter(Boolean);
    for (const f of ['AudioMixerASIO64.dll', 'AudioMixerASIO32.dll', 'README.txt']) assert.ok(zip.includes(`AudioMixer-ASIO-${v}/${f}`), f);
    const tar = cp.spawnSync('tar', ['tzf', pathx.join(out, `AudioMixer-${v}-drivers-source.tar.gz`)], { encoding: 'utf8' }).stdout.split('\n');
    for (const f of ['drivers/common/am_link.c', 'drivers/asio/audiomixer_asio.cpp', 'platforms/linux/alsa-plugin/pcm_audiomixer.c', 'platforms/macos/coreaudio-driver/AudioMixerDriver.c', 'drivers/android-hal/audio_hw.c', 'drivers/README.md']) assert.ok(tar.includes(`audio-mixer-drivers-${v}/${f}`), f);
    assert.ok(!tar.some(f => /\.(dll|so|o)$/.test(f)), 'no binaries in the source archive');
    assert.ok(!fsx.readdirSync(out).some(x => x.endsWith('.sha256')), 'no .sha256 files next to the release files');
  } finally { fsx.rmSync(out, { recursive: true, force: true }); }
});


test('clip detection (native streams): threshold, runs of samples, full scale, per channel, runs carried across blocks, the threshold is settable', async () => {
  const lv = require('./levels');
  const stereo = (l, r) => { const b = Buffer.alloc(l.length * 4); l.forEach((v, i) => { b.writeInt16LE(v, i * 4); b.writeInt16LE(r[i], i * 4 + 2); }); return b; };
  const quiet = n => new Array(n).fill(1000);
  try {
    assert.strictEqual(lv.config.clipDb, -0.1); assert.strictEqual(lv.limitOf(-0.1), 32393);
    // a flat top (4 samples at 32500, over -0.1 dBFS) clips channel 0; a lone full-scale sample clips channel 1; a lone peak under full scale does not clip
    const m = new lv.Meter(3);
    const b = Buffer.alloc(16 * 6);
    for (let i = 0; i < 16; i++) { b.writeInt16LE(i >= 4 && i < 8 ? 32500 : 1000, i * 6); b.writeInt16LE(i === 9 ? 32767 : 500, i * 6 + 2); b.writeInt16LE(i === 3 ? 32500 : 500, i * 6 + 4); }
    m.push(b); const s = m.snapshot();
    assert.deepStrictEqual(s.clipCh, [true, true, false]); assert.strictEqual(s.clipping, true); assert.strictEqual(s.clipRuns, 1); assert.strictEqual(s.clip, 6); assert.strictEqual(s.clipDb, -0.1);
    const q = new lv.Meter(2); q.push(stereo(quiet(32), quiet(32))); const sq = q.snapshot(); assert.strictEqual(sq.clipping, false); assert.deepStrictEqual(sq.clipCh, [false, false]); assert.strictEqual(sq.clip, 0);
    // a run is carried from one block to the next
    const c = new lv.Meter(2); c.push(stereo([1000, 1000, 32500, 32500], quiet(4))); assert.strictEqual(c.snapshot().clipping, false);   // 2 in a row so far
    c.push(stereo([32500, 1000, 1000, 1000], quiet(4))); assert.strictEqual(c.snapshot().clipping, true);                        // the third continues the run (snapshot() resets the counts, not the run)
    // the threshold decides what counts as "over"
    assert.strictEqual(lv.setClipDb(-3), -3); assert.strictEqual(lv.limitOf(-3), 23198);
    const t3 = new lv.Meter(2); t3.push(stereo([24000, 24000, 24000, 0], quiet(4))); const s3 = t3.snapshot(); assert.deepStrictEqual(s3.clipCh, [true, false]); assert.strictEqual(s3.clipDb, -3);
    assert.throws(() => lv.setClipDb(1), /clipDb/); assert.throws(() => lv.setClipDb('x'), /clipDb/); assert.throws(() => lv.setClipDb(-40), /clipDb/); assert.strictEqual(lv.config.clipDb, -3);
    // the bridge route: GET reads, POST needs the custom header, validates, and is refused for foreign origins
    await new Promise(r => server.listen(0, '127.0.0.1', r));
    const base = `http://127.0.0.1:${server.address().port}`, post = (body, headers = { 'X-Mixer-Action': 'meters' }, origin) => fetch(base + '/api/meters', { method: 'POST', headers: { 'Content-Type': 'application/json', ...(origin ? { Origin: origin } : {}), ...headers }, body: JSON.stringify(body) });
    assert.deepStrictEqual(await (await fetch(base + '/api/meters')).json(), { ok: true, clipDb: -3, clipRun: 3 });
    assert.strictEqual((await post({ clipDb: -1 })).status, 200); assert.strictEqual(lv.config.clipDb, -1);
    assert.strictEqual((await post({ clipDb: -1 }, {})).status, 400); assert.strictEqual((await post({ clipDb: 5 })).status, 400); assert.strictEqual((await post({ clipDb: -1 }, undefined, 'https://evil.example')).status, 403);
    assert.strictEqual(lv.config.clipDb, -1);
  } finally { lv.setClipDb(-0.1); server.closeAllConnections(); server.close(); }
});


test('levels: an open stream reports its levels to the page and stops when closed', async () => {
  const { _owners } = require('./asio-lock'); _owners.clear();
  const fa = fakeAudify();
  const sent = [];
  const conn = { send: m => sent.push(JSON.parse(m)), sendBinary() {} };
  const out = require('./output').createSession(conn, () => { throw new Error('no pa'); }, () => fa);
  out.onText(JSON.stringify({ type: 'start', channels: 2, sampleRate: 48000 }));
  const pcm = Buffer.alloc(192 * 4 * 2);
  for (let i = 0; i < pcm.length / 2; i++) pcm.writeInt16LE(8192, i * 2);
  out.onBinary(pcm);
  await new Promise(r => setTimeout(r, 200));
  const lv = sent.filter(m => m.type === 'levels');
  assert.ok(lv.length >= 1, 'levels were sent');
  const first = lv.find(m => m.peak[0] > -90) || lv[0];
  assert.strictEqual(first.direction, 'output'); assert.strictEqual(first.hostApi, 'ASIO'); assert.strictEqual(first.channels, 2); assert.strictEqual(first.frameSize, 192);
  assert.ok(Math.abs(first.peak[0] - -12) < 0.2, 'quarter scale is -12 dBFS: ' + first.peak[0]);
  out.onClose();
  const n = sent.length; await new Promise(r => setTimeout(r, 250));
  assert.strictEqual(sent.length, n, 'no reports after close');
});

test('universal ASIO driver: sources are ranked ASIO first, loopback last, duplex pairing, change id', () => {
  const u = require('./universal');
  const lists = [{ engine: 'naudiodon', devices: [
    { id: 1, name: 'Speakers (Realtek)', hostApi: 'Windows WASAPI', inputs: 0, outputs: 2, sampleRate: 48000 },
    { id: 2, name: 'Focusrite USB ASIO', hostApi: 'ASIO', inputs: 18, outputs: 20, sampleRate: 48000 },
    { id: 3, name: 'Stereo Mix (Realtek)', hostApi: 'Windows WASAPI', inputs: 2, outputs: 0, sampleRate: 48000 },
    { id: 4, name: 'Microphone (Realtek)', hostApi: 'MME', inputs: 2, outputs: 0, sampleRate: 44100 },
    { id: 5, name: 'ASIO4ALL v2', hostApi: 'ASIO', inputs: 4, outputs: 4, sampleRate: 48000 }] }];
  const r = u.detectSources(lists);
  assert.strictEqual(r.best.input.name, 'Focusrite USB ASIO'); assert.strictEqual(r.best.output.name, 'Focusrite USB ASIO');
  assert.deepStrictEqual(r.inputs.map(d => d.id), [2, 5, 4, 3]);                    // ASIO (more channels first), MME, loopback last
  assert.strictEqual(r.inputs[3].loopback, true); assert.deepStrictEqual(r.apis.slice(0, 2), ['ASIO', 'Windows WASAPI']);
  assert.strictEqual(r.asio.inputs, 2); assert.strictEqual(r.best.input.names.length, 18); assert.strictEqual(r.best.input.pairs[0], 'IN 1-2');
  assert.strictEqual(u.detectSources(lists).changeId, r.changeId);
  lists[0].devices.push({ id: 9, name: 'New Card ASIO', hostApi: 'ASIO', inputs: 2, outputs: 2, sampleRate: 48000 });
  assert.notStrictEqual(u.detectSources(lists).changeId, r.changeId);                 // plugging in an interface changes the id
  assert.deepStrictEqual(u.detectSources([]).best, { input: null, output: null });
});

test('universal ASIO driver: sessions open the best device on whichever engine has it', () => {
  const { _owners } = require('./asio-lock'); _owners.clear();
  const fa = fakeAudify();
  const mkPa = () => ({ SampleFormat16Bit: 8, getDevices: () => [{ id: 3, name: 'Speakers', hostAPIName: 'Windows WASAPI', maxInputChannels: 0, maxOutputChannels: 2 }],
    AudioIO: class { constructor() {} on() {} start() {} quit() {} write() { return true; } } });
  const run = (kind, loadPa, opts) => { const sent = []; const conn = { send: m => sent.push(JSON.parse(m)), sendBinary() {} };
    const s = kind === 'out' ? require('./output').createSession(conn, loadPa, () => fa) : require('./input').createInputSession(conn, loadPa, () => fa);
    s.onText(JSON.stringify({ type: 'start', channels: 2, sampleRate: 48000, ...opts })); return { sent, s }; };
  const a = run('out', mkPa, { deviceId: 'universal' });                                // PortAudio only has WASAPI, Audify has ASIO: ASIO wins
  assert.strictEqual(a.sent[0].type, 'started'); assert.strictEqual(a.sent[0].universal, true); assert.strictEqual(a.sent[0].hostApi, 'ASIO'); assert.strictEqual(a.sent[0].engine, 'audify');
  a.s.onClose();
  const b = run('in', () => { throw new Error('none'); }, { universal: true });         // capture, Audify only
  assert.strictEqual(b.sent[0].type, 'started'); assert.strictEqual(b.sent[0].universal, true); b.s.onClose();
  const c = run('out', () => { throw new Error('none'); }, { deviceId: 'universal', engine: 'naudiodon' });
  assert.match(c.sent[0].message, /Universal ASIO driver: no output device/);
  assert.strictEqual(_owners.size, 0);
});

test('endpoint: /api/universal', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const j = await (await fetch(base + '/api/universal')).json();
  assert.strictEqual(j.ok, true); assert.ok(Array.isArray(j.inputs) && Array.isArray(j.outputs) && typeof j.changeId === 'string' && j.best && 'input' in j.best);
  assert.strictEqual((await fetch(base + '/api/universal', { headers: { Origin: 'https://evil.example' } })).status, 403);
  server.closeAllConnections(); server.close();
});

// ── consumer licensing and OTA updates ──
function vendorKeys() {
  const { publicKey, privateKey } = cryptox.generateKeyPairSync('ec', { namedCurve: 'P-256' });
  const j = publicKey.export({ format: 'jwk' });
  return { privateKey, jwk: { kty: j.kty, crv: j.crv, x: j.x, y: j.y } };
}

test('license: signed keys verify, tampering / expiry / wrong computer are refused', () => {
  const lic = require('./license'), { privateKey, jwk } = vendorKeys();
  const now = Date.now();
  const mk = (over = {}) => lic.signKey({ v: 1, id: 'T1', plan: 'pro', name: 'Jane', issued: now - 1000, expires: now + 86400000, seats: 1, ...over }, privateKey);
  const ok = lic.verifyKey(mk(), { jwk, now });
  assert.strictEqual(ok.ok, true); assert.strictEqual(ok.plan.channels, 20); assert.ok(ok.plan.features.includes('plugins'));
  assert.strictEqual(lic.verifyKey(mk({ plan: 'studio', expires: null }), { jwk, now }).plan.channels, 32);
  assert.strictEqual(lic.PLANS.basic.channels, 10);
  // another vendor's key, a flipped payload, junk
  assert.strictEqual(lic.verifyKey(mk(), { jwk: vendorKeys().jwk, now }).reason, 'bad-signature');
  const k = mk(), parts = k.split('.');
  const forged = [parts[0], lic.b64u(Buffer.from(JSON.stringify({ v: 1, plan: 'studio', name: 'x' }))), parts[2]].join('.');
  assert.strictEqual(lic.verifyKey(forged, { jwk, now }).reason, 'bad-signature');
  assert.strictEqual(lic.verifyKey('nonsense', { jwk }).reason, 'malformed');
  assert.strictEqual(lic.verifyKey(mk(), { jwk, now: now + 3 * 86400000 }).reason, 'expired');
  assert.strictEqual(lic.verifyKey(mk({ plan: 'platinum' }), { jwk, now }).reason, 'unknown-plan');
  assert.strictEqual(lic.verifyKey(mk({ issued: now + 10 * 86400000 }), { jwk, now }).reason, 'not-yet-valid');
  // bound to a computer
  const bound = mk({ mid: 'AAAA-BBBB-CCCC-DDDD-EEEE' });
  assert.strictEqual(lic.verifyKey(bound, { jwk, now, machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' }).ok, true);
  assert.strictEqual(lic.verifyKey(bound, { jwk, now, machine: '1111-2222-3333-4444-5555' }).reason, 'wrong-machine');
  assert.match(lic.machineId(), /^[0-9A-F]{4}(-[0-9A-F]{4}){4}$/);
  assert.strictEqual(lic.machineId(), lic.machineId());
});

test('license: activation is stored per user, status falls back to BASIC when expired or invalid', () => {
  const lic = require('./license'), { privateKey, jwk } = vendorKeys();
  const file = pathx.join(fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'lic-')), 'sub', 'license.json');
  const now = Date.now();
  const key = lic.signKey({ v: 1, id: 'A1', plan: 'pro', name: 'Jane', issued: now, expires: now + 1000 * 60, seats: 1 }, privateKey);
  assert.strictEqual(lic.activate('junk', { file, jwk }).ok, false); assert.ok(!fsx.existsSync(file));
  assert.strictEqual(lic.activate(key, { file, jwk, machine: 'X' }).ok, true);
  assert.strictEqual(JSON.parse(fsx.readFileSync(file, 'utf8')).key, key);
  if (process.platform !== 'win32') assert.strictEqual(fsx.statSync(file).mode & 0o077, 0);         // private to the user
  process.env.BRIDGE_LICENSE_FILE = file;
  try {
    const st = lic.status({ jwk, now });
    assert.strictEqual(st.state, 'active'); assert.strictEqual(st.plan.id, 'pro'); assert.strictEqual(st.license.name, 'Jane');
    const late = lic.status({ jwk, now: now + 3600000 });
    assert.strictEqual(late.state, 'expired'); assert.strictEqual(late.plan.id, 'basic'); assert.strictEqual(late.plan.channels, 10);
    assert.strictEqual(lic.status({ jwk: vendorKeys().jwk, now }).state, 'invalid');                 // key of another vendor never raises the plan
    assert.strictEqual(lic.deactivate({ file }).removed, true); assert.strictEqual(lic.status({ jwk }).state, 'basic');
    assert.strictEqual(lic.hasFeature(lic.status({ key: key, jwk, now }), 'ota'), true);
    assert.strictEqual(lic.hasFeature(lic.status({ key: null }), 'ota'), false);
  } finally { delete process.env.BRIDGE_LICENSE_FILE; }
});


function fakeUpdateServer({ version = '9.9.9', tamper = false, badHost = false, privateKey, jwk } = {}) {
  const up = require('./update');
  const bytes = Buffer.from('installer-bytes');
  const sha = cryptox.createHash('sha256').update(bytes).digest('hex');
  const manifest = { product: 'audio-mixer', version, released: '2026-10-10', notes: ['Smooth faders', 'OTA updates'], files: {
    'win-x64-exe': { name: 'Audio Mixer-9.9.9.exe', url: badHost ? 'https://evil.example/a.exe' : 'https://raw.githubusercontent.com/o/r/main/releases/Audio%20Mixer-9.9.9.exe', size: bytes.length, sha256: sha },
    'linux-deb': { name: 'audio-mixer_9.9.9_all.deb', url: 'https://raw.githubusercontent.com/o/r/main/releases/audio-mixer_9.9.9_all.deb', sha256: sha } } };
  const env = up.signManifest(manifest, privateKey);
  if (tamper) env.payload = env.payload.replace('9.9.9', '0.0.1');
  return async (url) => {
    if (/update\.json$/.test(url)) return { ok: true, status: 200, json: async () => env };
    return { ok: true, status: 200, url, headers: { get: () => String(bytes.length) }, body: (async function* () { yield bytes.subarray(0, 5); yield bytes.subarray(5); })() };
  };
}

test('OTA: versions compare, manifests must be signed, files are matched to the system', async () => {
  const up = require('./update'), lic = require('./license');
  const { privateKey } = vendorKeys();
  assert.strictEqual(up.cmpVersion('1.10.0', '1.9.9'), 1); assert.strictEqual(up.cmpVersion('1.5.1', '1.5.1'), 0); assert.strictEqual(up.cmpVersion('1.5.1', '1.6.0'), -1);
  assert.strictEqual(up.hostOk('https://raw.githubusercontent.com/x'), true); assert.strictEqual(up.hostOk('http://raw.githubusercontent.com/x'), false); assert.strictEqual(up.hostOk('https://evil.example/x'), false);
  // the real check uses the embedded public key: a manifest signed by anyone else is ignored
  const fetchImpl = fakeUpdateServer({ privateKey });
  await assert.rejects(up.check({ current: '1.5.1', fetchImpl, url: 'https://raw.githubusercontent.com/o/r/main/releases/update.json' }), /not signed by the Audio Mixer publisher/);
  const env = up.signManifest({ product: 'audio-mixer', version: '2.0.0', files: { 'macos': { name: 'a.tar.gz', url: 'https://github.com/a', sha256: 'x' } } }, privateKey);
  assert.strictEqual(up.verifyManifest(env, vendorKeys().jwk).ok, false);
  const { privateKey: pk2, jwk: jwk2 } = vendorKeys();
  const ok = up.verifyManifest(up.signManifest({ product: 'audio-mixer', version: '2.0.0', files: {} }, pk2), jwk2);
  assert.strictEqual(ok.ok, true);
  assert.strictEqual(up.verifyManifest({ payload: ok.manifest, signature: 'x' }, jwk2).ok, false);
  const m = { files: { 'win-x64-exe': { name: 'a.exe' }, 'win-x86-msi': { name: 'b.msi' }, 'linux-deb': { name: 'c.deb' }, macos: { name: 'd.tgz' } } };
  assert.strictEqual(up.platformFile(m, { platform: 'win32', arch: 'x64' }).key, 'win-x64-exe');
  assert.strictEqual(up.platformFile(m, { platform: 'win32', arch: 'ia32' }).key, 'win-x86-msi');
  assert.strictEqual(up.platformFile(m, { platform: 'linux', debian: true }).key, 'linux-deb');
  assert.strictEqual(up.platformFile(m, { platform: 'linux', debian: false }), null);
  assert.strictEqual(up.platformFile(m, { platform: 'darwin' }).key, 'macos');
});

test('OTA: check and download with the embedded vendor key replaced by a test key (checksum verified, never run)', async () => {
  // run the real code path against a manifest signed with a key we hold: swap the embedded public key for this test only
  const lic = require('./license'), up = require('./update');
  const { privateKey, jwk } = vendorKeys();
  const saved = { ...lic.PUBLIC_JWK }; Object.assign(lic.PUBLIC_JWK, jwk);
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'ota-'));
  const url = 'https://raw.githubusercontent.com/o/r/main/releases/update.json';
  try {
    const info = await up.check({ current: '1.5.1', fetchImpl: fakeUpdateServer({ privateKey }), url, platform: 'win32', arch: 'x64' });
    assert.strictEqual(info.updateAvailable, true); assert.strictEqual(info.latest, '9.9.9'); assert.strictEqual(info.file.key, 'win-x64-exe'); assert.deepStrictEqual(info.notes, ['Smooth faders', 'OTA updates']);
    assert.strictEqual((await up.check({ current: '9.9.9', fetchImpl: fakeUpdateServer({ privateKey }), url, platform: 'win32' })).updateAvailable, false);
    const r = await up.download({ current: '1.5.1', fetchImpl: fakeUpdateServer({ privateKey }), url, dir, platform: 'win32', arch: 'x64' });
    assert.strictEqual(r.verified, true); assert.strictEqual(pathx.basename(r.file), 'Audio Mixer-9.9.9.exe'); assert.strictEqual(fsx.readFileSync(r.file).toString(), 'installer-bytes');
    assert.deepStrictEqual(fsx.readdirSync(dir), ['Audio Mixer-9.9.9.exe']);                         // no .part left behind
    await assert.rejects(up.download({ current: '9.9.9', fetchImpl: fakeUpdateServer({ privateKey }), url, dir, platform: 'win32' }), /up to date/);
    await assert.rejects(up.download({ current: '1.5.1', fetchImpl: fakeUpdateServer({ privateKey, badHost: true }), url, dir, platform: 'win32', arch: 'x64' }), /host is not allowed/);
    await assert.rejects(up.check({ current: '1.5.1', fetchImpl: fakeUpdateServer({ privateKey, tamper: true }), url }), /not signed/);
    await assert.rejects(up.check({ current: '1.5.1', fetchImpl: fakeUpdateServer({ privateKey }), url: 'https://evil.example/update.json' }), /host is not allowed/);
    // a corrupted file is discarded
    const bad = async (u) => /update\.json$/.test(u) ? fakeUpdateServer({ privateKey })(u) : { ok: true, status: 200, url: u, headers: { get: () => '3' }, body: (async function* () { yield Buffer.from('xyz'); })() };
    await assert.rejects(up.download({ current: '1.5.1', fetchImpl: bad, url, dir: pathx.join(dir, 'b'), platform: 'win32', arch: 'x64' }), /checksum mismatch/);
    assert.deepStrictEqual(fsx.readdirSync(pathx.join(dir, 'b')), []);
  } finally { Object.assign(lic.PUBLIC_JWK, saved); }
});

test('OTA: automatic updates install the Windows setup .exe once, only when switched on and licensed; Android is left to Google Play and never offered an APK', async () => {
  const lic = require('./license'), up = require('./update');
  const { privateKey, jwk } = vendorKeys();
  const saved = { ...lic.PUBLIC_JWK }; Object.assign(lic.PUBLIC_JWK, jwk);
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'ota-auto-')), prev = { f: process.env.BRIDGE_AUTO_UPDATE_FILE, e: process.env.BRIDGE_AUTO_UPDATE, u: process.env.BRIDGE_UPDATE_URL };
  process.env.BRIDGE_AUTO_UPDATE_FILE = pathx.join(dir, 'auto-update.json'); delete process.env.BRIDGE_AUTO_UPDATE;
  process.env.BRIDGE_UPDATE_URL = 'https://raw.githubusercontent.com/o/r/main/releases/update.json';
  const fetchImpl = fakeUpdateServer({ privateKey });
  try {
    assert.strictEqual(up.autoEnabled(), false, 'off by default');
    const spawned = [], spawnImpl = (f, a) => { spawned.push([pathx.basename(f), a]); return { unref() {} }; };
    const mk = (o = {}) => up.createAutoUpdater({ current: '1.5.1', platform: 'win32', checkFn: x => up.check({ ...x, fetchImpl, arch: 'x64' }), installFn: x => up.install({ ...x, fetchImpl, dir: pathx.join(dir, 'dl'), arch: 'x64', spawnImpl }), ...o });
    assert.strictEqual((await mk().tick()).result, 'off'); assert.strictEqual(spawned.length, 0);
    up.setAutoEnabled(true); assert.strictEqual(up.autoEnabled(), true);
    assert.match((await mk({ allowed: () => false }).tick()).result, /PRO or STUDIO/); assert.strictEqual(spawned.length, 0);
    const a = mk();
    assert.match((await a.tick()).result, /installing v9\.9\.9/); assert.deepStrictEqual(spawned, [['Audio Mixer-9.9.9.exe', ['/quiet']]]);
    assert.match((await a.tick()).result, /already tried/); assert.strictEqual(spawned.length, 1, 'a version is tried once per run');
    assert.match((await mk({ platform: 'linux' }).tick()).result, /Windows only/); assert.match((await mk({ platform: 'android' }).tick()).result, /Windows only/);
    up.setAutoEnabled(false); assert.strictEqual((await mk().tick()).result, 'off');
    // Android: the update is reported, but no APK is offered or downloaded (Play Protect / Play policy: the store app is updated by Google Play)
    const url = process.env.BRIDGE_UPDATE_URL, info = await up.check({ current: '1.5.1', fetchImpl, url, platform: 'android', arch: 'arm64' });
    assert.strictEqual(info.playManaged, true); assert.strictEqual(info.file, null); assert.ok(!info.others.some(o => o.key === 'android-apk'));
    await assert.rejects(up.download({ current: '1.5.1', fetchImpl, url, dir, platform: 'android', arch: 'arm64' }), /Google Play/);
  } finally {
    Object.assign(lic.PUBLIC_JWK, saved);
    for (const [k, v] of [['BRIDGE_AUTO_UPDATE_FILE', prev.f], ['BRIDGE_AUTO_UPDATE', prev.e], ['BRIDGE_UPDATE_URL', prev.u]]) { if (v === undefined) delete process.env[k]; else process.env[k] = v; }
  }
});

test('service: NSSM and PM2 commands (auto start, restart on exit, rotated logs, the user password never printed), the PM2 config is one forked server instance', () => {
  const svc = require('../scripts/service'), root = pathx.join(__dirname, '..');
  const cmds = svc.nssmCommands('install', { name: 'AudioMixer', nssm: 'nssm', dir: 'C:\\AM', node: 'C:\\node\\node.exe', script: 'C:\\AM\\bridge\\server.js', logs: 'C:\\Logs', port: 9000 }).map(c => c.join(' '));
  assert.strictEqual(cmds[0], 'nssm install AudioMixer C:\\node\\node.exe C:\\AM\\bridge\\server.js');
  for (const want of ['set AudioMixer Start SERVICE_AUTO_START', 'set AudioMixer AppExit Default Restart', 'set AudioMixer AppRestartDelay 2000', 'AppEnvironmentExtra NODE_ENV=production BRIDGE_PORT=9000', 'AppRotateFiles 1', 'AppDirectory C:\\AM'])
    assert.ok(cmds.some(c => c.includes(want)), want);
  assert.strictEqual(cmds[cmds.length - 1], 'nssm start AudioMixer'); assert.ok(!cmds.some(c => c.includes('ObjectName')), 'runs as LocalSystem unless a user is given');
  assert.ok(svc.nssmCommands('install', { user: 'PC\\me', password: 'pw' }).some(c => c.includes('ObjectName')));
  assert.deepStrictEqual(svc.nssmCommands('remove', { name: 'X', nssm: 'nssm' }), [['nssm', 'stop', 'X'], ['nssm', 'remove', 'X', 'confirm']]);
  assert.throws(() => svc.nssmCommands('explode'), /unknown nssm action/); assert.throws(() => svc.pm2Commands('explode'), /unknown pm2 action/);
  assert.deepStrictEqual(svc.pm2Commands('restart'), [['pm2', 'restart', 'audio-mixer']]);
  const eco = require(pathx.join(root, 'deploy', 'pm2', 'ecosystem.config.js')).apps[0];
  assert.strictEqual(eco.name, 'audio-mixer'); assert.strictEqual(eco.instances, 1); assert.strictEqual(eco.exec_mode, 'fork'); assert.ok(eco.autorestart && fsx.existsSync(eco.script));
  const cp = require('node:child_process'), out = cp.spawnSync(process.execPath, [pathx.join(root, 'scripts', 'service.js'), 'nssm', 'install', '--dry-run', '--user', 'PC\\me'], { encoding: 'utf8', env: { ...process.env, AM_SERVICE_PASSWORD: 'topsecret' } });
  assert.strictEqual(out.status, 0); assert.ok(!out.stdout.includes('topsecret') && out.stdout.includes('********'), 'the password is not echoed');
});

test('FlexASIO installer: only a pinned, SHA-256-verified github.com/dechamps/FlexASIO release is run, silently, on Windows', async () => {
  const fx = require('./flexasio'), bytes = Buffer.from('flexasio-installer'), sha = cryptox.createHash('sha256').update(bytes).digest('hex');
  const url = 'https://github.com/dechamps/FlexASIO/releases/download/flexasio-1.10/FlexASIO-1.10.exe', pin = { version: '1.10', url, sha256: sha, args: ['/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'] };
  assert.match(fx.pinProblem({ url: '', sha256: '' }), /no FlexASIO installer is pinned/); assert.match(fx.pinProblem({ ...pin, url: 'https://evil.example/a.exe' }), /github\.com\/dechamps/);
  assert.match(fx.pinProblem({ ...pin, sha256: 'abc' }), /not a SHA-256/); assert.match(fx.pinProblem({ ...pin, args: ['/S; calc'] }), /switches/); assert.strictEqual(fx.pinProblem({ ...JSON.parse(fsx.readFileSync(fx.PIN_FILE, 'utf8')), sha256: sha }), null, 'the shipped pin is valid once a hash is filled in'); assert.strictEqual(fx.pinProblem(pin), null);
  assert.deepStrictEqual(JSON.parse(fsx.readFileSync(fx.PIN_FILE, 'utf8')).sha256, '', 'shipped without a pin: nothing runs until one is filled in');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'flex-')), spawned = [], spawnImpl = (f, a) => { spawned.push([pathx.basename(f), a]); return { unref() {} }; };
  const ok = async () => ({ ok: true, status: 200, headers: { get: () => null }, body: (async function* () { yield bytes; })() });
  const r = await fx.install({ pin, fetchImpl: ok, dir, platform: 'win32', spawnImpl });
  assert.strictEqual(r.installing, true); assert.deepStrictEqual(spawned, [['FlexASIO-1.10.exe', ['/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART']]]);
  const bad = async () => ({ ok: true, status: 200, headers: { get: () => null }, body: (async function* () { yield Buffer.from('tampered'); })() });
  await assert.rejects(fx.install({ pin, fetchImpl: bad, dir: pathx.join(dir, 'b'), platform: 'win32', spawnImpl }), /checksum mismatch/); assert.strictEqual(spawned.length, 1, 'a tampered installer is never run');
  assert.deepStrictEqual(fsx.readdirSync(pathx.join(dir, 'b')), []);
  const hop = async () => ({ ok: false, status: 302, headers: { get: k => (k === 'location' ? 'https://evil.example/x.exe' : null) } });
  await assert.rejects(fx.install({ pin, fetchImpl: hop, dir, platform: 'win32', spawnImpl }), /untrusted host/);
  await assert.rejects(fx.install({ pin, fetchImpl: ok, dir, platform: 'linux', spawnImpl }), /Windows driver/);
  assert.strictEqual((await fx.install({ pin, fetchImpl: ok, dir: pathx.join(dir, 'd'), platform: 'win32', spawnImpl, dryRun: true })).installing, false); assert.strictEqual(spawned.length, 1);
});

test('WASAPI exclusive mode ("take control"): DeviceConfig::exclusive, --exclusive, the exclusive share mode with format fallback and aligned period; compiles for Windows x64 and x86', () => {
  const root = pathx.join(__dirname, '..'), cp = require('node:child_process'), src = fsx.readFileSync(pathx.join(root, 'engine/cpp/src/backend_wasapi.cpp'), 'utf8');
  assert.match(fsx.readFileSync(pathx.join(root, 'engine/cpp/include/audiomixer/runtime.hpp'), 'utf8'), /bool exclusive = false;/);
  assert.match(fsx.readFileSync(pathx.join(root, 'engine/cpp/app/main.cpp'), 'utf8'), /"--exclusive"\) cfg\.exclusive = true/);
  for (const w of ['AUDCLNT_SHAREMODE_EXCLUSIVE', 'AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED', 'AUDCLNT_E_DEVICE_IN_USE', 'AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED', 'IsFormatSupported']) assert.ok(src.includes(w), w);
  for (const t of ['x86_64-w64-mingw32', 'i686-w64-mingw32']) {
    if (cp.spawnSync('which', [t + '-g++']).status !== 0) continue;                       // no mingw-w64 here: the source checks above stand
    const o = pathx.join(osx.tmpdir(), 'wasapi-' + t + '.o'), c = cp.spawnSync(t + '-g++', ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', '-I' + pathx.join(root, 'engine/cpp/include'), '-I' + pathx.join(root, 'engine/cpp/src'), '-c', pathx.join(root, 'engine/cpp/src/backend_wasapi.cpp'), '-o', o], { encoding: 'utf8' });
    assert.strictEqual(c.status, 0, t + ': ' + c.stderr.slice(0, 600));
  }
});

test('native .exe setup options: the ASIO driver DLL is embedded and written / registered by --install-driver, FlexASIO is run only after its SHA-256 matches; Android HAL sources are in drivers/android-hal', () => {
  const root = pathx.join(__dirname, '..'), cp = require('node:child_process'), rd = f => fsx.readFileSync(pathx.join(root, f), 'utf8');
  const wt = rd('engine/cpp/app/win_tools.cpp'), main = rd('engine/cpp/app/main.cpp'), bn = rd('scripts/build-native.js');
  for (const w of ['--install-driver', '--uninstall-driver', '--install-flexasio', 'DllRegisterServer', 'DllUnregisterServer', 'CALG_SHA_256', '/VERYSILENT', 'AM_EMBED_ASIO']) assert.ok(wt.includes(w), w);
  assert.ok(wt.indexOf('got != lw') > 0 && wt.indexOf('got != lw') < wt.indexOf('CreateProcessW'), 'the installer is compared before it is started');
  assert.match(main, /runWinTool\(argc, argv, rc\)/); assert.match(bn, /DAM_EMBED_ASIO/); assert.match(bn, /buildAsio\(arch, work\)/);
  // the HAL sources: module symbol, streams, build file, policy, SELinux, include of the shared link code
  const hal = rd('drivers/android-hal/audio_hw.c');
  for (const s of ['HAL_MODULE_INFO_SYM', 'AUDIO_HARDWARE_MODULE_ID', 'open_output_stream', 'open_input_stream', 'am_link_open', 'am_link_play', 'out_write', 'in_read', '"../common/am_link.h"']) assert.ok(hal.includes(s), s);
  assert.match(rd('drivers/android-hal/Android.bp'), /audio\.audiomixer\.default/); assert.ok(rd('drivers/android-hal/Android.bp').includes('"../common/am_link.c"'));
  assert.match(rd('drivers/android-hal/audio_policy_configuration_audiomixer.xml'), /AUDIO_DEVICE_OUT_BUS/); assert.match(rd('drivers/android-hal/sepolicy/hal_audio_audiomixer.te'), /hal_audio_default/);
  assert.ok(fsx.existsSync(pathx.join(root, 'drivers/common/am_link.h')) && fsx.existsSync(pathx.join(root, 'drivers/common/am_link.c')));
  for (const t of ['x86_64-w64-mingw32', 'i686-w64-mingw32']) {
    if (cp.spawnSync('which', [t + '-g++']).status !== 0) continue;
    const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'wt-')); fsx.writeFileSync(pathx.join(dir, 'asio_blob.inc'), 'static const unsigned char kAsioDll[] = {0x4d,0x5a};\nstatic const unsigned kAsioDllSize = 2u;\nstatic const char kAsioDllName[] = "AudioMixerASIO64.dll";\n');
    const c = cp.spawnSync(t + '-g++', ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', '-DAM_EMBED_ASIO', '-I' + dir, '-I' + pathx.join(root, 'engine/cpp/app'), '-c', pathx.join(root, 'engine/cpp/app/win_tools.cpp'), '-o', pathx.join(dir, 'wt.o')], { encoding: 'utf8' });
    assert.strictEqual(c.status, 0, t + ': ' + c.stderr.slice(0, 600));
  }
});

test('real-time audio backends: WASAPI has no allocation on the audio thread, runs input-only streams and joins MMCSS; ALSA reads and writes whole blocks; both compile', () => {
  const root = pathx.join(__dirname, '..'), cp = require('node:child_process'), rd = f => fsx.readFileSync(pathx.join(root, f), 'utf8');
  const w = rd('engine/cpp/src/backend_wasapi.cpp'), a = rd('engine/cpp/src/backend_alsa.cpp');
  assert.ok(!/std::deque/.test(w), 'no std::deque (it allocates on push) in the WASAPI audio thread'); assert.match(w, /struct Fifo/);
  assert.match(w, /captureEvent_ = wantIn && !wantOut/); assert.match(w, /else if \(!out_\) room = /);                  // an input-only stream is clocked by the capture event and does run blocks
  assert.match(w, /AvSetMmThreadCharacteristicsW\(L"Pro Audio"/); assert.match(w, /AvRevertMmThreadCharacteristics/);
  assert.match(a, /while \(got < F && !stop_\)/); assert.match(a, /while \(put < F && !stop_\)/);                      // short reads / writes are completed
  for (const t of ['x86_64-w64-mingw32', 'i686-w64-mingw32']) {
    if (cp.spawnSync('which', [t + '-g++']).status !== 0) continue;
    const c = cp.spawnSync(t + '-g++', ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', '-I' + pathx.join(root, 'engine/cpp/include'), '-I' + pathx.join(root, 'engine/cpp/src'), '-c', pathx.join(root, 'engine/cpp/src/backend_wasapi.cpp'), '-o', pathx.join(osx.tmpdir(), 'rt-' + t + '.o')], { encoding: 'utf8' });
    assert.strictEqual(c.status, 0, t + ': ' + c.stderr.slice(0, 600));
  }
});


test('ASIO detection (Windows registry): both views are read, a failed read is retried and does not make the drivers vanish, other-bitness drivers are named', async () => {
  const d = require('./detect'), reg = { '64': 'HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\FlexASIO\r\nHKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\Audio Mixer\r\n', '32': 'HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\Old32 Driver\r\nHKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\Audio Mixer\r\n' };
  assert.deepStrictEqual(d.parseAsioKeys('\r\nHKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\ASIO\\ASIO4ALL v2 \r\nHKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\r\n'), ['ASIO4ALL v2']);
  const mem = () => ({ at: 0, names: [], bits: {} }), ok = async (c, a) => ({ out: reg[a[2].slice(5)], failed: false });
  const r = await d.readAsioRegistry({ runner: ok, memory: mem(), arch: 'x64' });
  assert.deepStrictEqual(r.names.sort(), ['Audio Mixer', 'FlexASIO', 'Old32 Driver']); assert.deepStrictEqual(r.bits['Audio Mixer'], [64, 32]); assert.deepStrictEqual(r.wrongBitness, ['Old32 Driver']); assert.strictEqual(r.stale, false);
  assert.deepStrictEqual((await d.readAsioRegistry({ runner: ok, memory: mem(), arch: 'ia32' })).wrongBitness, ['FlexASIO'], 'a 32-bit server cannot load the 64-bit-only driver');
  // a read that times out once is retried
  let calls = 0; const flaky = async (c, a) => (++calls === 1 ? { out: '', failed: true } : ok(c, a));
  assert.strictEqual((await d.readAsioRegistry({ runner: flaky, memory: mem(), arch: 'x64' })).names.length, 3); assert.strictEqual(calls, 3);
  // it keeps failing: the drivers seen a minute ago are kept (stale), after ten minutes they are gone; a missing key (an answer) clears the list at once
  const memory = mem(); let t = 1e6; const now = () => t;
  await d.readAsioRegistry({ runner: ok, memory, now, arch: 'x64' });
  const dead = async () => ({ out: '', failed: true }), none = async () => ({ out: '', failed: false });
  t += 60e3; const s1 = await d.readAsioRegistry({ runner: dead, memory, now, arch: 'x64' }); assert.strictEqual(s1.stale, true); assert.strictEqual(s1.names.length, 3);
  t += 11 * 60e3; assert.strictEqual((await d.readAsioRegistry({ runner: dead, memory, now, arch: 'x64' })).names.length, 0);
  await d.readAsioRegistry({ runner: ok, memory, now, arch: 'x64' }); assert.strictEqual((await d.readAsioRegistry({ runner: none, memory, now, arch: 'x64' })).names.length, 0, 'no key = no drivers, not stale');
  // the RtAudio probe is reused for 15 s (it blocks the event loop); a rescan asks again
  const src = fsx.readFileSync(pathx.join(__dirname, 'detect.js'), 'utf8'); assert.match(src, /nowMs - audifyMemory\.at < 15000/); assert.match(fsx.readFileSync(pathx.join(__dirname, 'server.js'), 'utf8'), /detect\(\{ force \}\)/);
});

test('RtAudio: an instance is always created for ONE named host API; a missing API entry or a substituted API is refused, never opened with no argument (single AND duplex streams)', async () => {
  const au = require('./audify'), fake = fakeAudify();
  const dsDev = { api: 'WINDOWS_DS', rtId: 1, name: 'Speakers', hostAPIName: 'Windows DirectSound', maxOutputChannels: 2, maxInputChannels: 0, sampleRates: [48000] };
  // the module has no entry for the API: the old code did new RtAudio(undefined) and RtAudio picked another API
  const noEntry = { ...fake, RtAudioApi: { ...fake.RtAudioApi } }; delete noEntry.RtAudioApi.WINDOWS_DS;
  assert.throws(() => au.openStreamRaw({ mod: noEntry, dev: dsDev, direction: 'output', channels: 2, sampleRate: 48000, frameSize: 256 }), /DirectSound host API is not available/);
  assert.strictEqual(fake.opened.length, 0, 'nothing was opened on another API');
  // an engine that substitutes another API (RtAudio does that for an API that is not compiled in)
  class Sub extends fake.RtAudio { getApi() { return 'Dummy'; } }
  assert.throws(() => au.openStreamRaw({ mod: { ...fake, RtAudio: Sub }, dev: dsDev, direction: 'output', channels: 2, sampleRate: 48000, frameSize: 256 }), /not compiled into this audio engine/);
  assert.strictEqual(fake.opened.length, 0);
  // the device scan skips such an API quietly and still lists the others
  const scan = au.detectAudify(() => noEntry); assert.ok(scan.devices.some(d => d.hostApi && /ASIO/i.test(d.hostApi)), 'ASIO is still listed'); assert.ok(!scan.devices.some(d => /DirectSound/i.test(d.hostApi)));

  // ASIO is single-client: read + write share ONE duplex stream, so the duplex path is where ASIO is really opened.
  // It must honour the SAME guard - a missing or substituted API entry is refused there too, never opened on
  // another API (an ASIO duplex stream silently running on WASAPI/DirectSound would be the real-world ASIO bug).
  // Exercised through openDuplex() - the public /ws/duplex path, with the single-client ASIO lock - which calls
  // openDuplexRaw internally; that is the real way an ASIO duplex stream is opened.
  const asioDev = { api: 'WINDOWS_ASIO', rtId: 0, name: 'Focusrite USB ASIO', hostAPIName: 'ASIO', maxInputChannels: 18, maxOutputChannels: 20, sampleRates: [48000] };
  const dupNoEntry = fakeAudify(); const mNoAsio = { ...dupNoEntry, RtAudioApi: { ...dupNoEntry.RtAudioApi } }; delete mNoAsio.RtAudioApi.WINDOWS_ASIO;
  assert.throws(() => au.openDuplex({ mod: mNoAsio, dev: asioDev, inChannels: 2, outChannels: 2, sampleRate: 48000, frameSize: 256 }), /ASIO host API is not available/);
  assert.strictEqual(dupNoEntry.opened.length, 0, 'duplex opened nothing on another API when ASIO has no entry');
  assert.strictEqual(au._asioOf(mNoAsio).size, 0, 'a refused ASIO duplex open leaves no dangling single-client lock');
  const dupSub = fakeAudify(); class SubA extends dupSub.RtAudio { getApi() { return 'Dummy'; } }
  const mSub = { ...dupSub, RtAudio: SubA };
  assert.throws(() => au.openDuplex({ mod: mSub, dev: asioDev, inChannels: 2, outChannels: 2, sampleRate: 48000, frameSize: 256 }), /not compiled into this audio engine/);
  assert.strictEqual(dupSub.opened.length, 0, 'duplex opened nothing when RtAudio substituted another API for ASIO');
  assert.strictEqual(au._asioOf(mSub).size, 0, 'a substituted-API ASIO duplex open leaves no dangling lock');
});


test("apiKey() matches RtAudio::getApiDisplayName()'s real strings for every compiled-in API (not a guess: these are rtaudio_api_names[][1] from audify's vendored RtAudio.cpp)", () => {
  const { apiKey } = require('./audify');
  // api, RtAudio display name -> our enum key (RtAudio.cpp: const char* rtaudio_api_names[][2])
  const real = [
    ['Unknown', null], ['CoreAudio', 'MACOSX_CORE'], ['ALSA', 'LINUX_ALSA'], ['Jack', 'UNIX_JACK'],
    ['Pulse', 'LINUX_PULSE'], ['OpenSoundSystem', 'LINUX_OSS'], ['ASIO', 'WINDOWS_ASIO'],
    ['WASAPI', 'WINDOWS_WASAPI'], ['DirectSound', 'WINDOWS_DS'], ['Dummy', null],
  ];
  for (const [name, key] of real) assert.strictEqual(apiKey(name), key, name);
});

test('endpoints: /api/license (BASIC by default), activation needs a valid key, updates need PRO', async () => {
  process.env.BRIDGE_LICENSE_FILE = pathx.join(fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'lep-')), 'license.json');
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const post = (p, headers = {}, body) => fetch(base + p, { method: 'POST', headers: { 'Content-Type': 'application/json', ...headers }, body });
  try {
    const st = await (await fetch(base + '/api/license')).json();
    assert.strictEqual(st.ok, true); assert.strictEqual(st.state, 'basic'); assert.strictEqual(st.plan.channels, 10); assert.match(st.machineId, /^[0-9A-F]{4}(-[0-9A-F]{4}){4}$/);
    assert.strictEqual((await post('/api/license/activate', {}, '{"key":"x"}')).status, 400);                         // custom header required
    const bad = await post('/api/license/activate', { 'X-Mixer-Action': 'license' }, '{"key":"AMIX1.abc.def"}');
    assert.strictEqual(bad.status, 400); assert.strictEqual((await bad.json()).reason, 'bad-signature');
    assert.strictEqual((await post('/api/license/activate', { 'X-Mixer-Action': 'license' }, 'not json')).status, 400);
    assert.strictEqual((await post('/api/license/activate', { 'X-Mixer-Action': 'license', 'Content-Type': 'text/plain' }, '{}')).status, 400);
    assert.strictEqual((await post('/api/license/deactivate', { 'X-Mixer-Action': 'license' })).status, 200);
    const dl = await post('/api/update/download', { 'X-Mixer-Action': 'update' });
    assert.strictEqual(dl.status, 402); assert.strictEqual((await dl.json()).needs, 'ota');                           // BASIC: can check, cannot download
    assert.strictEqual((await post('/api/update/download', {})).status, 400);
    assert.strictEqual((await fetch(base + '/api/license', { headers: { Origin: 'https://evil.example' } })).status, 403);
  } finally { server.closeAllConnections(); server.close(); delete process.env.BRIDGE_LICENSE_FILE; }
});

test('duplex: one native stream reads and writes the same interface', () => {
  const { _owners } = require('./asio-lock'); _owners.clear();
  const { createDuplexSession } = require('./duplex');
  const fa = fakeAudify();
  const dev = require('./audify').listDevices(() => fa).devices.find(d => /ASIO/.test(d.hostAPIName));
  const sent = [], bin = [];
  const conn = { send: m => sent.push(JSON.parse(m)), sendBinary: b => bin.push(b.length) };
  const s = createDuplexSession(conn, () => { throw new Error('no pa'); }, () => fa);
  s.onText(JSON.stringify({ type: 'start', deviceId: dev.id, inChannels: 2, channels: 2, sampleRate: 48000 }));
  assert.strictEqual(sent[0].type, 'started'); assert.strictEqual(sent[0].duplex, true); assert.strictEqual(sent[0].inChannels, 2);
  assert.strictEqual(fa.opened.length, 1); assert.ok(fa.opened[0].out && fa.opened[0].inp);                 // ONE stream with both directions
  const frame = Buffer.alloc(sent[0].frameSize * 4);
  s.onBinary(frame); assert.strictEqual(fa.written.length, 1);                                               // page audio reaches the device
  s.onBinary(Buffer.alloc(3)); assert.strictEqual(fa.written.length, 1);                                     // partial frame dropped
  assert.strictEqual(_owners.size, 1);
  s.onText(JSON.stringify({ type: 'stop' })); assert.strictEqual(sent[sent.length - 1].type, 'stopped'); assert.strictEqual(_owners.size, 0);
  const e = [];  const s2 = createDuplexSession({ send: m => e.push(JSON.parse(m)), sendBinary() {} }, () => { throw new Error('x'); }, () => fa);
  s2.onText(JSON.stringify({ type: 'start', deviceId: 5000 })); assert.match(e[0].message, /device not found/);
  const wasapi = require('./audify').listDevices(() => fa).devices.find(d => /WASAPI/.test(d.hostAPIName));
  s2.onText(JSON.stringify({ type: 'start', deviceId: wasapi.id })); assert.match(e[1].message, /cannot read and write|device not found|could not/);
  s.onClose(); s2.onClose();
});


test('windows native helpers: AudioDevices.exe output and the VBScript fallback', async () => {
  const wn = require('./winnative');
  const json = JSON.stringify({ ok: true, devices: [{ id: '{a}', name: 'Microphone (Focusrite USB)', kind: 'input', channels: 2, sampleRate: 48000, default: true }, { id: '{b}', name: 'Speakers (Focusrite USB)', kind: 'output', channels: 2, sampleRate: 48000, default: false }] });
  const calls = [];
  const r = await wn.listEndpoints({ platform: 'win32', arch: 'x64', exists: () => true, run: async (c, a) => { calls.push(c); return '﻿' + json; } });
  assert.match(calls[0], /x64[\\/]AudioDevices\.exe$/); assert.strictEqual(r.engine, 'wasapi-native');
  assert.strictEqual(r.devices[0].inputs, 2); assert.strictEqual(r.devices[0].outputs, 0); assert.strictEqual(r.devices[1].outputs, 2); assert.ok(r.devices.every(d => d.id < 0 && d.native));
  assert.strictEqual(require('./interfaces').groupInterfaces(r.devices.map(d => ({ id: d.id, name: d.name, hostApi: d.hostApi, inputs: d.inputs, outputs: d.outputs })))[0].name, 'Focusrite USB');
  assert.match(wn.exePath('ia32'), /x86[\\/]AudioDevices\.exe$/);
  assert.strictEqual(await wn.listEndpoints({ platform: 'linux' }), null);
  assert.strictEqual(await wn.listEndpoints({ platform: 'win32', exists: () => false }), null);
  assert.strictEqual(await wn.listEndpoints({ platform: 'win32', exists: () => true, run: async () => 'garbage' }), null);
  const w = await wn.listWmi({ platform: 'win32', exists: () => true, run: async (c, a) => { assert.strictEqual(c, 'cscript'); assert.strictEqual(a[0], '//nologo'); return '{"ok":true,"devices":[{"name":"Realtek Audio","vendor":"Realtek","status":"OK"}]}'; } });
  assert.deepStrictEqual(w, [{ name: 'Realtek Audio', vendor: 'Realtek', status: 'OK' }]);
  // the shipped sources and binaries exist, and the binaries are Windows PE files of the right machine type
  const dir = pathx.join(__dirname, '..', 'platforms', 'windows', 'native');
  for (const f of ['src/AudioDevices.cpp', 'vbs/audio-devices.vbs']) assert.ok(require('node:fs').existsSync(pathx.join(dir, f)), f);
  for (const [f, m] of [['x64/AudioDevices.exe', 0x8664], ['x86/AudioDevices.exe', 0x14c]]) { const b = require('node:fs').readFileSync(pathx.join(dir, f)); assert.strictEqual(b.readUInt16LE(0), 0x5a4d); assert.strictEqual(b.readUInt16LE(b.readUInt32LE(0x3c) + 4), m); }
});

test('interfaces: DirectSound "Primary Sound" default mappers are flagged, sorted last and not mistaken for hardware', () => {
  const { groupInterfaces, isPrimary } = require('./interfaces');
  const dev = (id, name, inputs, outputs) => ({ id, name, hostApi: 'Windows DirectSound', inputs, outputs, sampleRate: 48000 });
  const list = groupInterfaces([dev(0, 'Primary Sound Capture Driver', 2, 0), dev(1, 'Primary Sound Driver', 0, 2), dev(2, 'Microphone (USB Mic)', 1, 0)]);
  assert.ok(isPrimary('Primary Sound Capture Driver') && isPrimary('Primary Sound Driver') && !isPrimary('Microphone (USB Mic)'));
  assert.strictEqual(list[0].name, 'USB Mic'); assert.ok(!list[0].systemDefault);
  const prim = list.filter(i => i.systemDefault); assert.strictEqual(prim.length, 2);
  assert.ok(prim.some(i => /^System default input/.test(i.name)) && prim.some(i => /^System default output/.test(i.name)));
});

test('bluetooth: A2DP + hands-free endpoints are one device, written through A2DP; RtAudio converts the hands-free rate', () => {
  const { groupInterfaces, isBluetooth } = require('./interfaces');
  const dev = (id, name, inputs, outputs) => ({ id, name, hostApi: 'Windows WASAPI', inputs, outputs, sampleRate: 48000 });
  const list = groupInterfaces([
    dev(0, 'Headset (Galaxy Buds2 Pro Hands-Free AG Audio)', 1, 1), dev(1, 'Headphones (Galaxy Buds2 Pro Stereo)', 0, 2),
    dev(2, 'Speakers (Realtek Audio)', 0, 2), dev(3, 'Stereo Mix (Realtek Audio)', 2, 0),
  ]);
  const bt = list.filter(i => i.bluetooth); assert.strictEqual(bt.length, 1);
  assert.strictEqual(bt[0].name, 'Galaxy Buds2 Pro'); assert.strictEqual(bt[0].read.deviceId, 0);        // microphone: hands-free
  assert.strictEqual(bt[0].write.deviceId, 1); assert.strictEqual(bt[0].write.profile, 'a2dp');          // playback: A2DP stereo, not the phone-quality endpoint
  assert.strictEqual(list.filter(i => !i.bluetooth && !i.loopback).length, 1);                             // Realtek stays separate
  assert.ok(isBluetooth('Headset (X Hands-Free AG Audio)') && !isBluetooth('Speakers (Realtek Audio)'));
  // resampler: 16 kHz -> 48 kHz triples the frames, continuous across chunks
  const a = require('./audify');
  const rs = a.createResampler(16000, 48000, 1);
  const chunk = n0 => { const b = Buffer.alloc(8 * 2); for (let i = 0; i < 8; i++) b.writeInt16LE(n0 + i * 100, i * 2); return b; };
  const o1 = rs(chunk(0)), o2 = rs(chunk(800));
  const tot = (o1.length + o2.length) / 2; assert.ok(tot >= 44 && tot <= 48, 'frames ' + tot);
  const all = []; for (const o of [o1, o2]) for (let i = 0; i < o.length; i += 2) all.push(o.readInt16LE(i));
  assert.ok(all.every((v, i) => i === 0 || v >= all[i - 1]), 'monotonic ramp stays smooth over the chunk edge');
  assert.strictEqual(a.createResampler(48000, 48000, 2), null);
  // RtAudio: a hands-free device that only offers 16 kHz opens at 16 kHz and still delivers the mixer's rate; other devices still refuse
  const fa = fakeAudify(); const { RtAudio } = fa; const orig = RtAudio.prototype.getDevices;
  const btDev = { id: 1100, rtId: 9, api: 'WINDOWS_WASAPI', hostAPIName: 'Windows WASAPI', name: 'Headset (Galaxy Buds2 Pro Hands-Free AG Audio)', maxInputChannels: 1, maxOutputChannels: 1, sampleRates: [8000, 16000] };
  const got = []; const st = a.openStream({ mod: fa, dev: btDev, direction: 'input', channels: 2, sampleRate: 48000, onData: b => got.push(b.length) });
  assert.strictEqual(st.resampled, true); assert.strictEqual(st.deviceRate, 16000); assert.strictEqual(st.sampleRate, 48000); assert.strictEqual(st.channels, 1);
  assert.strictEqual(fa.opened[fa.opened.length - 1].rate, 16000);
  const out = a.openStream({ mod: fa, dev: { ...btDev, name: 'Headphones (Galaxy Buds2 Pro Stereo)', maxOutputChannels: 2, sampleRates: [44100, 48000] }, direction: 'output', channels: 2, sampleRate: 96000 });
  assert.strictEqual(out.deviceRate, 48000); st.close(); out.close();
  assert.throws(() => a.openStream({ mod: fa, dev: { ...btDev, api: 'WINDOWS_ASIO', hostAPIName: 'ASIO', name: 'Focusrite USB ASIO', maxOutputChannels: 2 }, direction: 'output', channels: 2, sampleRate: 48000 }), /does not support 48000.*control panel/);
  void orig;
});

test('ASIO (RtAudio): one stream per driver. Reading and writing a device share a duplex stream, probing never touches an open driver', () => {
  const a = require('./audify');
  const fa = fakeAudify();
  const dev = a.listDevices(() => fa).devices.find(d => d.api === 'WINDOWS_ASIO');
  let probes = 0; const origGet = fa.RtAudio.prototype.getDevices;
  fa.RtAudio.prototype.getDevices = function () { if (this.api === 6) probes++; return origGet.call(this); };
  const got = [];
  const out = a.openStream({ mod: fa, dev, direction: 'output', channels: 2, sampleRate: 48000 });
  assert.strictEqual(fa.opened.length, 1); assert.ok(fa.opened[0].out && !fa.opened[0].inp);
  const before = probes; const again = a.listDevices(() => fa);                                      // page scan while the driver is open
  assert.strictEqual(probes, before); assert.ok(again.devices.some(d => d.api === 'WINDOWS_ASIO' && d.id === dev.id));   // list kept, driver not probed
  const inp = a.openStream({ mod: fa, dev, direction: 'input', channels: 2, sampleRate: 48000, onData: b => got.push(b.length) });
  assert.strictEqual(fa.opened.length, 2); assert.ok(fa.opened[1].out && fa.opened[1].inp);          // re-opened as ONE duplex stream
  assert.strictEqual(inp.channels, 2); assert.strictEqual(out.channels, 2);
  out.write(Buffer.alloc(out.frameSize * 4)); assert.strictEqual(fa.written.length, 1);              // the first user is still attached
  assert.throws(() => a.openStream({ mod: fa, dev, direction: 'input', channels: 2, sampleRate: 48000 }), /already open for reading/);
  inp.close(); out.write(Buffer.alloc(out.frameSize * 4)); assert.strictEqual(fa.written.length, 2);  // closing one side keeps the other running
  out.close(); assert.strictEqual(a._asioOf(fa).size, 0);
  const afterClose = probes; a.listDevices(() => fa); assert.ok(probes > afterClose);                 // free again: probed again
  // a different sample rate cannot share the stream, and the first user keeps its stream
  const o2 = a.openStream({ mod: fa, dev, direction: 'output', channels: 2, sampleRate: 48000 });
  assert.throws(() => a.openStream({ mod: fa, dev, direction: 'input', channels: 2, sampleRate: 44100 }), /same sample rate/);
  o2.write(Buffer.alloc(o2.frameSize * 4)); o2.close();
  // the duplex endpoint owns the driver
  const d = a.openDuplex({ mod: fa, dev, inChannels: 2, outChannels: 2, sampleRate: 48000 });
  assert.throws(() => a.openStream({ mod: fa, dev, direction: 'input', channels: 2, sampleRate: 48000 }), /already open for reading and writing/);
  d.close(); assert.strictEqual(a._asioOf(fa).size, 0);
});

test('ASIO duplex sharing is keyed on the driver NAME, not dev.id: read and write opened from two independent device scans share ONE duplex stream', () => {
  const a = require('./audify');
  const fa = fakeAudify();
  const base = a.listDevices(() => fa).devices.find(d => d.api === 'WINDOWS_ASIO');
  // the output session and the input session each enumerate the device on their own, so the SAME physical driver carries a different dev.id in each
  const outDev = { ...base }, inDev = { ...base, id: base.id + 500 };
  const got = [];
  const out = a.openStream({ mod: fa, dev: outDev, direction: 'output', channels: 2, sampleRate: 48000 });
  assert.strictEqual(a._asioOf(fa).size, 1); assert.ok(fa.opened[0].out && !fa.opened[0].inp);
  const inp = a.openStream({ mod: fa, dev: inDev, direction: 'input', channels: 2, sampleRate: 48000, onData: b => got.push(b.length) });
  // the differing ids must not hide that it is the same driver: it re-opens as ONE shared duplex stream, not a second (colliding) single-client stream
  assert.strictEqual(a._asioOf(fa).size, 1, 'one registry entry for the driver despite the two ids');
  assert.strictEqual(fa.opened.length, 2); assert.ok(fa.opened[1].out && fa.opened[1].inp, 're-opened as one duplex stream');
  assert.strictEqual(inp.channels, 2); assert.strictEqual(out.channels, 2);
  out.write(Buffer.alloc(out.frameSize * 4)); assert.ok(fa.written.length >= 1);                 // the writer stays attached across the re-open
  inp.close(); out.write(Buffer.alloc(out.frameSize * 4)); out.close();                            // closing the reader (its own id) still finds the shared entry
  assert.strictEqual(a._asioOf(fa).size, 0);
  // the duplex endpoint also refuses a second open of the same driver seen under a different id
  const dd = a.openDuplex({ mod: fa, dev: outDev, inChannels: 2, outChannels: 2, sampleRate: 48000 });
  assert.throws(() => a.openStream({ mod: fa, dev: inDev, direction: 'output', channels: 2, sampleRate: 48000 }), /already open for reading and writing/);
  dd.close(); assert.strictEqual(a._asioOf(fa).size, 0);
});

test('system audio: interfaces are read from the OS on Linux, macOS and Windows without an audio engine', async () => {
  const sa = require('./sysaudio');
  const alsa = 'card 1: USB [Scarlett 2i2 USB], device 0: USB Audio [USB Audio]\ncard 0: PCH [HDA Intel PCH], device 0: ALC [ALC3246 Analog]\n';
  const pactl = '1\talsa_input.usb-Scarlett.analog-stereo\tmodule-alsa-card.c\ts16le 2ch 48000Hz\tRUNNING\n2\talsa_output.usb.monitor\tmodule\ts16le 2ch 44100Hz\tIDLE\n';
  const run = async (cmd, args) => (cmd === 'arecord' ? alsa : cmd === 'aplay' ? alsa : cmd === 'pactl' ? (args[2] === 'sources' ? pactl : '1\talsa_output.usb.analog-stereo\tm\ts16le 2ch 44100Hz\tIDLE\n') : '');
  const l = await sa.list({ platform: 'linux', run });
  assert.strictEqual(l.engine, 'alsa-native'); assert.ok(l.devices.every(d => d.id < 0 && d.native));
  assert.ok(l.devices.some(d => d.name.startsWith('Scarlett') && d.inputs === 2) && l.devices.some(d => d.name.startsWith('Scarlett') && d.outputs === 2));
  assert.ok(l.devices.some(d => d.hostApi === 'PulseAudio' && /^Monitor of/.test(d.name)));
  const gi = require('./interfaces').groupInterfaces(l.devices.map(d => ({ id: d.id, name: d.name, hostApi: d.hostApi, inputs: d.inputs, outputs: d.outputs })));
  assert.ok(gi.some(i => /Scarlett/.test(i.name) && i.inputs && i.outputs));
  const proc = await sa.list({ platform: 'linux', run: async () => '', readFile: () => ' 0 [PCH            ]: HDA-Intel - HDA Intel PCH\n                      HDA Intel PCH at 0x1\n' });
  assert.ok(proc && proc.devices.length === 2);                                                    // /proc/asound/cards fallback: input + output
  const mac = JSON.stringify({ SPAudioDataType: [{ _items: [{ _name: 'MacBook Pro Microphone', coreaudio_device_input: 1, coreaudio_default_audio_input_device: 'spaudio_yes', coreaudio_device_srate: 48000 },
    { _name: 'MacBook Pro Speakers', coreaudio_device_output: 2, coreaudio_default_audio_output_device: 'spaudio_yes' }, { _name: 'Scarlett 2i2', coreaudio_device_input: 2, coreaudio_device_output: 2 }] }] });
  const m = await sa.list({ platform: 'darwin', run: async () => mac });
  assert.strictEqual(m.engine, 'coreaudio-native'); assert.strictEqual(m.devices.length, 4); assert.ok(m.devices[0].isDefault);
  assert.strictEqual(await sa.list({ platform: 'darwin', run: async () => 'garbage' }), null);
  assert.strictEqual(await sa.list({ platform: 'win32' }), null);
  // the real detection fills `native` when no engine is installed (this machine has none) and /api/interfaces serves it
  const info = await require('./detect').detect();
  if (!info.portaudio) assert.ok(info.native === null || Array.isArray(info.native.devices));
});

test('plugin inserts (PHASE / FX slots): read, write, validate, plan gate', async () => {
  const ins = require('./inserts');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'ins-')), file = pathx.join(dir, 'inserts.json');
  const scan = () => ({ plugins: [{ name: 'Pultec', format: 'VST3', valid: true, compatible: true }, { name: 'Old', format: 'VST2', valid: true, compatible: true }, { name: 'Plain', format: 'VST2', valid: false, compatible: false, reason: 'a plain DLL, not a VST2 plugin' }] });
  assert.deepStrictEqual(ins.read(file), { slots: {} });
  let st = ins.set({ slot: 'phase:ch1', plugin: 'Pultec' }, { file, scan }); assert.deepStrictEqual(st.slots['phase:ch1'], { plugin: 'Pultec', format: 'VST3', bypass: false });
  st = ins.set({ slot: 'phase:ch1', bypass: true }, { file, scan }); assert.strictEqual(st.slots['phase:ch1'].bypass, true);
  st = ins.set({ slot: 'fx:1', plugin: 'Old', format: 'VST2' }, { file, scan });
  assert.deepStrictEqual(Object.keys(ins.read(file).slots).sort(), ['fx:1', 'phase:ch1']);                       // read back from disk
  assert.strictEqual(ins.set({ slot: 'fx:1', plugin: null }, { file, scan }).slots['fx:1'], undefined);
  for (const [b, code] of [[{ slot: '../x', plugin: 'Pultec' }, 400], [{ slot: 'fx:1', plugin: 'Nope' }, 404], [{ slot: 'fx:2', plugin: 'Plain' }, 409], [{ slot: 'fx:9', bypass: true }, 404], [{ slot: 'fx:1', plugin: 'x'.repeat(200) }, 400]]) {
    assert.throws(() => ins.set(b, { file, scan }), e => e.status === code, JSON.stringify(b));
  }
  assert.strictEqual(fsx.statSync(file).mode & 0o077, 0);
  // endpoints: GET open, POST needs the header and the PRO plan
  process.env.BRIDGE_INSERTS_FILE = file; process.env.BRIDGE_LICENSE_FILE = pathx.join(dir, 'license.json');
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const g = await (await fetch(base + '/api/inserts')).json(); assert.strictEqual(g.ok, true); assert.ok(g.slots['phase:ch1']);
  const post = h => fetch(base + '/api/inserts', { method: 'POST', headers: { 'Content-Type': 'application/json', ...h }, body: JSON.stringify({ slot: 'fx:3', plugin: null }) });
  assert.strictEqual((await post({})).status, 400);                                                               // header required
  const r = await post({ 'X-Mixer-Action': 'inserts' }); assert.strictEqual(r.status, 402); assert.strictEqual((await r.json()).needs, 'plugins');   // BASIC plan
  server.closeAllConnections(); server.close();
  delete process.env.BRIDGE_INSERTS_FILE; delete process.env.BRIDGE_LICENSE_FILE;
});

test('RtAudio WASAPI: closest device rate + converter, mono devices get a mixdown, ASIO keeps its explicit errors', () => {
  const a = require('./audify');
  const fa = fakeAudify();
  const wasapi = { id: 1200, rtId: 5, api: 'WINDOWS_WASAPI', hostAPIName: 'Windows WASAPI', name: 'Speakers (USB Mono)', maxInputChannels: 0, maxOutputChannels: 1, sampleRates: [48000] };
  const s = a.openStream({ mod: fa, dev: wasapi, direction: 'output', channels: 2, sampleRate: 44100 });          // mixer at 44.1 kHz stereo, device 48 kHz mono
  assert.strictEqual(s.deviceRate, 48000); assert.strictEqual(s.resampled, true); assert.strictEqual(s.mixedDown, true); assert.strictEqual(s.channels, 2); assert.strictEqual(s.deviceChannels, 1);
  const o = fa.opened[fa.opened.length - 1]; assert.strictEqual(o.out.nChannels, 1); assert.strictEqual(o.rate, 48000);
  s.write(Buffer.alloc(s.frameSize * 4)); s.close();
  const st = Buffer.alloc(8); st.writeInt16LE(1000, 0); st.writeInt16LE(3000, 2); st.writeInt16LE(-200, 4); st.writeInt16LE(-400, 6);
  const mono = a.remapChannels(st, 2, 1); assert.deepStrictEqual([mono.readInt16LE(0), mono.readInt16LE(2)], [2000, -300]);
  assert.strictEqual(a.remapChannels(st, 2, 2), st);
  const asio = { id: 1000, rtId: 0, api: 'WINDOWS_ASIO', hostAPIName: 'ASIO', name: 'Focusrite USB ASIO', maxInputChannels: 18, maxOutputChannels: 2, sampleRates: [44100, 48000] };
  assert.throws(() => a.openStream({ mod: fakeAudify(), dev: asio, direction: 'output', channels: 8, sampleRate: 48000 }), /only 2 output/);
  assert.throws(() => a.openStream({ mod: fakeAudify(), dev: asio, direction: 'output', channels: 2, sampleRate: 96000 }), /control panel/);
});

test('native plugin host: a real VST2 effect runs in its own process and processes audio, parameters and errors', async () => {
  const { spawnSync } = require('node:child_process');
  const ph = require('./pluginhost');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'host-'));
  const root = pathx.join(__dirname, '..', 'platforms', 'windows', 'plugin-host');
  const hostBin = pathx.join(dir, 'PluginHost'), plug = pathx.join(dir, 'gain.vst');
  const c1 = spawnSync('g++', ['-O2', '-std=c++11', '-o', hostBin, pathx.join(root, 'src', 'PluginHost.cpp'), '-ldl']);
  const c2 = spawnSync('g++', ['-shared', '-fPIC', '-std=c++11', '-o', plug, pathx.join(root, 'test', 'GainPlugin.cpp')]);
  if (process.platform === 'win32' || c1.status !== 0 || c2.status !== 0) return;              // needs a C++ compiler (Windows ships the .exe)
  const h = new ph.PluginHostProcess({ host: hostBin, plugin: plug, sampleRate: 48000, blockSize: 256 });
  const info = await h.start();
  assert.strictEqual(info.name, 'Test Gain'); assert.strictEqual(info.inputs, 2); assert.strictEqual(info.params, 1);
  const blk = Buffer.alloc(700 * 8); for (let i = 0; i < 1400; i++) blk.writeFloatLE(i % 2 ? -0.1 : 0.1, i * 4);      // 700 frames: several host blocks
  let out = await h.process(blk); assert.strictEqual(out.length, blk.length); assert.ok(Math.abs(out.readFloatLE(0) - 0.1) < 1e-6 && Math.abs(out.readFloatLE(out.length - 4) + 0.1) < 1e-6);   // unity at 0.5
  h.setParam(0, 1); out = await h.process(blk); assert.ok(Math.abs(out.readFloatLE(0) - 0.2) < 1e-6);                  // +6 dB
  const ps = await h.params(); assert.strictEqual(ps[0].name, 'Gain'); assert.ok(Math.abs(ps[0].value - 1) < 1e-6);
  h.stop(); await new Promise(r => h.child.once('close', r));
  // refusals come back as clear errors, never a crash of the bridge
  for (const [file, re] of [[pathx.join(dir, 'missing.vst'), /could not load/], [pathx.join(dir, 'x.vst3'), /could not load/], [hostBin, /could not load|not a VST2/]]) {
    await assert.rejects(new ph.PluginHostProcess({ host: hostBin, plugin: file }).start(), re);
  }
  await assert.rejects(new ph.PluginHostProcess({ host: pathx.join(dir, 'nope'), plugin: plug }).start(), /cannot start the plugin host/);
  // the WebSocket session: plan gate, slot lookup, processing, bypass, parameters, stop
  const sent = [], bin = []; const conn = { send: m => sent.push(JSON.parse(m)), sendBinary: b => bin.push(b) };
  const store = { slots: { 'fx:1': { plugin: 'gain', format: 'VST2', bypass: false } } };
  const scan = () => ({ plugins: [{ name: 'gain', format: 'VST2', file: plug, valid: true, compatible: true }] });
  const mk = (over = {}) => ph.createInsertSession(conn, { scan, read: () => store, status: () => ({ plan: { features: ['core', 'plugins'] } }), hostFile: () => hostBin, ...over });
  const wait = async f => { for (let i = 0; i < 100 && !f(); i++) await new Promise(r => setTimeout(r, 30)); };
  let s = mk({ status: () => ({ plan: { features: ['core'] } }) }); s.onText(JSON.stringify({ type: 'start', slot: 'fx:1' })); await wait(() => sent.length); assert.match(sent.pop().message, /PRO or STUDIO/);
  s = mk(); s.onText(JSON.stringify({ type: 'start', slot: 'fx:7' })); await wait(() => sent.length); assert.match(sent.pop().message, /slot is empty/);
  s = mk({ hostFile: () => null }); s.onText(JSON.stringify({ type: 'start', slot: 'fx:1' })); await wait(() => sent.length); assert.match(sent.pop().message, /not installed/);
  s = mk(); s.onText(JSON.stringify({ type: 'start', slot: 'fx:1', sampleRate: 48000 })); await wait(() => sent.length);
  assert.strictEqual(sent[0].type, 'started'); assert.strictEqual(sent[0].name, 'Test Gain'); sent.length = 0;
  s.onBinary(blk); await wait(() => bin.length); assert.ok(Math.abs(bin[0].readFloatLE(0) - 0.1) < 1e-6);
  s.onText(JSON.stringify({ type: 'param', i: 0, v: 1 })); s.onBinary(blk); await wait(() => bin.length > 1); assert.ok(Math.abs(bin[1].readFloatLE(0) - 0.2) < 1e-6);
  s.onText(JSON.stringify({ type: 'params' })); await wait(() => sent.length); assert.strictEqual(sent[0].list[0].name, 'Gain'); sent.length = 0;
  s.onText(JSON.stringify({ type: 'bypass', on: true })); s.onBinary(blk); assert.strictEqual(bin[2].readFloatLE(0), blk.readFloatLE(0));   // dry
  s.onBinary(Buffer.alloc(5)); assert.strictEqual(bin.length, 3);                                                               // partial frame ignored
  s.onText(JSON.stringify({ type: 'stop' })); assert.strictEqual(sent[sent.length - 1].type, 'stopped'); s.onClose();
  // VST 3: the same session runs a real VST3 effect (a test plugin written against the public interface layout), picked by class name from a bundle
  const v3 = pathx.join(dir, 'gain3.so'), bundle = pathx.join(dir, 'gain3.vst3'), c3 = spawnSync('g++', ['-shared', '-fPIC', '-std=c++11', '-o', v3, pathx.join(root, 'test', 'Gain3Plugin.cpp')]);
  if (c3.status === 0 && process.platform === 'linux') {
    const arch = { x64: 'x86_64', arm64: 'aarch64', ia32: 'i386' }[process.arch];
    if (arch) { fsx.mkdirSync(pathx.join(bundle, 'Contents', arch + '-linux'), { recursive: true }); fsx.copyFileSync(v3, pathx.join(bundle, 'Contents', arch + '-linux', 'gain3.so')); }
    const flat = pathx.join(dir, 'flat.vst3'); fsx.copyFileSync(v3, flat);                                                                                  // a bare module file works as well as a bundle
    for (const file of arch ? [flat, bundle] : [flat]) {
      const h3 = new ph.PluginHostProcess({ host: hostBin, plugin: file, className: 'Test Gain 3' }), i3 = await h3.start();
      assert.deepStrictEqual([i3.name, i3.format, i3.inputs, i3.outputs, i3.params, i3.vendor], ['Test Gain 3', 'VST3', 2, 2, 1, 'Audio Mixer tests']);
      let o3 = await h3.process(blk); assert.ok(Math.abs(o3.readFloatLE(0) - 0.1) < 1e-6 && Math.abs(o3.readFloatLE(o3.length - 4) + 0.1) < 1e-6);        // unity at 0.5
      h3.setParam(0, 1); o3 = await h3.process(blk); assert.ok(Math.abs(o3.readFloatLE(0) - 0.2) < 1e-6);                                                   // +6 dB, through the process call's parameter changes
      const p3 = await h3.params(); assert.deepStrictEqual([p3[0].name, p3[0].label, p3[0].display], ['Gain', 'x', '2.00']); assert.ok(Math.abs(p3[0].value - 1) < 1e-6);
      h3.stop(); await new Promise(r => h3.child.once('close', r));
    }
    const first = new ph.PluginHostProcess({ host: hostBin, plugin: flat }); assert.strictEqual((await first.start()).name, 'Test Gain 3');                  // the instrument class is skipped without a name
    first.stop(); await new Promise(r => first.child.once('close', r));
    await assert.rejects(new ph.PluginHostProcess({ host: hostBin, plugin: flat, className: 'Nope' }).start(), /no audio class named "Nope"/);
    store.slots['fx:1'].format = 'VST3';
    s = mk({ scan: () => ({ plugins: [{ name: 'gain3', format: 'VST3', file: flat, className: 'Test Gain 3', valid: true, compatible: true }] }) }); store.slots['fx:1'].plugin = 'gain3';
    sent.length = 0; bin.length = 0; s.onText(JSON.stringify({ type: 'start', slot: 'fx:1' })); await wait(() => sent.length); assert.strictEqual(sent[0].type, 'started'); assert.strictEqual(sent[0].name, 'Test Gain 3');
    s.onBinary(blk); await wait(() => bin.length); assert.ok(Math.abs(bin[0].readFloatLE(0) - 0.1) < 1e-6); s.onClose();
  }
  await assert.rejects(new ph.PluginHostProcess({ host: hostBin, plugin: pathx.join(dir, 'missing.vst3') }).start(), /could not load|no binary/);
  // bundles with many plugins: one entry per effect class, read from moduleinfo.json (the instruments are left out)
  const pl = require('./plugins'), mb = pathx.join(dir, 'Pack.vst3'); fsx.mkdirSync(pathx.join(mb, 'Contents', 'Resources'), { recursive: true });
  fsx.writeFileSync(pathx.join(mb, 'Contents', 'Resources', 'moduleinfo.json'), JSON.stringify({ Classes: [{ Name: 'Comp', Catetory: 'Audio Module Class', 'Sub Categories': ['Fx'] }, { Name: 'Verb', Category: 'Audio Module Class', 'Sub Categories': ['Fx', 'Reverb'] }, { Name: 'Synth', Category: 'Audio Module Class', 'Sub Categories': ['Instrument'] }, { Name: 'Ctl', Category: 'Component Controller Class' }] }));
  assert.deepStrictEqual(pl.bundleClasses(mb), ['Comp', 'Verb']);
  const sc = pl.scan({ platform: 'linux', env: { AUDIO_MIXER_PLUGINS: dir }, home: dir, hostArch: process.arch });
  assert.deepStrictEqual(sc.plugins.filter(p => /^Pack/.test(p.name)).map(p => [p.name, p.className]), [['Pack: Comp', 'Comp'], ['Pack: Verb', 'Verb']]);
  assert.ok(ph.hostPath({ platform: 'win32', arch: 'x64', env: {}, exists: () => true }).endsWith(pathx.join('x64', 'PluginHost.exe')));
  assert.ok(ph.hostPath({ platform: 'win32', arch: 'ia32', env: {}, exists: () => true }).endsWith(pathx.join('x86', 'PluginHost.exe')));
  assert.strictEqual(ph.hostPath({ platform: 'linux', env: {}, exists: () => false }), null);
  // the shipped Windows hosts are PE files of the right machine type
  for (const [f, m] of [['x64', 0x8664], ['x86', 0x14c]]) { const b = fsx.readFileSync(pathx.join(root, f, 'PluginHost.exe')); assert.strictEqual(b.readUInt16LE(0), 0x5a4d); assert.strictEqual(b.readUInt16LE(b.readUInt32LE(0x3c) + 4), m); }
});



test('RtAudio DirectSound / WASAPI / ASIO: warnings do not close the stream, cut 31-character names join their interface', () => {
  const a = require('./audify'); const { groupInterfaces, untruncate } = require('./interfaces');
  // RtAudio 5 (WARNING = 0) and 6 (WARNING = 1) both: a warning is not an error
  assert.ok(a.isWarning({}, 0) && a.isWarning({}, 1) && !a.isWarning({}, 2) && !a.isWarning({}, 9));
  assert.ok(a.isWarning({ RtAudioErrorType: { RTAUDIO_NO_ERROR: 0, RTAUDIO_WARNING: 1, RTAUDIO_UNKNOWN_ERROR: 2 } }, 1) && !a.isWarning({ RtAudioErrorType: { RTAUDIO_NO_ERROR: 0, RTAUDIO_WARNING: 1, RTAUDIO_UNKNOWN_ERROR: 2 } }, 2));
  assert.ok(a.isWarning({}, 'RTAUDIO_WARNING') && !a.isWarning({}, 'RTAUDIO_DRIVER_ERROR'));
  const fa = fakeAudify(); let errCb = null;
  fa.RtAudio.prototype.openStream = function (out, inp, fmt, rate, frames, name, cb, fo, flags, onErr) { errCb = onErr; return frames === 0 ? 192 : frames; };
  const ds = { id: 1300, rtId: 2, api: 'WINDOWS_DS', hostAPIName: 'Windows DirectSound', name: 'Speakers (Realtek)', maxInputChannels: 0, maxOutputChannels: 2, sampleRates: [44100, 48000] };
  const errors = [], warns = [];
  const st = a.openStream({ mod: fa, dev: ds, direction: 'output', channels: 2, sampleRate: 48000, onError: e => errors.push(e.message), onWarning: e => warns.push(e.message) });
  errCb(1, 'RtApiDs: buffer underrun'); errCb(0, 'skipped');                                         // glitches: stream stays open
  assert.deepStrictEqual(errors, []); assert.strictEqual(warns.length, 2); assert.strictEqual(st.warnings, 2); assert.match(st.lastWarning, /skipped/);
  errCb(9, 'RtApiDs: device lost'); assert.deepStrictEqual(errors, ['RtApiDs: device lost']);       // a real error still ends it
  // 31-character names of MME / DirectSound
  const dev = (id, name, hostApi, inputs, outputs) => ({ id, name, hostApi, inputs, outputs, sampleRate: 48000 });
  const full = 'Microphone (Focusrite USB Audio)', cut = full.slice(0, 31);
  assert.strictEqual(cut.length, 31);
  const list = groupInterfaces([dev(0, cut, 'MME', 2, 0), dev(1, 'Speakers (Focusrite USB Audio)'.slice(0, 31), 'Windows DirectSound', 0, 2), dev(2, full, 'Windows WASAPI', 2, 0), dev(3, 'Speakers (Focusrite USB Audio)', 'Windows WASAPI', 0, 2)]);
  assert.strictEqual(list.length, 1); assert.strictEqual(list[0].name, 'Focusrite USB Audio'); assert.strictEqual(list[0].apis.length, 4);
  assert.strictEqual(untruncate([dev(0, 'Short (Mic)', 'MME', 1, 0)])[0].name, 'Short (Mic)');                                                       // short names untouched
  assert.strictEqual(groupInterfaces([dev(0, cut, 'MME', 2, 0)]).length, 1);                                                                           // nothing to join: kept
});

test('Swift Core Audio helper: its JSON is read by the bridge, the source is shipped', async () => {
  const sa = require('./sysaudio');
  const json = JSON.stringify({ ok: true, devices: [{ id: 'BuiltInMic', name: 'MacBook Pro Microphone', kind: 'input', channels: 1, sampleRate: 48000, default: true, transport: 'builtin' },
    { id: 'Scarlett', name: 'Scarlett 2i2', kind: 'input', channels: 2, sampleRate: 44100, default: false, transport: 'usb' }, { id: 'Scarlett', name: 'Scarlett 2i2', kind: 'output', channels: 2, sampleRate: 44100, default: true, transport: 'usb' }] });
  const calls = [];
  const m = await sa.list({ platform: 'darwin', exists: () => true, helper: '/x/AudioDevices', run: async c => { calls.push(c); return json; } });
  assert.deepStrictEqual(calls, ['/x/AudioDevices']); assert.strictEqual(m.engine, 'coreaudio-native'); assert.strictEqual(m.devices.length, 3);
  assert.strictEqual(m.devices[1].transport, 'usb'); assert.ok(m.devices[0].isDefault && m.devices[0].inputs === 1 && m.devices[2].outputs === 2);
  const gi = require('./interfaces').groupInterfaces(m.devices.map(d => ({ id: d.id, name: d.name, hostApi: d.hostApi, inputs: d.inputs, outputs: d.outputs })));
  assert.ok(gi.some(i => i.name === 'Scarlett 2i2' && i.inputs === 2 && i.outputs === 2));
  const fb = await sa.list({ platform: 'darwin', exists: () => true, helper: '/x/AudioDevices', run: async c => (c === '/x/AudioDevices' ? 'garbage' : JSON.stringify({ SPAudioDataType: [{ _items: [{ _name: 'Mic', coreaudio_device_input: 1 }] }] })) });
  assert.strictEqual(fb.devices[0].name, 'Mic');                                                              // broken helper: system_profiler
  assert.strictEqual(sa.fromHelper('{"ok":false}'), null);
  const src = fsx.readFileSync(pathx.join(__dirname, '..', 'platforms', 'macos', 'native', 'AudioDevices.swift'), 'utf8');
  assert.ok(src.includes('import CoreAudio') && src.includes('kAudioHardwarePropertyDevices') && src.includes('JSONSerialization'));
});




test('RtApiAsio::probeDeviceInfo: a driver that fails to probe is kept for a moment instead of vanishing, then dropped; diagnostics say why', () => {
  const a = require('./audify'); const fa = fakeAudify();
  let mode = 'ok'; const orig = fa.RtAudio.prototype.getDevices;
  const foc = { id: 0, name: 'Focusrite USB ASIO', inputChannels: 18, outputChannels: 20, sampleRates: [44100, 48000], preferredSampleRate: 48000 };
  const second = { id: 1, name: 'ASIO4ALL v2', inputChannels: 4, outputChannels: 4, sampleRates: [48000], preferredSampleRate: 48000 };
  fa.RtAudio.prototype.getDevices = function () {
    if (this.api !== 6) return orig.call(this);
    if (mode === 'throw') throw new Error('RtApiAsio::probeDeviceInfo: error (-1) initializing driver (Focusrite USB ASIO)');
    if (mode === 'zero') return [{ ...foc, inputChannels: 0, outputChannels: 0, sampleRates: [] }, second];        // the driver is listed but cannot be probed
    return [foc, second];
  };
  const names = r => r.devices.filter(d => d.api === 'WINDOWS_ASIO').map(d => d.name + '#' + d.id);
  const t0 = Date.now(); const real = Date.now; Date.now = () => t0;
  try {
    const r1 = a.listDevices(() => fa); const base = names(r1); assert.deepStrictEqual(base, ['Focusrite USB ASIO#1000', 'ASIO4ALL v2#1001'].map(x => x)); assert.strictEqual((r1.problems || []).length, 0);
    mode = 'zero'; Date.now = () => t0 + 5000;
    const r2 = a.listDevices(() => fa); assert.deepStrictEqual(names(r2), base);                                   // busy for a moment: same devices, same ids
    assert.deepStrictEqual(r2.problems[0].unprobed, ['Focusrite USB ASIO']); assert.deepStrictEqual(r2.problems[0].kept, ['Focusrite USB ASIO']);
    assert.match(a.describe(() => fa).problems[0].hint, /probeDeviceInfo.*open in another program/);
    mode = 'throw'; Date.now = () => t0 + 8000;
    const r3 = a.listDevices(() => fa); assert.deepStrictEqual(names(r3), base);                                   // the whole probe threw: last list kept
    assert.match(r3.problems[0].message, /probeDeviceInfo/);
    Date.now = () => t0 + 30000;                                                                                   // still failing after the grace time: really gone
    const r4 = a.listDevices(() => fa); assert.deepStrictEqual(names(r4), []);
    mode = 'ok'; Date.now = () => t0 + 31000; assert.deepStrictEqual(names(a.listDevices(() => fa)).map(n => n.split('#')[0]), ['Focusrite USB ASIO', 'ASIO4ALL v2']);   // plugged in again
  } finally { Date.now = real; }
  const asio = {}; const m = a.mergeAsio(asio, [{ name: 'X', inputChannels: 0, outputChannels: 0 }], 1);
  assert.deepStrictEqual(m.list, []); assert.deepStrictEqual(m.unprobed, ['X']);                                    // never probed OK: not listed (the registry lists it as "driver only")
});


test('ASIO partial probe: a driver read with channels on one side only (no inputs) is re-probed and never loses a direction, so a duplex interface reaches the mixer with its inputs', () => {
  const a = require('./audify');
  const foc = { id: 0, name: 'Focusrite USB ASIO', sampleRates: [48000], preferredSampleRate: 48000 };
  const asioDev = r => r.devices.find(d => d.api === 'WINDOWS_ASIO');
  const mk = script => {               // script: array of getDevices() results for the ASIO api, consumed one per probe
    const fa = fakeAudify(); const orig = fa.RtAudio.prototype.getDevices; let n = 0;
    fa.RtAudio.prototype.getDevices = function () { if (this.api !== 6) return orig.call(this); const s = script[Math.min(n, script.length - 1)]; n++; return s; };
    return { fa, probes: () => n };
  };

  // 1. first probe reads outputs but NO inputs (partial); the retry reads the full duplex shape -> the interface is listed WITH its 18 inputs
  { const { fa, probes } = mk([[{ ...foc, inputChannels: 0, outputChannels: 20 }], [{ ...foc, inputChannels: 18, outputChannels: 20 }]]);
    const d = asioDev(a.listDevices(() => fa));
    assert.strictEqual(d.maxInputChannels, 18, 'the inputs the retry found are adopted'); assert.strictEqual(d.maxOutputChannels, 20);
    assert.strictEqual(probes(), 2, 'a one-sided ASIO reading triggers exactly one retry'); }

  // 2. a device already seen as full (18/20) must not lose its inputs when a later scan reads it partially (0/20)
  { const { fa } = mk([[{ ...foc, inputChannels: 18, outputChannels: 20 }]]);
    assert.strictEqual(asioDev(a.listDevices(() => fa)).maxInputChannels, 18);                  // scan 1: full
    fa.RtAudio.prototype.getDevices = function () { return this.api === 6 ? [{ ...foc, inputChannels: 0, outputChannels: 20 }] : []; };   // later scans only ever read outputs
    assert.strictEqual(asioDev(a.listDevices(() => fa)).maxInputChannels, 18, 'the known inputs are kept through a partial probe');
    assert.strictEqual(asioDev(a.listDevices(() => fa)).maxInputChannels, 18); }

  // 3. a genuinely one-sided driver (no inputs on either probe) is confirmed once, then NOT reloaded on every later scan
  { const { fa, probes } = mk([[{ ...foc, inputChannels: 0, outputChannels: 20 }]]);            // every probe returns output-only
    assert.strictEqual(asioDev(a.listDevices(() => fa)).maxOutputChannels, 20); assert.strictEqual(probes(), 2, 'retried once to be sure');
    const before = probes(); a.listDevices(() => fa); assert.strictEqual(probes() - before, 1, 'confirmed one-sided: no second probe on later scans'); }

  // 4. mergeDev-through-mergeAsio never downgrades, in either direction
  { const asio = {}; a.mergeAsio(asio, [{ name: 'X', inputChannels: 4, outputChannels: 6 }], 1);
    const m = a.mergeAsio(asio, [{ name: 'X', inputChannels: 0, outputChannels: 6 }], 2);
    assert.strictEqual(m.list[0].inputChannels, 4, 'a later input-less read does not erase the remembered inputs'); assert.strictEqual(m.list[0].outputChannels, 6); }
});

test('a device the engine could not probe (0 channels, not ASIO) is flagged, takes its mode from the OS endpoint list, and a stream is still tried', () => {
  const a = require('./audify'), { groupInterfaces } = require('./interfaces'); const fa = fakeAudify();
  const orig = fa.RtAudio.prototype.getDevices; let calls = 0;
  fa.RtAudio.prototype.getDevices = function () {
    const l = orig.call(this);
    if (this.api === 7 || this.api === 2 || /WASAPI/i.test(String(this.api))) calls++;
    return l.map(d => /Speakers/.test(d.name) ? { ...d, inputChannels: 0, outputChannels: 0, sampleRates: [] } : d);   // the same device never probes
  };
  const r = a.detectAudify(() => fa);
  const bad = r.devices.filter(d => d.probeFailed);
  // whichever fake devices exist: every flagged device really has no channels, and every unflagged one has some
  assert.ok(r.devices.every(d => !!d.probeFailed === !(d.inputs || d.outputs) || /asio/i.test(d.hostApi)));
  // the OS endpoint list knows the device by name with its direction: the engine device takes that mode
  const dev = { id: 1000, name: 'Speakers (USB DAC)', hostApi: 'Windows WASAPI', inputs: 0, outputs: 0, probeFailed: true };
  const os = { id: -1, name: 'Speakers (USB DAC)', hostApi: 'Windows WASAPI', native: true, inputs: 0, outputs: 2 };
  const g = groupInterfaces([dev, os]);
  assert.strictEqual(g.length, 1); assert.ok(g[0].outputs === 2 && g[0].write && !g[0].read);
  assert.ok(g[0].apis.some(x => x.deviceId === 1000 && x.outputs === 2));                              // the engine device (openable id) is offered for WRITE
  // no OS entry to learn from: stays unknown (no guess)
  assert.strictEqual(groupInterfaces([dev])[0].outputs, 0);
  // a stream is tried on such a device (stereo) instead of "has no output channels"
  const m = fakeAudify(); const d2 = { id: 1000, rtId: 0, api: 'WINDOWS_WASAPI', hostAPIName: 'Windows WASAPI', name: 'Speakers (USB DAC)', maxInputChannels: 0, maxOutputChannels: 0, defaultSampleRate: 48000, sampleRates: [], probeFailed: true };
  const st = a.openStream({ mod: m, dev: d2, direction: 'output', channels: 2, sampleRate: 48000 });
  assert.ok(st && st.channels === 2); st.close();
  assert.throws(() => a.openStream({ mod: m, dev: { ...d2, probeFailed: false }, direction: 'output', channels: 2, sampleRate: 48000 }), /no output channels/);   // a real 0 stays an error
  assert.ok(Array.isArray(bad));
});










test('OTA server: signed manifests only, uploads are checksum-verified, the real update client checks and downloads from it, Range / HEAD / limits / auth', async () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), http = require('node:http'), crypto = require('node:crypto');
  const { createOta } = require('../ota-server/server'), update = require('./update'), lic = require('../scripts/license');
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ota-')), keys = path.join(tmp, 'vendor');
  lic.initKeys(keys); const jwk = lic.loadPublic(keys), priv = lic.loadPrivate(keys);
  const logs = [], ota = createOta({ dataDir: path.join(tmp, 'data'), token: 'secret-token', jwk, rate: 1000, allowHttp: true, publicUrl: 'https://ota.test', log: o => logs.push(o) });
  await ota.reindex();
  const srv = http.createServer(ota.handler); await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const base = 'http://127.0.0.1:' + srv.address().port, auth = { Authorization: 'Bearer secret-token' };
  const sha = b => crypto.createHash('sha256').update(b).digest('hex');
  const exe = Buffer.from('MZ' + 'x'.repeat(5000)), exe2 = Buffer.from('MZ' + 'y'.repeat(7000));
  const put = (n, body, h = {}) => fetch(base + '/admin/files/' + encodeURIComponent(n) + (h.q || ''), { method: 'PUT', headers: { ...auth, 'X-SHA256': sha(body), ...h.headers }, body });
  const man = (version, files, extra = {}) => ({ product: 'audio-mixer', version, channel: 'stable', released: '2026-10-10', notes: ['a'], files: Object.fromEntries(Object.entries(files).map(([k, [name, buf]]) => [k, { name, url: 'https://ota.test/releases/' + encodeURIComponent(name), size: buf.length, sha256: sha(buf) }])), ...extra });
  const publish = (m, q = '', key = priv) => fetch(base + '/admin/manifest/stable' + q, { method: 'PUT', headers: auth, body: JSON.stringify(update.signManifest(m, key)) });
  let srv3 = null;
  try {
    // admin: closed without the token, nothing published yet
    assert.strictEqual((await fetch(base + '/admin/files')).status, 401);
    assert.strictEqual((await fetch(base + '/admin/files', { headers: { Authorization: 'Bearer nope' } })).status, 401);
    assert.strictEqual((await fetch(base + '/update.json')).status, 404);
    assert.deepStrictEqual((await (await fetch(base + '/healthz')).json()).channels, {});
    // uploads: the checksum is required and checked, files are not replaced silently
    assert.strictEqual((await fetch(base + '/admin/files/a.exe', { method: 'PUT', headers: auth, body: exe })).status, 400);
    const bad = await put('Audio Mixer-9.9.9.exe', exe, { headers: { 'X-SHA256': sha(exe2) } }); assert.strictEqual(bad.status, 400); assert.match((await bad.json()).error, /checksum mismatch/);
    assert.ok(!fs.existsSync(path.join(ota.dirs.releases, 'Audio Mixer-9.9.9.exe')) && !fs.existsSync(path.join(ota.dirs.releases, 'Audio Mixer-9.9.9.exe.part')));
    assert.strictEqual((await put('../evil.exe', exe)).status, 400);
    assert.strictEqual((await put('Audio Mixer-9.9.9.exe', exe)).status, 201);
    assert.strictEqual((await (await put('Audio Mixer-9.9.9.exe', exe)).json()).unchanged, true);                       // same file again: fine
    assert.strictEqual((await put('Audio Mixer-9.9.9.exe', exe2)).status, 409);                                          // other content under a published name: refused
    // manifests: unsigned, foreign key, missing file, changed file, wrong host, http url
    const m1 = man('9.9.9', { 'win-x64-exe': ['Audio Mixer-9.9.9.exe', exe] });
    const other = lic.initKeys(path.join(tmp, 'other')) && lic.loadPrivate(path.join(tmp, 'other'));
    assert.strictEqual((await publish(m1, '', other)).status, 400);
    assert.strictEqual((await fetch(base + '/admin/manifest/stable', { method: 'PUT', headers: auth, body: JSON.stringify({ payload: JSON.stringify(m1), signature: 'AAAA' }) })).status, 400);
    const missing = await publish(man('9.9.9', { 'win-x64-exe': ['Audio Mixer-9.9.9.exe', exe], 'linux-deb': ['audio-mixer_9.9.9_all.deb', exe2] })); assert.strictEqual(missing.status, 400); assert.match(JSON.stringify(await missing.json()), /has not been uploaded/);
    const changed = man('9.9.9', { 'win-x64-exe': ['Audio Mixer-9.9.9.exe', exe] }); changed.files['win-x64-exe'].sha256 = sha(exe2); assert.match(JSON.stringify(await (await publish(changed)).json()), /differs from the uploaded file/);
    const wrongHost = man('9.9.9', { 'win-x64-exe': ['Audio Mixer-9.9.9.exe', exe] }); wrongHost.files['win-x64-exe'].url = 'https://evil.example/releases/x.exe'; assert.match(JSON.stringify(await (await publish(wrongHost)).json()), /not under https:\/\/ota\.test/);
    // publish, then serve exactly what was published
    const ok = await publish(m1); assert.strictEqual(ok.status, 200); assert.strictEqual((await ok.json()).version, '9.9.9');
    const served = await fetch(base + '/update.json'), env = await served.json();
    assert.ok(update.verifyManifest(env, jwk).ok && served.headers.get('etag'));
    assert.strictEqual((await fetch(base + '/update.json', { headers: { 'If-None-Match': served.headers.get('etag') } })).status, 304);
    assert.strictEqual((await publish(m1)).status, 200);                                                               // the same manifest again: unchanged
    const old = await publish(man('9.9.8', { 'win-x64-exe': ['Audio Mixer-9.9.9.exe', exe] })); assert.strictEqual(old.status, 409);   // no downgrade
    assert.strictEqual((await (await fetch(base + '/healthz')).json()).channels.stable, '9.9.9');
    // the real update client, against this server (https name routed to the local port)
    process.env.BRIDGE_UPDATE_HOSTS = 'ota.test';
    const route = async (u, o) => { const r = await fetch(String(u).replace('https://ota.test', base), o); return { ok: r.ok, status: r.status, headers: r.headers, body: r.body, url: String(u), redirected: false, json: () => r.json(), text: () => r.text(), arrayBuffer: () => r.arrayBuffer() }; };
    const info = await update.check({ current: '1.0.0', fetchImpl: route, url: 'https://ota.test/update.json', platform: 'win32', arch: 'x64', jwk });
    assert.ok(info.updateAvailable && info.latest === '9.9.9' && info.signed && info.file.name === 'Audio Mixer-9.9.9.exe' && info.file.sha256 === sha(exe));
    const dl = await update.download({ current: '1.0.0', fetchImpl: route, url: 'https://ota.test/update.json', dir: path.join(tmp, 'dl'), platform: 'win32', arch: 'x64', jwk });
    assert.ok(dl.verified && fs.readFileSync(dl.file).equals(exe));
    assert.rejects(update.check({ current: '1.0.0', fetchImpl: (u, o) => route(u, o).then(r => { throw new Error('no network'); }), url: 'https://ota.test/update.json' }));
    // files: HEAD, Range, resume, unknown, traversal
    const head = await fetch(base + '/releases/' + encodeURIComponent('Audio Mixer-9.9.9.exe'), { method: 'HEAD' }); assert.strictEqual(head.headers.get('content-length'), String(exe.length)); assert.strictEqual(head.headers.get('accept-ranges'), 'bytes');
    const part = await fetch(base + '/releases/' + encodeURIComponent('Audio Mixer-9.9.9.exe'), { headers: { Range: 'bytes=10-19' } }); assert.strictEqual(part.status, 206); assert.strictEqual(part.headers.get('content-range'), `bytes 10-19/${exe.length}`); assert.ok(Buffer.from(await part.arrayBuffer()).equals(exe.subarray(10, 20)));
    const tail = await fetch(base + '/releases/' + encodeURIComponent('Audio Mixer-9.9.9.exe'), { headers: { Range: 'bytes=-5' } }); assert.ok(Buffer.from(await tail.arrayBuffer()).equals(exe.subarray(exe.length - 5)));
    assert.strictEqual((await fetch(base + '/releases/' + encodeURIComponent('Audio Mixer-9.9.9.exe'), { headers: { Range: 'bytes=99999-' } })).status, 416);
    assert.strictEqual((await fetch(base + '/releases/nothing.exe')).status, 404);
    for (const p of ['/releases/..%2F..%2Fetc%2Fpasswd', '/releases/%2e%2e%2f', '/releases/a/b']) assert.ok([400, 404].includes((await fetch(base + p)).status), p);
    assert.strictEqual(new URL('/releases/%2e%2e', 'http://x').pathname, '/', 'the URL parser itself collapses this one to the site root, before any route matches');
    assert.strictEqual((await fetch(base + '/update.json', { method: 'POST' })).status, 405);
    // a file a manifest lists cannot be deleted; an unlisted one can; stats count the downloads
    assert.strictEqual((await fetch(base + '/admin/files/' + encodeURIComponent('Audio Mixer-9.9.9.exe'), { method: 'DELETE', headers: auth })).status, 409);
    assert.strictEqual((await put('spare.msi', exe2)).status, 201); assert.strictEqual((await fetch(base + '/admin/files/spare.msi', { method: 'DELETE', headers: auth })).status, 200);
    const files = (await (await fetch(base + '/admin/files', { headers: auth })).json()).files; assert.deepStrictEqual(files.map(f => [f.name, f.referencedBy]), [['Audio Mixer-9.9.9.exe', ['stable']]]);
    const st = await (await fetch(base + '/admin/stats', { headers: auth })).json(), d = Object.values(st.days)[0]; assert.ok(d.manifest.stable >= 3 && d.download['Audio Mixer-9.9.9.exe'] === 1);
    // a restart keeps everything (index, manifests), files added by hand are noticed
    fs.writeFileSync(path.join(ota.dirs.releases, 'by-hand.msi'), exe2); await ota.reindex(); assert.ok(ota.index()['by-hand.msi'] && ota.index()['by-hand.msi'].sha256 === sha(exe2));
    // channels are separate
    assert.strictEqual((await fetch(base + '/beta/update.json')).status, 404);
    const beta = await fetch(base + '/admin/manifest/beta', { method: 'PUT', headers: auth, body: JSON.stringify(update.signManifest(man('10.0.0', { 'win-x64-exe': ['Audio Mixer-9.9.9.exe', exe] }, { channel: 'beta' }), priv)) }); assert.strictEqual(beta.status, 200);
    assert.strictEqual((await (await fetch(base + '/beta/update.json')).json()).payload.includes('10.0.0'), true);
    // the publish tool: signs here, uploads what the server lacks, publishes (plain http only for localhost)
    // (a server without OTA_PUBLIC_URL, because the tool puts the server address into the file URLs)
    const ota3 = createOta({ dataDir: path.join(tmp, 'd3'), token: 'secret-token', jwk, allowHttp: true }), s3 = http.createServer(ota3.handler); await new Promise(r => s3.listen(0, '127.0.0.1', r)); await ota3.reindex();
    const base3 = 'http://127.0.0.1:' + s3.address().port; srv3 = s3;
    const rel = path.join(tmp, 'rel'), ver = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'package.json'), 'utf8')).version;
    fs.mkdirSync(rel); fs.writeFileSync(path.join(rel, `AudioMixer-${ver}-x64.msi`), Buffer.from('msi-' + 'z'.repeat(3000)));
    const out = await new Promise((res, rej) => require('node:child_process').execFile('node', [path.join(__dirname, '..', 'scripts', 'ota.js'), 'publish', '--server', base3, '--token', 'secret-token', '--dir', keys, '--releases', rel, '--channel', 'beta', '--notes', 'x|y'], { env: { ...process.env, OTA_ALLOW_HTTP: '' } }, (e, so, se) => (e ? rej(new Error(se + so)) : res(so))));
    assert.match(out, /upload AudioMixer-.*-x64\.msi/); assert.match(out, new RegExp('published beta ' + ver.replace(/\./g, '\\.')));
    assert.ok(update.verifyManifest(await (await fetch(base3 + '/beta/update.json')).json(), jwk).manifest.version === ver);
    await assert.rejects(new Promise((res, rej) => require('node:child_process').execFile('node', [path.join(__dirname, '..', 'scripts', 'ota.js'), 'status', '--server', 'http://ota.example.com', '--token', 'x'], (e, so, se) => (e ? rej(new Error(se)) : res(so)))), /must be https/);
    assert.ok(logs.some(l => l.event === 'publish'));
  } finally { srv.closeAllConnections(); srv.close(); if (srv3) { srv3.closeAllConnections(); srv3.close(); } delete process.env.BRIDGE_UPDATE_HOSTS; }
  // admin routes do not exist without a token; the rate limit answers 429
  const ota2 = createOta({ dataDir: path.join(tmp, 'd2'), jwk, rate: 3 }), s2 = http.createServer(ota2.handler); await new Promise(r => s2.listen(0, '127.0.0.1', r));
  try { const b2 = 'http://127.0.0.1:' + s2.address().port; assert.strictEqual((await fetch(b2 + '/admin/files')).status, 404); const codes = []; for (let i = 0; i < 5; i++) codes.push((await fetch(b2 + '/update.json')).status); assert.deepStrictEqual(codes, [404, 404, 404, 429, 429]); }
  finally { s2.closeAllConnections(); s2.close(); }
});

test('OTA install: the downloaded Windows setup .exe is run silently (/quiet) and detached; every other system (or one this computer could not detect) is only ever downloaded, never run', async () => {
  const update = require('./update'), lic = require('../scripts/license');
  const tmp = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'ota-install-')), keys = pathx.join(tmp, 'vendor');
  lic.initKeys(keys); const jwk = lic.loadPublic(keys), priv = lic.loadPrivate(keys);
  const exe = Buffer.from('MZ' + 'x'.repeat(5000)), tgz = Buffer.from('tgz' + 'y'.repeat(3000)), sha = b => cryptox.createHash('sha256').update(b).digest('hex');
  const man = { product: 'audio-mixer', version: '9.9.9', channel: 'stable', released: '2026-10-10', notes: [], files: {
    'win-x64-exe': { name: 'Audio Mixer-9.9.9.exe', url: 'https://ota.test/releases/Audio%20Mixer-9.9.9.exe', size: exe.length, sha256: sha(exe) },
    'macos': { name: 'AudioMixerServer-9.9.9-macos.tar.gz', url: 'https://ota.test/releases/AudioMixerServer-9.9.9-macos.tar.gz', size: tgz.length, sha256: sha(tgz) },
  } };
  const env = update.signManifest(man, priv);
  const route = async u => { if (String(u).endsWith('update.json')) return { ok: true, status: 200, json: async () => env }; if (String(u).includes('Audio%20Mixer-9.9.9.exe')) return { ok: true, status: 200, url: String(u), headers: new Map([['content-length', String(exe.length)]]), body: [exe] }; if (String(u).includes('AudioMixerServer-9.9.9-macos.tar.gz')) return { ok: true, status: 200, url: String(u), headers: new Map([['content-length', String(tgz.length)]]), body: [tgz] }; return { ok: false, status: 404 }; };
  const calls = [];
  const fakeSpawn = (file, args, opts) => { calls.push({ file, args, opts }); return { unref() { calls.push('unrefed'); } }; };
  process.env.BRIDGE_UPDATE_HOSTS = 'ota.test';
  try {
    // Windows, a real .exe: runs it, detached, with /quiet - and the install folder / icon / drivers+bridge+scripts in Program Files are untouched
    // by anything here, because this just launches the signed setup .exe, and its own MSI major upgrade (same UpgradeCode every release uses, see
    // the "installer:" tests above) is what keeps them - install() does not stage, move or register a single file of its own.
    const r1 = await update.install({ current: '1.0.0', fetchImpl: route, url: 'https://ota.test/update.json', dir: pathx.join(tmp, 'dl1'), platform: 'win32', arch: 'x64', jwk, spawnImpl: fakeSpawn });
    assert.ok(r1.ok && r1.installing === true && r1.verified && /Installing v9\.9\.9 now/.test(r1.note));
    assert.strictEqual(calls.length, 2); assert.strictEqual(calls[0].args[0], '/quiet'); assert.strictEqual(calls[0].file, r1.file);
    assert.deepStrictEqual(calls[0].opts, { detached: true, stdio: 'ignore', windowsHide: true }); assert.strictEqual(calls[1], 'unrefed');
    // macOS / Linux: downloaded and verified exactly like download(), but spawnImpl is never called - there is no silent installer for those systems
    calls.length = 0;
    const r2 = await update.install({ current: '1.0.0', fetchImpl: route, url: 'https://ota.test/update.json', dir: pathx.join(tmp, 'dl2'), platform: 'darwin', arch: 'x64', jwk, spawnImpl: fakeSpawn });
    assert.ok(r2.ok && r2.installing === false && r2.verified && calls.length === 0 && /Run it yourself/.test(r2.note));
    // a system this manifest has no file for (as real as "could not be detected": either way, nothing here may guess and run something anyway)
    await assert.rejects(update.install({ current: '1.0.0', fetchImpl: route, url: 'https://ota.test/update.json', dir: pathx.join(tmp, 'dl3'), platform: 'android', arch: 'arm64', jwk, spawnImpl: fakeSpawn }), /Google Play/);
    assert.strictEqual(calls.length, 0);
  } finally { delete process.env.BRIDGE_UPDATE_HOSTS; }
  // the CLI and the server expose it the same way download already is
  assert.match(fsx.readFileSync(pathx.join(__dirname, '..', 'client', 'cli.js'), 'utf8'), /o\.arg === 'install'.*upd\.install/);
  const srvSrc = fsx.readFileSync(pathx.join(__dirname, '..', 'bridge', 'server.js'), 'utf8');
  assert.match(srvSrc, /'\/api\/update\/install'/); assert.match(srvSrc, /updater\.install\(\{ current: VERSION \}\)/);
});

test('OTA dashboard: served only with the admin token configured, strict CSP, history / roll back / unpublish / verify / config API, browser-side signing is accepted by the server and the app', async () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), http = require('node:http'), crypto = require('node:crypto');
  const { createOta } = require('../ota-server/server'), update = require('./update'), lic = require('../scripts/license');
  const dash = require('../ota-server/dashboard/app.js');
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'dash-')), keys = path.join(tmp, 'vendor'); lic.initKeys(keys); const jwk = lic.loadPublic(keys);
  const ota = createOta({ dataDir: path.join(tmp, 'data'), token: 'tk', jwk, publicUrl: 'https://ota.test' }); await ota.reindex();
  const noTok = createOta({ dataDir: path.join(tmp, 'd0'), jwk });
  const run = async (o, f) => { const s = http.createServer(o.handler); await new Promise(r => s.listen(0, '127.0.0.1', r)); try { await f('http://127.0.0.1:' + s.address().port); } finally { s.closeAllConnections(); s.close(); } };
  await run(noTok, async b => { for (const p of ['/admin/', '/admin', '/admin/app.js', '/admin/config']) assert.strictEqual((await fetch(b + p, { redirect: 'manual' })).status, 404, p); });
  await run(ota, async b => {
    const auth = { Authorization: 'Bearer tk' }, sha = buf => crypto.createHash('sha256').update(buf).digest('hex');
    // static dashboard: public files, nothing from outside, no inline script / style, locked down
    const page = await fetch(b + '/admin/'), html = await page.text(), csp = page.headers.get('content-security-policy');
    assert.strictEqual(page.status, 200); assert.match(csp, /default-src 'none'/); assert.match(csp, /script-src 'self'/); assert.match(csp, /frame-ancestors 'none'/); assert.strictEqual(page.headers.get('x-frame-options'), 'DENY');
    assert.ok(!/<script(?![^>]*\bsrc=)/.test(html) && !/\sstyle=|\son\w+=/.test(html) && !/https?:\/\//.test(html.replace(/<!doctype html>/i, '')));
    assert.strictEqual((await fetch(b + '/admin', { redirect: 'manual' })).status, 301);
    assert.match(await (await fetch(b + '/admin/app.js')).text(), /Audio Mixer OTA server: vendor dashboard/); assert.strictEqual((await fetch(b + '/admin/style.css')).headers.get('content-type'), 'text/css; charset=utf-8');
    assert.strictEqual((await fetch(b + '/admin/app.js', { method: 'POST' })).status, 401);                     // anything but GET / HEAD on /admin/* needs the token
    const src = fs.readFileSync(path.join(__dirname, '..', 'ota-server', 'dashboard', 'app.js'), 'utf8');
    assert.ok(!/innerHTML|outerHTML|insertAdjacentHTML|document\.write|eval\(|new Function|setAttribute\('style'/.test(src), 'server data is only ever written as text');
    assert.strictEqual((await fetch(b + '/admin/config')).status, 401);
    const cfg = await (await fetch(b + '/admin/config', { headers: auth })).json(); assert.deepStrictEqual(cfg.jwk, jwk); assert.strictEqual(cfg.publicUrl, 'https://ota.test'); assert.strictEqual(cfg.maxFile, 400 * 1024 * 1024);
    // the pure helpers of the page: file-name matching, manifest shape, signing with the private key in the "browser" (WebCrypto), verification
    const exe = Buffer.from('MZ-exe-' + 'a'.repeat(500)), msi = Buffer.from('msi-' + 'b'.repeat(600)), old = Buffer.from('MZ-old-' + 'c'.repeat(300));
    const names = { 'Audio Mixer-3.0.0.0.exe': exe, 'AudioMixer-3.0.0.0-x64.msi': msi, 'Audio Mixer-2.9.0.0.exe': old };
    for (const [n, buf] of Object.entries(names)) assert.strictEqual((await fetch(b + '/admin/files/' + encodeURIComponent(n), { method: 'PUT', headers: { ...auth, 'X-SHA256': sha(buf) }, body: buf })).status, 201);
    const files = (await (await fetch(b + '/admin/files', { headers: auth })).json()).files, g = dash.guess(files);
    assert.strictEqual(g.version, '3.0.0.0'); assert.deepStrictEqual(g.map, { 'win-x64-exe': 'Audio Mixer-3.0.0.0.exe', 'win-x64-msi': 'AudioMixer-3.0.0.0-x64.msi' });
    assert.strictEqual(await dash.sha256Hex(exe), sha(exe));
    const mk = (version, map) => dash.buildManifest({ version, channel: 'stable', notes: ['  a  ', '', 'b'], map, files, base: cfg.publicUrl + '/', released: '2026-10-10' });
    const m3 = mk('3.0.0.0', g.map); assert.deepStrictEqual(m3.notes, ['a', 'b']); assert.strictEqual(m3.files['win-x64-exe'].url, 'https://ota.test/releases/Audio%20Mixer-3.0.0.0.exe'); assert.strictEqual(m3.files['win-x64-exe'].sha256, sha(exe));
    assert.deepStrictEqual(Object.keys(require('../scripts/make-update').build({ releases: (() => { const d = fs.mkdtempSync(path.join(tmp, 'r')); fs.writeFileSync(path.join(d, 'Audio Mixer-3.0.0.0.exe'), exe); return d; })(), version: '3.0.0.0', base: 'https://ota.test/releases' })).sort(), Object.keys(m3).sort());   // same manifest shape as the command-line tool
    const key = await dash.importPrivate(fs.readFileSync(path.join(keys, 'private.pem'), 'utf8')), env3 = await dash.signManifest(m3, key);
    assert.ok(update.verifyManifest(env3, jwk).ok, 'the app\'s verifier accepts a signature made in the browser'); assert.ok(await dash.verifyEnvelope(env3, jwk));
    assert.ok(!(await dash.verifyEnvelope({ ...env3, payload: env3.payload.replace('3.0.0.0', '3.0.0.1') }, jwk)));
    await assert.rejects(dash.importPrivate('not a key'), /PKCS#8/);
    assert.ok(update.verifyManifest(update.signManifest(m3, lic.loadPrivate(keys)), jwk).ok && await dash.verifyEnvelope(update.signManifest(m3, lic.loadPrivate(keys)), jwk));   // and the other way round
    const put = (env, ch = 'stable', q = '') => fetch(b + '/admin/manifest/' + ch + q, { method: 'PUT', headers: auth, body: JSON.stringify(env) });
    // publish 2.9.0.0, then 3.0.0.0; history lists both newest first; a roll back publishes the old signed manifest again
    assert.strictEqual((await put(await dash.signManifest(mk('2.9.0.0', { 'win-x64-exe': 'Audio Mixer-2.9.0.0.exe' }), key))).status, 200);
    assert.strictEqual((await put(env3)).status, 200);
    let hist = (await (await fetch(b + '/admin/history/stable', { headers: auth })).json()).versions; assert.deepStrictEqual(hist.map(v => [v.version, v.current, v.notes]), [['3.0.0.0', true, ['a', 'b']], ['2.9.0.0', false, ['a', 'b']]]);
    const oldEnv = await (await fetch(b + '/admin/history/stable/2.9.0.0', { headers: auth })).json(); assert.ok(update.verifyManifest(oldEnv, jwk).ok);
    assert.strictEqual((await put(oldEnv)).status, 409); const back = await put(oldEnv, 'stable', '?force=1'); assert.strictEqual(back.status, 200);
    assert.strictEqual((await (await fetch(b + '/healthz')).json()).channels.stable, '2.9.0.0');
    assert.strictEqual((await fetch(b + '/admin/history/stable/9.9.9', { headers: auth })).status, 404); assert.strictEqual((await fetch(b + '/admin/history/..%2Fx', { headers: auth })).status, 404);
    assert.deepStrictEqual((await (await fetch(b + '/admin/history/beta', { headers: auth })).json()).versions, []);
    // verify: damaged / missing files are reported
    assert.strictEqual((await (await fetch(b + '/admin/verify', { headers: auth })).json()).allGood, true);
    fs.appendFileSync(path.join(ota.dirs.releases, 'Audio Mixer-3.0.0.0.exe'), 'x'); fs.unlinkSync(path.join(ota.dirs.releases, 'AudioMixer-3.0.0.0-x64.msi'));
    const v = await (await fetch(b + '/admin/verify', { headers: auth })).json(); assert.strictEqual(v.allGood, false); assert.deepStrictEqual(v.files.filter(f => !f.ok).map(f => [f.name, f.actual]).sort(), [['Audio Mixer-3.0.0.0.exe', 'size differs'], ['AudioMixer-3.0.0.0-x64.msi', 'missing']]);
    // unpublish: the apps get 404, the history stays, a bad channel is refused
    assert.strictEqual((await fetch(b + '/admin/manifest/stable', { method: 'DELETE', headers: auth })).status, 200); assert.strictEqual((await fetch(b + '/update.json')).status, 404);
    assert.strictEqual((await fetch(b + '/admin/manifest/stable', { method: 'DELETE', headers: auth })).status, 404);
    hist = (await (await fetch(b + '/admin/history/stable', { headers: auth })).json()).versions; assert.strictEqual(hist.length, 2);
    assert.strictEqual((await fetch(b + '/admin/files/%E0%A4%A', { method: 'DELETE', headers: auth })).status, 400);              // broken % escape: 400, not a crash
  });
});

test('OTA public download page: one file, served without a token, a CSP that allow-lists its own inline script / style by hash instead of weakening to unsafe-inline, the DOWNLOAD link works without JavaScript, the inline script fills in what /api/latest knows', async () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), http = require('node:http'), crypto = require('node:crypto');
  const { createOta } = require('../ota-server/server'), update = require('./update'), lic = require('../scripts/license'), dash = require('../ota-server/dashboard/app.js');
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'pub-')), keys = path.join(tmp, 'vendor'); lic.initKeys(keys); const jwk = lic.loadPublic(keys);
  // no OTA_ADMIN_TOKEN at all: the admin dashboard is off, the public page must still work
  const ota = createOta({ dataDir: path.join(tmp, 'data'), jwk, publicUrl: 'https://ota.test' }); await ota.reindex();
  const run = async f => { const s = http.createServer(ota.handler); await new Promise(r => s.listen(0, '127.0.0.1', r)); try { await f('http://127.0.0.1:' + s.address().port); } finally { s.closeAllConnections(); s.close(); } };
  await run(async b => {
    // the page: one file, public, nothing from outside, no http(s):// reference, same lockdown family as the admin dashboard
    const page = await fetch(b + '/'), html = await page.text(), csp = page.headers.get('content-security-policy');
    assert.strictEqual(page.status, 200); assert.match(csp, /default-src 'none'/); assert.match(csp, /frame-ancestors 'none'/); assert.strictEqual(page.headers.get('x-frame-options'), 'DENY');
    assert.ok(!/\sstyle=|\son\w+=/.test(html) && !/https?:\/\//.test(html.replace(/<!doctype html>/i, '')));
    assert.match(html, /id="dl-primary" href="\/latest"/);                                          // the DOWNLOAD link works even with JavaScript disabled
    assert.ok(html.includes('/api/latest') && html.includes('<style>') && html.includes('<script>'), 'the script and the style are inline (one file), not loaded from outside');
    assert.strictEqual((await fetch(b + '/', { method: 'HEAD' })).status, 200);
    // no 'unsafe-inline': the CSP allow-lists the exact inline <script> and <style> text by its own SHA-256 hash, computed from the file itself
    const scriptText = /<script>([\s\S]*?)<\/script>/.exec(html)[1], styleText = /<style>([\s\S]*?)<\/style>/.exec(html)[1];
    const sriHash = t => "'sha256-" + crypto.createHash('sha256').update(t, 'utf8').digest('base64') + "'";
    assert.ok(!/unsafe-inline/.test(csp)); assert.match(csp, new RegExp('script-src ' + sriHash(scriptText).replace(/[+/]/g, '\\$&'))); assert.match(csp, new RegExp('style-src ' + sriHash(styleText).replace(/[+/]/g, '\\$&')));
    assert.ok(!/innerHTML|outerHTML|insertAdjacentHTML|document\.write|eval\(|new Function|setAttribute\('style'/.test(scriptText), 'server data is only ever written as text');
    // nothing published yet: /api/latest 404s, the page says so and hides the download button (checked at the JSON level: the page itself needs a browser)
    const none = await (await fetch(b + '/api/latest')).json(); assert.strictEqual(none.ok, false);
    // publish a release, then the data the page's app.js reads matches what it expects (file, others, driver, notes)
    const exe = Buffer.from('MZ-pub-' + 'a'.repeat(500)), deb = Buffer.from('deb-pub-' + 'b'.repeat(500));
    for (const [n, buf] of Object.entries({ 'Audio Mixer-5.0.0.0.exe': exe, 'audio-mixer_5.0.0.0_all.deb': deb })) {
      assert.strictEqual((await fetch(b + '/admin/files/' + encodeURIComponent(n), { method: 'PUT', headers: { 'X-SHA256': crypto.createHash('sha256').update(buf).digest('hex') }, body: buf })).status, 404, 'no admin token at all: 404, not 401');
    }
    fs.writeFileSync(path.join(ota.dirs.releases, 'Audio Mixer-5.0.0.0.exe'), exe); fs.writeFileSync(path.join(ota.dirs.releases, 'audio-mixer_5.0.0.0_all.deb'), deb); await ota.reindex();
    const files = Object.entries(ota.index()).map(([name, f]) => ({ name, size: f.size, sha256: f.sha256 }));
    const m = dash.buildManifest({ version: '5.0.0.0', channel: 'stable', notes: ['Smooth faders', 'OTA updates'], map: { 'win-x64-exe': 'Audio Mixer-5.0.0.0.exe', 'linux-deb': 'audio-mixer_5.0.0.0_all.deb' }, files, base: 'https://ota.test/releases/', released: '2026-10-12' });
    const env = update.signManifest(m, lic.loadPrivate(keys));
    fs.writeFileSync(path.join(ota.dirs.manifests, 'stable.json'), JSON.stringify(env));
    const j = await (await fetch(b + '/api/latest', { headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)' } })).json();
    assert.ok(j.ok && j.version === '5.0.0.0' && j.released === '2026-10-12' && j.notes.includes('Smooth faders'));
    assert.strictEqual(j.file.key, 'win-x64-exe'); assert.ok(j.others.some(o => o.key === 'linux-deb'));
  });
});

test('mail: minimal SMTP client (bridge/mail.js) speaks EHLO/STARTTLS/AUTH LOGIN/MAIL FROM/RCPT TO/DATA against a real socket, dot-stuffs the body, and never throws when unconfigured', async () => {
  const net = require('node:net'), tls = require('node:tls'), crypto = require('node:crypto'), fs = require('node:fs'), os = require('node:os'), path = require('node:path'), cp = require('node:child_process');
  const mail = require('./mail');
  assert.strictEqual(mail.configured({}), false); assert.strictEqual(mail.configured({ SMTP_HOST: 'x' }), true);
  assert.deepStrictEqual(await mail.send({ to: 'a@example.com', subject: 's', text: 't', env: {} }), { sent: false, reason: 'not configured' });
  assert.ok(mail.EMAIL_RE.test('a@b.co')); assert.ok(!mail.EMAIL_RE.test('not-an-email'));
  assert.strictEqual(mail.dotStuff('.leading\nnormal\n..two dots'), '..leading\r\nnormal\r\n...two dots');
  // a fake STARTTLS-capable SMTP server (a real socket, a real self-signed certificate, a real TLS upgrade)
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'tlscert-')), key = path.join(dir, 'k.pem'), cert = path.join(dir, 'c.pem');
  cp.execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-keyout', key, '-out', cert, '-days', '1', '-nodes', '-subj', '/CN=localhost']);
  let received = null;
  const smtp = net.createServer(sock => {
    let buf = '', inData = false, dataBuf = '', authStage = 0;
    sock.write('220 fake.smtp greeting\r\n');
    const handleLine = (line, s) => {
      if (inData) { if (line === '.') { inData = false; received = dataBuf; s.write('250 OK\r\n'); dataBuf = ''; } else dataBuf += (line.startsWith('..') ? line.slice(1) : line) + '\r\n'; return; }
      if (authStage === 1) { authStage = 2; s.write('334 UGFzc3dvcmQ6\r\n'); return; }
      if (authStage === 2) { authStage = 0; s.write('235 Authenticated\r\n'); return; }
      if (/^EHLO/i.test(line)) return s.write('250-fake.smtp\r\n' + (!s.encrypted ? '250-STARTTLS\r\n' : '') + '250 AUTH LOGIN\r\n');
      if (/^STARTTLS/i.test(line)) return s.write('220 go ahead\r\n');
      if (/^AUTH LOGIN/i.test(line)) { authStage = 1; return s.write('334 VXNlcm5hbWU6\r\n'); }
      if (/^MAIL FROM/i.test(line) || /^RCPT TO/i.test(line)) return s.write('250 OK\r\n');
      if (/^DATA/i.test(line)) { inData = true; dataBuf = ''; return s.write('354 go ahead\r\n'); }
      if (/^QUIT/i.test(line)) { s.write('221 bye\r\n'); return s.end(); }
      s.write('500 unrecognized\r\n');
    };
    const onData = chunk => { buf += chunk.toString('latin1'); let i; while ((i = buf.indexOf('\r\n')) >= 0) { const line = buf.slice(0, i); buf = buf.slice(i + 2); handleLine(line, sock); } };
    sock.on('data', onData); sock.on('error', () => {});
    sock._upgradeToTls = () => { sock.removeListener('data', onData); const s2 = new tls.TLSSocket(sock, { isServer: true, key: fs.readFileSync(key), cert: fs.readFileSync(cert) }); s2.encrypted = true; s2.on('data', chunk => { buf += chunk.toString('latin1'); let i; while ((i = buf.indexOf('\r\n')) >= 0) { const line = buf.slice(0, i); buf = buf.slice(i + 2); handleLine(line, s2); } }); s2.on('error', () => {}); };
  });
  smtp.on('connection', sock => { const w = sock.write.bind(sock); sock.write = (d, ...r) => { const ok = w(d, ...r); if (String(d).startsWith('220 go ahead')) setImmediate(() => sock._upgradeToTls()); return ok; }; });
  await new Promise(r => smtp.listen(0, '127.0.0.1', r));
  const origReject = process.env.NODE_TLS_REJECT_UNAUTHORIZED; process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0';
  try {
    const r = await mail.send({ host: '127.0.0.1', port: smtp.address().port, user: 'bob', pass: 'secret', from: 'vendor@audio-mixer.test', to: 'client@example.com', subject: 'Your license request', text: 'Hi Jane,\n.Starts with a dot.\n' });
    assert.deepStrictEqual(r, { sent: true });
    assert.match(received, /^From: vendor@audio-mixer\.test$/m); assert.match(received, /^To: client@example\.com$/m); assert.match(received, /^Subject: Your license request$/m);
    assert.ok(received.includes('\r\n.Starts with a dot.'), 'dot-stuffing was correctly undone by the (fake) server, proving the client stuffed it going out');
    await assert.rejects(mail.send({ host: '127.0.0.1', port: smtp.address().port, from: 'v@x.test', to: 'not-an-email', subject: 's', text: 't' }), /not an e-mail address/);
  } finally { delete process.env.NODE_TLS_REJECT_UNAUTHORIZED; if (origReject !== undefined) process.env.NODE_TLS_REJECT_UNAUTHORIZED = origReject; smtp.close(); }
});

test('OTA license key requests: the app\'s LICENSE page can POST its request file here instead of only downloading / copying it, stored for the vendor, e-mailed to the requester when SMTP_* is set, rate limited, never reachable without the right method or a valid plan', async () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), http = require('node:http'), net = require('node:net');
  const { createOta } = require('../ota-server/server'), lic = require('./license');
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lic-req-'));
  let received = null;
  const smtp = net.createServer(sock => {
    let buf = '', inData = false, dataBuf = '';
    sock.write('220 fake\r\n');
    sock.on('data', chunk => {
      buf += chunk.toString('latin1'); let i;
      while ((i = buf.indexOf('\r\n')) >= 0) {
        const line = buf.slice(0, i); buf = buf.slice(i + 2);
        if (inData) { if (line === '.') { inData = false; received = dataBuf; sock.write('250 OK\r\n'); } else dataBuf += (line.startsWith('..') ? line.slice(1) : line) + '\r\n'; continue; }
        if (/^EHLO/i.test(line)) sock.write('250-fake\r\n250 OK\r\n');
        else if (/^MAIL FROM/i.test(line) || /^RCPT TO/i.test(line)) sock.write('250 OK\r\n');
        else if (/^DATA/i.test(line)) { inData = true; dataBuf = ''; sock.write('354 go\r\n'); }
        else if (/^QUIT/i.test(line)) { sock.write('221 bye\r\n'); sock.end(); }
        else sock.write('500 ?\r\n');
      }
    });
    sock.on('error', () => {});
  });
  await new Promise(r => smtp.listen(0, '127.0.0.1', r));
  const savedEnv = { SMTP_HOST: process.env.SMTP_HOST, SMTP_PORT: process.env.SMTP_PORT, SMTP_FROM: process.env.SMTP_FROM };
  process.env.SMTP_HOST = '127.0.0.1'; process.env.SMTP_PORT = String(smtp.address().port); process.env.SMTP_FROM = 'vendor@audio-mixer.test';
  try {
    const ota = createOta({ dataDir: path.join(tmp, 'data'), token: 'lic-admin-token', rate: 1000 }); await ota.reindex();
    const server = http.createServer(ota.handler); await new Promise(r => server.listen(0, '127.0.0.1', r));
    const base = 'http://127.0.0.1:' + server.address().port;
    try {
      const req = { app: 'audio-mixer', kind: 'license-request', v: 1, machineId: 'AAAA-BBBB-CCCC', plan: 'pro', name: 'Jane Doe', email: 'jane@example.com', platform: 'linux', arch: 'x64', version: '1.12.2.0', at: Date.now() };
      // CORS preflight: the app's own page (a different origin than the OTA server) posts here
      const opt = await fetch(base + '/request-license', { method: 'OPTIONS' });
      assert.strictEqual(opt.status, 204); assert.match(opt.headers.get('access-control-allow-methods'), /POST/);
      // a valid request with an e-mail: stored, and a real receipt e-mail sent through the fake SMTP server above
      const r1 = await fetch(base + '/request-license', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(req) });
      const j1 = await r1.json(); assert.strictEqual(r1.status, 200); assert.deepStrictEqual(j1, { ok: true, received: true, emailed: true });
      assert.match(received, /^Subject: Audio Mixer: license key request received$/m); assert.match(received, /PRO license key \(machine AAAA-BBBB-CCCC\)/); assert.match(received, /^To: jane@example\.com$/m);
      // a request with no e-mail: stored, never e-mailed, never throws for lack of an address
      const r2 = await fetch(base + '/request-license', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ plan: 'studio', machineId: 'ZZZZ' }) });
      assert.deepStrictEqual(await r2.json(), { ok: true, received: true, emailed: false });
      // bad plan, bad e-mail, bad JSON, wrong method
      assert.strictEqual((await fetch(base + '/request-license', { method: 'POST', body: JSON.stringify({ plan: 'nope' }) })).status, 400);
      assert.strictEqual((await fetch(base + '/request-license', { method: 'POST', body: JSON.stringify({ plan: 'pro', email: 'not-an-email' }) })).status, 400);
      assert.strictEqual((await fetch(base + '/request-license', { method: 'POST', body: 'not json' })).status, 400);
      assert.strictEqual((await fetch(base + '/request-license', { method: 'GET' })).status, 405);
      assert.strictEqual((await fetch(base + '/request-license', { method: 'DELETE' })).status, 405);
      // stored append-only, newest first via the admin listing; the vendor key stays out of a plain person's reach (sandbox/admin signing is untouched by this)
      assert.ok(fs.existsSync(path.join(tmp, 'data', 'license-requests.jsonl')));
      const stored = fs.readFileSync(path.join(tmp, 'data', 'license-requests.jsonl'), 'utf8').trim().split('\n').map(JSON.parse);
      assert.strictEqual(stored.length, 2); assert.strictEqual(stored[0].plan, 'pro'); assert.strictEqual(stored[0].email, 'jane@example.com');
      assert.strictEqual((await fetch(base + '/admin/license-requests')).status, 401, 'no admin token: refused, same as every other /admin/ route');
      const list = await (await fetch(base + '/admin/license-requests?limit=2', { headers: { Authorization: 'Bearer lic-admin-token' } })).json();
      assert.strictEqual(list.ok, true); assert.strictEqual(list.requests.length, 2); assert.strictEqual(list.requests[0].plan, 'studio', 'newest first');
      // a valid plan from bridge/license.js's own table is accepted, every PLANS key and no others
      for (const plan of Object.keys(lic.PLANS)) assert.strictEqual((await fetch(base + '/request-license', { method: 'POST', body: JSON.stringify({ plan }) })).status, 200);
      // rate limited per address, separately from the download/admin limiters
      let limited = false;
      for (let i = 0; i < 20; i++) { const r = await fetch(base + '/request-license', { method: 'POST', body: JSON.stringify({ plan: 'basic' }) }); if (r.status === 429) { limited = true; assert.ok(r.headers.get('retry-after')); break; } }
      assert.ok(limited, 'the public submission route is rate limited');
    } finally { server.closeAllConnections(); server.close(); }
  } finally {
    for (const k of Object.keys(savedEnv)) { if (savedEnv[k] === undefined) delete process.env[k]; else process.env[k] = savedEnv[k]; }
    smtp.close();
  }
});

test('OTA audit log: every admin action is recorded with who / what / result, hash-chained, tampering is found, refused attempts never store a token, the dashboard shows it', async () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), http = require('node:http'), crypto = require('node:crypto');
  const { createAudit } = require('../ota-server/audit'), { createOta } = require('../ota-server/server'), update = require('./update'), lic = require('../scripts/license'), dash = require('../ota-server/dashboard/app.js');
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'aud-'));
  // the log itself: chain, restart, filters, paging, sanitising, tamper detection
  const f = path.join(tmp, 'a.jsonl'); let a = createAudit({ file: f });
  for (let i = 0; i < 6; i++) a.record({ action: i % 2 ? 'file.upload' : 'manifest.publish', actor: { fp: 'abcd1234', ip: '10.0.0.' + i, ua: 'x\u0000y\nz' }, target: 'n' + i, detail: { i, long: 'x'.repeat(1000), nested: { deep: { deeper: { deepest: 1 } } } }, status: 200, result: i === 3 ? 'rejected' : 'ok' });
  assert.deepStrictEqual(a.verify(), { chainOk: true, entries: 6, brokenAt: null, reason: '' });
  const e0 = a.list({ limit: 1 }).entries[0]; assert.ok(e0.seq === 6 && /^[0-9a-f]{64}$/.test(e0.hash) && e0.actor.ua === 'x y z' && e0.detail.long.length === 300 && !JSON.stringify(e0.detail).includes('deepest'));
  assert.deepStrictEqual(a.list({ limit: 2 }).entries.map(e => e.seq), [6, 5]); assert.deepStrictEqual(a.list({ limit: 2, before: 5 }).entries.map(e => e.seq), [4, 3]);
  assert.strictEqual(a.list({ action: 'file.' }).total, 3); assert.strictEqual(a.list({ result: 'rejected' }).total, 1); assert.strictEqual(a.list({ limit: 2 }).hasMore, true); assert.strictEqual(a.list({ limit: 500 }).hasMore, false);
  a = createAudit({ file: f }); assert.strictEqual(a.record({ action: 'x' }).seq, 7); assert.ok(a.verify().chainOk);                    // a restart continues the chain
  const lines = fs.readFileSync(f, 'utf8').split('\n'), edit = fn => { const c = lines.slice(); fn(c); fs.writeFileSync(f, c.join('\n')); return createAudit({ file: f }).verify(); };
  assert.match(edit(c => { c[2] = c[2].replace('"ok"', '"error"'); }).reason, /entry 3 was changed/);
  assert.match(edit(c => { c.splice(2, 1); }).reason, /does not follow|jump/);
  assert.match(edit(c => { c[1] = 'garbage'; }).reason, /not a valid entry/);
  fs.writeFileSync(f, lines.join('\n')); assert.ok(createAudit({ file: f }).verify().chainOk);
  // the server: actions, outcomes, actors
  const keys = path.join(tmp, 'vendor'); lic.initKeys(keys); const jwk = lic.loadPublic(keys), priv = lic.loadPrivate(keys);
  const ota = createOta({ dataDir: path.join(tmp, 'data'), token: 'audit-secret-token', jwk, allowHttp: true, trustProxy: true }); await ota.reindex();
  const srv = http.createServer(ota.handler); await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const b = 'http://127.0.0.1:' + srv.address().port, auth = { Authorization: 'Bearer audit-secret-token', 'User-Agent': 'TestAgent/1.0' }, sha = x => crypto.createHash('sha256').update(x).digest('hex');
  try {
    const exe = Buffer.from('MZ' + 'e'.repeat(800)), put = (n, buf, h = {}) => fetch(b + '/admin/files/' + encodeURIComponent(n), { method: 'PUT', headers: { ...auth, 'X-SHA256': sha(buf), ...h }, body: buf });
    const man = (v, name, buf) => ({ product: 'audio-mixer', version: v, channel: 'stable', released: '2026-10-10', notes: [], files: { 'win-x64-exe': { name, url: 'https://x.test/releases/' + encodeURIComponent(name), size: buf.length, sha256: sha(buf) } } });
    const pub = (m, q = '', key = priv) => fetch(b + '/admin/manifest/stable' + q, { method: 'PUT', headers: auth, body: JSON.stringify(update.signManifest(m, key)) });
    assert.strictEqual((await fetch(b + '/admin/config', { headers: auth })).status, 200); await fetch(b + '/admin/config', { headers: auth });          // dashboard opened (once per 30 minutes)
    assert.strictEqual((await put('Audio Mixer-1.0.0.0.exe', exe)).status, 201); assert.strictEqual((await put('Audio Mixer-1.0.0.0.exe', exe)).status, 200);
    assert.strictEqual((await put('bad.exe', exe, { 'X-SHA256': sha('other') })).status, 400);
    assert.strictEqual((await pub(man('1.0.0.0', 'Audio Mixer-1.0.0.0.exe', exe), '', lic.initKeys(path.join(tmp, 'o')) && lic.loadPrivate(path.join(tmp, 'o')))).status, 400);   // foreign signature
    assert.strictEqual((await pub(man('1.0.0.0', 'Audio Mixer-1.0.0.0.exe', exe))).status, 200);
    assert.strictEqual((await put('Audio Mixer-1.1.0.0.exe', Buffer.from('MZ-new' + 'n'.repeat(400)))).status, 201); const exe2 = Buffer.from('MZ-new' + 'n'.repeat(400));
    assert.strictEqual((await pub(man('1.1.0.0', 'Audio Mixer-1.1.0.0.exe', exe2))).status, 200);
    assert.strictEqual((await pub(man('1.0.0.0', 'Audio Mixer-1.0.0.0.exe', exe), '?force=1')).status, 200);                                            // roll back
    assert.strictEqual((await fetch(b + '/admin/files/' + encodeURIComponent('Audio Mixer-1.0.0.0.exe'), { method: 'DELETE', headers: auth })).status, 409);
    assert.strictEqual((await fetch(b + '/admin/files/' + encodeURIComponent('Audio Mixer-1.1.0.0.exe'), { method: 'DELETE', headers: auth })).status, 200);
    assert.strictEqual((await fetch(b + '/admin/verify', { headers: auth })).status, 200);
    assert.strictEqual((await fetch(b + '/admin/manifest/stable', { method: 'DELETE', headers: auth })).status, 200);
    assert.strictEqual((await fetch(b + '/admin/files', { headers: { Authorization: 'Bearer wrong-token-xyz', 'User-Agent': 'Intruder/9' } })).status, 401);
    assert.strictEqual((await fetch(b + '/admin/audit')).status, 401); assert.strictEqual((await fetch(b + '/admin/audit/export')).status, 401);
    const j = await (await fetch(b + '/admin/audit?limit=500', { headers: auth })).json(), by = (act, res) => j.entries.filter(e => e.action === act && (!res || e.result === res));
    assert.ok(j.ok && j.entries.every(e => e.hash && e.prev) && j.entries[0].seq > j.entries[j.entries.length - 1].seq, 'newest first');
    assert.strictEqual(by('session.open').length, 1);                                                                                    // two config calls, one session entry
    assert.deepStrictEqual(by('file.upload').map(e => [e.target, e.result, e.status]).reverse(), [['Audio Mixer-1.0.0.0.exe', 'ok', 201], ['Audio Mixer-1.0.0.0.exe', 'ok', 200], ['bad.exe', 'rejected', 400], ['Audio Mixer-1.1.0.0.exe', 'ok', 201]]);
    assert.match(by('file.upload', 'rejected')[0].detail.error, /checksum mismatch/); assert.strictEqual(by('file.upload')[2].detail.unchanged, true);
    assert.deepStrictEqual(by('manifest.publish').map(e => [e.result, e.detail.version]).reverse(), [['rejected', undefined], ['ok', '1.0.0.0'], ['ok', '1.1.0.0']]);   // an unsigned manifest is not trusted: no version is recorded from it
    assert.match(by('manifest.publish', 'rejected')[0].detail.error, /not signed by the Audio Mixer publisher/);
    const rb = by('manifest.rollback'); assert.strictEqual(rb.length, 1); assert.ok(rb[0].detail.forced && rb[0].detail.previous === '1.1.0.0' && rb[0].detail.version === '1.0.0.0');
    assert.deepStrictEqual(by('file.delete').map(e => [e.target, e.result]).reverse(), [['Audio Mixer-1.0.0.0.exe', 'rejected'], ['Audio Mixer-1.1.0.0.exe', 'ok']]);
    assert.strictEqual(by('manifest.unpublish')[0].detail.version, '1.0.0.0'); assert.deepStrictEqual(by('files.verify')[0].detail, { files: 1, damaged: [] });
    const denied = by('auth.denied'); assert.strictEqual(denied.length, 3);                                                         // wrong token, and the two calls without any token
    const intr = denied.find(e => e.actor.ua === 'Intruder/9'); assert.ok(intr.result === 'denied' && intr.actor.fp === '' && intr.actor.ip === '127.0.0.1' && intr.target === 'GET /admin/files' && intr.status === 401);
    assert.ok(by('manifest.publish', 'ok')[0].actor.fp === crypto.createHash('sha256').update('audit-secret-token').digest('hex').slice(0, 8) && by('manifest.publish')[0].actor.ua === 'TestAgent/1.0');
    const raw = fs.readFileSync(path.join(ota.dirs.releases, '..', 'audit.jsonl'), 'utf8'); assert.ok(!raw.includes('audit-secret-token') && !raw.includes('wrong-token-xyz'), 'no token, right or wrong, is ever written to the log');
    assert.deepStrictEqual((await (await fetch(b + '/admin/audit?action=file.&result=rejected', { headers: auth })).json()).entries.map(e => e.target), ['Audio Mixer-1.0.0.0.exe', 'bad.exe']);
    const v = await (await fetch(b + '/admin/audit/verify', { headers: auth })).json(); assert.ok(v.chainOk && v.entries === j.chain);
    const ex = await fetch(b + '/admin/audit/export', { headers: auth }); assert.match(ex.headers.get('content-disposition'), /audit\.jsonl/); assert.ok((await ex.text()).trim().split('\n').length >= j.chain);
    assert.strictEqual((await (await fetch(b + '/admin/audit?action=audit.', { headers: auth })).json()).entries[0].action, 'audit.export');                // exporting is logged
    // flooding with wrong tokens does not flood the log
    const before = (await (await fetch(b + '/admin/audit?limit=1', { headers: auth })).json()).chain;
    for (let i = 0; i < 150; i++) await fetch(b + '/admin/files', { headers: { Authorization: 'Bearer x' + i, 'X-Forwarded-For': '203.0.113.9' } });      // a guessing attack from one address
    const after = await (await fetch(b + '/admin/audit?limit=500&action=auth.', { headers: auth })).json(), rl = await (await fetch(b + '/admin/audit?action=admin.', { headers: auth })).json();
    assert.ok(after.entries.filter(e => e.actor.ip === '203.0.113.9').length === 20 && rl.total === 5 && (await (await fetch(b + '/admin/audit?limit=1', { headers: auth })).json()).chain - before === 25, 'at most 20 refused sign-ins and 5 rate-limit notes per address and minute');
  } finally { srv.closeAllConnections(); srv.close(); }
  // dashboard: the Audit view, readable summaries
  const src = fs.readFileSync(path.join(__dirname, '..', 'ota-server', 'dashboard', 'app.js'), 'utf8');
  assert.ok(/audit: \['Audit', auditView\]/.test(src) && src.includes("'/admin/audit/verify'") && src.includes("'/admin/audit/export'") && !/innerHTML/.test(src));
  assert.strictEqual(dash.fmtDetail({ version: '2.0.0.0', files: ['a', 'b'], forced: true, previous: null, ok: false }), 'version 2.0.0.0 • files a, b • forced'); assert.strictEqual(dash.fmtDetail(null), ''); assert.ok(dash.fmtDetail({ x: 'y'.repeat(500) }).length <= 220);
  for (const k of ['file.upload', 'manifest.rollback', 'auth.denied', 'session.open']) assert.ok(dash.ACTIONS[k], k);
});


test('OS detection for update links: user agents, Client Hints, explicit choice, system; the right release file per system; the OTA server\'s /latest (302) and /api/latest (JSON, CORS), stats by detected system', async () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), http = require('node:http'), crypto = require('node:crypto');
  const od = require('./osdetect'), update = require('./update'), lic = require('../scripts/license');
  const { createOta } = require('../ota-server/server');
  const d = (ua, extra = {}) => { const r = od.detectOs({ userAgent: ua, ...extra }); return [r.os, r.arch, r.distro, r.source].join('|'); };
  const WIN = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36';
  assert.strictEqual(d(WIN), 'windows|x64||user-agent');
  assert.strictEqual(d('Mozilla/5.0 (Windows NT 6.1; WOW64; Trident/7.0; rv:11.0) like Gecko'), 'windows|x64||user-agent');
  assert.strictEqual(d('Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 Chrome/49.0 Safari/537.36'), 'windows|x86||user-agent');
  assert.strictEqual(d('Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Mobile Safari/537.36'), 'android|||user-agent');            // "Linux" in an Android user agent is not Linux
  assert.strictEqual(d('Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1'), 'ios|arm64||user-agent'); // "Mac OS X" in an iPhone one is not macOS
  assert.strictEqual(d('Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Safari/605.1.15'), 'macos|||user-agent');
  assert.strictEqual(d('Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0'), 'linux|x64|debian|user-agent');
  assert.strictEqual(d('Mozilla/5.0 (X11; Fedora; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0'), 'linux|x64|other|user-agent');
  assert.strictEqual(d('Mozilla/5.0 (X11; CrOS x86_64 14541.0.0) AppleWebKit/537.36 Chrome/126.0 Safari/537.36'), '|||none');                                              // Chrome OS: no package
  for (const ua of ['curl/8.5.0', 'audio-mixer-update', '', undefined]) assert.strictEqual(d(ua), '|||none', String(ua));
  assert.strictEqual(d('curl/8', { platformHint: '"Android"' }), 'android|||client-hints'); assert.strictEqual(d('', { platformHint: '"macOS"', archHint: '"arm"' }), 'macos|arm64||client-hints'); assert.strictEqual(d('', { platformHint: '"Windows"', archHint: '"x86"' }), 'windows|x64||client-hints');
  assert.strictEqual(d(WIN, { os: 'android' }), 'android|||param'); assert.strictEqual(d(WIN, { os: 'win', arch: 'ia32' }), 'windows|x86||param'); assert.strictEqual(d('', { os: 'ubuntu' }), '|||none');
  assert.strictEqual(d(WIN, { os: 'linux', distro: 'Ubuntu' }), 'linux||debian|param');
  assert.deepStrictEqual(od.detectOs({ node: { platform: 'win32', arch: 'ia32' } }), { os: 'windows', arch: 'x86', distro: 'other', source: 'system' });
  assert.deepStrictEqual(od.detectOs({ node: { platform: 'linux', arch: 'arm64', debian: true } }), { os: 'linux', arch: 'arm64', distro: 'debian', source: 'system' });
  // which key per system, best first
  const keys = (os, arch, distro) => od.keysFor({ os, arch, distro });
  assert.deepStrictEqual(keys('windows', 'x64'), ['win-x64-exe', 'win-x64-msi']); assert.deepStrictEqual(keys('windows', 'x86'), ['win-x86-msi']); assert.deepStrictEqual(keys('linux', 'x64', 'debian'), ['linux-deb', 'linux-tar']);
  assert.deepStrictEqual(keys('linux', 'x64', null), ['linux-tar', 'linux-deb']); assert.deepStrictEqual(keys('linux', 'x64', 'other'), ['linux-tar']); assert.deepStrictEqual(keys('macos'), ['macos-dmg', 'macos']); assert.deepStrictEqual(keys('android'), ['android-apk']); assert.deepStrictEqual(keys('ios'), ['ios-project']); assert.deepStrictEqual(keys(null), []);
  // the update client picks by the same rules (a Debian box without a .deb in the manifest gets the archive; Android gets the .apk)
  const man = { files: Object.fromEntries(Object.keys(od.FILES).map(k => [k, { name: k + '.bin', url: 'https://x.test/' + k, size: 1, sha256: 'a'.repeat(64) }])) }, pf = (platform, arch, debian) => (update.platformFile(man, { platform, arch, debian }) || {}).key;
  assert.deepStrictEqual([pf('win32', 'x64'), pf('win32', 'ia32'), pf('linux', 'x64', true), pf('linux', 'x64', false), pf('darwin', 'arm64'), pf('android', 'arm64')], ['win-x64-exe', 'win-x86-msi', 'linux-deb', 'linux-tar', 'macos-dmg', 'android-apk']);
  delete man.files['macos-dmg']; delete man.files['linux-deb']; assert.deepStrictEqual([pf('darwin', 'x64'), pf('linux', 'x64', true)], ['macos', 'linux-tar']); assert.strictEqual(update.platformFile({ files: {} }, { platform: 'win32', arch: 'x64' }), null);
  assert.ok(require('../scripts/make-update').build.toString().includes("'android-apk'") && require('../scripts/make-update').build.toString().includes("'ios-project'"));
  // the OTA server
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ol-')), keysDir = path.join(tmp, 'v'); lic.initKeys(keysDir); const jwk = lic.loadPublic(keysDir), priv = lic.loadPrivate(keysDir);
  const ota = createOta({ dataDir: path.join(tmp, 'data'), token: 'tk', jwk, publicUrl: 'https://ota.test', rate: 1000 }); await ota.reindex();
  const srv = http.createServer(ota.handler); await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const b = 'http://127.0.0.1:' + srv.address().port, auth = { Authorization: 'Bearer tk' }, sha = x => crypto.createHash('sha256').update(x).digest('hex');
  try {
    assert.strictEqual((await fetch(b + '/latest')).status, 404); assert.strictEqual((await (await fetch(b + '/api/latest')).json()).ok, false);                          // nothing published yet
    const names = { 'win-x64-exe': 'Audio Mixer-7.0.0.0.exe', 'win-x64-msi': 'AudioMixer-7.0.0.0-x64.msi', 'win-x86-msi': 'AudioMixer-7.0.0.0-x86.msi', 'linux-deb': 'audio-mixer_7.0.0.0_all.deb', 'linux-tar': 'AudioMixer-7.0.0.0-linux.tar.gz', 'macos-dmg': 'AudioMixer-7.0.0.0-macos.dmg', 'android-apk': 'AudioMixer-7.0.0.0-android.apk' };
    const files = {};
    for (const [k, n] of Object.entries(names)) { const buf = Buffer.from(k + '-content-' + 'x'.repeat(50)); assert.strictEqual((await fetch(b + '/admin/files/' + encodeURIComponent(n), { method: 'PUT', headers: { ...auth, 'X-SHA256': sha(buf) }, body: buf })).status, 201); files[k] = { name: n, url: 'https://ota.test/releases/' + encodeURIComponent(n), size: buf.length, sha256: sha(buf) }; }
    const manifest = { product: 'audio-mixer', version: '7.0.0.0', channel: 'stable', released: '2026-10-11', notes: ['smart links'], files };
    assert.strictEqual((await fetch(b + '/admin/manifest/stable', { method: 'PUT', headers: auth, body: JSON.stringify(update.signManifest(manifest, priv)) })).status, 200);
    const UA = { win: WIN, android: 'Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 Chrome/126.0 Mobile Safari/537.36', mac: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 Safari/605.1.15', ubuntu: 'Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:128.0) Firefox/128.0', fedora: 'Mozilla/5.0 (X11; Fedora; Linux x86_64; rv:128.0) Firefox/128.0', iphone: 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 Mobile/15E148 Safari/604.1' };
    const go = async (p, ua, h = {}) => fetch(b + p, { redirect: 'manual', headers: { 'User-Agent': ua || 'x', ...h } });
    const loc = async (p, ua, h) => { const r = await go(p, ua, h); return [r.status, r.headers.get('location')]; };
    const u = k => files[k].url;
    assert.deepStrictEqual(await loc('/latest', UA.win), [302, u('win-x64-exe')]); assert.deepStrictEqual(await loc('/download', UA.android), [302, u('android-apk')]); assert.deepStrictEqual(await loc('/latest', UA.mac), [302, u('macos-dmg')]);
    assert.deepStrictEqual(await loc('/latest', UA.ubuntu), [302, u('linux-deb')]); assert.deepStrictEqual(await loc('/latest', UA.fedora), [302, u('linux-tar')]);
    assert.deepStrictEqual(await loc('/latest?os=windows&arch=x86', UA.android), [302, u('win-x86-msi')]);                                                       // an explicit choice wins over the user agent
    assert.deepStrictEqual(await loc('/latest', 'curl/8', { 'Sec-CH-UA-Platform': '"Android"' }), [302, u('android-apk')]);
    const ios = await go('/latest', UA.iphone); assert.strictEqual(ios.status, 404); assert.match((await ios.json()).error, /no download for this system \(ios\)/);                                 // no iOS file in this manifest
    const unk = await go('/latest', 'curl/8'); assert.strictEqual(unk.status, 404); const ub = await unk.json(); assert.match(ub.error, /could not be detected/); assert.ok(ub.others.length === 7);
    assert.strictEqual((await go('/latest?channel=Bad!', UA.win)).status, 400); assert.strictEqual((await go('/latest?channel=beta', UA.win)).status, 404);
    // the JSON for apps: detected system, version, notes, the file with how-to, the others, CORS
    const r = await go('/api/latest', UA.android), j = await r.json();
    assert.strictEqual(r.headers.get('access-control-allow-origin'), '*'); assert.match(r.headers.get('vary'), /User-Agent/); assert.match(r.headers.get('accept-ch'), /Sec-CH-UA-Platform/);
    assert.ok(j.ok && j.os === 'android' && j.detected === 'user-agent' && j.version === '7.0.0.0' && j.notes[0] === 'smart links' && j.file.key === 'android-apk' && j.file.url === u('android-apk') && j.file.sha256 === files['android-apk'].sha256 && /Open the downloaded \.apk/.test(j.file.how) && j.others.length === 6 && !j.others.some(o => o.key === 'android-apk') && j.manifest === 'https://ota.test/update.json');
    const jw = await (await go('/api/latest?arch=x86', UA.win)).json(); assert.ok(jw.file.key === 'win-x86-msi' && jw.arch === 'x86' && jw.detected === 'user-agent');
    const ji = await (await go('/api/latest', UA.iphone)).json(); assert.ok(ji.ok && ji.os === 'ios' && ji.file === null && ji.others.length === 7);
    assert.strictEqual((await go('/update.json', 'x')).headers.get('access-control-allow-origin'), '*');
    const pre = await fetch(b + '/api/latest', { method: 'OPTIONS', headers: { Origin: 'https://app.example' } }); assert.strictEqual(pre.status, 204); assert.match(pre.headers.get('access-control-allow-methods'), /GET/);
    assert.strictEqual((await fetch(b + '/latest', { method: 'POST' })).status, 405);
    const st = await (await fetch(b + '/admin/stats', { headers: auth })).json(), det = Object.values(st.days)[0].detect; assert.ok(det.android >= 3 && det.windows >= 3 && det.macos === 1 && det.linux === 2 && det.unknown >= 1 && det.ios >= 2, JSON.stringify(det));
  } finally { srv.closeAllConnections(); srv.close(); }
});

test('OTA server: the system driver (Windows ASIO zip) is published in the manifest, offered next to the app on /api/latest and /driver, and never replaces the installer', async () => {
  const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), http = require('node:http'), crypto = require('node:crypto');
  const { createOta } = require('../ota-server/server'), lic = require('../scripts/license'), update = require('./update'), mk = require('../scripts/make-update');
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'od-')), keysDir = path.join(tmp, 'v'); lic.initKeys(keysDir);
  const ota = createOta({ dataDir: path.join(tmp, 'data'), token: 'tk', jwk: lic.loadPublic(keysDir), publicUrl: 'https://ota.test', rate: 1000 }); await ota.reindex();
  const srv = http.createServer(ota.handler); await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const b = 'http://127.0.0.1:' + srv.address().port, auth = { Authorization: 'Bearer tk' }, sha = x => crypto.createHash('sha256').update(x).digest('hex');
  try {
    // make-update / ota.js know the driver file name
    const rel = fs.mkdtempSync(path.join(tmp, 'rel')); fs.writeFileSync(path.join(rel, 'AudioMixer-8.0.0.0-asio-driver-windows.zip'), 'zip'); fs.writeFileSync(path.join(rel, 'Audio Mixer-8.0.0.0.exe'), 'exe');
    assert.deepStrictEqual(Object.keys(mk.build({ releases: rel, version: '8.0.0.0', base: 'https://ota.test/releases' }).files).sort(), ['win-asio-driver', 'win-x64-exe']);
    const names = { 'win-x64-exe': 'Audio Mixer-8.0.0.0.exe', 'win-asio-driver': 'AudioMixer-8.0.0.0-asio-driver-windows.zip', 'linux-tar': 'AudioMixer-8.0.0.0-linux.tar.gz' }, files = {};
    for (const [k, n] of Object.entries(names)) { const buf = Buffer.from(k + '-content-' + 'x'.repeat(40)); assert.strictEqual((await fetch(b + '/admin/files/' + encodeURIComponent(n), { method: 'PUT', headers: { ...auth, 'X-SHA256': sha(buf) }, body: buf })).status, 201); files[k] = { name: n, url: 'https://ota.test/releases/' + encodeURIComponent(n), size: buf.length, sha256: sha(buf) }; }
    const manifest = { product: 'audio-mixer', version: '8.0.0.0', channel: 'stable', released: '2026-10-11', notes: [], files };
    assert.strictEqual((await fetch(b + '/admin/manifest/stable', { method: 'PUT', headers: auth, body: JSON.stringify(update.signManifest(manifest, lic.loadPrivate(keysDir))) })).status, 200);
    const WIN = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/126.0 Safari/537.36', LIN = 'Mozilla/5.0 (X11; Fedora; Linux x86_64; rv:128.0) Firefox/128.0';
    const go = (p, ua) => fetch(b + p, { redirect: 'manual', headers: { 'User-Agent': ua } });
    const w = await (await go('/api/latest', WIN)).json();
    assert.strictEqual(w.file.key, 'win-x64-exe');                                                              // the installer is still what /latest gives
    assert.ok(w.driver && w.driver.key === 'win-asio-driver' && w.driver.url === files['win-asio-driver'].url && w.driver.sha256 === files['win-asio-driver'].sha256 && /regsvr32/.test(w.driver.how));
    assert.deepStrictEqual(w.others.map(o => o.key), ['linux-tar']);                                           // the driver is not listed twice
    const l = await (await go('/api/latest', LIN)).json(); assert.strictEqual(l.driver, null); assert.deepStrictEqual(l.others.map(o => o.key).sort(), ['win-asio-driver', 'win-x64-exe']);
    const d = await go('/driver', WIN); assert.strictEqual(d.status, 302); assert.strictEqual(d.headers.get('location'), files['win-asio-driver'].url);
    assert.strictEqual((await go('/latest', WIN)).headers.get('location'), files['win-x64-exe'].url);
    const nd = await go('/driver', LIN); assert.strictEqual(nd.status, 404); assert.match((await nd.json()).error, /no driver file is published for this system \(linux\)/);
    assert.strictEqual((await go('/driver?os=windows', LIN)).headers.get('location'), files['win-asio-driver'].url);   // an explicit choice, like /latest
    assert.strictEqual((await go('/driver?channel=beta', WIN)).status, 404);
    assert.strictEqual((await fetch(b + '/driver', { method: 'POST' })).status, 405);
    // the app's own update check reports the driver too, and still downloads the installer
    const env = JSON.parse(fs.readFileSync(path.join(tmp, 'data', 'manifests', 'stable.json'), 'utf8'));
    process.env.BRIDGE_UPDATE_HOSTS = 'ota.test';
    const chk = await update.check({ current: '7.0.0.0', platform: 'win32', arch: 'x64', url: 'https://ota.test/update.json', jwk: lic.loadPublic(keysDir), fetchImpl: async () => ({ ok: true, status: 200, json: async () => env }) });
    assert.strictEqual(chk.file.key, 'win-x64-exe'); assert.strictEqual(chk.driver.key, 'win-asio-driver'); assert.ok(!chk.others.some(o => o.key === 'win-asio-driver'));
  } finally { srv.closeAllConnections(); srv.close(); delete process.env.BRIDGE_UPDATE_HOSTS; }
});




test('native engine: the embedded C core, the C++ engine, the Rust crate, the device runtime and the Apple wrappers are held to the same vectors (49 cases, 397 numbers)', (tc) => {
  const be = require('../scripts/build-engine');
  const steps = { 'embedded C core': be.buildC(), 'C++ engine': be.buildCpp(), 'Rust crate': be.buildRust(), 'device runtime': be.buildIo(), 'Apple wrappers': be.checkApple() };
  let ran = 0;
  for (const [name, r] of Object.entries(steps)) { if (r.skipped) continue; ran++; assert.strictEqual(r.ok, true, name + ': ' + r.detail); }
  if (!ran) return tc.skip('no compiler on this machine');
  for (const [name, re] of [['embedded C core', /49 cases, 397 numbers checked, 0 failed/], ['C++ engine', /49 cases, 397 numbers checked, 0 failed/], ['Rust crate', /49 cases, 397 numbers checked, 0 failed/]]) if (!steps[name].skipped) assert.match(steps[name].detail, re, name);
  if (!steps['device runtime'].skipped) assert.match(steps['device runtime'].detail, /runtime: all good/);
  // the vectors are what the reference code gives today: a change in the page's loudness / meter / scope code or in the bridge's meter shows up here
  const fsx = require('node:fs'), pathx = require('node:path'), osx = require('node:os'), cp = require('node:child_process');
  const tmp = pathx.join(fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'vec-')), 'v.txt'), root = pathx.join(__dirname, '..', 'engine', 'tests');
  try {
    const g = cp.spawnSync(process.execPath, [pathx.join(root, 'gen-vectors.js'), tmp], { encoding: 'utf8' }); assert.strictEqual(g.status, 0, g.stderr);
    assert.strictEqual(fsx.readFileSync(tmp, 'utf8'), fsx.readFileSync(pathx.join(root, 'vectors.txt'), 'utf8'), 'engine/tests/vectors.txt is what the reference code gives (node engine/tests/gen-vectors.js rewrites it)');
  } finally { fsx.rmSync(pathx.dirname(tmp), { recursive: true, force: true }); }
});


test('native engine in the bridge: the Node addon (C++) gives the numbers of the JavaScript meter, the loudness meter and the scope of the vectors', (tc) => {
  const be = require('../scripts/build-engine'), r = be.buildNode();
  if (r.skipped) return tc.skip(r.skipped); assert.strictEqual(r.ok, true, r.detail);
  // the loader and levels.js decide once, when first loaded: an earlier test may have loaded them before this build existed (a fresh checkout), so load them again
  // the server keeps the copies it loaded: they go back in the cache when this test ends, so later tests and the server share one levels.js again
  const saved = ['../engine/node', './levels.js'].map(m => [require.resolve(m), require.cache[require.resolve(m)]]);
  for (const [k] of saved) delete require.cache[k];
  tc.after(() => { for (const [k, v] of saved) { if (v) require.cache[k] = v; else delete require.cache[k]; } });
  const native = require(r.file), lv = require('./levels.js'), { parse } = require('../engine/tests/vectors'), { gen, genS16 } = require('../engine/tests/signals');
  // the bridge's Int16 meter: the same snapshots as the JavaScript class, block by block, and with a changed clip threshold
  for (const [spec, ch, db] of [['clipsine:440:1.4', 2, -0.1], ['sine:300:0.9', 1, -3], ['noise:11:0.99', 6, -0.1], ['sine:1000:0.1', 2, -0.1]]) {
    lv.setClipDb(db); const x = genS16(spec, 48000, 4800, ch), buf = Buffer.from(x.buffer), a = new lv.Meter(ch), b = new native.Meter(ch, db);
    for (let i = 0; i < 4; i++) { const part = buf.subarray(i * buf.length / 4, (i + 1) * buf.length / 4); a.push(part); b.push(part); const sa = a.snapshot(), sb = b.snapshot(); sb.clipDb = db; assert.deepStrictEqual(sb, sa, spec + ' block ' + i); }
  }
  lv.setClipDb(-0.1); const odd = Buffer.alloc(401); odd.writeInt16LE(32767, 1); const m1 = new native.Meter(1, -0.1); m1.push(odd.subarray(1, 399)); assert.strictEqual(m1.snapshot().frames, 199, 'a Buffer at an odd address, a partial last sample ignored');
  assert.throws(() => m1.push('nope'), /Buffer/);
  // loudness and scope of the vectors
  let n = 0;
  for (const c of parse()) {
    if (c.kind === 'lufs') { const fsHz = Number(c.head[2]), len = Math.round(fsHz * Number(c.head[4])), l = gen(c.head[5], fsHz, len, 0), rr = gen(c.head[5], fsHz, len, 1), m = new native.Loudness(fsHz); for (let i = 0; i < len; i += 2048) m.feed(l.subarray(i, i + 2048), rr.subarray(i, i + 2048)); const v = m.values(); n++;
      [v.momentary, v.shortTerm, v.integrated, v.lra].forEach((x, k) => assert.ok(c.expect[k] === -Infinity ? x === -Infinity : Math.abs(x - c.expect[k]) <= 0.02, c.id + ' ' + k)); assert.ok(Math.abs(v.truePeak - c.expect[4]) <= 0.01 || v.truePeak === c.expect[4], c.id + ' true peak'); }
    if (c.kind === 'scope') { const fsHz = Number(c.head[2]), A = gen(c.head[7], fsHz, Number(c.head[3]), 0), s = native.scope(A, fsHz, { timebase: Number(c.head[4]), triggerLevel: Number(c.head[5]), triggerEdge: c.head[6] === '1' ? 'rise' : 'fall' }); n++; assert.ok(s.N === c.expect[0] && s.start === c.expect[1] && s.triggered === (c.expect[2] === 1) && Math.abs(s.freq - c.expect[6]) <= 1e-6 * Math.max(1, c.expect[6]), c.id); }
  }
  assert.ok(n >= 20); assert.throws(() => native.scope(new Float64Array(10), 48000, {}), /Float32Array/);
  // the mixer
  const mx = new native.Mixer(2, 48000); mx.setStrip(0, { gainDb: -6, pan: -1 }); mx.setStrip(1, { mute: true }); mx.setMaster({ gainDb: 0, limiter: false });
  const x = gen('sine:440:0.5', 48000, 4096, 0), ol = new Float32Array(4096), orr = new Float32Array(4096); mx.process([x, x], ol, orr); const st = mx.state();
  assert.ok(Math.abs(Math.max(...ol.map(Math.abs)) - 0.25) < 0.005 && Math.max(...orr.map(Math.abs)) < 0.001 && Math.abs(st.strips[0].pk - 0.25) < 0.005 && st.strips.length === 2 && st.master.length === 2, 'strip gain, pan and the state');
  assert.throws(() => mx.process([x], ol, orr), /one Float32Array per strip/);
  // the bridge uses it when it is there, and the loader reports it
  const node = require('../engine/node'); assert.strictEqual(node.available, true); assert.ok(lv.native(), 'bridge/levels.js picked the addon up');
  assert.strictEqual(new (require('./levels.js').NativeMeter)(2).ch, 2);
});

test('npm installer (npm-installer/): per-system choice of file and own installation, signed manifest + SHA-256 checked before anything runs, Windows ASIO registration only on request', async () => {
  const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), http = require('node:http'), crypto = require('node:crypto');
  const root = path.join(__dirname, '..', 'npm-installer'), det = require(path.join(root, 'lib', 'detect')), mf = require(path.join(root, 'lib', 'manifest')), { main, parseArgs } = require(path.join(root, 'lib', 'cli'));
  const lic = require('../scripts/license'), update = require('./update'), osdetect = require('./osdetect');
  // the package is self-contained and agrees with the app: same public key, same file choice
  assert.deepStrictEqual(mf.JWK, require('./license-public.json'));
  const pj = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8')); assert.ok(/^\d+\.\d+\.\d+$/.test(pj.version) && pj.bin['audio-mixer-install'] && !pj.dependencies);
  for (const f of fs.readdirSync(path.join(root, 'lib')).filter(n => n.endsWith('.js'))) assert.ok(!/require\(['"]\.\.\//.test(fs.readFileSync(path.join(root, 'lib', f), 'utf8')), f + ' does not reach into the repository');
  for (const d of [{ os: 'windows', arch: 'x64' }, { os: 'windows', arch: 'x86' }, { os: 'linux', arch: 'x64', distro: 'debian' }, { os: 'linux', arch: 'x64', distro: 'other' }, { os: 'android' }, { os: 'ios' }]) assert.deepStrictEqual(det.keysFor(d), osdetect.keysFor(d), JSON.stringify(d));
  assert.deepStrictEqual(det.keysFor({ os: 'macos' }), ['macos']); assert.deepStrictEqual(det.keysFor({ os: 'linux', distro: 'debian' }, { hasApt: false }), ['linux-tar']);
  assert.strictEqual(det.detect({ platform: 'win32', archName: 'ia32', env: { PROCESSOR_ARCHITEW6432: 'AMD64' } }).arch, 'x64');   // 32-bit Node.js on 64-bit Windows
  assert.deepStrictEqual(det.detect({ platform: 'linux', archName: 'x64', debian: true }), { os: 'linux', arch: 'x64', distro: 'debian' });
  assert.throws(() => parseArgs(['--nope']), /unknown option/); assert.throws(() => parseArgs(['--server']), /needs a value/);

  // a small server: the manifest and the files
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'npmi-')), keys = path.join(tmp, 'k'); lic.initKeys(keys);
  const priv = lic.loadPrivate(keys), pub = lic.loadPublic(keys), sha = b => crypto.createHash('sha256').update(b).digest('hex');
  const names = { 'win-x64-exe': 'Audio Mixer-9.0.0.0.exe', 'win-x86-msi': 'AudioMixer-9.0.0.0-x86.msi', 'linux-deb': 'audio-mixer_9.0.0.0_all.deb', 'linux-tar': 'AudioMixer-9.0.0.0-linux.tar.gz', 'macos': 'AudioMixer-9.0.0.0-macos.tar.gz', 'android-apk': 'AudioMixer-9.0.0.0-android.apk' };
  const bodies = {}; for (const [k, n] of Object.entries(names)) bodies[n] = Buffer.from(k + ' content ' + 'x'.repeat(30));
  let hits = [], tamper = false, base = '';
  const srv = http.createServer((req, res) => {
    const u = decodeURIComponent(req.url.split('?')[0]); hits.push(u);
    if (u === '/update.json' || u === '/other.json') {
      const files = {}; for (const [k, n] of Object.entries(names)) files[k] = { name: n, url: base + '/releases/' + encodeURIComponent(n), size: bodies[n].length, sha256: sha(bodies[n]) };
      const env = update.signManifest({ product: 'audio-mixer', version: '9.0.0.0', channel: 'stable', files }, u === '/other.json' ? lic.loadPrivate((() => { const d = path.join(tmp, 'k2'); lic.initKeys(d); return d; })()) : priv);
      return res.end(JSON.stringify(env));
    }
    const m = /^\/releases\/(.+)$/.exec(u); if (m && bodies[m[1]]) return res.end(tamper ? Buffer.from('tampered' + 'x'.repeat(30)) : bodies[m[1]]);
    res.statusCode = 404; res.end('no');
  });
  await new Promise(r => srv.listen(0, '127.0.0.1', r)); base = 'http://127.0.0.1:' + srv.address().port;
  const calls = [], spawn = (c, a, o) => { calls.push({ c, a, o }); return { status: 0 }; };
  const run = async (args, io = {}) => { const out = []; calls.length = 0; const code = await main(['--server', base + '/update.json', '--allow-http', ...args], { log: s => out.push(s), spawn, jwk: pub, ...io }); return { code, out: out.join('\n') }; };
  try {
    // Windows 64-bit: the setup .exe; the ASIO driver is registered only with --asio (or the setup's own question in an interactive install)
    let r = await run(['--os', 'windows', '--arch', 'x64', '--yes']); assert.strictEqual(r.code, 0, r.out);
    assert.strictEqual(calls.length, 1); assert.strictEqual(calls[0].c, 'powershell.exe'); assert.match(calls[0].a[3], /Audio Mixer-9\.0\.0\.0\.exe'.*@\('\/passive','\/noasio'\) -Wait/); assert.match(r.out, /SHA-256 matches the signed manifest/);
    assert.ok(!fs.existsSync(calls[0].a[3].match(/-FilePath '([^']+)'/)[1]), 'the downloaded file is deleted afterwards');
    r = await run(['--os', 'windows', '--arch', 'x64', '--quiet', '--asio']); assert.match(calls[0].a[3], /@\('\/quiet','\/asio'\)/);
    r = await run(['--os', 'windows', '--arch', 'x64', '--asio', '--yes']); assert.match(calls[0].a[3], /@\('\/passive','\/asio'\)/);
    r = await run(['--os', 'windows', '--arch', 'x64'], { ask: async () => true }); assert.ok(!/ArgumentList/.test(calls[0].a[3]), 'interactive: no switches, the setup asks its own questions');
    r = await run(['--os', 'windows', '--arch', 'x64'], { ask: async () => false }); assert.strictEqual(calls.length, 0); assert.match(r.out, /Nothing was run/);
    r = await run(['--os', 'windows', '--arch', 'x64', 'uninstall', '--yes']); assert.match(calls[0].a[3], /@\('\/uninstall'\)/);
    // Windows 32-bit: the .msi (Program Files (x86)); --asio adds the AsioDriver feature to the default feature list
    r = await run(['--os', 'windows', '--arch', 'x86', '--yes']); assert.strictEqual(calls[0].c, 'msiexec.exe'); assert.deepStrictEqual(calls[0].a.slice(0, 1).concat(calls[0].a.slice(2)), ['/i', '/passive']);
    r = await run(['--os', 'windows', '--arch', 'x86', '--yes', '--asio']); assert.match(calls[0].a[3], /^ADDLOCAL=Main,.*Desktop,AsioDriver$/);
    // macOS and Linux: the system's own installer from the archive, the .deb with apt
    r = await run(['--os', 'macos', '--yes']); assert.deepStrictEqual(calls.map(x => x.c), ['tar', 'sh']); assert.match(calls[1].a[0], /AudioMixer\/install\.command$/);
    r = await run(['--os', 'linux', '--distro', 'other', '--yes'], {}); assert.deepStrictEqual(calls.map(x => x.c), ['tar', 'sh']); assert.match(calls[1].a.join(' '), /audio-mixer-9\.0\.0\.0\/install\.sh --yes$/);
    r = await run(['--os', 'linux', '--distro', 'debian', '--yes'], { hasApt: true }); assert.match([calls[0].c].concat(calls[0].a).join(' '), /apt install -y .*audio-mixer_9\.0\.0\.0_all\.deb$/);
    r = await run(['--os', 'linux', '--distro', 'debian', '--yes', 'uninstall'], { hasApt: true }); assert.match([calls[0].c].concat(calls[0].a).join(' '), /apt remove -y audio-mixer$/);
    // dry run: the plan, no download, nothing run
    hits = []; r = await run(['--os', 'windows', '--arch', 'x64', '--dry-run', '--json']); const plan = JSON.parse(r.out); assert.strictEqual(plan.file.name, 'Audio Mixer-9.0.0.0.exe'); assert.ok(plan.steps.length === 1 && calls.length === 0 && !hits.some(h => h.startsWith('/releases')));
    // Android / iOS: only downloaded, with the how-to
    const out = path.join(tmp, 'out'); r = await run(['--os', 'android', '--out', out]); assert.strictEqual(calls.length, 0); assert.ok(fs.existsSync(path.join(out, 'AudioMixer-9.0.0.0-android.apk'))); assert.match(r.out, /open it/);
    // nothing runs when something is wrong: a changed file, a manifest signed by another key, a host that is not allowed
    tamper = true; await assert.rejects(run(['--os', 'windows', '--arch', 'x64', '--yes']), /does not match the signed SHA-256/); assert.strictEqual(calls.length, 0); tamper = false;
    await assert.rejects(main(['--server', base + '/other.json', '--allow-http', '--os', 'linux', '--yes'], { log() {}, spawn, jwk: pub }), /not signed by the Audio Mixer publisher/);
    await assert.rejects(main(['--server', base + '/update.json', '--os', 'linux', '--yes'], { log() {}, spawn, jwk: pub }), /only https is accepted/);
    await assert.rejects(main(['--server', 'https://evil.example/update.json', '--os', 'linux', '--yes'], { log() {}, spawn }), /./);
    // a local folder (a checkout's <os>/releases) works without a network
    const rel = path.join(tmp, 'rel'); fs.mkdirSync(rel); for (const n of ['AudioMixer-1.2.0.0-linux.tar.gz', 'AudioMixer-1.10.0.0-linux.tar.gz']) fs.writeFileSync(path.join(rel, n), 'x');
    r = await main(['--dir', rel, '--os', 'linux', '--distro', 'other', '--yes'], { log: s => { r2 = s; }, spawn }); var r2; assert.strictEqual(r, 0); assert.match(calls[0].a.join(' '), /AudioMixer-1\.10\.0\.0-linux\.tar\.gz/);
  } finally { srv.closeAllConnections(); srv.close(); fs.rmSync(tmp, { recursive: true, force: true }); }
});





test('a driver that disappears without a WebSocket close frame (killed host: only FIN) frees its device at once, so the restarted host can connect', async () => {
  const net = require('node:net');
  const { server } = require('./server');
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port, mask = Buffer.from([1, 2, 3, 4]);
  const frame = t => { const p = Buffer.from(t), m = Buffer.alloc(p.length); for (let i = 0; i < p.length; i++) m[i] = p[i] ^ mask[i % 4]; return Buffer.concat([Buffer.from([0x81, 0x80 | p.length]), mask, m]); };
  const count = async () => (await (await fetch(`http://127.0.0.1:${port}/api/virtual`)).json()).devices.length;
  const connect = name => new Promise((resolve, reject) => {
    const s = net.connect(port, '127.0.0.1', () => s.write(`GET /ws/virtual HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n`));
    let buf = '', sent = false;
    s.on('data', d => { buf += d.toString('latin1'); if (!sent && buf.includes('\r\n\r\n')) { sent = true; s.write(frame(JSON.stringify({ type: 'hello', name, channels: 2, rate: 48000 }))); } if (/"type":"ready"/.test(buf)) resolve(s); else if (/"type":"error"/.test(buf)) reject(new Error(buf.slice(-100))); });
    s.on('error', () => {});
  });
  try {
    const a = await connect('FinDriver');
    assert.strictEqual(await count(), 1);
    a.end();                                                        // FIN without a close frame
    let n = 1; for (let i = 0; i < 40 && n; i++) { await new Promise(r => setTimeout(r, 50)); n = await count(); }
    assert.strictEqual(n, 0, 'the device is released within about 2 s (it used to stay for about a minute)');
    const b = await connect('FinDriver');                           // the same name is accepted again
    assert.strictEqual(await count(), 1); b.destroy();
  } finally { server.closeAllConnections(); server.close(); }
});

test('audify WASAPI output: the queue of blocks waiting for the device is bounded (a slow start or clock drift no longer adds an ever-growing delay)', () => {
  const a = require('./audify');
  const mk = () => {
    const f = fakeAudify(), queue = [], st = { cleared: 0, frameCb: 'unset' };
    const Base = f.RtAudio;
    f.RtAudio = class extends Base {
      openStream(out, inp, fmt, rate, frames, name, cb, frameCb, flags) { st.frameCb = frameCb; return super.openStream(out, inp, fmt, rate, frames, name, cb); }
      write(b) { queue.push(b[0]); }
      clearOutputQueue() { st.cleared++; queue.length = 0; }
    };
    return { f, queue, st, take: n => { for (let i = 0; i < n && queue.length; i++) { queue.shift(); if (st.frameCb) st.frameCb(); } } };
  };
  const wasapi = { id: 1000, rtId: 5, api: 'WINDOWS_WASAPI', hostAPIName: 'Windows WASAPI', name: 'Speakers (Realtek)', maxOutputChannels: 2, maxInputChannels: 0, sampleRates: [48000] };
  const block = (n, frames) => { const b = Buffer.alloc(frames * 4); b[0] = n; return b; };
  // the device takes nothing for a while (start-up): after the limit only the newest few blocks are kept
  let t = mk(); const s = a.openStream({ mod: t.f, dev: wasapi, direction: 'output', channels: 2, sampleRate: 48000 });
  assert.strictEqual(s.frameSize, 512); assert.strictEqual(typeof t.st.frameCb, 'function', 'Audify is asked to report every block the device takes');
  for (let i = 1; i <= 60; i++) s.write(block(i, 512));
  assert.ok(s.trimmed > 0 && t.st.cleared === s.trimmed, 'backlog dropped');
  assert.ok(t.queue.length <= 15, 'at most about 150 ms waits: ' + t.queue.length);
  assert.deepStrictEqual(t.queue.slice(-3), [58, 59, 60], 'the newest blocks are the ones that stay');
  // the device now runs: it takes the blocks as fast as they arrive, nothing more is dropped
  const trimmed = s.trimmed; t.take(100);
  for (let i = 61; i <= 400; i++) { s.write(block(i & 255, 512)); t.take(1); }
  assert.strictEqual(s.trimmed, trimmed, 'a steady stream is never trimmed');
  assert.ok(t.queue.length <= 1);
  // a device that takes slightly fewer blocks than arrive (clock drift): bounded, not growing for ever
  t = mk(); const d = a.openStream({ mod: t.f, dev: wasapi, direction: 'output', channels: 2, sampleRate: 48000 });
  for (let i = 0; i < 2000; i++) { d.write(block(i & 255, 512)); if (i % 20) t.take(1); }      // 5 % too fast
  assert.ok(t.queue.length <= 15 && d.trimmed > 0 && d.trimmed < 20, `bounded (${t.queue.length} waiting, ${d.trimmed} trims in 2000 blocks)`);
  // ASIO and the other host APIs are untouched, and it can be switched off
  const asio = { id: 1001, rtId: 0, api: 'WINDOWS_ASIO', hostAPIName: 'ASIO', name: 'Focusrite USB ASIO', maxOutputChannels: 20, maxInputChannels: 18, sampleRates: [44100, 48000, 96000] };
  t = mk(); const x = a.openStream({ mod: t.f, dev: asio, direction: 'output', channels: 2, sampleRate: 48000, frameSize: 128 });
  assert.strictEqual(t.st.frameCb, null); for (let i = 0; i < 100; i++) x.write(block(i, 128)); assert.strictEqual(t.st.cleared, 0); assert.strictEqual(t.queue.length, 100);
  t = mk(); const off = a.openStream({ mod: t.f, dev: wasapi, direction: 'output', channels: 2, sampleRate: 48000, queueControl: false });
  assert.strictEqual(t.st.frameCb, null); for (let i = 0; i < 100; i++) off.write(block(i, 512)); assert.strictEqual(t.queue.length, 100);
  // input streams never use it
  t = mk(); a.openStream({ mod: t.f, dev: { ...wasapi, maxInputChannels: 2, maxOutputChannels: 0 }, direction: 'input', channels: 2, sampleRate: 48000 }); assert.strictEqual(t.st.frameCb, null);
});





test('reboot system: restart the server for real, reboot the computer only on request (dry run), commands per OS', async () => {
  const power = require('./power'), { spawn } = require('node:child_process');
  assert.strictEqual(power.rebootCommand('win32').cmd, 'shutdown.exe'); assert.ok(power.rebootCommand('win32').args.includes('/t'));          // 10 s to cancel
  assert.deepStrictEqual(power.cancelCommand('win32').args, ['/a']);
  assert.strictEqual(power.rebootCommand('linux').cmd, 'systemctl'); assert.strictEqual(power.rebootCommand('darwin').cmd, 'osascript');
  assert.strictEqual(power.rebootCommand('android'), null); assert.strictEqual(power.capabilities({ platform: 'ios' }).reboot, false);
  await assert.rejects(power.rebootOs({ platform: 'android' }), /cannot be rebooted/);
  const calls = [];
  const r = await power.rebootOs({ platform: 'linux', dry: false, run: async (c, a) => { calls.push(c); return c === 'systemctl' ? { ok: false, err: 'denied' } : { ok: true }; } });
  assert.deepStrictEqual(calls, ['systemctl', 'shutdown']); assert.ok(r.ok);                                                                    // falls back to shutdown -r now
  await assert.rejects(power.rebootOs({ platform: 'win32', dry: false, run: async () => ({ ok: false, err: 'Access is denied' }) }), /administrator/);
  assert.match((await power.rebootOs({ platform: 'win32', dry: true })).command, /^shutdown\.exe \/r \/t 10/);
  // the copy that replaces the server: same program, same arguments, no browser
  assert.deepStrictEqual(power.relaunch({ execPath: '/n', execArgv: [], argv: ['/n', '/a/server.js'], self: undefined }), { cmd: '/n', args: ['/a/server.js', '--no-open'] });
  assert.deepStrictEqual(power.relaunch({ execPath: '/x/am', execArgv: [], argv: ['/x/am', '/x/am', '--port', '9'], self: { sea: true, script: null } }), { cmd: '/x/am', args: ['--port', '9', '--no-open'] });
  // restartSelf: the port is closed first, then the copy is started, then this process exits
  const order = []; const fakeServer = { close(cb) { order.push('close'); setImmediate(cb); }, closeAllConnections() { order.push('drop'); } };
  await new Promise(res => power.restartSelf({ server: fakeServer, sockets: new Set([{ destroy() { order.push('sock'); } }]), plan: { cmd: 'x', args: ['y'] }, dry: false, delay: 1, spawnImpl: (c, a) => { order.push('spawn ' + c + ' ' + a); return { unref() {} }; }, exit: code => { order.push('exit ' + code); res(); } }));
  assert.deepStrictEqual(order, ['close', 'sock', 'drop', 'spawn x y', 'exit 0']);
  // the real thing, over HTTP: a server on its own port starts a new process of itself; the dry run of the OS reboot reports the command and needs the word REBOOT
  const port = 20000 + Math.floor(Math.random() * 20000), base = `http://127.0.0.1:${port}`, kids = [];
  const env = { ...process.env, BRIDGE_PORT: String(port), BRIDGE_POWER_DRY_RUN: '1' };
  const child = spawn(process.execPath, [pathx.join(__dirname, 'server.js')], { env, stdio: 'ignore' }); kids.push(child);
  const up = async (not) => { for (let i = 0; i < 100; i++) { try { const j = await (await fetch(base + '/api/power')).json(); if (j.ok && j.pid !== not) return j; } catch (_) { /* not yet */ } await new Promise(r => setTimeout(r, 100)); } throw new Error('server not up'); };
  try {
    const p1 = await up(-1); assert.strictEqual(p1.restart, true);
    const act = (path, action, body) => fetch(base + path, { method: 'POST', headers: { 'X-Mixer-Action': action, 'Content-Type': 'application/json' }, body: JSON.stringify(body || {}) });
    assert.strictEqual((await act('/api/reboot', 'reboot', {})).status, 400);                                                                    // no word, no reboot
    assert.strictEqual((await fetch(base + '/api/reboot', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{"confirm":"REBOOT"}' })).status, 400);   // header required
    assert.strictEqual((await fetch(base + '/api/reboot', { method: 'POST', headers: { 'X-Mixer-Action': 'reboot', 'Content-Type': 'application/json', Origin: 'https://evil.example' }, body: '{"confirm":"REBOOT"}' })).status, 403);
    const dry = await (await act('/api/reboot', 'reboot', { confirm: 'REBOOT' })).json(); assert.ok(dry.ok && dry.dry === true && /shutdown|systemctl|osascript/.test(dry.command));
    const rs = await (await act('/api/restart', 'restart')).json(); assert.ok(rs.ok && rs.dry === true);                                         // dry run: nothing restarts
    assert.strictEqual((await up(-1)).pid, p1.pid);
  } finally { for (const k of kids) k.kill(); }
  const port2 = port + 1, base2 = `http://127.0.0.1:${port2}`, env2 = { ...process.env, BRIDGE_PORT: String(port2) };
  const child2 = spawn(process.execPath, [pathx.join(__dirname, 'server.js')], { env: env2, stdio: 'ignore' });
  let newPid = 0;
  try {
    let p1 = null; for (let i = 0; i < 100 && !p1; i++) { try { p1 = await (await fetch(base2 + '/api/power')).json(); } catch (_) { await new Promise(r => setTimeout(r, 100)); } }
    assert.ok(p1);
    const rs = await (await fetch(base2 + '/api/restart', { method: 'POST', headers: { 'X-Mixer-Action': 'restart' } })).json(); assert.ok(rs.ok && rs.restarting && rs.dry === false);
    for (let i = 0; i < 120 && !newPid; i++) { await new Promise(r => setTimeout(r, 100)); try { const j = await (await fetch(base2 + '/api/power')).json(); if (j.pid !== p1.pid) newPid = j.pid; } catch (_) { /* restarting */ } }
    assert.ok(newPid && newPid !== p1.pid, 'a new process answers on the same port');
  } finally { child2.kill(); if (newPid) { try { process.kill(newPid); } catch (_) { /* gone */ } } }
});





test('license key generator: a local, form-based way to issue a key with any of the three key pairs, bound to 127.0.0.1, refuses a foreign origin, the private key never leaves the process', async () => {
  const gen = require('../scripts/license-generator'), lic = require('./license'), http = require('node:http');
  // vendor: a real key pair, a real HTTP round trip, the signed key verifies with that same public key and no other
  const vdir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'gen-vendor-'));
  require('../scripts/license.js').initKeys(vdir);
  let server = gen.createServer({ kind: 'vendor', dirArgv: ['--dir', vdir] });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  let base = `http://127.0.0.1:${server.address().port}`;
  const page = await fetch(base + '/'); const html = await page.text(), csp = page.headers.get('content-security-policy');
  assert.strictEqual(page.status, 200); assert.match(csp, /default-src 'none'/); assert.ok(!/unsafe-inline/.test(csp));
  assert.ok(!/innerHTML|outerHTML|insertAdjacentHTML|document\.write|eval\(|new Function/.test(html), 'the key and summary are only ever set as text');
  let r = await fetch(base + '/issue', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Gen-Action': '1' }, body: JSON.stringify({ plan: 'pro', name: 'Jane Doe', email: 'j@example.com', machine: 'AAAA-BBBB-CCCC-DDDD-EEEE', days: 30, seats: 2 }) });
  let j = await r.json(); assert.strictEqual(r.status, 200); assert.ok(j.ok && j.key.startsWith('AMIX1.') && /VENDOR PRO for Jane Doe/.test(j.summary));
  const vpub = require('../scripts/license.js').loadPublic(vdir);
  let v = lic.verifyKey(j.key, { jwk: vpub, machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' }); assert.ok(v.ok && v.payload.seats === 2 && v.plan.id === 'pro');
  assert.strictEqual(lic.verifyKey(j.key, { machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' }).reason, 'bad-signature');            // not the real vendor key: a normal build refuses it
  // validation: bad plan, oversized / non-JSON body, wrong method; a vendor key needs no name any more ("Customer <serial>")
  assert.strictEqual((await fetch(base + '/issue', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Gen-Action': '1' }, body: JSON.stringify({ plan: 'gold' }) })).status, 400);
  assert.strictEqual((await fetch(base + '/issue', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Gen-Action': '1' }, body: JSON.stringify({ plan: 'pro' }) })).status, 200);
  assert.strictEqual((await fetch(base + '/issue', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Gen-Action': '1' }, body: 'not json' })).status, 400);
  assert.strictEqual((await fetch(base + '/issue', { method: 'GET' })).status, 404);
  // CSRF / DNS-rebinding guards: a foreign Origin or Host is refused, the same functions bridge/server.js itself uses
  assert.strictEqual((await fetch(base + '/issue', { method: 'POST', headers: { Origin: 'https://evil.example', 'Content-Type': 'application/json', 'X-Gen-Action': '1' }, body: JSON.stringify({ plan: 'pro', name: 'x' }) })).status, 403);
  for (const h of [{}, { 'Content-Type': 'text/plain' }, { 'Content-Type': 'application/json' }, { 'Content-Type': 'application/json', 'X-Gen-Action': '1', Origin: 'null' }])   // CSRF: simple requests, missing header, sandboxed iframe
    assert.strictEqual((await fetch(base + '/issue', { method: 'POST', headers: h, body: JSON.stringify({ plan: 'pro' }) })).status, 403, JSON.stringify(h));
  const rawHost = await new Promise(res => { const req = http.request({ host: '127.0.0.1', port: server.address().port, path: '/issue', method: 'POST', headers: { Host: 'evil.example', 'Content-Type': 'application/json' } }, out => { out.resume(); out.on('end', () => res(out.statusCode)); }); req.end(JSON.stringify({ plan: 'pro', name: 'x' })); });
  assert.strictEqual(rawHost, 403);
  await new Promise(r => server.close(r));
  // sandbox: no name required, its own key pair, the vendor key does not verify it
  const sdir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'gen-sandbox-')), spub = pathx.join(sdir, 'pub.json');
  require('../scripts/license-sandbox.js').init(sdir, spub);
  server = gen.createServer({ kind: 'sandbox', dirArgv: ['--dir', sdir] }); await new Promise(r => server.listen(0, '127.0.0.1', r)); base = `http://127.0.0.1:${server.address().port}`;
  r = await fetch(base + '/issue', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Gen-Action': '1' }, body: JSON.stringify({ plan: 'studio', days: 5 }) }); j = await r.json();
  assert.strictEqual(r.status, 200); assert.ok(j.ok && /SANDBOX STUDIO/.test(j.summary));
  v = lic.verifyKey(j.key, { jwk: JSON.parse(fsx.readFileSync(spub, 'utf8')) }); assert.ok(v.ok && v.payload.sandbox === true);
  assert.strictEqual(lic.verifyKey(j.key, { jwk: vpub }).ok, false);
  await new Promise(r => server.close(r)); fsx.rmSync(sdir, { recursive: true, force: true });
  // admin: mirrors vendor (name required, admin: true on the payload), its own key pair
  const adir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'gen-admin-'));
  require('../scripts/license-admin.js').initKeys(adir);
  server = gen.createServer({ kind: 'admin', dirArgv: ['--dir', adir] }); await new Promise(r => server.listen(0, '127.0.0.1', r)); base = `http://127.0.0.1:${server.address().port}`;
  assert.strictEqual((await fetch(base + '/issue', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Gen-Action': '1' }, body: JSON.stringify({ plan: 'studio' }) })).status, 400);   // name required
  r = await fetch(base + '/issue', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Gen-Action': '1' }, body: JSON.stringify({ plan: 'studio', name: 'Reseller Client', days: 15 }) }); j = await r.json();
  v = lic.verifyKey(j.key, { jwk: require('../scripts/license-admin.js').loadPublic(adir) }); assert.ok(v.ok && v.payload.admin === true);
  await new Promise(r => server.close(r)); fsx.rmSync(adir, { recursive: true, force: true });
  // an unknown --kind is refused up front rather than silently defaulting
  assert.throws(() => gen.createServer({ kind: 'nope' }), /kind must be one of/);
  // the CLI entry point itself (not just createServer() called as a library) must stay up and actually serve a request: main() returns 0 once
  // listen() is scheduled, and process.exit(0) right after that would kill the process before the async listen() callback ever runs - exactly
  // the kind of bug a library-only test cannot catch.
  const { spawn } = require('node:child_process');
  const cli = spawn(process.execPath, [pathx.join(__dirname, '..', 'scripts', 'license-generator.js'), '--dir', vdir, '--port', '0'], { stdio: ['ignore', 'pipe', 'pipe'] });
  try {
    const url = await new Promise((res, rej) => {
      let out = ''; cli.stdout.on('data', d => { out += d; const m = /http:\/\/127\.0\.0\.1:(\d+)\//.exec(out); if (m) res('http://127.0.0.1:' + m[1] + '/'); });
      cli.on('exit', code => rej(new Error('the process exited (code ' + code + ') before it printed its address')));
      setTimeout(() => rej(new Error('no address printed within 4s')), 4000);
    });
    await new Promise(r => setTimeout(r, 50));                          // give the exit-right-after-return bug a moment to strike, if it is back
    assert.strictEqual(cli.exitCode, null, 'the server process is still running');
    const r2 = await fetch(url); assert.strictEqual(r2.status, 200);
  } finally { cli.kill(); fsx.rmSync(vdir, { recursive: true, force: true }); }
});












test('finder (client/finder.js): finds the mixer link by probing /api/status, scans the LAN only on request and only private /24 neighbours, refuses foreign Host headers and the start call without its header, static page with a hash-locked CSP', async () => {
  const f = require('../client/finder'), http = require('node:http'), crypto = require('node:crypto');
  assert.deepStrictEqual(f.parsePorts('8765-8767,9000,bad,70000'), [8765, 8766, 8767, 9000]);
  assert.ok(f.isPrivate('192.168.1.5') && f.isPrivate('10.0.0.1') && f.isPrivate('172.20.1.1') && !f.isPrivate('8.8.8.8') && !f.isPrivate('172.32.0.1'));
  // a real mixer-shaped server and a server that is not a mixer
  const mk = body => http.createServer((req, res) => { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(body)); });
  const mixer = mk({ ok: true, name: 'audio-mixer-bridge', version: '9.9.9' }), other = mk({ ok: true, name: 'something-else' });
  await Promise.all([mixer, other].map(s => new Promise(r => s.listen(0, '127.0.0.1', r))));
  const pm = mixer.address().port, po = other.address().port;
  assert.deepStrictEqual(await f.probe('127.0.0.1', pm), { version: '9.9.9' });
  assert.strictEqual(await f.probe('127.0.0.1', po), null, 'only an Audio Mixer answer counts');
  assert.strictEqual(await f.probe('127.0.0.1', 1), null);
  // the scan: local ports always, neighbours only with lan=1 (the fake network has two hosts, the mixer answers on one of them)
  const probed = [];
  const probeFn = async (h, p) => { probed.push(h + ':' + p); return h === '127.0.0.1' ? f.probe(h, p) : (h === '192.168.7.20' && p === pm ? { version: '1.0.0' } : null); };
  let started = 0;
  const app = f.createFinder({ ports: [pm, po], probeFn, lan: () => ({ own: ['192.168.7.5'], others: ['192.168.7.20', '192.168.7.21'] }), startMixer: async () => { started++; return { ok: true, message: 'Starting' }; }, cacheMs: 0 });
  await new Promise(r => app.server.listen(0, '127.0.0.1', r));
  const base = 'http://127.0.0.1:' + app.server.address().port, get = (p, h) => fetch(base + p, { headers: h });
  try {
    let r = await (await get('/api/scan')).json();
    assert.deepStrictEqual(r.found.map(x => x.url), [`http://127.0.0.1:${pm}/`]); assert.ok(r.found[0].local && r.found[0].version === '9.9.9');
    assert.ok(!probed.some(x => x.startsWith('192.168.')), 'no LAN probe without lan=1');
    r = await (await get('/api/scan?lan=1')).json();
    assert.deepStrictEqual(r.found.map(x => x.url), [`http://127.0.0.1:${pm}/`, `http://192.168.7.20:${pm}/`]);
    assert.deepStrictEqual([...new Set(probed.filter(x => x.startsWith('192.168.')))].sort(), [`192.168.7.20:${pm}`, `192.168.7.20:${po}`, `192.168.7.21:${pm}`, `192.168.7.21:${po}`].sort());
    // Host header: DNS rebinding and foreign names are refused (fetch cannot set Host, so use a raw request)
    const code = await new Promise(res => { const q = http.request({ host: '127.0.0.1', port: app.server.address().port, path: '/api/scan', headers: { Host: 'evil.example' } }, x => { x.resume(); res(x.statusCode); }); q.end(); });
    assert.strictEqual(code, 403);
    assert.strictEqual((await fetch(base + '/api/start', { method: 'POST' })).status, 403); assert.strictEqual(started, 0, 'start needs its header');
    assert.strictEqual((await (await fetch(base + '/api/start', { method: 'POST', headers: { 'X-Finder': '1' } })).json()).ok, true); assert.strictEqual(started, 1);
    assert.strictEqual((await get('/api/nothing')).status, 404);
    // the page: static, nothing from outside, no innerHTML, no animation / effects, the CSP hashes exactly its own inline script and style
    const page = await get('/'), html = await page.text(), csp = page.headers.get('content-security-policy');
    const hash = t => "'sha256-" + crypto.createHash('sha256').update(t, 'utf8').digest('base64') + "'";
    assert.ok(csp.includes('script-src ' + hash(/<script>([\s\S]*?)<\/script>/.exec(html)[1])) && csp.includes('style-src ' + hash(/<style>([\s\S]*?)<\/style>/.exec(html)[1])) && csp.includes("default-src 'none'") && !/unsafe-inline/.test(csp));
    assert.ok(!/https?:\/\//.test(html) && !/innerHTML|eval\(|animation|transition|filter:|backdrop|box-shadow|requestAnimationFrame/.test(html), 'nothing external, nothing that costs GPU time');
    assert.ok(/document\.hidden/.test(html), 'it does not poll while the tab is hidden');
  } finally { app.server.close(); mixer.close(); other.close(); }
});

test('native engine in place of Audify: --list-devices in the bridge shape, and input / output / duplex sessions stream real PCM through audiomixer-native --stream (null device)', { timeout: 240000 }, async t => {
  const cp = require('node:child_process'), osx = require('node:os'), pathx = require('node:path'), fsx = require('node:fs');
  const has = cmd => { try { return cp.spawnSync(cmd, ['--version'], { encoding: 'utf8' }).status === 0; } catch (_) { return false; } };
  if (!has('cmake') || !(has('c++') || has('g++'))) { t.skip('cmake and a C++ compiler are needed to build the engine'); return; }
  const build = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'am-engine-'));
  try {
    const root = pathx.join(__dirname, '..');
    assert.strictEqual(cp.spawnSync('cmake', ['-S', pathx.join(root, 'engine', 'cpp'), '-B', build], { encoding: 'utf8' }).status, 0);
    assert.strictEqual(cp.spawnSync('cmake', ['--build', build, '--target', 'audiomixer-native', '-j', '4'], { encoding: 'utf8' }).status, 0, 'the engine builds');
    const exe = pathx.join(build, 'audiomixer-native');
    const prev = { e: process.env.AUDIO_MIXER_ENGINE, n: process.env.AUDIO_MIXER_ENGINE_NULL };
    process.env.AUDIO_MIXER_ENGINE = exe; process.env.AUDIO_MIXER_ENGINE_NULL = '1';
    const native = require('./native-engine'), audify = require('./audify');
    native._reset();
    try {
      // the device list, in the shape audify.listDevices() gives, with Audify's id range (so the callers route these ids as before)
      const list = audify.listDevices();
      const nul = list.devices.find(d => d.engineBackend === 'null');
      assert.ok(nul && nul.id >= audify.AUDIFY_BASE && nul.hostAPIName === 'Null' && nul.maxInputChannels === 2 && nul.maxOutputChannels === 2);
      assert.strictEqual(audify.detectAudify().provider, 'native', 'the page is told the native engine made the list (its labels say NATIVE ENGINE, not Audify)');
      assert.strictEqual(native.listDevices({ includeNull: false, now: Date.now() + 60000 }).devices.some(d => d.engineBackend === 'null'), false, 'the null device is hidden from the page');
      native._reset(); audify.listDevices();
      const until = async (f, ms = 8000) => { const t0 = Date.now(); while (!f()) { if (Date.now() - t0 > ms) return false; await new Promise(r => setTimeout(r, 25)); } return true; };
      const conn = () => { const c = { msgs: [], bytes: 0, chunks: [], closed: false }; c.send = m => c.msgs.push(JSON.parse(m)); c.sendBinary = b => { c.bytes += b.length; c.chunks.push(b.length); }; c.close = () => { c.closed = true; }; return c; };
      // input: the bridge's /ws/input session reads the null device through the engine
      const ci = conn(), si = require('./input').createInputSession(ci, () => { throw new Error('no PortAudio'); });
      si.onText(JSON.stringify({ type: 'start', deviceId: nul.id, channels: 2, sampleRate: 48000 }));
      const started = ci.msgs.find(m => m.type === 'started');
      assert.ok(started && started.engine === 'native' && started.hostApi === 'Null', JSON.stringify(ci.msgs));
      assert.ok(await until(() => ci.bytes >= 48000 * 4 / 4), 'a quarter second of captured PCM reaches the page');
      assert.ok(ci.chunks.every(n => n % 4 === 0), 'every buffer holds whole stereo frames');
      si.onText(JSON.stringify({ type: 'stop' }));
      // output: the /ws/output session plays PCM from the page through the engine
      const co = conn(), so = require('./output').createSession(co, () => { throw new Error('no PortAudio'); });
      so.onText(JSON.stringify({ type: 'start', deviceId: nul.id, channels: 2, sampleRate: 48000 }));
      assert.ok(co.msgs.some(m => m.type === 'started' && m.engine === 'native'), JSON.stringify(co.msgs));
      for (let i = 0; i < 5; i++) so.onBinary(Buffer.alloc(4800 * 4));
      await new Promise(r => setTimeout(r, 300));
      assert.ok(!co.msgs.some(m => m.type === 'error'), JSON.stringify(co.msgs));
      so.onText(JSON.stringify({ type: 'stop' }));
      // duplex: one engine process reads and writes the same device
      const cd = conn(), sd = require('./duplex').createDuplexSession(cd, () => { throw new Error('no PortAudio'); });
      sd.onText(JSON.stringify({ type: 'start', deviceId: nul.id, channels: 2, inChannels: 2, sampleRate: 48000 }));
      assert.ok(cd.msgs.some(m => m.type === 'started' && m.duplex === true), JSON.stringify(cd.msgs));
      sd.onBinary(Buffer.alloc(4800 * 4));
      assert.ok(await until(() => cd.bytes > 0), 'duplex captures as it plays');
      sd.onText(JSON.stringify({ type: 'stop' }));
      assert.ok(await until(() => native._state.open.size === 0), 'every engine process is let go');
      // ALSA devices come from the names ALSA advertises, each opened for a moment for its real channel count; a layer that converts any count (here the
      // null plugin, the only kind this machine has: no sound card) keeps 2. Only when the engine was built with ALSA.
      if (/\balsa\b/.test(cp.spawnSync(exe, ['--list'], { encoding: 'utf8' }).stdout)) {
        const home = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'am-alsa-'));
        fsx.writeFileSync(pathx.join(home, '.asoundrc'), 'pcm.amtest {\n  type null\n  hint { show on description "AmF X1 test device" }\n}\n');
        const r = cp.spawnSync(exe, ['--list-devices'], { encoding: 'utf8', env: { ...process.env, HOME: home } });
        fsx.rmSync(home, { recursive: true, force: true });
        const d = JSON.parse(r.stdout).find(x => x.backend === 'alsa' && x.id === 'amtest');
        assert.ok(d && d.name === 'AmF X1 test device' && d.inChannels === 2 && d.outChannels === 2 && d.defaultRate === 48000, r.stdout);
      }
      // a single-client driver (ASIO; the null device stands in for it here): WRITE then READ on the same device share ONE engine process, restarted as duplex
      native._shared.add('null');
      try {
        const key = 'null\u0000null';
        const cw = conn(), sw = require('./output').createSession(cw, () => { throw new Error('no PortAudio'); });
        sw.onText(JSON.stringify({ type: 'start', deviceId: nul.id, channels: 2, sampleRate: 48000 }));
        assert.ok(cw.msgs.some(m => m.type === 'started' && m.engine === 'native'), JSON.stringify(cw.msgs));
        assert.ok(await until(() => native._state.open.size === 1), 'WRITE runs one engine process');
        const cr = conn(), sr = require('./input').createInputSession(cr, () => { throw new Error('no PortAudio'); });
        sr.onText(JSON.stringify({ type: 'start', deviceId: nul.id, channels: 2, sampleRate: 48000 }));
        assert.ok(cr.msgs.some(m => m.type === 'started' && m.engine === 'native'), 'READ on the device WRITE holds is accepted, not refused: ' + JSON.stringify(cr.msgs));
        assert.ok(await until(() => cr.bytes > 0 && native._state.open.size === 1), 'READ gets audio while one process (duplex) serves both');
        sw.onBinary(Buffer.alloc(4800 * 4));
        assert.ok(!cw.msgs.some(m => m.type === 'error') && !cr.msgs.some(m => m.type === 'error'), JSON.stringify([cw.msgs, cr.msgs]));
        const rd = conn(), sx = require('./input').createInputSession(rd, () => { throw new Error('no PortAudio'); });
        sx.onText(JSON.stringify({ type: 'start', deviceId: nul.id, channels: 2, sampleRate: 48000 }));
        assert.ok(rd.msgs.some(m => m.type === 'error' && /already open for reading/.test(m.message)), 'a second reader of the same driver is refused: ' + JSON.stringify(rd.msgs));
        sr.onText(JSON.stringify({ type: 'stop' }));
        assert.ok(await until(() => native._groups.get(key) && !native._groups.get(key).users.input && native._state.open.size === 1), 'closing READ keeps WRITE running');
        sw.onText(JSON.stringify({ type: 'stop' }));
        assert.ok(await until(() => native._state.open.size === 0 && !native._groups.has(key)), 'closing both lets the driver go');
      } finally { native._shared.delete('null'); }
    } finally {
      native._reset();
      if (prev.e === undefined) delete process.env.AUDIO_MIXER_ENGINE; else process.env.AUDIO_MIXER_ENGINE = prev.e;
      if (prev.n === undefined) delete process.env.AUDIO_MIXER_ENGINE_NULL; else process.env.AUDIO_MIXER_ENGINE_NULL = prev.n;
    }
  } finally { fsx.rmSync(build, { recursive: true, force: true }); }
});

test('remote control (control.js): GET /api/audio-devices, POST /api/select-device and the /ws/control board (volume, mute, linked pairs, meters), all behind the local-only checks', async () => {
  const control = require('./control'), lv = require('./levels');
  control.reset();
  // selection: only a driver that is on this PC, matched without case
  assert.throws(() => control.select('', ['Wing ASIO']), /deviceName is required/);
  assert.throws(() => control.select('Focusrite USB ASIO', ['Wing ASIO']), e => e.status === 404);
  assert.strictEqual(control.select('wing asio', ['Wing ASIO']), 'Wing ASIO'); assert.strictEqual(control.selected(), 'Wing ASIO');
  // the board: at unity and unmuted the buffer passes through untouched; volume, dB fader and mute scale with clamping
  const pcm = Buffer.alloc(8); pcm.writeInt16LE(20000, 0); pcm.writeInt16LE(20000, 2); pcm.writeInt16LE(-20000, 4); pcm.writeInt16LE(1000, 6);
  assert.strictEqual(control.apply(pcm, 2), pcm);
  control.setVolume(0, 0.5); control.setFader(1, 10);
  const g = control.apply(pcm, 2);
  assert.strictEqual(g.readInt16LE(0), 10000); assert.strictEqual(g.readInt16LE(2), 32767); assert.strictEqual(g.readInt16LE(6), Math.round(1000 * Math.pow(10, 0.5)));
  control.setMute(0, true); assert.strictEqual(control.apply(pcm, 2).readInt16LE(0), 0); control.setMute(0, false); assert.strictEqual(control.gain(0), 0.5);
  control.setFader(0, -90); assert.strictEqual(control.gain(0), 0);
  for (const [ch, v] of [[-1, 0.5], [32, 0.5], [1.5, 0.5], [0, -0.1], [0, 3.2], [0, 'x']]) assert.throws(() => control.setVolume(ch, v));
  assert.throws(() => control.setFader(0, 11)); assert.throws(() => control.setMute(0, 1)); assert.throws(() => control.toggleLock(2, 2)); assert.throws(() => control.toggleLock(0, 40));
  // linked pairs: linking copies ch1 to ch2, then one moves both; toggling again unlinks; linking elsewhere breaks the old pair
  control.reset(); control.setVolume(4, 0.25);
  assert.strictEqual(control.toggleLock(4, 5)[4], 5); assert.strictEqual(control.gain(5), 0.25);
  assert.deepStrictEqual(control.setVolume(5, 0.75), [5, 4]); assert.strictEqual(control.gain(4), 0.75);
  control.setMute(4, true); assert.strictEqual(control.gain(5), 0);
  control.toggleLock(5, 6); assert.strictEqual(control.state().locks[4], -1); assert.strictEqual(control.state().locks[5], 6);
  const l = control.toggleLock(5, 6); assert.strictEqual(l[5], -1); assert.strictEqual(l[6], -1);
  control.reset();
  // an output stream opens the selected ASIO driver when the page names none, and sends what the board made of the audio
  const written = []; let opened = null;
  const fakePa = { SampleFormat16Bit: 8, getDevices: () => [{ id: 5, name: 'Wing ASIO', hostAPIName: 'ASIO', maxOutputChannels: 32, maxInputChannels: 32 }, { id: 7, name: 'Focusrite USB ASIO', hostAPIName: 'ASIO', maxOutputChannels: 8, maxInputChannels: 8 }],
    AudioIO: class { constructor(o) { opened = o; this.h = {}; } on(e, f) { this.h[e] = f; } start() {} write(b) { written.push(Buffer.from(b)); return true; } quit() {} } };
  control.select('Focusrite USB ASIO', ['Wing ASIO', 'Focusrite USB ASIO']); control.setMute(1, true);
  const s = require('./output').createSession({ send() {} }, () => fakePa);
  s.onText(JSON.stringify({ type: 'start', channels: 2, sampleRate: 48000 }));
  assert.strictEqual(opened.outOptions.deviceId, 7);
  s.onBinary(pcm); assert.strictEqual(written[0].readInt16LE(0), 20000); assert.strictEqual(written[0].readInt16LE(2), 0, 'channel 1 muted');
  s.onClose();
  // a duplex stream with monitor: the input goes straight to the output through the board, and the page's frames are ignored
  written.length = 0; let io = null;
  const dpa = { ...fakePa, AudioIO: class extends fakePa.AudioIO { constructor(o) { super(o); io = this; } } };
  const d = require('./duplex').createDuplexSession({ send() {}, sendBinary() {} }, () => dpa);
  d.onText(JSON.stringify({ type: 'start', deviceId: 5, channels: 2, inChannels: 2, sampleRate: 48000, monitor: true }));
  io.h.data(pcm); assert.strictEqual(written.length, 1); assert.strictEqual(written[0].readInt16LE(0), 20000); assert.strictEqual(written[0].readInt16LE(2), 0);
  d.onBinary(pcm); assert.strictEqual(written.length, 1, 'page frames ignored while monitoring');
  d.onClose(); control.reset();
  // the routes and the socket
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const sockets = [];
  try {
    const port = server.address().port, base = `http://127.0.0.1:${port}`;
    const post = (body, headers = { 'X-Mixer-Action': 'select-device' }, origin) => fetch(base + '/api/select-device', { method: 'POST', headers: { 'Content-Type': 'application/json', ...(origin ? { Origin: origin } : {}), ...headers }, body: JSON.stringify(body) });
    const list = await (await fetch(base + '/api/audio-devices')).json();
    assert.strictEqual(list.ok, true); assert.ok(Array.isArray(list.devices)); assert.strictEqual(list.selected, null);
    assert.strictEqual((await post({ deviceName: 'x' }, {})).status, 400, 'the custom header is required');
    assert.strictEqual((await post({ deviceName: 'x' }, undefined, 'https://evil.example')).status, 403);
    assert.strictEqual((await post({})).status, 400);
    if (!list.devices.includes('No Such ASIO Driver')) assert.strictEqual((await post({ deviceName: 'No Such ASIO Driver' })).status, 404);
    assert.strictEqual(control.selected(), null);
    const until = async (f, ms = 3000) => { const t0 = Date.now(); while (!f()) { if (Date.now() - t0 > ms) throw new Error('timeout'); await new Promise(r => setTimeout(r, 10)); } };
    const open = async () => { const ws = new WebSocket(`ws://127.0.0.1:${port}/ws/control`), inbox = []; ws.onmessage = e => inbox.push(JSON.parse(e.data)); await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; }); sockets.push(ws); await until(() => inbox.length); return { ws, inbox }; };
    const a = await open(), b = await open();
    assert.strictEqual(a.inbox[0].type, 'INIT'); assert.strictEqual(a.inbox[0].data.volumes.length, 32); assert.strictEqual(a.inbox[0].data.locks[0], -1);
    a.ws.send(JSON.stringify({ type: 'TOGGLE_LOCK', data: { ch1: 2, ch2: 3 } }));
    a.ws.send(JSON.stringify({ type: 'SET_VOLUME', data: { channel: 2, value: 0.1 } }));
    await until(() => b.inbox.filter(m => m.type === 'VOLUME_CHANGED' && m.data.value === 0.1).length === 2);
    assert.ok(b.inbox.some(m => m.type === 'LOCK_CHANGED' && m.data.locks[2] === 3), 'every page hears the link');
    assert.strictEqual(control.gain(3), 0.1);
    a.ws.send(JSON.stringify({ type: 'FADER_CHANGE', channel: 7, value: -20 })); await until(() => Math.abs(control.gain(7) - 0.1) < 1e-12);
    a.ws.send(JSON.stringify({ type: 'SET_MUTE', data: { channel: 7, muted: true } })); await until(() => b.inbox.some(m => m.type === 'MUTE_CHANGED' && m.data.channel === 7));
    for (const bad of ['not json', '[]', '{"type":"SET_VOLUME","data":{"channel":99,"value":0.5}}', '{"type":"NOPE"}']) a.ws.send(bad);
    await until(() => a.inbox.filter(m => m.type === 'error').length === 4);
    const m = lv.attach({ send() {} }, { sid: 9, direction: 'output', device: 'Wing ASIO', hostApi: 'ASIO', engine: 'audify', sampleRate: 48000, channels: 2 }, { interval: 20 });
    m.push(pcm);
    await until(() => a.inbox.some(x => x.type === 'METERS' && x.data.some(d => d.device === 'Wing ASIO')));
    m.stop();
    const foreign = await new Promise(res => { const sk = require('node:net').connect(port, '127.0.0.1', () => sk.write(`GET /ws/control HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nOrigin: https://evil.example\r\n\r\n`)); let got = ''; sk.on('data', d => { got += d; }); sk.on('close', () => res(!/ 101 /.test(got))); setTimeout(() => sk.destroy(), 800); });
    assert.ok(foreign, 'a foreign page cannot open /ws/control');
  } finally { sockets.forEach(w => w.close()); control.reset(); server.closeAllConnections(); server.close(); }
});

test('native engine keeps ASIO and Windows sound apart on one onboard chip (Realtek): WASAPI stays shared next to ASIO, ASIO refuses an exclusive lock', () => {
  const ne = require('./native-engine');
  const { EventEmitter } = require('node:events');
  ne._reset();
  const args = [];
  const spawnImpl = (f, a) => { args.push(a); const c = new EventEmitter(); c.stdout = new EventEmitter(); c.stderr = new EventEmitter(); c.stdin = new EventEmitter(); c.stdin.write = () => {}; c.stdin.end = () => {}; c.kill = () => {}; return c; };
  const [asio, spk, usb] = ne.toDevices([
    { backend: 'asio', id: 'Realtek ASIO', name: 'Realtek ASIO', inChannels: 2, outChannels: 2 },
    { backend: 'wasapi', id: '{0.0.0}.{a}', name: 'Speakers (Realtek(R) Audio)', outChannels: 2 },
    { backend: 'wasapi', id: '{0.0.0}.{b}', name: 'Speakers (USB Audio)', outChannels: 2 },
  ], 1000);
  assert.strictEqual(asio.hardware, 'realtek'); assert.strictEqual(spk.hardware, 'realtek'); assert.strictEqual(usb.hardware, null);
  assert.strictEqual(ne.hardwareFamily('ASIO4ALL v2'), null); assert.strictEqual(ne.toDevices([{ backend: 'asio', name: 'ASIO4ALL v2' }], 0)[0].hardware, 'wdm');
  // an exclusive Windows stream on the chip blocks Realtek ASIO with a clear message
  const ex = ne.openStream({ dev: spk, direction: 'output', channels: 2, exclusive: true, exe: 'x', spawnImpl });
  assert.strictEqual(ex.exclusive, true);
  assert.throws(() => ne.openStream({ dev: asio, direction: 'input', channels: 2, exe: 'x', spawnImpl }), /exclusive mode/);
  ex.close();
  // with ASIO open, music on the same chip is opened shared; another device keeps exclusive
  ne.openStream({ dev: asio, direction: 'input', channels: 2, exe: 'x', spawnImpl });
  const music = ne.openStream({ dev: spk, direction: 'output', channels: 2, exclusive: true, exe: 'x', spawnImpl });
  assert.strictEqual(music.exclusive, false); assert.strictEqual(music.separated, true); assert.ok(!args[args.length - 1].includes('--exclusive'));
  const other = ne.openStream({ dev: usb, direction: 'output', channels: 2, exclusive: true, exe: 'x', spawnImpl });
  assert.strictEqual(other.exclusive, true);
  ne._reset();
});






test('license key vault: PIN-locked folder (AES-256-GCM, scrypt), only the encrypted copy on disk, lock / change-PIN, session lockout after 5 wrong PINs', () => {
  const vault = require('../scripts/license-vault'), L = require('../scripts/license'), gen = require('../scripts/license-generator'), lic = require('./license');
  const d = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'vault-'));
  try {
    assert.throws(() => L.initKeys(d, { pin: '123' }), /at least 6/);
    L.initKeys(d, { pin: '482916' });
    assert.deepStrictEqual(fsx.readdirSync(d).sort(), ['private.pem.enc', 'public.jwk.json']);
    const box = JSON.parse(fsx.readFileSync(pathx.join(d, 'private.pem.enc'), 'utf8'));
    assert.strictEqual(box.alg, 'aes-256-gcm'); assert.strictEqual(box.kdf, 'scrypt'); assert.ok(box.N >= 1 << 17);
    assert.throws(() => L.issue({ dir: d, plan: 'pro' }), /locked/);
    assert.throws(() => L.loadPrivate(d, '000000'), /wrong PIN/);
    assert.ok(lic.verifyKey(L.issue({ dir: d, plan: 'pro', pin: '482916' }).key, { jwk: L.loadPublic(d) }).ok);
    const tampered = { ...box, ct: Buffer.from(box.ct, 'base64').map((b, i) => (i === 5 ? b ^ 1 : b)).toString('base64') };
    assert.throws(() => vault.decrypt(tampered, '482916'), /wrong PIN/, 'a changed file is detected');
    vault.changePin(d, '482916', 'a longer passphrase');
    assert.throws(() => L.loadPrivate(d, '482916')); assert.ok(L.loadPrivate(d, 'a longer passphrase'));
    // the generator session: 5 wrong PINs -> 60 s lockout (even for the right PIN), then it opens; idle 15 min closes it
    let now = 0; const s = gen.makeSession(() => now);
    for (let i = 0; i < 5; i++) assert.throws(() => s.unlock(d, 'wrong-pin'), /wrong PIN/);
    assert.throws(() => s.unlock(d, 'a longer passphrase'), e => e.status === 429);
    now += 61000; s.unlock(d, 'a longer passphrase'); assert.ok(s.isOpen() && s.key(d));
    now += 15 * 60 * 1000 + 1; assert.ok(!s.isOpen()); assert.throws(() => s.key(d), /locked/);
  } finally { fsx.rmSync(d, { recursive: true, force: true }); }
  const d2 = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'vault-'));
  try { L.initKeys(d2); vault.lock(d2, '135790'); assert.deepStrictEqual(fsx.readdirSync(d2).sort(), ['private.pem.enc', 'public.jwk.json']); assert.ok(L.loadPrivate(d2, '135790')); }
  finally { fsx.rmSync(d2, { recursive: true, force: true }); }
});


test('generator + local OTA: PUBLISH signs update.json with the unlocked key, serves only published files (Range), takes key requests from the app into the INBOX', async () => {
  const gen = require('../scripts/license-generator'), L = require('../scripts/license'), up = require('./update'), http = require('node:http');
  const tmp = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'gota-')), vdir = pathx.join(tmp, 'v'), root = pathx.join(tmp, 'proj'), rel = pathx.join(tmp, 'rel');
  fsx.mkdirSync(pathx.join(root, 'releases'), { recursive: true }); fsx.mkdirSync(rel);
  fsx.writeFileSync(pathx.join(root, 'package.json'), JSON.stringify({ version: '1.22.0.1' }));
  fsx.writeFileSync(pathx.join(rel, 'AmF X1-1.22.0.1.exe'), Buffer.alloc(5000, 7)); fsx.writeFileSync(pathx.join(rel, 'secret.txt'), 'not published');
  L.initKeys(vdir, { pin: '482916' }); const jwk = L.loadPublic(vdir);
  const srv = gen.createServer({ kind: 'vendor', dirArgv: ['--dir', vdir], root, releases: rel, acceptRequests: true });   // --accept-requests: older copies' submit path
  await new Promise(r => srv.listen(0, '127.0.0.1', r)); const port = srv.address().port;
  const call = (method, p, body, headers = {}) => new Promise((res, rej) => {
    const rq = http.request({ host: '127.0.0.1', port, method, path: p, headers: { 'Content-Type': 'application/json', 'X-Gen-Action': '1', Host: '127.0.0.1:' + port, ...headers } }, r => { const c = []; r.on('data', d => c.push(d)); r.on('end', () => { const b = Buffer.concat(c); let j = null; try { j = JSON.parse(b.toString('utf8')); } catch (_) { /* binary */ } res({ status: r.statusCode, h: r.headers, j, b }); }); });
    rq.on('error', rej); if (body) rq.write(typeof body === 'string' ? body : JSON.stringify(body)); rq.end();
  });
  try {
    assert.strictEqual((await call('GET', '/update.json')).status, 404);
    assert.strictEqual((await call('POST', '/ota/publish', {})).status, 401, 'publishing signs: the PIN lock applies');
    assert.strictEqual((await call('POST', '/unlock', { pin: '482916' })).status, 200);
    let r = await call('POST', '/ota/publish', { repo: true }); assert.strictEqual(r.status, 200); assert.strictEqual(r.j.files, 1);
    r = await call('GET', '/update.json', null, { Origin: 'http://localhost:8765' });
    assert.strictEqual(r.h['access-control-allow-origin'], '*');
    const v = up.verifyManifest(r.j, jwk); assert.strictEqual(v.ok, true, 'signed with the vendor key');
    const m = JSON.parse(r.j.payload);
    assert.strictEqual(m.version, '1.22.0.1'); assert.strictEqual(m.files['win-x64-exe'].url, `http://127.0.0.1:${port}/releases/AmF%20X1-1.22.0.1.exe`);
    assert.ok(fsx.existsSync(pathx.join(root, 'releases', 'update.json')), 'also written for GitHub');
    r = await call('GET', '/releases/AmF%20X1-1.22.0.1.exe'); assert.strictEqual(r.status, 200); assert.strictEqual(r.b.length, 5000);
    r = await call('GET', '/releases/AmF%20X1-1.22.0.1.exe', null, { Range: 'bytes=4000-' }); assert.strictEqual(r.status, 206); assert.strictEqual(r.b.length, 1000);
    assert.strictEqual((await call('GET', '/releases/secret.txt')).status, 404, 'only files the manifest lists');
    assert.strictEqual((await call('GET', '/releases/..%2Fv%2Fprivate.pem.enc')).status, 404);
    // an older copy's vendor-submit button, from a page origin (only with --accept-requests)
    assert.strictEqual((await call('OPTIONS', '/request-license', null, { Origin: 'null' })).status, 204);
    r = await call('POST', '/request-license', { app: 'audio-mixer', kind: 'license-request', v: 1, machineId: 'CC2B-A1AC-00B9-85AD-6C4D', plan: 'master', name: 'Amir <b>', email: 'a@example.com' }, { Origin: 'null' });
    assert.strictEqual(r.status, 200);
    assert.strictEqual((await call('POST', '/request-license', { kind: 'other' })).status, 400);
    r = await call('GET', '/ota/status'); assert.strictEqual(r.j.requests.length, 1); assert.strictEqual(r.j.requests[0].machineId, 'CC2B-A1AC-00B9-85AD-6C4D'); assert.strictEqual(r.j.requests[0].name, 'Amir b');
    assert.strictEqual(r.j.published.version, '1.22.0.1');
    // the page and the key routes still refuse a foreign origin
    assert.strictEqual((await call('POST', '/issue', { plan: 'pro' }, { Origin: 'https://evil.example' })).status, 403);
    assert.strictEqual((await call('GET', '/update.json', null, { Host: 'evil.example' })).status, 403, 'DNS-rebinding guard');
  } finally { srv.close(); fsx.rmSync(tmp, { recursive: true, force: true }); }
  const page = gen.page('vendor'); assert.ok(page.includes('id="inbox-card"') && page.includes('PUBLISH UPDATE'));
});





test('device scans stay light: the engine list refreshes in the background at most every 30 s (no ASIO reload per scan), SCAN NOW probes at once, the OS scan (PowerShell on Windows) is reused 30 s', async () => {
  const ne = require('./native-engine'); ne._reset();
  let sync = 0, bg = 0, pending = null;
  const out = JSON.stringify([{ backend: 'asio', id: 'X', name: 'X ASIO', inChannels: 2, outChannels: 2 }]);
  const run = () => { sync++; return out; }, runAsync = (exe, args, o, cb) => { bg++; pending = () => cb(null, out); };
  const t = 1e6, o = { exe: 'x', run, runAsync };
  assert.strictEqual(ne.listDevices({ ...o, now: t }).devices.length, 1); assert.strictEqual(sync, 1);   // the first list is probed
  for (let i = 1; i <= 5; i++) ne.listDevices({ ...o, now: t + i * 5000 });                           // 25 s of page scans
  assert.strictEqual(sync + bg, 1, 'no probe inside 30 s');
  ne.listDevices({ ...o, now: t + 31000 }); ne.listDevices({ ...o, now: t + 32000 });
  assert.strictEqual(bg, 1, 'one background refresh, never two at once'); assert.strictEqual(sync, 1, 'the scan itself never blocks');
  pending(); ne.listDevices({ ...o, now: t + 40000 }); assert.strictEqual(bg, 1, 'the 30 s restart with the refresh');
  ne.invalidate(); ne.listDevices({ ...o, now: t + 41000 }); assert.strictEqual(sync, 2, 'SCAN NOW / device change probes at once');
  ne._reset();
  const det = fsx.readFileSync(pathx.join(__dirname, 'detect.js'), 'utf8');
  assert.ok(det.includes('const OS_SCAN_MS = 30000;') && det.includes('if (force) nativeEngine.invalidate();') && det.includes('const r = await osScan(force);'));
});



test('perf controller: AMD / Intel detection, post-GC growth and detached-context traps, report format, optional hw_perf_engine.node (never estimated)', () => {
  const pc = require('../scripts/perf-controller');
  assert.strictEqual(pc.detectCpu({ cpus: [{ model: 'AMD Ryzen 9 7950X 16-Core Processor' }], env: {} }).vendor, 'AMD');
  assert.strictEqual(pc.detectCpu({ cpus: [{ model: 'Intel(R) Core(TM) i7-12700K' }], env: {} }).vendor, 'Intel');
  assert.match(pc.detectCpu({ cpus: [{ model: '' }], env: { PROCESSOR_IDENTIFIER: 'AMD64 Family 25 Model 33, AuthenticAMD' } }).profile, /uProf/);
  const t = pc.makeTraps({ growthPct: 15, samples: 2 }), s = (heapUsed, detachedContexts = 0) => ({ heapUsed, detachedContexts });
  assert.deepStrictEqual(t(s(100)), []); assert.deepStrictEqual(t(s(120)), []);
  assert.match(t(s(130))[0], /post-GC heap grew 30% over its baseline for 2 samples/);
  assert.match(t(s(130, 2)).join(), /detached contexts rose to 2/);
  const base = { rss: 100 * 1048576, heapUsed: 20 * 1048576, heapLimit: 8192 * 1048576, detachedContexts: 1 };
  const off = pc.formatReport({ s: base, cpu: { vendor: 'AMD' }, hw: null, artifact: './profiles/AMD-x.heapsnapshot' });
  assert.match(off, /^Runtime Metrics: RSS: 100\.0 MB \| Heap Used: 20\.0 MB \/ 8192\.0 MB \| Detached Contexts: 1\nHardware Telemetry: Engine Active: AMD \| L3 Miss Rate: not available/);
  assert.match(off, /Artifact Path: Saved to \.\/profiles\/AMD-x\.heapsnapshot$/);
  assert.match(pc.formatReport({ s: base, cpu: { vendor: 'Intel' }, hw: { l3MissRate: 3.456, busBandwidthMBs: 18000.4, busStalls: 12 } }), /L3 Miss Rate: 3\.46% \| Bus Bandwidth: 18000 MB\/s \| Bus Stalls: 12/);
  const tmp = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'hw-')); fsx.writeFileSync(pathx.join(tmp, 'hw_perf_engine.node'), 'x');
  const hw = pc.loadHardware({ dirs: [tmp], req: () => ({ sample: () => ({ l3MissRate: 1 }) }) }); assert.strictEqual(hw.sample().l3MissRate, 1);
  assert.strictEqual(pc.loadHardware({ dirs: [pathx.join(tmp, 'none')] }), null); fsx.rmSync(tmp, { recursive: true, force: true });
});


test('ASIO with no input / output, or failing at start, falls back to the same hardware through WASAPI (shared); matching by chip family, name, then default', () => {
  const ne = require('./native-engine'); const { EventEmitter } = require('node:events');
  ne._reset();
  const spawned = [];
  const spawnImpl = (f, a) => { const c = new EventEmitter(); c.stdout = new EventEmitter(); c.stderr = new EventEmitter(); c.stdin = new EventEmitter(); c.stdin.write = () => {}; c.stdin.end = () => {}; c.kill = () => {}; c.args = a; spawned.push(c); return c; };
  const raw = [
    { backend: 'asio', id: 'Realtek ASIO', name: 'Realtek ASIO', inChannels: 0, outChannels: 2 },
    { backend: 'asio', id: 'Focusrite USB ASIO', name: 'Focusrite USB ASIO', inChannels: 2, outChannels: 2 },
    { backend: 'wasapi', id: 'w1', name: 'Microphone (Realtek(R) Audio)', inChannels: 2 },
    { backend: 'wasapi', id: 'w2', name: 'Analogue 1 + 2 (Focusrite USB Audio)', inChannels: 2 },
    { backend: 'wasapi', id: 'w3', name: 'Speakers (Focusrite USB Audio)', outChannels: 2 },
    { backend: 'wasapi', id: 'w4', name: 'Headset Mic (Bluetooth)', inChannels: 1, defaultIn: true },
  ];
  ne._state.list = { devices: ne.toDevices(raw, 1000) }; ne._state.at = Date.now();
  const [rtAsio, fAsio] = ne._state.list.devices;
  // matching
  assert.strictEqual(ne.wasapiFallback(rtAsio, 'input').name, 'Microphone (Realtek(R) Audio)', 'same chip family');
  assert.strictEqual(ne.wasapiFallback(fAsio, 'input').name, 'Analogue 1 + 2 (Focusrite USB Audio)', 'name words');
  assert.strictEqual(ne.wasapiFallback({ name: 'ASIO4ALL v2' }, 'input').name, 'Headset Mic (Bluetooth)', 'the default device last');
  // 1) no inputs on the ASIO driver: the input opens on WASAPI, shared, and says so
  const a = ne.openStream({ dev: rtAsio, direction: 'input', channels: 2, exe: 'x', spawnImpl });
  assert.deepStrictEqual([a.fallback.via, a.fallback.to, a.exclusive], ['wasapi', 'Microphone (Realtek(R) Audio)', false]);
  assert.ok(spawned[0].args.includes('wasapi') && spawned[0].args.includes('w1'));
  assert.throws(() => ne.openStream({ dev: rtAsio, direction: 'input', channels: 2, exe: 'x', spawnImpl, wasapiFallback: false }), /no input channels/);
  // 2) the ASIO driver errors right after starting (no sound yet): the same stream object switches to WASAPI
  let switched = null; const errs = [];
  const b = ne.openStream({ dev: fAsio, direction: 'output', channels: 2, exe: 'x', spawnImpl, onFallback: f => { switched = f; }, onError: e => errs.push(e) });
  const asioProc = spawned[spawned.length - 1]; assert.ok(asioProc.args.includes('asio'));
  asioProc.stderr.emit('data', JSON.stringify({ type: 'error', message: 'ASIO driver is busy (used by another program)' }) + '\n');
  assert.ok(switched && /busy/.test(switched.reason) && switched.to === 'Speakers (Focusrite USB Audio)');
  assert.strictEqual(b.fallback.via, 'wasapi'); assert.strictEqual(errs.length, 0, 'the caller sees a switch, not an error');
  assert.ok(spawned[spawned.length - 1].args.includes('w3'));
  ne._reset();
});


test('interfaces: an ASIO interface with no input or output gets the missing side from the same hardware on WASAPI (never loopback or Bluetooth)', () => {
  const i = require('./interfaces'), dev = (id, name, hostApi, inputs, outputs) => ({ id, name, hostApi, inputs, outputs });
  const r = i.groupInterfaces([dev(1, 'Realtek ASIO', 'ASIO', 2, 0), dev(2, 'Speakers (Realtek(R) Audio)', 'Windows WASAPI', 0, 2), dev(7, 'Stereo Mix (Realtek(R) Audio)', 'Windows WASAPI', 2, 0),
    dev(8, 'Headphones (WH-1000XM4 Stereo)', 'Windows WASAPI', 0, 2), dev(9, 'Lonely ASIO', 'ASIO', 0, 0)], []);
  const rt = r.find(x => x.name === 'Realtek ASIO');
  assert.strictEqual(rt.read.api, 'ASIO'); assert.deepStrictEqual([rt.write.api, rt.write.deviceId, rt.write.fallback], ['Windows WASAPI', 2, 'wasapi']);
  assert.strictEqual(rt.outputs, 2); assert.strictEqual(rt.fallback.via, 'wasapi');
  const lonely = r.find(x => x.name === 'Lonely ASIO'); assert.strictEqual(lonely.write, null, 'no matching hardware: nothing is guessed');
  assert.ok(!r.some(x => x.asio && x.write && x.write.deviceId === 8), 'Bluetooth is never a fallback');
});


test('public key-request listener: IPv6 /64 grouping, bounded rate table, per-sender daily cap', () => {
  const gen = require('../scripts/license-generator');
  assert.strictEqual(gen.senderKey('2001:db8:1:2:aaaa::1'), gen.senderKey('2001:db8:1:2:bbbb::9'));
  assert.notStrictEqual(gen.senderKey('2001:db8:1:3::1'), gen.senderKey('2001:db8:1:2::1'));
  assert.strictEqual(gen.senderKey('::ffff:1.2.3.4'), '1.2.3.4');
  let t = 0; const lim = gen.rateLimiter(2, () => t, 10);
  assert.ok(lim('1.1.1.1') && lim('1.1.1.1')); assert.strictEqual(lim('1.1.1.1'), false);
  for (let i = 0; i < 8; i++) lim('9.9.9.' + i);               // other senders do not reset this one's window (no global clear)
  assert.strictEqual(lim('1.1.1.1'), false);
  for (let i = 0; i < 50; i++) lim('8.8.8.' + i);
  assert.ok(lim('8.8.8.49') !== undefined);                      // the table stays bounded under a flood of new senders
  t = 61000; assert.ok(lim('1.1.1.1'));
  const cap = gen.dailyCap(2, () => t);
  assert.ok(cap('2001:db8::1') && cap('2001:db8::2')); assert.strictEqual(cap('2001:db8::3'), false);
  t += 86400001; assert.ok(cap('2001:db8::4'));
});

test('public key API behind nginx: X-Real-IP is trusted only with trustProxy over loopback', async () => {
  const gen = require('../scripts/license-generator'), os = require('node:os'), pathx = require('node:path'), fsx = require('node:fs');
  const seen = [], dir = fsx.mkdtempSync(pathx.join(os.tmpdir(), 'am-kp-'));
  const run = async (trustProxy) => {
    const s = gen.createServer({ kind: 'vendor', dirArgv: ['--dir', dir], publicOnly: true, trustProxy, limit: ip => { seen.push(ip); return true; } });
    await new Promise(r => s.listen(0, '127.0.0.1', r));
    await fetch(`http://127.0.0.1:${s.address().port}/update.json`, { headers: { 'X-Real-IP': '203.0.113.7' } });
    await fetch(`http://127.0.0.1:${s.address().port}/update.json`, { headers: { 'X-Real-IP': 'bad value<>' } });
    s.close();
  };
  await run(false); await run(true);
  assert.deepStrictEqual(seen.map(x => x.replace('::ffff:', '')), ['127.0.0.1', '127.0.0.1', '203.0.113.7', '127.0.0.1']);
  fsx.rmSync(dir, { recursive: true, force: true });
});

test('license CHECK KEY: genuine / upgrade / same / downgrade / wrong PC / forged, nothing stored', () => {
  const license = require('./license'), crypto = require('node:crypto');
  const { privateKey, publicKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
  const jwk = publicKey.export({ format: 'jwk' });
  const key = (plan, extra = {}) => license.signKey({ v: 1, plan, issued: 1000, ...extra }, privateKey);
  const up = license.check(key('studio'), { jwk, machine: 'AAAA-BBBB', current: 'basic', now: 5000 });
  assert.strictEqual(up.valid, true); assert.strictEqual(up.change, 'upgrade'); assert.deepStrictEqual(up.channels, [10, 32]);
  assert.deepStrictEqual(up.adds, ['plugins', 'ota', 'studio']); assert.strictEqual(up.canActivate, true); assert.strictEqual(up.anyPc, true);
  assert.strictEqual(license.check(key('pro'), { jwk, machine: 'X', current: 'pro', now: 5000 }).change, 'same');
  const down = license.check(key('pro'), { jwk, machine: 'X', current: 'master', now: 5000 });
  assert.strictEqual(down.change, 'downgrade'); assert.strictEqual(down.canActivate, false);
  const bound = license.check(key('master', { mid: 'AAAA-BBBB', expires: 5000 + 10 * 86400000 }), { jwk, machine: 'AAAABBBB', current: 'basic', now: 5000 });
  assert.strictEqual(bound.boundToThisPc, true); assert.strictEqual(bound.daysLeft, 10);
  const other = license.check(key('master', { mid: 'CCCC-DDDD' }), { jwk, machine: 'AAAA-BBBB', current: 'basic', now: 5000 });
  assert.strictEqual(other.valid, false); assert.strictEqual(other.reason, 'wrong-machine'); assert.strictEqual(other.genuine, true);
  const exp = license.check(key('pro', { expires: 2000 }), { jwk, machine: 'X', current: 'basic', now: 5000 });
  assert.strictEqual(exp.valid, false); assert.strictEqual(exp.reason, 'expired');
  const forged = key('master').split('.'); forged[1] = license.b64u(JSON.stringify({ v: 1, plan: 'master', issued: 1 }));
  const f = license.check(forged.join('.'), { jwk, machine: 'X', current: 'basic', now: 5000 });
  assert.strictEqual(f.valid, false); assert.strictEqual(f.genuine, false);
  assert.strictEqual(license.check('hello', { jwk, machine: 'X', current: 'basic' }).genuine, false);
});

test('POST /api/license/check answers without storing a key', async () => {
  const pathx = require('node:path'), fsx = require('node:fs'), osx = require('node:os'), license = require('./license');
  process.env.BRIDGE_LICENSE_FILE = pathx.join(fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'lck-')), 'license.json');
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  try {
    const res = await fetch(`http://127.0.0.1:${server.address().port}/api/license/check`, { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Mixer-Action': 'license' }, body: '{"key":"AMIX1.bad.bad"}' });
    const r = await res.json();
    assert.strictEqual(res.status, 200); assert.strictEqual(r.valid, false); assert.strictEqual(r.genuine, false); assert.strictEqual(r.current, 'basic');
    assert.strictEqual(license.readStored(), null);
  } finally { server.closeAllConnections(); await new Promise(r => server.close(r)); delete process.env.BRIDGE_LICENSE_FILE; }
});

test('key-request inbox is encrypted to the vendor key: unreadable on disk, opens only with the private key, tamper-evident, old records sealed on unlock', async () => {
  const ic = require('../scripts/inbox-crypto'), gen = require('../scripts/license-generator'), vault = require('../scripts/license-vault');
  const crypto = require('node:crypto'), fsx = require('node:fs'), osx = require('node:os'), pathx = require('node:path');
  const { privateKey, publicKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
  const pub = publicKey.export({ format: 'jwk' }), rec = { at: 1, plan: 'pro', name: 'Jane Secret', email: 'jane@example.com', machineId: 'ABCD-1234' };
  const box = ic.seal(rec, pub), raw = JSON.stringify(box);
  for (const s of ['Jane', 'jane@example.com', 'ABCD-1234']) assert.ok(!raw.includes(s), 'plain text leaked: ' + s);
  assert.deepStrictEqual(ic.open(box, privateKey), rec);
  const other = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' }).privateKey;
  assert.throws(() => ic.open(box, other));                                                         // wrong key
  assert.throws(() => ic.open({ ...box, plan: 'master' }, privateKey));                              // header changed
  const ct = Buffer.from(box.ct, 'base64'); ct[0] ^= 1; assert.throws(() => ic.open({ ...box, ct: ct.toString('base64') }, privateKey));   // body changed
  assert.notStrictEqual(ic.seal(rec, pub).ct, box.ct);                                              // fresh ephemeral key every time
  // through the public listener: stored sealed, LOCKED without the PIN, readable after unlock
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'am-inbox-'));
  fsx.writeFileSync(pathx.join(dir, 'public.jwk.json'), JSON.stringify(pub));
  vault.writeLocked(dir, privateKey.export({ type: 'pkcs8', format: 'pem' }), 'pin-123456');
  fsx.appendFileSync(pathx.join(dir, 'license-requests.jsonl'), JSON.stringify({ at: 2, plan: 'basic', name: 'Old Plain', email: 'old@example.com', machineId: '' }) + '\n');   // made before encryption
  const s = gen.createServer({ kind: 'vendor', dirArgv: ['--dir', dir], publicOnly: true, limit: () => true, requestCap: () => true, acceptRequests: true });
  await new Promise(r => s.listen(0, '127.0.0.1', r));
  const post = () => fetch(`http://127.0.0.1:${s.address().port}/request-license`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ kind: 'license-request', ...rec }) }).then(r => r.json());
  assert.strictEqual((await post()).ok, true); assert.strictEqual((await post()).duplicate, true);   // dedupe works on the sealed hash
  s.close();
  const disk = fsx.readFileSync(pathx.join(dir, 'license-requests.jsonl'), 'utf8');
  assert.ok(!disk.includes('Jane') && !disk.includes('jane@example.com'));
  const session = gen.makeSession();
  const locked = gen.readInbox(dir, session);
  assert.ok(locked.some(r => r.locked && r.plan === 'pro')); assert.ok(!JSON.stringify(locked).includes('Jane'));
  session.unlock(dir, 'pin-123456');
  const open = gen.readInbox(dir, session);
  assert.ok(open.some(r => r.name === 'Jane Secret' && r.email === 'jane@example.com')); assert.ok(open.some(r => r.name === 'Old Plain'));
  assert.ok(!fsx.readFileSync(pathx.join(dir, 'license-requests.jsonl'), 'utf8').includes('old@example.com'));   // the old plain record is now sealed too
  fsx.rmSync(dir, { recursive: true, force: true });
});

test('DEVICE SIMULATOR (UM2 base): ASIO busy -> WASAPI twin, mono ch1 -> both sides, native rate, busy error, unplug while others play, wrappers, diagnostics', { timeout: 60000 }, async () => {
  const sim = require('./sim'), diag = require('./diagnostics'), hw = require('./hw-match'), ifs = require('./interfaces');
  const { server } = require('./server');
  // the UM2's two names have no word in common: the family must pair them (the reason its fallback never worked)
  assert.ok(hw.score('BEHRINGER USB AUDIO', 'Microphone (USB Audio CODEC)') >= 100);
  assert.strictEqual(hw.bestTwin('BEHRINGER USB AUDIO', [{ name: 'Microphone Array (Realtek(R) Audio)' }]), null);          // never another card
  assert.strictEqual(hw.score('ASIO4ALL v2', 'Microphone (USB Audio CODEC)'), 0);
  sim.configure({ enabled: true, profiles: ['um2', 'realtek', 'focusrite', 'asio4all'], faults: {} });
  const clients = [];
  try {
    const pa = require('./portaudio').load(); assert.ok(pa.simulated);
    const grouped = ifs.groupInterfaces(pa.getDevices().map(d => ({ id: d.id, name: d.name, hostApi: d.hostAPIName, inputs: d.maxInputChannels, outputs: d.maxOutputChannels })), ['BEHRINGER USB AUDIO', 'Focusrite USB ASIO', 'ASIO4ALL v2']);
    const w = grouped.find(g => /asio4all/i.test(g.name)); assert.ok(w && w.wrapper && w.preferNative);
    const codec = grouped.find(g => g.name === 'USB Audio CODEC'); assert.ok(codec && codec.twinOf && codec.twinOf.name === 'BEHRINGER USB AUDIO' && codec.twinOf.read, 'the UM2 Windows endpoint is marked as its twin (never read twice)');
    // the engine fallback no longer picks "the default device" of another card
    const ne = require('./native-engine');
    assert.strictEqual(ne.wasapiFallback({ name: 'BEHRINGER USB AUDIO' }, 'input', { devices: [{ name: 'Microphone Array (Realtek(R) Audio)', engineBackend: 'wasapi', maxInputChannels: 2, isDefaultInput: true }] }), null);
    assert.strictEqual(ne.wasapiFallback({ name: 'BEHRINGER USB AUDIO' }, 'input', { devices: [{ name: 'Microphone Array (Realtek(R) Audio)', engineBackend: 'wasapi', maxInputChannels: 2, isDefaultInput: true }, { name: 'Microphone (USB Audio CODEC)', engineBackend: 'wasapi', maxInputChannels: 2 }] }).name, 'Microphone (USB Audio CODEC)');
    await new Promise(r => server.listen(0, '127.0.0.1', r));
    const port = server.address().port, devs = pa.getDevices(), id = n => devs.find(d => d.name === n).id;
    const open = (deviceId, extra = {}) => new Promise((res, rej) => {
      const ws = new WebSocket(`ws://127.0.0.1:${port}/ws/input`); ws.binaryType = 'arraybuffer'; clients.push(ws);
      const s = { ws, msgs: [], frames: [] };
      ws.onmessage = e => { if (typeof e.data === 'string') s.msgs.push(JSON.parse(e.data)); else s.frames.push(Buffer.from(e.data)); };
      ws.onopen = () => { ws.send(JSON.stringify({ type: 'start', deviceId, channels: 2, sampleRate: 48000, ...extra })); res(s); }; ws.onerror = rej;
    });
    const until = async (s, pred, ms = 6000) => { const t = Date.now(); while (Date.now() - t < ms) { const m = s.msgs.find(pred); if (m) return m; await new Promise(r => setTimeout(r, 50)); } return null; };
    const lr = s => { const b = Buffer.concat(s.frames.slice(-5)); let l = 0, r = 0; for (let i = 0; i + 3 < b.length; i += 4) { l = Math.max(l, Math.abs(b.readInt16LE(i))); r = Math.max(r, Math.abs(b.readInt16LE(i + 2))); } return { l, r }; };
    // 1) UM2 through its ASIO driver: signal on input 1 only -> copied to both sides
    let a = await open(id('BEHRINGER USB AUDIO'));
    assert.ok(await until(a, m => m.type === 'started' && m.hostApi === 'ASIO' && m.simulated));
    assert.ok(await until(a, m => m.type === 'mapped' && m.source === 1));
    await new Promise(r => setTimeout(r, 200)); let p = lr(a); assert.ok(p.l > 1000 && Math.abs(p.l - p.r) < 2, JSON.stringify(p));
    // 2) a second reader of the same single-client UM2 driver gets silence -> switches to the Windows twin by itself, with sound
    let b = await open(id('BEHRINGER USB AUDIO'), { takeover: false });
    const fbOrErr = await until(b, m => m.type === 'fallback' || m.type === 'error', 8000);
    if (fbOrErr && fbOrErr.type === 'error') assert.match(fbOrErr.message, /in use|busy|ASIO/i);   // the ASIO lock refused it up front: also a readable answer
    else { assert.ok(fbOrErr, 'expected a fallback'); assert.strictEqual(fbOrErr.fallback.to, 'Microphone (USB Audio CODEC)'); assert.ok(await until(b, m => m.type === 'started' && m.fallbackFrom === 'BEHRINGER USB AUDIO')); }
    b.ws.close(); a.ws.close(); await new Promise(r => setTimeout(r, 150));
    // 2b) the driver held by ANOTHER program (no lock of ours): exact silence for 3 s -> fallback to the twin, sound comes back
    sim.configure({ faults: { um2: { asioHeldByOtherApp: true } } });
    a = await open(id('BEHRINGER USB AUDIO'));
    const fb = await until(a, m => m.type === 'fallback', 8000); assert.ok(fb, 'silence fallback'); assert.strictEqual(fb.fallback.to, 'Microphone (USB Audio CODEC)');
    assert.ok(await until(a, m => m.type === 'started' && m.hostApi === 'Windows WASAPI'));
    await new Promise(r => setTimeout(r, 400)); assert.ok(lr(a).l > 1000, 'sound after the fallback');
    a.ws.close(); sim.configure({ faults: {} });
    // 3) a rate the UM2 cannot do: opened at its own rate and converted
    a = await open(id('BEHRINGER USB AUDIO'), { sampleRate: 96000 });
    const st = await until(a, m => m.type === 'started'); assert.strictEqual(st.sampleRate, 96000); assert.strictEqual(st.deviceRate, 48000);
    a.ws.close();
    // 4) Focusrite's driver refuses a second client with an error -> readable error + advice in the diagnostics
    sim.configure({ faults: { focusrite: { asioHeldByOtherApp: true } } });
    a = await open(id('Focusrite USB ASIO'), { autoFallback: false });
    const err = await until(a, m => m.type === 'error'); assert.match(err.message, /in use by another program/);
    assert.ok(diag.list({ filter: 'errors' }).some(e => /Focusrite USB ASIO/.test(e.message) && e.advice && /one program at a time/.test(e.advice.means)));
    a.ws.close(); sim.configure({ faults: { realtek: { unplugAfterMs: 600 } } });
    require('./driver-lock').forget('*');   // this step is about two ASIO drivers at once: start without the locks learned above
    // 5) all interfaces at once: one ASIO driver per program, so the Focusrite runs through its own Windows endpoint; Realtek unplugged
    //    mid-stream; the UM2 and the Focusrite keep delivering
    const um = await open(id('BEHRINGER USB AUDIO')), rt = await open(id('Microphone Array (Realtek(R) Audio)')), fo = await open(id('Focusrite USB ASIO'));
    const foFb = await until(fo, m => m.type === 'fallback'); assert.ok(foFb && foFb.fallback.to === 'Analogue 1 + 2 (Focusrite USB Audio)', JSON.stringify(fo.msgs.slice(0, 3)));
    assert.ok(await until(rt, m => m.type === 'error' && /unplugged/.test(m.message), 4000));
    const n1 = um.frames.length, n2 = fo.frames.length; await new Promise(r => setTimeout(r, 400));
    assert.ok(um.frames.length > n1 && fo.frames.length > n2, 'the others keep playing');
    for (const s of [um, rt, fo]) s.ws.close();
    // 6) the diagnostics API: entries, a filter, a plain-text report without personal data
    const d = await (await fetch(`http://127.0.0.1:${port}/api/diagnostics?filter=asio`)).json();
    assert.ok(d.ok && d.entries.length && d.entries.every(e => e.area === 'asio' || /asio/i.test(e.message) || (e.data && /asio/i.test(e.data.hostApi || ''))));
    const rep = await (await fetch(`http://127.0.0.1:${port}/api/diagnostics/report`)).text(); assert.match(rep, /\[SIM\] BEHRINGER USB AUDIO/); assert.match(rep, /what to do:/);
    assert.strictEqual(diag.redact('C:\\Users\\Amir\\x jane@example.com CC2B-A1AC-00B9-85AD-6C4D'), 'C:\\Users\\<user>\\x <email> CC2B-…');
  } finally {
    for (const ws of clients) try { ws.close(); } catch (_) { /* closed */ }
    await new Promise(r => setTimeout(r, 300));
    sim.configure({ enabled: false, faults: {} }); sim.closeAll(); server.closeAllConnections();
    await new Promise(r => { const t = setTimeout(r, 2000); server.close(() => { clearTimeout(t); r(); }); });
  }
});


// ---- PAID EDITIONS (block 0, 1.22.1.0 Beta): signed edition token, automatic offline activation, WhatsApp upgrade --------------------------------
{
  const crypto = require('node:crypto'), fsx = require('node:fs'), osx = require('node:os'), pathx = require('node:path');
  const ed = require('./edition');
  const pair = () => { const k = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' }); const j = k.publicKey.export({ format: 'jwk' }); return { priv: k.privateKey, jwk: { kty: 'EC', crv: 'P-256', x: j.x, y: j.y } }; };
  const V = pair(), OTHER = pair(), MID = 'AAAA-BBBB-CCCC-DDDD-EEEE';
  const b64u = b => Buffer.from(b).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  const signRaw = (payload, priv) => { const body = b64u(JSON.stringify(payload)); return 'AMFXE1.' + body + '.' + b64u(crypto.sign('sha256', Buffer.from(body), { key: priv, dsaEncoding: 'ieee-p1363' })); };

  test('edition token: genuine Pro / Studio Master verify; tampered, unsigned, foreign-key, wrong-edition, bad-serial and expired tokens are refused', () => {
    const now = Date.UTC(2026, 10, 5);
    const pro = ed.signToken({ edition: 'pro', privateKey: V.priv, order: 'WA-1001', now }), sm = ed.signToken({ edition: 'studio-master', privateKey: V.priv, now });
    let v = ed.verifyToken(pro.token, { jwk: V.jwk, now }); assert.strictEqual(v.ok, true); assert.strictEqual(v.plan, 'pro'); assert.strictEqual(v.payload.order, 'WA-1001'); assert.match(v.payload.serial, /^AMFX-PRO-/);
    v = ed.verifyToken(sm.token, { jwk: V.jwk, now }); assert.strictEqual(v.ok, true); assert.strictEqual(v.plan, 'master'); assert.strictEqual(v.edition.name, 'AmF X1 Studio Master'); assert.match(v.payload.serial, /^AMFX-SM-/);
    // tampered: Pro payload rewritten to Studio Master, old signature kept
    const [p0, body, sig] = pro.token.split('.'), forged = JSON.parse(Buffer.from(body.replace(/-/g, '+').replace(/_/g, '/'), 'base64').toString());
    forged.edition = 'studio-master'; forged.plan = 'master';
    assert.strictEqual(ed.verifyToken([p0, b64u(JSON.stringify(forged)), sig].join('.'), { jwk: V.jwk, now }).reason, 'bad-signature');
    assert.strictEqual(ed.verifyToken(pro.token.slice(0, -4) + 'AAAA', { jwk: V.jwk, now }).reason, 'bad-signature', 'changed signature');
    assert.strictEqual(ed.verifyToken([p0, body, ''].join('.'), { jwk: V.jwk, now }).reason, 'bad-signature', 'unsigned');
    assert.strictEqual(ed.verifyToken([p0, body].join('.'), { jwk: V.jwk, now }).reason, 'malformed');
    assert.strictEqual(ed.verifyToken('AMIX1.' + body + '.' + sig, { jwk: V.jwk, now }).reason, 'malformed', 'a license key is not an edition token');
    assert.strictEqual(ed.verifyToken(pro.token, { jwk: OTHER.jwk, now }).reason, 'bad-signature', 'signed by another key');
    assert.strictEqual(ed.verifyToken(pro.token, { now }).reason, 'bad-signature', 'the shipped vendor key does not trust a test key');
    // genuinely signed, but the payload is wrong
    const base = { v: 1, kind: 'amfx1-edition', edition: 'pro', plan: 'pro', serial: 'AMFX-PRO-ABCD-EFGH-JKLM', issued: now };
    assert.strictEqual(ed.verifyToken(signRaw({ ...base, plan: 'master' }, V.priv), { jwk: V.jwk, now }).reason, 'wrong-edition', 'Pro may not carry the MASTER plan');
    assert.strictEqual(ed.verifyToken(signRaw({ ...base, edition: 'ultimate' }, V.priv), { jwk: V.jwk, now }).reason, 'wrong-edition');
    assert.strictEqual(ed.verifyToken(signRaw({ ...base, kind: 'license' }, V.priv), { jwk: V.jwk, now }).reason, 'wrong-edition');
    assert.strictEqual(ed.verifyToken(signRaw({ ...base, serial: 'AMFX-SM-ABCD-EFGH-JKLM' }, V.priv), { jwk: V.jwk, now }).reason, 'bad-serial', 'serial of another edition');
    assert.strictEqual(ed.verifyToken(signRaw({ ...base, serial: 'AMFX-PRO-0000-0000-0000' }, V.priv), { jwk: V.jwk, now }).reason, 'bad-serial');
    const exp = ed.signToken({ edition: 'pro', privateKey: V.priv, days: 30, now });
    assert.strictEqual(ed.verifyToken(exp.token, { jwk: V.jwk, now: now + 29 * 86400000 }).ok, true);
    assert.strictEqual(ed.verifyToken(exp.token, { jwk: V.jwk, now: now + 31 * 86400000 }).reason, 'expired');
    assert.throws(() => ed.signToken({ edition: 'basic', privateKey: V.priv }), /edition must be one of/);
  });

  test('edition serials: random per copy, AMFX-PRO-XXXX-XXXX-XXXX / AMFX-SM-…, no look-alike characters', () => {
    const seen = new Set();
    for (let i = 0; i < 2000; i++) { const s = ed.newSerial(i % 2 ? 'pro' : 'studio-master'); assert.match(s, ed.SERIAL_RE); assert.ok(!/[01OI]/.test(s.slice(8))); seen.add(s); }
    assert.strictEqual(seen.size, 2000, 'no repeats');
    assert.match(ed.newSerial('pro'), /^AMFX-PRO-[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}-[2-9A-HJ-NP-Z]{4}$/);
  });

  test('edition activation: first start verifies the shipped token and records it bound to this machine; another machine or a bad token gets Basic', () => {
    const tmp = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'am-ed-')), shipped = pathx.join(tmp, 'edition-token.json'), file = pathx.join(tmp, 'home', 'edition.json');
    try {
      const L = require('./license'), now = Date.now();
      const opts = extra => ({ machine: MID, jwk: V.jwk, shipped, file, now, ...extra });
      assert.strictEqual(ed.current(opts()), null, 'Basic: no token, no activation');
      const t = ed.signToken({ edition: 'pro', privateKey: V.priv, now });
      fsx.writeFileSync(shipped, JSON.stringify({ token: t.token }));
      let c = ed.current(opts());
      assert.strictEqual(c.id, 'pro'); assert.strictEqual(c.plan, 'pro'); assert.strictEqual(c.source, 'installer'); assert.strictEqual(c.activatedNow, true); assert.strictEqual(c.serial, t.payload.serial);
      const rec = JSON.parse(fsx.readFileSync(file, 'utf8')); assert.strictEqual(rec.machineId, MID); assert.strictEqual(rec.token, t.token);
      if (process.platform !== 'win32') assert.strictEqual(fsx.statSync(file).mode & 0o777, 0o600, 'activation file readable by the user only');
      c = ed.current(opts()); assert.strictEqual(c.source, 'activated', 'second start: from the activation');
      fsx.rmSync(shipped);   // a Basic OTA update replaced the installed files: the activation keeps the edition
      assert.strictEqual(ed.current(opts()).id, 'pro');
      assert.strictEqual(ed.current(opts({ machine: 'FFFF-FFFF-FFFF-FFFF-FFFF' })), null, 'activation copied to another computer: Basic');
      // license status: the edition counts like a key for its plan; a higher key wins; a lower key does not lower it
      const st = L.status({ key: null, machine: MID, edition: { jwk: V.jwk, shipped, file, now } });
      assert.strictEqual(st.state, 'active'); assert.strictEqual(st.plan.id, 'pro'); assert.strictEqual(st.edition.serial, t.payload.serial); assert.strictEqual(st.license.edition, 'pro');
      const key = (plan) => L.signKey({ v: 1, plan, issued: now, id: 'K-' + plan }, V.priv);
      assert.strictEqual(L.status({ key: key('master'), machine: MID, jwk: V.jwk, edition: { jwk: V.jwk, shipped, file, now } }).plan.id, 'master');
      assert.strictEqual(L.status({ key: key('basic'), machine: MID, jwk: V.jwk, edition: { jwk: V.jwk, shipped, file, now } }).plan.id, 'pro');
      assert.strictEqual(L.status({ key: 'AMIX1.broken.key', machine: MID, jwk: V.jwk, edition: { jwk: V.jwk, shipped, file, now } }).plan.id, 'pro', 'a broken stored key does not drop the edition');
      assert.strictEqual(L.status({ key: null, machine: MID, edition: false }).plan.id, 'basic', 'edition: false = keys only');
      // a tampered token in a fresh install: refused, Basic, and the reason is reported
      fsx.rmSync(file); const [a, b] = t.token.split('.');
      fsx.writeFileSync(shipped, JSON.stringify({ token: [a, b, b64u(Buffer.alloc(64, 1))].join('.') }));
      c = ed.current(opts()); assert.strictEqual(c.error.reason, 'bad-signature'); assert.ok(!fsx.existsSync(file), 'nothing recorded');
      const st2 = L.status({ key: null, machine: MID, edition: { jwk: V.jwk, shipped, file, now } });
      assert.strictEqual(st2.plan.id, 'basic'); assert.strictEqual(st2.edition, null); assert.strictEqual(st2.editionError.reason, 'bad-signature');
    } finally { fsx.rmSync(tmp, { recursive: true, force: true }); }
  });


  test('Key Generator: BUILD EDITION TOKEN needs the PIN, records the sale encrypted, SALES LOG readable only unlocked; /request-license is off unless --accept-requests', async () => {
    const gen = require('../scripts/license-generator'), vault = require('../scripts/license-vault'), http = require('node:http');
    const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'am-gen-ed-'));
    fsx.writeFileSync(pathx.join(dir, 'public.jwk.json'), JSON.stringify(V.jwk));
    vault.writeLocked(dir, V.priv.export({ type: 'pkcs8', format: 'pem' }), 'pin-482916');
    const srv = gen.createServer({ kind: 'vendor', dirArgv: ['--dir', dir], root: dir });
    await new Promise(r => srv.listen(0, '127.0.0.1', r)); const port = srv.address().port;
    const call = (method, p, body, headers = {}) => new Promise((res, rej) => {
      const rq = http.request({ host: '127.0.0.1', port, method, path: p, headers: { 'Content-Type': 'application/json', 'X-Gen-Action': '1', Host: '127.0.0.1:' + port, ...headers } }, r => { const c = []; r.on('data', d => c.push(d)); r.on('end', () => { let j = null; try { j = JSON.parse(Buffer.concat(c).toString()); } catch (_) { /* html */ } res({ status: r.statusCode, j }); }); });
      rq.on('error', rej); if (body) rq.write(JSON.stringify(body)); rq.end();
    });
    try {
      const sale = { edition: 'pro', order: 'WA-77', buyer: 'Siti <b>', whatsapp: '+60 12-345 6789' };
      let r = await call('POST', '/edition', sale); assert.strictEqual(r.status, 401); assert.strictEqual(r.j.locked, true, 'signing needs the PIN');
      assert.strictEqual((await call('POST', '/unlock', { pin: 'pin-482916' })).status, 200);
      assert.strictEqual((await call('POST', '/edition', { edition: 'gold' })).status, 400);
      assert.strictEqual((await call('POST', '/edition', sale, { 'X-Gen-Action': '' })).status, 403, 'CSRF guard');
      assert.strictEqual((await call('POST', '/edition', sale, { Origin: 'https://evil.example' })).status, 403);
      r = await call('POST', '/edition', sale); assert.strictEqual(r.status, 200);
      const v = ed.verifyToken(r.j.token, { jwk: V.jwk }); assert.strictEqual(v.ok, true); assert.strictEqual(v.payload.order, 'WA-77'); assert.strictEqual(r.j.serial, v.payload.serial); assert.strictEqual(r.j.file, 'edition-token-' + r.j.serial + '.json');
      const log = fsx.readFileSync(pathx.join(dir, 'sales.jsonl'), 'utf8');
      assert.ok(!log.includes(r.j.serial) && !log.includes('Siti') && !log.includes('60123456789') && !log.includes('WA-77'), 'the sales log on disk is sealed');
      r = await call('GET', '/sales'); assert.strictEqual(r.j.sales[0].serial, v.payload.serial); assert.strictEqual(r.j.sales[0].buyer, 'Siti b'); assert.strictEqual(r.j.sales[0].whatsapp, '60123456789');
      await call('POST', '/lock', {});
      r = await call('GET', '/sales'); assert.strictEqual(r.j.sales[0].locked, true); assert.strictEqual(r.j.sales[0].serial, undefined, 'locked: no sale details');
      r = await call('POST', '/request-license', { kind: 'license-request', plan: 'pro' }); assert.strictEqual(r.status, 410); assert.ok(r.j.whatsapp.startsWith('https://wa.me/601120871185'));
      assert.strictEqual((await call('GET', '/status')).j.acceptRequests, false);
    } finally { srv.close(); fsx.rmSync(dir, { recursive: true, force: true }); }
    assert.strictEqual(gen.waDigits('+60 12-345 6789'), '60123456789'); assert.strictEqual(gen.waDigits('123'), '');
    const page = gen.page('vendor'), js = /<script>([\s\S]*?)<\/script>/.exec(page)[1];
    assert.doesNotThrow(() => new Function(js));   // eslint-disable-line no-new-func
    assert.ok(page.includes('BUILD EDITION TOKEN') && page.includes('id="wasend"') && page.includes('SEND SERIAL VIA WHATSAPP') && js.includes("window.open('https://wa.me/' + d + '?text=' + encodeURIComponent(text), '_blank', 'noopener')"));
    assert.ok(!gen.page('sandbox').includes('BUILD EDITION TOKEN'), 'editions are vendor-only');
  });
}

// ---- DUAL MODE FADER AUTOMATION + AI MODE CONTROL (block 1, 1.22.1.0 Beta) ----------------------------------------------------------------------


// ---- AI DRIVER LOCK (block 2, 1.22.1.0 Beta) ------------------------------------------------------------------------------------------------------
test('AI driver lock: ladder ASIO -> WASAPI exclusive (only when built) -> WASAPI shared -> DirectSound -> MME; WASAPI twin preferred; locks expire and can be forgotten', () => {
  const dl = require('./driver-lock'); dl.forget('*');
  const devs = [
    { name: 'BEHRINGER USB AUDIO', hostAPIName: 'ASIO', maxInputChannels: 2, maxOutputChannels: 2 },
    { name: 'Microphone (USB Audio CODEC)', hostAPIName: 'MME', maxInputChannels: 2, maxOutputChannels: 0 },
    { name: 'Microphone (USB Audio CODEC)', hostAPIName: 'Windows DirectSound', maxInputChannels: 2, maxOutputChannels: 0 },
    { name: 'Microphone (USB Audio CODEC)', hostAPIName: 'Windows WASAPI', maxInputChannels: 2, maxOutputChannels: 0 },
    { name: 'Speakers (USB Audio CODEC)', hostAPIName: 'Windows WASAPI', maxInputChannels: 0, maxOutputChannels: 2 },
    { name: 'Microphone Array (Realtek(R) Audio)', hostAPIName: 'Windows WASAPI', maxInputChannels: 2, maxOutputChannels: 0 },
  ];
  assert.strictEqual(dl.twin('BEHRINGER USB AUDIO', devs.filter(d => d.hostAPIName !== 'ASIO')).hostAPIName, 'Windows WASAPI', 'same hardware: WASAPI before DirectSound and MME (lowest latency)');
  assert.strictEqual(dl.twin('BEHRINGER USB AUDIO', [devs[5]]), null, 'never another card');
  const L = dl.ladder('BEHRINGER USB AUDIO', devs, 'input');
  assert.deepStrictEqual(L.map(x => x.mode), ['asio', 'wasapi-exclusive', 'wasapi-shared', 'directsound', 'mme']);
  assert.strictEqual(L[0].available, true); assert.strictEqual(L[2].device.name, 'Microphone (USB Audio CODEC)'); assert.strictEqual(L[3].device.hostAPIName, 'Windows DirectSound'); assert.strictEqual(L[4].device.hostAPIName, 'MME');
  assert.strictEqual(L[1].available, false); assert.match(L[1].why, /not built/, 'exclusive is reported, never pretended');
  assert.strictEqual(dl.ladder('BEHRINGER USB AUDIO', devs, 'output')[2].device.name, 'Speakers (USB Audio CODEC)');
  // a wrapper is the first choice when it is the ONLY ASIO driver (the old rule reported "no path" and the row looked not connected) ...
  assert.strictEqual(dl.ladder('ASIO4ALL v2', [{ name: 'ASIO4ALL v2', hostAPIName: 'ASIO', maxInputChannels: 2 }], 'input')[0].available, true, 'the only ASIO driver: first choice');
  // ... and held back only next to a device with its own ASIO driver
  assert.strictEqual(dl.ladder('ASIO4ALL v2', [{ name: 'ASIO4ALL v2', hostAPIName: 'ASIO', maxInputChannels: 2 }, devs[0]], 'input')[0].available, false, 'next to a native ASIO driver it is not the first choice');
  // learn, route, expire, forget
  const t0 = Date.now();
  assert.strictEqual(dl.route(devs[0], devs, 'input').dev, devs[0], 'no lock: its own ASIO driver');
  dl.learn('BEHRINGER USB AUDIO', 'input', { mode: 'wasapi-shared', device: 'Microphone (USB Audio CODEC)', hostApi: 'Windows WASAPI', reason: 'ASIO delivered silence' }, t0);
  const r = dl.route(devs[0], devs, 'input', t0 + 1000); assert.strictEqual(r.dev, devs[3], 'locked: straight to the WASAPI endpoint'); assert.strictEqual(r.locked.mode, 'wasapi-shared');
  assert.strictEqual(dl.route(devs[0], devs, 'output', t0 + 1000).dev, devs[0], 'a read lock does not move writes');
  assert.strictEqual(dl.route(devs[0], devs, 'input', t0 + dl.RETRY_MS + 1).dev, devs[0], 'after the retry time its own ASIO driver is tried again');
  assert.strictEqual(dl.route(devs[0], devs.filter(d => d.hostAPIName !== 'Windows WASAPI'), 'input', t0 + 1000).dev, devs[0], 'a locked endpoint that is gone: back to ASIO');
  assert.ok(JSON.parse(require('node:fs').readFileSync(dl.file(), 'utf8')).locks['in:behringer usb audio'], 'kept on disk');
  dl.reset(); assert.strictEqual(dl.get('BEHRINGER USB AUDIO', 'input', t0 + 1000).mode, 'wasapi-shared', 'read back after a restart');
  assert.strictEqual(dl.forget('BEHRINGER USB AUDIO'), 1); assert.strictEqual(dl.get('BEHRINGER USB AUDIO', 'input'), null);
  dl.learn('X', 'input', { mode: 'asio', device: 'X' }); dl.learn('Y', 'output', { mode: 'mme', device: 'Y' }); assert.strictEqual(dl.forget('*'), 2);
});

test('AI driver lock on the simulator: UM2 held by another program -> WASAPI once, then straight there; FORGET -> ASIO first again; a refused Focusrite driver opens its Windows endpoint', { timeout: 60000 }, async () => {
  const sim = require('./sim'), dl = require('./driver-lock'), diag = require('./diagnostics');
  dl.forget('*');
  sim.configure({ enabled: true, profiles: ['um2', 'focusrite'], faults: { um2: { asioHeldByOtherApp: true }, focusrite: { asioHeldByOtherApp: true } } });
  const pa = require('./portaudio').load(), devs = pa.getDevices(), id = n => devs.find(d => d.name === n).id;
  const open = (name, extra = {}) => { const s = { msgs: [], frames: 0 }; s.sess = require('./input').createInputSession({ send: m => s.msgs.push(JSON.parse(m)), sendBinary() { s.frames++; }, close() {} }); s.sess.onText(JSON.stringify({ type: 'start', deviceId: id(name), channels: 2, sampleRate: 48000, ...extra })); return s; };
  const until = async (s, pred, ms = 7000) => { const t = Date.now(); while (Date.now() - t < ms) { const m = s.msgs.find(pred); if (m) return m; await new Promise(r => setTimeout(r, 40)); } return null; };
  try {
    // 1) first start: ASIO is silent for 3 s (held elsewhere) -> the Windows twin, and the lock is learned
    let a = open('BEHRINGER USB AUDIO');
    let fb = await until(a, m => m.type === 'fallback'); assert.ok(fb); assert.strictEqual(fb.fallback.via, 'wasapi');
    assert.ok(await until(a, m => m.type === 'started' && m.hostApi === 'Windows WASAPI'));
    const lk = dl.get('BEHRINGER USB AUDIO', 'input'); assert.ok(lk, 'locked'); assert.strictEqual(lk.mode, 'wasapi-shared'); assert.strictEqual(lk.device, 'Microphone (USB Audio CODEC)'); assert.match(lk.reason, /silence/);
    a.sess.onClose();
    // 2) next start: no 3 s of silence, straight to the twin
    const t = Date.now(); a = open('BEHRINGER USB AUDIO');
    fb = await until(a, m => m.type === 'fallback', 2000); assert.ok(fb && fb.fallback.via === 'lock', JSON.stringify(a.msgs.slice(0, 2)));
    assert.ok(await until(a, m => m.type === 'started' && m.hostApi === 'Windows WASAPI', 2000)); assert.ok(Date.now() - t < 2000, 'no waiting on a busy driver');
    assert.ok(a.msgs.every(m => !(m.type === 'started' && m.hostApi === 'ASIO')), 'ASIO not even tried');
    a.sess.onClose();
    assert.ok(diag.list({ filter: 'lock' }).some(e => /AI driver lock/.test(e.message) && /BEHRINGER USB AUDIO/.test(e.message)), 'readable in DIAGNOSTICS');
    // 3) the other program closed; FORGET: ASIO first again, and a clean ASIO run with signal locks ASIO
    sim.configure({ faults: { focusrite: { asioHeldByOtherApp: true } } }); dl.forget('BEHRINGER USB AUDIO');
    a = open('BEHRINGER USB AUDIO');
    assert.ok(await until(a, m => m.type === 'started' && m.hostApi === 'ASIO'));
    await until(a, () => (dl.get('BEHRINGER USB AUDIO', 'input') || {}).mode === 'asio', 3000);
    assert.strictEqual((dl.get('BEHRINGER USB AUDIO', 'input') || {}).mode, 'asio', 'signal on ASIO: ASIO locked');
    a.sess.onClose();
    // 4) a driver that REFUSES to open (Focusrite, busy) -> its Windows endpoint opens instead (it used to stop with an error), and that is locked
    a = open('Focusrite USB ASIO');
    fb = await until(a, m => m.type === 'fallback'); assert.ok(fb && /in use by another program/.test(fb.fallback.reason), JSON.stringify(a.msgs.slice(0, 3)));
    assert.ok(await until(a, m => m.type === 'started' && /WASAPI/.test(m.hostApi)));
    assert.match(dl.get('Focusrite USB ASIO', 'input').reason, /ASIO did not open/);
    a.sess.onClose();
    // 5) the API: ladders and locks; FORGET needs the action header
    const { server } = require('./server');
    await new Promise(r => server.listen(0, '127.0.0.1', r)); const base = 'http://127.0.0.1:' + server.address().port;
    try {
      const j = await (await fetch(base + '/api/driver-lock')).json();
      assert.ok(j.ok && j.exclusive === false && j.locks.some(l => l.name === 'Focusrite USB ASIO'));
      const um = j.interfaces.find(i => i.name === 'BEHRINGER USB AUDIO'); assert.deepStrictEqual(um.input.map(x => x.mode), ['asio', 'wasapi-exclusive', 'wasapi-shared', 'directsound', 'mme']);
      assert.strictEqual(um.input[2].device, 'Microphone (USB Audio CODEC)');
      assert.strictEqual((await fetch(base + '/api/driver-lock', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ forget: '*' }) })).status, 400, 'needs X-Mixer-Action');
      const f = await (await fetch(base + '/api/driver-lock', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Mixer-Action': 'driver-lock' }, body: JSON.stringify({ forget: '*' }) })).json();
      assert.ok(f.ok && f.removed >= 1 && f.locks.length === 0);
    } finally { await new Promise(r => server.close(r)); }
  } finally { sim.configure({ enabled: false, faults: {} }); sim.closeAll(); dl.forget('*'); }
});


// ---- FX LIBRARY: 300 DSP effect presets over the real AI DSP rack (1.22.1.0 Beta) -----------------------------------------------------------------


// ---- HEADPHONE CROSSFEED + spatial architecture doc (block 3, 1.22.1.0 Beta) --------------------------------------------------------------------

test('spatial architecture doc: honest about trademarks and figures, AMFB DNA is the project own profile, Eclipsa/IAMF marked open and research-only', () => {
  const fsx = require('node:fs'), pathx = require('node:path');
  const f = pathx.join(__dirname, '..', 'docs', 'spatial-audio-architecture.md'); assert.ok(fsx.existsSync(f), 'docs/spatial-audio-architecture.md exists');
  const d = fsx.readFileSync(f, 'utf8');
  assert.match(d, /does \*\*not\*\* implement, decode, license or claim/i, 'trademarks are described, not claimed');
  assert.match(d, /AMFB DNA.*own|own.*AMFB DNA/is); assert.match(d, /not a licensed format/i);
  assert.match(d, /Eclipsa|IAMF/); assert.match(d, /open.*royalty-free|royalty-free.*open/i);
  assert.match(d, /Research only|research item|research-only/i, 'IAMF render is marked research-only, not shipped');
  assert.match(d, /Confidence/, 'a confidence column so figures are not quoted blind');
  for (const tm of ['Dolby Atmos', 'DTS:X', 'Sony 360', 'Auro-3D', 'MPEG-H', 'THX', 'IMAX Enhanced']) assert.ok(d.includes(tm), tm + ' covered');
  assert.match(d, /Shipped \(1\.22\.1\.0\)|Shipped/i, 'crossfeed / 360 marked as shipped, the rest planned');
});

// ---- Demo-audio removal + SYSTEM OPTIMIZE page (1.22.1.0 Beta) -----------------------------------------------------------------------------------


// ---- ASIO4ALL "not connected" fixes (1.22.1.0 Beta) ----------------------------------------------------------------------------------------------
test('ASIO4ALL with no channels gets the Windows DEFAULT endpoint as its fallback (it was a dead row: no READ / WRITE)', () => {
  const ifs = require('./interfaces');
  const devs = [
    { id: 1, name: 'ASIO4ALL v2', hostApi: 'ASIO', inputs: 0, outputs: 0 },                       // nothing enabled in its panel / probe failed
    { id: 2, name: 'Microphone Array (Realtek(R) Audio)', hostApi: 'Windows WASAPI', inputs: 2, outputs: 0 },
    { id: 3, name: 'Speakers (Realtek(R) Audio)', hostApi: 'Windows WASAPI', inputs: 0, outputs: 2 },
    { id: 4, name: 'Primary Sound Driver', hostApi: 'MME', inputs: 0, outputs: 2 },
    { id: 5, name: 'Primary Sound Capture Driver', hostApi: 'MME', inputs: 2, outputs: 0 },
  ];
  const g = ifs.groupInterfaces(devs, ['ASIO4ALL v2']), a = g.find(i => i.name === 'ASIO4ALL v2');
  assert.ok(a.wrapper && a.read && a.write, 'ASIO4ALL is openable both ways: ' + JSON.stringify(a));
  assert.strictEqual(a.fallback.via, 'wasapi'); assert.strictEqual(a.fallback.defaultEndpoint, true);
  assert.ok(a.inputs > 0 && a.outputs > 0);
  // the native engine marks the Windows defaults: those win over the first endpoint in the list
  const devs2 = [{ id: 1, name: 'ASIO4ALL v2', hostApi: 'ASIO', inputs: 0, outputs: 0 },
    { id: 2, name: 'Microphone (USB Audio CODEC)', hostApi: 'Windows WASAPI', inputs: 2, outputs: 0 },
    { id: 3, name: 'Microphone Array (Realtek(R) Audio)', hostApi: 'Windows WASAPI', inputs: 2, outputs: 0, isDefaultInput: true }];
  const b = ifs.groupInterfaces(devs2, ['ASIO4ALL v2']).find(i => i.name === 'ASIO4ALL v2');
  assert.strictEqual(b.fallback.input, 'Microphone Array (Realtek(R) Audio)', 'the default input');
  // a wrapper that DOES report channels is left alone
  const c = ifs.groupInterfaces([{ id: 1, name: 'ASIO4ALL v2', hostApi: 'ASIO', inputs: 2, outputs: 2 }, devs[1]], ['ASIO4ALL v2']).find(i => i.name === 'ASIO4ALL v2');
  assert.ok(!c.fallback, 'no fallback when it has its own channels');
});

test('AI driver lock + ASIO4ALL: first choice when it is the only ASIO driver, held back next to a native one; its fallback never doubles a native device', () => {
  const dl = require('./driver-lock');
  const realtek = [{ name: 'Microphone Array (Realtek(R) Audio)', hostAPIName: 'Windows WASAPI', maxInputChannels: 2, maxOutputChannels: 0 }, { name: 'Speakers (Realtek(R) Audio)', hostAPIName: 'Windows WASAPI', maxInputChannels: 0, maxOutputChannels: 2 }];
  const a4a = { name: 'ASIO4ALL v2', hostAPIName: 'ASIO', maxInputChannels: 2, maxOutputChannels: 2 };
  let L = dl.ladder('ASIO4ALL v2', [a4a, ...realtek], 'input');
  assert.strictEqual(L[0].available, true, 'the only ASIO driver here: used first (it was reported as "no path")');
  assert.strictEqual(L[2].device.name, 'Microphone Array (Realtek(R) Audio)', 'its Windows fallback is the default-type endpoint');
  const um2 = [{ name: 'BEHRINGER USB AUDIO', hostAPIName: 'ASIO', maxInputChannels: 2, maxOutputChannels: 2 }, { name: 'Microphone (USB Audio CODEC)', hostAPIName: 'Windows WASAPI', maxInputChannels: 2, maxOutputChannels: 0 }];
  L = dl.ladder('ASIO4ALL v2', [a4a, ...um2, ...realtek], 'input');
  assert.strictEqual(L[0].available, false); assert.match(L[0].why, /own ASIO driver is present/);
  // the wrapper twin skips the UM2's own endpoint (its ASIO driver may be reading it) and prefers a default-flagged endpoint
  const all = [a4a, ...um2, ...realtek];
  assert.strictEqual(dl.twin('ASIO4ALL v2', all.filter(d => !/asio/i.test(d.hostAPIName) && d.maxInputChannels > 0), 10, all).name, 'Microphone Array (Realtek(R) Audio)');
  const flagged = [{ ...realtek[0], isDefaultInput: false }, { name: 'Line In (Realtek(R) Audio)', hostAPIName: 'Windows WASAPI', maxInputChannels: 2, isDefaultInput: true }];
  assert.strictEqual(dl.twin('ASIO4ALL v2', flagged, 10, flagged).name, 'Line In (Realtek(R) Audio)');
  assert.strictEqual(dl.twin('ASIO4ALL v2', [{ name: 'Stereo Mix (Realtek(R) Audio)', hostAPIName: 'Windows WASAPI', maxInputChannels: 2 }], 10, []), null, 'never a loopback');
});

test('ASIO4ALL on the simulator: opens alone; next to a native ASIO driver (one ASIO driver per program) it falls back to Realtek instead of erroring', { timeout: 30000 }, async () => {
  const sim = require('./sim'), dl = require('./driver-lock'); dl.forget('*');
  const open = (devs, name) => { const s = { msgs: [] }; s.sess = require('./input').createInputSession({ send: m => s.msgs.push(JSON.parse(m)), sendBinary() {}, close() {} }); s.sess.onText(JSON.stringify({ type: 'start', deviceId: devs.find(d => d.name === name).id, channels: 2, sampleRate: 48000 })); return s; };
  const until = async (s, pred, ms = 4000) => { const t = Date.now(); while (Date.now() - t < ms) { const m = s.msgs.find(pred); if (m) return m; await new Promise(r => setTimeout(r, 40)); } return null; };
  try {
    sim.configure({ enabled: true, profiles: ['realtek', 'asio4all'], faults: {} });
    let devs = require('./portaudio').load().getDevices();
    let a = open(devs, 'ASIO4ALL v2'); assert.ok(await until(a, m => m.type === 'started' && m.hostApi === 'ASIO'), 'ASIO4ALL alone opens on ASIO'); a.sess.onClose();
    sim.configure({ profiles: ['um2', 'realtek', 'asio4all'] }); devs = require('./portaudio').load().getDevices();
    const um = open(devs, 'BEHRINGER USB AUDIO'); assert.ok(await until(um, m => m.type === 'started' && m.hostApi === 'ASIO'));
    a = open(devs, 'ASIO4ALL v2');
    const fb = await until(a, m => m.type === 'fallback' || m.type === 'error');
    assert.ok(fb && fb.type === 'fallback', 'a fallback, not an error: ' + JSON.stringify(a.msgs.slice(0, 2)));
    assert.strictEqual(fb.fallback.to, 'Microphone Array (Realtek(R) Audio)', 'not the UM2 endpoint its own ASIO driver is reading');
    assert.ok(await until(a, m => m.type === 'started' && /WASAPI/.test(m.hostApi)));
    assert.strictEqual(dl.get('ASIO4ALL v2', 'input'), null, 'a one-ASIO-per-program refusal is a situation, never locked');
    a.sess.onClose(); um.sess.onClose();
  } finally { sim.configure({ enabled: false, faults: {} }); sim.closeAll(); dl.forget('*'); }
});

// ---- Realtek WASAPI pin (1.22.1.0 Beta): in-app routing only, no driver / registry / scheduled-task changes -----------------------------------
test('Realtek pin: the Realtek ASIO driver is pinned to the chip WASAPI endpoint by default; UNPIN lets ASIO go first; never a loopback; other drivers untouched', () => {
  const dl = require('./driver-lock'); dl.forget('*'); dl.setPin('Realtek ASIO', true);
  const devs = [{ name: 'Realtek ASIO', hostAPIName: 'ASIO', maxInputChannels: 2, maxOutputChannels: 2 },
    { name: 'Stereo Mix (Realtek High Definition Audio)', hostAPIName: 'Windows WASAPI', maxInputChannels: 2, maxOutputChannels: 0 },
    { name: 'Microphone (Realtek High Definition Audio)', hostAPIName: 'Windows WASAPI', maxInputChannels: 2, maxOutputChannels: 0 },
    { name: 'Speakers (Realtek High Definition Audio)', hostAPIName: 'Windows WASAPI', maxInputChannels: 0, maxOutputChannels: 2 },
    { name: 'BEHRINGER USB AUDIO', hostAPIName: 'ASIO', maxInputChannels: 2, maxOutputChannels: 2 }];
  const pin = dl.pinOf('Realtek ASIO'); assert.ok(pin && pin.on && pin.builtIn); assert.match(pin.reason, /WASAPI/);
  assert.strictEqual(dl.pinOf('BEHRINGER USB AUDIO'), null, 'only Realtek'); assert.strictEqual(dl.pinOf('ASIO4ALL v2'), null, 'a wrapper is never pinned');
  let L = dl.ladder('Realtek ASIO', devs, 'input');
  assert.strictEqual(L[0].available, false); assert.strictEqual(L[0].pinned, true); assert.match(L[0].why, /pinned/);
  assert.strictEqual(L[2].device.name, 'Microphone (Realtek High Definition Audio)', 'the microphone, never Stereo Mix');
  let r = dl.route(devs[0], devs, 'input'); assert.strictEqual(r.dev.name, 'Microphone (Realtek High Definition Audio)'); assert.strictEqual(r.locked.pinned, true);
  r = dl.route(devs[0], devs, 'output'); assert.strictEqual(r.dev.name, 'Speakers (Realtek High Definition Audio)');
  assert.strictEqual(dl.route(devs[4], devs, 'input').dev, devs[4], 'the UM2 keeps its own ASIO driver');
  assert.deepStrictEqual(dl.pins(devs).map(p => p.name), ['Realtek ASIO']);
  dl.setPin('Realtek ASIO', false);
  assert.strictEqual(dl.pinOf('Realtek ASIO').on, false); assert.strictEqual(dl.route(devs[0], devs, 'input').dev, devs[0], 'unpinned: ASIO first');
  assert.strictEqual(dl.ladder('Realtek ASIO', devs, 'input')[0].available, true);
  dl.reset(); assert.strictEqual(dl.pinOf('Realtek ASIO').on, false, 'the opt-out is saved');
  dl.setPin('Realtek ASIO', true); assert.strictEqual(dl.pinOf('Realtek ASIO').on, true);
  assert.strictEqual(dl.setPin('BEHRINGER USB AUDIO', false), null, 'cannot pin other hardware');
  const src = require('node:fs').readFileSync(require('node:path').join(__dirname, 'driver-lock.js'), 'utf8');
  assert.ok(!/reg(\.exe)?\s+(add|delete)|schtasks|rename(Sync)?\(|unlinkSync|MMDevices/i.test(src), 'no OS or driver tampering in the pin');
});

test('Realtek pin on the simulator: Realtek ASIO opens its WASAPI endpoint straight away (ASIO never opened); the API lists and toggles the pin', { timeout: 30000 }, async () => {
  const sim = require('./sim'), dl = require('./driver-lock'); dl.forget('*'); dl.setPin('Realtek ASIO', true);
  const until = async (s, pred, ms = 3000) => { const t = Date.now(); while (Date.now() - t < ms) { const m = s.msgs.find(pred); if (m) return m; await new Promise(r => setTimeout(r, 40)); } return null; };
  try {
    sim.configure({ enabled: true, profiles: ['um2', 'realtekasio'], faults: {} });   // one Realtek chip, as on a real PC
    const devs = require('./portaudio').load().getDevices(), rt = devs.find(d => d.name === 'Realtek ASIO');
    assert.ok(rt, 'the simulator has a Realtek ASIO driver');
    const s = { msgs: [] }; s.sess = require('./input').createInputSession({ send: m => s.msgs.push(JSON.parse(m)), sendBinary() {}, close() {} });
    s.sess.onText(JSON.stringify({ type: 'start', deviceId: rt.id, channels: 2, sampleRate: 48000 }));
    const fb = await until(s, m => m.type === 'fallback'); assert.ok(fb && fb.fallback.via === 'lock' && /Realtek onboard/.test(fb.fallback.reason), JSON.stringify(s.msgs.slice(0, 2)));
    assert.strictEqual(fb.fallback.to, 'Microphone (Realtek High Definition Audio)');
    assert.ok(await until(s, m => m.type === 'started' && /WASAPI/.test(m.hostApi)));
    assert.ok(s.msgs.every(m => !(m.type === 'started' && m.hostApi === 'ASIO')), 'the Realtek ASIO driver is not opened');
    assert.strictEqual(dl.get('Realtek ASIO', 'input'), null, 'a pin is policy, not a learned lock');
    s.sess.onClose();
    const { server } = require('./server');
    await new Promise(r => server.listen(0, '127.0.0.1', r)); const base = 'http://127.0.0.1:' + server.address().port;
    try {
      const j = await (await fetch(base + '/api/driver-lock')).json();
      assert.ok(j.pins.some(p => p.name === 'Realtek ASIO' && p.on));
      const rtl = j.interfaces.find(i => i.name === 'Realtek ASIO'); assert.strictEqual(rtl.input[0].pinned, true);
      const post = body => fetch(base + '/api/driver-lock', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Mixer-Action': 'driver-lock' }, body: JSON.stringify(body) });
      assert.strictEqual((await (await post({ pin: 'Realtek ASIO', on: false })).json()).pin.on, false);
      assert.strictEqual((await post({ pin: 'BEHRINGER USB AUDIO', on: false })).status, 400);
      assert.strictEqual((await (await post({ pin: 'Realtek ASIO', on: true })).json()).pin.on, true);
    } finally { await new Promise(r => server.close(r)); }
  } finally { sim.configure({ enabled: false, faults: {} }); sim.closeAll(); dl.forget('*'); dl.setPin('Realtek ASIO', true); }
});


// ---- SURROUND MASTER (1.22.1.0 Beta): STEREO / QUAD / 7.1.2 virtual master pages, 10 channels, QUAD + SURROUND for Pro / Studio Master -----


// ---- ARRANGE (1.22.1.0 Beta): a DAW-style arrangement page without recording ---------------------------------------------------------------

// ---- Key Generator second factor (TOTP, scripts/license-totp.js) --------------------------------------------------------------------------
test('license-totp: RFC 6238 vectors, base32, setup -> confirm -> verify with the PIN, replay and wrong code refused, generator unlock needs the code', () => {
  const T = require("../scripts/license-totp"), vault = require("../scripts/license-vault"), gen = require("../scripts/license-generator"), fs = fsx, os = osx, crypto = cryptox;
  const secret = Buffer.from('12345678901234567890');
  assert.strictEqual(T.totp(secret, 59, 8), 94287082); assert.strictEqual(T.totp(secret, 1111111109, 8), 7081804); assert.strictEqual(T.totp(secret, 1234567890, 8), 89005924); assert.strictEqual(T.totp(secret, 20000000000, 8), 65353130);
  assert.strictEqual(T.base32(secret), 'GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ'); assert.deepStrictEqual(T.unbase32('gezd gnbv-GY3TQOJQGEZDGNBVGY3TQOJQ'), secret);
  const dir = fs.mkdtempSync(pathx.join(os.tmpdir(), 'amx-totp-')); const pin = 'correct-horse';
  const { privateKey } = crypto.generateKeyPairSync('ec', { namedCurve: 'P-256' });
  vault.writeLocked(dir, privateKey.export({ type: 'pkcs8', format: 'pem' }), pin);
  assert.strictEqual(T.isSet(dir), false); assert.strictEqual(T.verify(dir, pin, '000000'), true, 'nothing set up: the PIN alone decides');
  const s = T.setup(dir, pin); assert.match(s.secret, /^[A-Z2-7]{32}$/); assert.ok(s.uri.startsWith('otpauth://totp/')); assert.ok(s.uri.includes('secret=' + s.secret));
  assert.strictEqual(T.isSet(dir), false, 'pending until confirmed');
  T.setup(dir, pin);   // a pending (unconfirmed) secret may be replaced: a mistyped one must not lock the vendor out
  const s2 = T.setup(dir, pin); assert.notStrictEqual(s2.secret, s.secret); Object.assign(s, s2);
  const now = 1_700_000_000_000, sec = T.unbase32(s.secret), code = T.pad(T.totp(sec, now / 1000));
  assert.throws(() => T.confirm(dir, pin, '123456', now), /wrong authenticator code/);
  assert.throws(() => T.confirm(dir, 'wrong-pin!!', code, now), /wrong PIN/);
  assert.strictEqual(T.confirm(dir, pin, code, now), true); assert.strictEqual(T.isSet(dir), true);
  assert.throws(() => T.verify(dir, pin, code, now), /wrong authenticator code/, 'the confirming code cannot be replayed');
  const next = T.pad(T.totp(sec, now / 1000 + 30)); assert.strictEqual(T.verify(dir, pin, next, now + 30000), true); assert.throws(() => T.verify(dir, pin, next, now + 31000), /wrong/, 'no replay');
  assert.strictEqual(T.verify(dir, pin, T.pad(T.totp(sec, now / 1000 + 90)), now + 60000), true, 'one step ahead is accepted (clock drift)');
  assert.throws(() => T.verify(dir, pin, T.pad(T.totp(sec, now / 1000 + 300)), now + 120000), /wrong/, 'far ahead is not');
  // the generator session: PIN alone no longer opens; PIN + code does; the secret file alone says nothing (encrypted with the PIN)
  let clock = now + 200000; const session = gen.makeSession(() => clock);
  assert.throws(() => session.unlock(dir, pin), /6 digits/); assert.strictEqual(session.isOpen(), false);
  assert.throws(() => session.unlock(dir, pin, '000000'), /wrong authenticator code/);
  session.unlock(dir, pin, T.pad(T.totp(sec, clock / 1000))); assert.strictEqual(session.isOpen(), true);
  const onDisk = JSON.parse(fs.readFileSync(T.file(dir), 'utf8')); assert.ok(onDisk.box && onDisk.box.ct && !JSON.stringify(onDisk).includes(s.secret), 'the secret is not on disk in clear');
  assert.throws(() => T.remove(dir, 'wrong-pin!!'), /wrong PIN/); assert.strictEqual(T.remove(dir, pin), true); assert.strictEqual(T.isSet(dir), false);
  fs.rmSync(dir, { recursive: true, force: true });
});

test('OTA server: the staff sign-in shared with the licence server opens the admin API; without PORTAL_* it stays token-only', async () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path'), http = require('node:http');
  const { createOta } = require('../ota-server/server'), { createPortalAuth } = require('./portal-auth/auth'), lic = require('../scripts/license');
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ota-staff-')), keys = path.join(tmp, 'vendor'); lic.initKeys(keys); const jwk = lic.loadPublic(keys);
  const db = path.join(tmp, 'portal.db'), secret = 'q'.repeat(48);
  const staff = createPortalAuth({ dbFile: db, secret }); staff.addStaff({ email: 'ops@amfb.my', name: 'Ops', password: 'a long staff password' });
  const ota = createOta({ dataDir: path.join(tmp, 'data'), token: 'secret-token-1234567890', jwk, allowHttp: true, portalDb: db, portalSecret: secret });
  const s = http.createServer(ota.handler); await new Promise(r => s.listen(0, '127.0.0.1', r)); const base = `http://127.0.0.1:${s.address().port}`;
  const plain = createOta({ dataDir: path.join(tmp, 'data2'), token: 'secret-token-1234567890', jwk, allowHttp: true }), s2 = http.createServer(plain.handler);
  await new Promise(r => s2.listen(0, '127.0.0.1', r)); const base2 = `http://127.0.0.1:${s2.address().port}`;
  try {
    const page = await fetch(base + '/admin/login'); assert.strictEqual(page.status, 200); assert.match(page.headers.get('content-security-policy'), /cdn\.tailwindcss\.com/);
    assert.strictEqual((await fetch(base + '/admin/login.js')).status, 200);
    const bad = await fetch(base + '/admin/auth/login', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ email: 'ops@amfb.my', password: 'wrong wrong wrong' }) }); assert.strictEqual(bad.status, 401);
    const li = await (await fetch(base + '/admin/auth/login', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ email: 'ops@amfb.my', password: 'a long staff password' }) })).json();
    assert.ok(li.ok && li.token);
    assert.strictEqual((await fetch(base + '/admin/files', { headers: { authorization: 'Bearer ' + li.token } })).status, 200);
    const fromLicence = staff.login('ops@amfb.my', 'a long staff password').token;   // a token the licence server gave: the same secret, the same database
    assert.strictEqual((await fetch(base + '/admin/files', { headers: { authorization: 'Bearer ' + fromLicence } })).status, 200);
    assert.strictEqual((await fetch(base + '/admin/files', { headers: { authorization: 'Bearer ' + li.token.slice(0, -2) + 'xx' } })).status, 401);
    assert.strictEqual((await fetch(base + '/admin/files')).status, 401);
    // without PORTAL_DB / PORTAL_JWT_SECRET: no sign-in page, no login route, a staff token is refused
    assert.strictEqual((await fetch(base2 + '/admin/login')).status, 401);
    assert.notStrictEqual((await fetch(base2 + '/admin/auth/login', { method: 'POST', headers: { 'content-type': 'application/json' }, body: '{}' })).status, 200);
    assert.strictEqual((await fetch(base2 + '/admin/files', { headers: { authorization: 'Bearer ' + li.token } })).status, 401);
  } finally { s.close(); s2.close(); staff.close(); fs.rmSync(tmp, { recursive: true, force: true }); }
});
