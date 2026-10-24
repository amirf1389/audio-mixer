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
  assert.ok(CATALOG.every(x => /^https:\/\//.test(x.url)), 'every official link is https');
  assert.ok(CATALOG.every(x => !('download' in x)) && win.every(x => !('downloadable' in x)), 'no entry offers a download any more');
  const cat = require('./catalog'); assert.deepStrictEqual(Object.keys(cat).sort(), ['CATALOG', 'isInstalled', 'listCatalog']);   // no downloader in the module
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

test('build produces a self-contained PC-mode package that serves the mixer', async () => {
  const { build, FILES, DIRS, listDir } = require('../scripts/build');
  const out = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'dist-'));
  const r = build({ out });
  assert.strictEqual(r.dest, pathx.join(out, 'audio-mixer-pc'));
  for (const f of FILES) assert.ok(fsx.existsSync(pathx.join(r.dest, f)), 'missing ' + f);
  assert.ok(!fsx.existsSync(pathx.join(r.dest, 'bridge', 'test.js')) && !fsx.existsSync(pathx.join(r.dest, 'bridge', 'audit.js')));
  const pkg = JSON.parse(fsx.readFileSync(pathx.join(r.dest, 'package.json'), 'utf8'));
  assert.ok(!('test' in pkg.scripts) && pkg.scripts.start);
  const manifest = fsx.readFileSync(pathx.join(r.dest, 'MANIFEST.sha256'), 'utf8').trim().split('\n');
  assert.strictEqual(manifest.length, FILES.length + DIRS.flatMap(listDir).length);
  for (const d of ['bridge', 'client', 'scripts', 'deploy', 'native']) assert.ok(fsx.existsSync(pathx.join(r.dest, d)), d);          // folders that go to Program Files
  const walkAll = d => fsx.readdirSync(d, { withFileTypes: true }).flatMap(e => [e.name, ...(e.isDirectory() ? walkAll(pathx.join(d, e.name)) : [])]);
  assert.ok(!walkAll(r.dest).some(n => n.startsWith('.')), 'no .vscode / dotfiles in the installed tree');
  for (const line of manifest) {
    const [sum, rel] = line.split('  ');
    assert.strictEqual(cryptox.createHash('sha256').update(fsx.readFileSync(pathx.join(r.dest, rel))).digest('hex'), sum);
  }
  const built = require(pathx.join(r.dest, 'bridge', 'server.js'));
  const port = await built.start(0);
  const page = await fetch(`http://127.0.0.1:${port}/`);
  assert.strictEqual(page.status, 200);
  assert.ok((await page.text()).length > 100000);
  assert.strictEqual((await (await fetch(`http://127.0.0.1:${port}/api/catalog`)).json()).ok, true);
  assert.strictEqual((await fetch(`http://127.0.0.1:${port}/bridge/server.js`)).status, 404);
  built.server.closeAllConnections(); built.server.close();
  const help = require('node:child_process').spawnSync(process.execPath, [pathx.join(r.dest, 'client', 'cli.js'), '--help'], { encoding: 'utf8' });
  assert.match(help.stdout, /service install\|uninstall\|status/);
  fsx.rmSync(out, { recursive: true, force: true });
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

test('installer helpers: checksums, version, zip extraction', () => {
  const bi = require('../scripts/build-installer');
  assert.deepStrictEqual(bi.parseShasums(`${'a'.repeat(64)}  node-v1-win-x64.zip\ngarbage\n${'b'.repeat(64)} *x.msi`), { 'node-v1-win-x64.zip': 'a'.repeat(64), 'x.msi': 'b'.repeat(64) });
  assert.strictEqual(bi.version4('1.2.3'), '1.2.3.0');
  assert.strictEqual(bi.version4('1.2.3-beta.1'), '1.2.3.1');
  const zip = makeZip([['node-v1/LICENSE', Buffer.from('lic')], ['node-v1/node.exe', Buffer.alloc(5000, 7)]]);
  assert.strictEqual(bi.extractFromZip(zip, n => n.endsWith('/node.exe')).length, 5000);
  assert.strictEqual(bi.extractFromZip(zip, n => n === 'nope'), null);
  assert.throws(() => bi.extractFromZip(Buffer.from('not a zip at all, definitely'), () => true), /not a zip/);
});

test('installer: official Node.js runtime is fetched, checked and cached; a bad checksum is refused', async () => {
  const bi = require('../scripts/build-installer');
  const zip = makeZip([['node-v98.1.2-win-x64/LICENSE', Buffer.from('MIT')], ['node-v98.1.2-win-x64/node.exe', Buffer.from('MZ-fake-node')]]);
  const cache = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'rt-'));
  const r = await bi.fetchNodeRuntime({ cache, fetchImpl: fakeNodeFetch(zip) });
  assert.strictEqual(r.version, 'v98.1.2');                                  // newest LTS, not the newer non-LTS
  assert.strictEqual(fsx.readFileSync(r.exe).toString(), 'MZ-fake-node');
  const again = await bi.fetchNodeRuntime({ cache, fetchImpl: async () => { throw new Error('should use the cache'); } });
  assert.strictEqual(again.exe, r.exe);
  const cache2 = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'rt-'));
  await assert.rejects(bi.fetchNodeRuntime({ cache: cache2, fetchImpl: fakeNodeFetch(zip, { badSum: true }) }), /checksum mismatch/);
  assert.deepStrictEqual(fsx.readdirSync(cache2), []);                       // nothing cached from a bad download
  await assert.rejects(bi.fetchNodeRuntime({ cache: cache2, version: '../../x', fetchImpl: fakeNodeFetch(zip) }), /bad Node\.js version/);
  fsx.rmSync(cache, { recursive: true, force: true }); fsx.rmSync(cache2, { recursive: true, force: true });
});

test('installer: stages the app with the bundled runtime for x64 and x86 (no NSIS involved)', async () => {
  const bi = require('../scripts/build-installer');
  for (const arch of ['x64', 'x86']) {
    const zip = makeZip([[`node-v98.1.2-win-${arch}/LICENSE`, Buffer.from('MIT')], [`node-v98.1.2-win-${arch}/node.exe`, Buffer.from('MZ-fake-node-' + arch)], [`node-v98.1.2-win-${arch}/node_modules/npm/bin/npm-cli.js`, Buffer.from('// npm')], [`node-v98.1.2-win-${arch}/node_modules/npm/package.json`, Buffer.from('{}')]]);
    const out = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'inst-'));
    const r = await bi.buildInstaller({ out, arch, fetchImpl: fakeNodeFetch(zip, { arch }), bundleAudify: false });
    assert.strictEqual(pathx.basename(r.stage), `stage-${arch}`);
    assert.strictEqual(fsx.readFileSync(pathx.join(r.stage, 'runtime', 'node.exe')).toString(), 'MZ-fake-node-' + arch);
    assert.strictEqual(fsx.readFileSync(pathx.join(r.stage, 'runtime', 'node_modules', 'npm', 'bin', 'npm-cli.js'), 'utf8'), '// npm');   // npm travels with the runtime
    assert.ok(fsx.readFileSync(pathx.join(r.stage, 'MANIFEST.sha256'), 'utf8').includes('  runtime/node_modules/npm/bin/npm-cli.js'));   // and is covered by the verification scan
    assert.ok(fsx.existsSync(pathx.join(r.stage, 'client', 'cli.js')) && fsx.existsSync(pathx.join(r.stage, 'MANIFEST.sha256')));
    assert.ok(!fsx.existsSync(pathx.join(r.stage, 'start-pc-mode.sh')));
    assert.match(fsx.readFileSync(pathx.join(r.stage, 'start-pc-mode.bat'), 'utf8'), /runtime\\node\.exe/);
    assert.match(fsx.readFileSync(pathx.join(r.stage, 'start-local-server.bat'), 'utf8'), /runtime\\node\.exe" bridge\\server\.js/);
    fsx.rmSync(out, { recursive: true, force: true });
  }
  assert.ok(!fsx.existsSync(pathx.join(__dirname, '..', 'platforms', 'windows', 'installer', 'audio-mixer.nsi')), 'the NSIS script is gone');
  await assert.rejects(bi.fetchNodeRuntime({ cache: osx.tmpdir(), arch: 'arm64', fetchImpl: async () => { throw new Error('x'); } }), /unknown architecture/);
});

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

test('now playing on iOS and Qt Android: the routes exist, the permissions are declared, the Java classes are staged', () => {
  const root = pathx.join(__dirname, '..'), rd = f => fsx.readFileSync(pathx.join(root, f), 'utf8');
  const sw = rd('platforms/ios/AudioMixer/LocalServer.swift');
  assert.ok(sw.includes('"/api/nowplaying"') && sw.includes('MPMusicPlayerController.systemMusicPlayer') && sw.includes('MPMediaLibrary.authorizationStatus()') && sw.includes('"access"'));
  assert.ok(rd('platforms/ios/project.yml.template').includes('NSAppleMusicUsageDescription'));
  const cpp = rd('platforms/android/qt/src/MixerServer.cpp');
  assert.ok(cpp.includes('"/api/nowplaying"') && cpp.includes('com/audiomixer/app/AndroidNowPlaying'));
  assert.ok(rd('platforms/android/qt/android/AndroidManifest.xml').includes('BIND_NOTIFICATION_LISTENER_SERVICE') && rd('platforms/android/qt/android/AndroidManifest.xml').includes('com.audiomixer.app.NowPlayingListener'));
  const prep = rd('scripts/prepare-qt.js'); for (const f of ['Json', 'NowPlaying', 'AndroidNowPlaying', 'NowPlayingListener']) { assert.ok(prep.includes("'" + f + "'"), f); assert.ok(fsx.existsSync(pathx.join(root, 'platforms/android/src/com/audiomixer/app', f + '.java')), f); }
  assert.ok(/android\/qt\/android\/src\//.test(rd('.gitignore')));
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

test('verify: setup .exe (embedded package + hash trailer, also after a signature is appended) and checksum sidecar', () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path');
  const v = require('../client/verify'), be = require('../scripts/build-exe');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'vfy-')), f = path.join(dir, 'Audio Mixer-1.exe');
  const msi = Buffer.concat([Buffer.from([0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1]), Buffer.alloc(500, 3)]);
  const code = '{87FE9B11-C8D7-5DEA-97C2-5C2ABE8BED40}';
  fs.writeFileSync(f, be.packPayload(Buffer.concat([Buffer.from('MZ'), Buffer.alloc(300)]), msi, code));
  fs.writeFileSync(f + '.sha256', `${v.sha256(f)}  Audio Mixer-1.exe\n`);
  assert.ok(v.checkInstallerFile(f).every(r => r.level === 'PASS'));
  const t = be.readTrailer(fs.readFileSync(f), fs.readFileSync(f).length);
  assert.strictEqual(t.productCode, code); assert.ok(t.payload.equals(msi));
  fs.writeFileSync(f + '.sha256', `${'1'.repeat(64)}  x\n`);
  assert.ok(v.checkInstallerFile(f).some(r => r.level === 'FAIL' && /does NOT match/.test(r.title)));
  const bad = fs.readFileSync(f); bad[400] ^= 0xff;                          // flip a payload byte
  fs.writeFileSync(f, bad); fs.unlinkSync(f + '.sha256');
  assert.ok(v.checkInstallerFile(f).some(r => r.level === 'FAIL' && /changed after the build/.test(r.title)));
  fs.writeFileSync(f, 'not an exe');
  assert.ok(v.checkInstallerFile(f).some(r => r.level === 'FAIL' && /executable/.test(r.title)));
  assert.throws(() => be.packPayload(Buffer.alloc(4), msi, 'not-a-guid'), /bad product code/);
  fs.rmSync(dir, { recursive: true, force: true });
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
    getApi() { return ({ 6: 'ASIO', 7: 'WASAPI', 3: 'JACK' })[this.api] || 'Dummy'; }
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

test('installer bundles Audify: pinned Windows binaries, copied into the stage and listed in the manifest', () => {
  const bi = require('../scripts/build-installer');
  const cache = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'au-'));
  const mkRun = (content) => (cmd, args, opts) => {
    const nm = pathx.join(opts.cwd.endsWith('audify') ? pathx.dirname(opts.cwd) : opts.cwd, opts.cwd.endsWith('audify') ? '' : 'node_modules');
    if (!opts.cwd.endsWith('audify')) {
      for (const [m, files] of [['audify', ['index.js', 'package.json']], ['bindings', ['bindings.js']], ['file-uri-to-path', ['index.js']], ['prebuild-install', ['bin.js']]]) {
        fsx.mkdirSync(pathx.join(nm, m), { recursive: true }); files.forEach(f => fsx.writeFileSync(pathx.join(nm, m, f), '//'));
      }
    } else {
      const rel = pathx.join(opts.cwd, 'build', 'Release'); fsx.mkdirSync(rel, { recursive: true });
      Object.keys(bi.AUDIFY_WIN_SHA256.x64).forEach(f => fsx.writeFileSync(pathx.join(rel, f), content));
    }
    return { status: 0 };
  };
  assert.throws(() => bi.fetchAudify({ cache, run: mkRun('tampered') }), /checksum mismatch/);   // a binary that is not the pinned build is refused and removed
  const stage = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'st-'));
  const fake = { version: 'x', nm: pathx.join(cache, 'audify-1.10.1', 'node_modules'), dirs: [['audify', ['index.js', 'build/Release']], ['bindings', ['bindings.js']], ['file-uri-to-path', ['index.js']]] };
  fsx.mkdirSync(pathx.join(fake.nm, 'audify', 'build', 'Release'), { recursive: true }); fsx.writeFileSync(pathx.join(fake.nm, 'audify', 'build', 'Release', 'audify.node'), 'x');
  bi.stageAudify(fake, stage);
  assert.ok(fsx.existsSync(pathx.join(stage, 'bridge', 'node_modules', 'audify', 'build', 'Release', 'audify.node')));
  assert.ok(fsx.existsSync(pathx.join(stage, 'bridge', 'node_modules', 'bindings', 'bindings.js')));
  fsx.rmSync(cache, { recursive: true, force: true }); fsx.rmSync(stage, { recursive: true, force: true });
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

test('msi: stable GUIDs, per-machine Program Files paths for x64 and x86, per-user flavour, uninstall entries', () => {
  const m = require('../scripts/build-msi');
  assert.strictEqual(m.guid('a/b.js'), m.guid('a/b.js')); assert.notStrictEqual(m.guid('a/b.js'), m.guid('a/c.js'));
  assert.match(m.guid('x'), /^[0-9A-F]{8}-[0-9A-F]{4}-5[0-9A-F]{3}-[89AB][0-9A-F]{3}-[0-9A-F]{12}$/);
  assert.strictEqual(m.productCode('x64', 'machine', '1.4.0'), m.productCode('x64', 'machine', '1.4.0'));
  assert.notStrictEqual(m.productCode('x64', 'machine', '1.4.0'), m.productCode('x86', 'machine', '1.4.0'));
  assert.strictEqual(new Set(Object.values(m.UPGRADE_CODES)).size, 4);
  const stage = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'msi-'));
  fsx.mkdirSync(pathx.join(stage, 'bridge')); fsx.writeFileSync(pathx.join(stage, 'bridge', 'a & b.js'), 'x'); fsx.writeFileSync(pathx.join(stage, 'LICENSE'), 'MIT');
  const x64 = m.wxs({ stage, version: '1.4.0', arch: 'x64' });
  assert.match(x64, /InstallScope="perMachine"/); assert.match(x64, /Directory Id="ProgramFiles64Folder"/); assert.match(x64, /Win64="yes"/);
  assert.match(x64, /Root="HKLM"/); assert.match(x64, /UpgradeCode="6F3C2B8E-5D41-4A7B-9C0E-2A1D7B64F3A9"/);
  assert.match(x64, /Name="a &amp; b\.js"/); assert.ok(!/Feature Id="Autostart"|AutostartRun|CurrentVersion\\Run/.test(x64), 'no start at login in the package'); assert.match(x64, /Feature Id="Desktop"[^>]*Level="1"/);
  assert.match(x64, /Id="ScUninstall"[^>]*msiexec\.exe" Arguments="\/x \{[0-9A-F-]{36}\}"/);          // Start Menu uninstall entry
  assert.match(x64, /ARPURLINFOABOUT/);                                                                  // Settings > Apps entry details
  assert.match(x64, /Id="ScPc"[^>]*AudioMixerServer\.exe" Arguments="\/open"/);                     // main shortcut: start-up screen, then the browser
  assert.match(x64, /Id="ScPlugins"[^>]*AudioMixerServer\.exe" Arguments="\/plugins"/);             // native launcher, no cmd one-liner
  assert.ok(!/CurrentVersion\\Run|Name="AudioMixer" Type="string" Value="&quot;/.test(x64), 'no Run entry: the package does not start anything at login');
  assert.ok(!/vbs|wscript|cmd\.exe|NSIS/i.test(x64), 'no scripts, no shell one-liners');
  const x86 = m.wxs({ stage, version: '1.4.0', arch: 'x86' });
  assert.match(x86, /Directory Id="ProgramFilesFolder"/); assert.ok(!/ProgramFiles64Folder/.test(x86)); assert.ok(!/Win64="yes"/.test(x86));
  const user = m.wxs({ stage, version: '1.4.0', arch: 'x64', scope: 'user' });
  assert.match(user, /InstallScope="perUser"/); assert.match(user, /LocalAppDataFolder/); assert.match(user, /Root="HKCU"/);
  assert.throws(() => m.wxs({ stage, version: '1', vbs: 'x', arch: 'arm64' }), /bad arch/);
  assert.strictEqual((x64.match(/<File /g) || []).length, 2);                                           // exactly the staged files
  assert.match(m.rtf('a\\b {c}\nü'), /^\{\\rtf1.*a\\\\b \\\{c\\\}\\par\n\\u252\?\}$/s);
  fsx.rmSync(stage, { recursive: true, force: true });
});

test('msi: Start Menu tool shortcuts and optional features (plugin host, helpers) split by path', () => {
  const m = require('../scripts/build-msi');
  const stage = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'msi-'));
  for (const d of ['native/host/x64', 'native/win/x64', 'client']) fsx.mkdirSync(pathx.join(stage, d), { recursive: true });
  fsx.writeFileSync(pathx.join(stage, 'native/host/x64/PluginHost.exe'), 'x'); fsx.writeFileSync(pathx.join(stage, 'native/win/x64/AudioDevices.exe'), 'x'); fsx.writeFileSync(pathx.join(stage, 'client/cli.js'), 'x');
  const w = m.wxs({ stage, version: '1.14.0', arch: 'x64' });
  for (const id of ['ScLicense', 'ScPluginList', 'ScDrivers', 'ScUpdate', 'ScDoctor']) assert.match(w, new RegExp(`Id="${id}"`));
  assert.match(w, /cli\.js&quot; license --pause/); assert.match(w, /cli\.js&quot; update --pause/);
  const feat = id => (w.match(new RegExp(`<Feature Id="${id}"[\\s\\S]*?</Feature>`)) || [''])[0];
  assert.match(feat('PluginHost'), /PluginHost_exe|PluginHost\.exe/i); assert.match(feat('WinHelpers'), /AudioDevices/i);
  assert.ok(!/PluginHost|AudioDevices/i.test(feat('Main')), 'optional files are not in Main');
  assert.match(feat('Main'), /ComponentRef Id="c_client_cli_js"|cli_js/);
  assert.match(feat('Tools'), /ToolShortcuts/);
  const f = m.filesXml(stage, true); assert.strictEqual(f.groups.PluginHost.length, 1); assert.strictEqual(f.groups.WinHelpers.length, 1); assert.strictEqual(f.comps.length, 1);
  fsx.rmSync(stage, { recursive: true, force: true });
});

test('cli: license / plugins / update commands exist and the stub has the feature switches', () => {
  const cli = fsx.readFileSync(pathx.join(__dirname, '..', 'client', 'cli.js'), 'utf8');
  for (const c of ["'license'", "'plugins'", "'update'"]) assert.ok(cli.includes(`o.cmd === ${c}`));
  const stub = fsx.readFileSync(pathx.join(__dirname, '..', 'platforms', 'windows', 'installer', 'setup-stub.c'), 'utf8');
  for (const sw of ['/noplugins', '/nohelpers', '/notools', '/noshortcuts', '/desktop', '/nodesktop', '/noautostart']) assert.ok(stub.includes(`L"${sw}"`), sw);
  assert.match(stub, /ADDLOCAL=Main/);
  assert.match(stub, /stop_old_server\(old\)/); assert.match(stub, /_wcsnicmp\(path, dir, n\)/);   // upgrade ends only processes that run from the install folder
});

test('page: interfaces opened by LIVE SOURCES reach the mixer channels, and LIVE INPUT PATCH lists them', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  assert.match(html, /if \(this\.cfg\.auto && !\(this\.cfg\.noAutoPatch && this\.cfg\.noAutoPatch\[cap\.key\]\)\) this\.autoPatchOne\(cap\)/);        // a freshly read interface (and the browser microphone, guarded against feedback by the real-time engine) is patched unless the user unpatched it
  assert.match(html, /autoPatchOne\(cap\) \{/); assert.match(html, /value="ls:\$\{esc\(i\.key\)\}"/);  // routing page offers the server interfaces
  assert.match(html, /\/\^ls:\/\.test\(String\(deviceId\)\)/); assert.match(html, /l\.shared/);        // shared capture nodes are branched, never closed by REMOVE
});

test('page: an interface knows whether it is a DAC (write only), an input (read only) or both, and automatic choices follow the device', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  assert.match(html, /label: read && write \? 'READ \+ WRITE' : write \? 'DAC \/ OUTPUT: WRITE ONLY'/);
  assert.match(html, /if \(cur && !cur\.auto\) return;/);                                        // user choices are kept, automatic ones are refreshed
  assert.match(html, /write: e0\.write && m\.write/);                                             // a DAC is never read, an input-only device never written
  assert.match(html, /e\[what\] = !!on; e\.auto = false;/);
});

test('boot screens: browser page + launcher helper (macOS / Linux), native Windows splash, Android and iOS start-up views', () => {
  const rd = (...a) => fsx.readFileSync(pathx.join(__dirname, '..', ...a), 'utf8');
  const boot = rd('boot.html');
  assert.match(boot, /api\/status/); assert.match(boot, /location\.replace\(url\)/); assert.ok(!/<script[^>]+src=/.test(boot) && !/https?:\/\/(?!localhost)/.test(boot.replace(/'http:\/\/localhost:'/g, '')), 'self-contained');
  const en = rd('platforms', 'linux', 'ensure-node.sh');
  assert.match(en, /am_splash\(\)/); assert.match(en, /AM_SPLASH_ARGS="--no-open"/); assert.match(en, /AUDIO_MIXER_NO_SPLASH/);
  const unix = rd('scripts', 'build-unix.js');
  assert.strictEqual((unix.match(/am_splash "\$APP" "\$@"/g) || []).length, 3);                       // Linux .deb launcher, macOS app, portable command
  assert.strictEqual((unix.match(/cli\.js" \$AM_SPLASH_ARGS "\$@"/g) || []).length, 3);              // no second browser window when the screen is open
  assert.ok(rd('scripts', 'build.js').includes("'boot.html'"));
  const l = rd('platforms', 'windows', 'installer', 'launcher.c');
  assert.match(l, /\/open/); assert.match(l, /AudioMixerBoot/); assert.match(l, /server_up\(\)/);
  assert.ok(rd('scripts', 'build-exe.js').includes("'-lgdi32', '-lws2_32'"));
  assert.match(rd('platforms', 'android', 'src', 'com', 'audiomixer', 'app', 'MainActivity.java'), /new BootView\(this\)/);
  assert.match(rd('platforms', 'android', 'src', 'com', 'audiomixer', 'app', 'BootView.java'), /TITAN OS/);
  assert.match(rd('platforms', 'ios', 'AudioMixer', 'AudioMixerApp.swift'), /struct BootView: View/);
});

test('Windows tray icon (/open stays resident): reopen the mixer, stop/start the local server by its real pid, exit shows a system notification; compiles clean (-Werror)', () => {
  const cp = require('node:child_process');
  const l = fsx.readFileSync(pathx.join(__dirname, '..', 'platforms', 'windows', 'installer', 'launcher.c'), 'utf8');
  // the server's pid comes from its own GET /api/power, not from a launcher-held process handle (client.js --ensure may itself exit once the real server answers)
  assert.match(l, /static int server_pid\(void\)/); assert.match(l, /GET \/api\/power HTTP\/1\.0/); assert.match(l, /strstr\(buf, "\\"pid\\":"\)/);
  assert.match(l, /OpenProcess\(PROCESS_TERMINATE, FALSE, \(DWORD\)pid\)/); assert.match(l, /TerminateProcess\(p, 0\)/);
  // the menu: reopen, stop/start (label follows whether the server answers right now), exit
  assert.match(l, /ID_TRAY_OPEN/); assert.match(l, /ID_TRAY_TOGGLE/); assert.match(l, /ID_TRAY_EXIT/);
  assert.match(l, /running \? L"Stop Server" : L"Start Server"/);
  // exit stops the server first, then a real Windows notification (the tray icon's own balloon, NIIF_INFO) before the icon is removed
  assert.match(l, /case ID_TRAY_EXIT:\s*\n\s*if \(server_pid\(\)\) stop_server_now\(\);\s*\n\s*tray_notify\(L"Audio Mixer", L"Audio Mixer has shut down\."\);/);
  assert.match(l, /dwInfoFlags = NIIF_INFO/);
  assert.match(l, /Shell_NotifyIconW\(NIM_ADD, &g_nid\)/); assert.match(l, /Shell_NotifyIconW\(NIM_DELETE, &g_nid\)/);
  assert.match(l, /if \(open\) \{ finish_open\(inst\); run_tray\(inst\); \}/);                        // only the interactive /open flow gets a tray icon
  // -Wall -Wextra -Werror, the same flags build-exe.js compiles it with, and the full link through the real build function (resources, icon, libs)
  const gcc = cp.spawnSync('which', ['x86_64-w64-mingw32-gcc']);
  if (gcc.status !== 0) return;
  const r = cp.spawnSync('x86_64-w64-mingw32-gcc', ['-municode', '-Wall', '-Wextra', '-Werror', '-c', pathx.join(__dirname, '..', 'platforms', 'windows', 'installer', 'launcher.c'), '-o', pathx.join(osx.tmpdir(), 'launcher-tray-check.o')], { encoding: 'utf8' });
  try { assert.strictEqual(r.status, 0, r.stderr); } finally { fsx.rmSync(pathx.join(osx.tmpdir(), 'launcher-tray-check.o'), { force: true }); }
  const { compileLauncher } = require('../scripts/build-exe');
  const work = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'launcher-tray-'));
  try { const exe = compileLauncher({ work, version: '1.15.0.0', arch: 'x64' }); assert.ok(fsx.statSync(exe).size > 10000); }
  finally { fsx.rmSync(work, { recursive: true, force: true }); }
});

test('page: the header mic FFT follows the RTA axis and is fed by every LIVE SOURCES interface', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  assert.match(html, /micAnalyser\.fftSize = 4096/);                                                  // 512 points: 94 Hz per bin, nothing readable below ~250 Hz
  assert.match(html, /AX = \[20, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000, 20000\]/);         // the labels under the RTA
  assert.ok(!/Math\.pow\(i \/ bars, 1\.8\)/.test(html), 'the old power-curve bin mapping is gone');
  assert.match(html, /syncMicFeed\(\) \{/); assert.match(html, /\(window\.state\.micActive \|\| window\.state\.micFeeds\)/);
  assert.match(html, /\$\('ls-mic-sel'\)\.onchange = e => \{ this\.mic = e\.target\.value \|\| null; this\.syncMicFeed\(\); \};/);
});

test('upgrade: an older server on the port is replaced instead of reused (CLI) and the Windows launcher waits for the current version', async () => {
  const http = require('node:http');
  const cli = pathx.join(__dirname, '..', 'client', 'cli.js');
  const src = fsx.readFileSync(cli, 'utf8');
  assert.match(src, /async function replaceStaleServer/); assert.match(src, /upd\.cmpVersion\(info\.version, own\) > 0\) return 'newer'/);   // never replaces a newer one
  assert.match(src, /argv\.includes\('--ensure'\)/);
  const l = fsx.readFileSync(pathx.join(__dirname, '..', 'platforms', 'windows', 'installer', 'launcher.c'), 'utf8');
  assert.match(l, /server_current\(void\)/); assert.match(l, /AMIX_VERSION/); assert.match(l, /cli\.js\\" --no-open --ensure/);
  assert.ok(fsx.readFileSync(pathx.join(__dirname, '..', 'scripts', 'build-exe.js'), 'utf8').includes('-DAMIX_VERSION='));
  // run it for real: a fake OLD server answers /api/status, the client ends it (pid from the status) and starts the current one on that port
  const { spawn } = require('node:child_process');
  const port = 20000 + Math.floor(Math.random() * 20000);
  const old = spawn(process.execPath, ['-e', `require('http').createServer((q,r)=>{r.setHeader('Content-Type','application/json');r.end(JSON.stringify({ok:true,name:'audio-mixer-bridge',version:'1.0.0',pid:process.pid}))}).listen(${port},'127.0.0.1')`], { stdio: 'ignore' });
  const get = () => new Promise(res => http.get({ host: '127.0.0.1', port, path: '/api/status', timeout: 1500 }, r => { let b = ''; r.on('data', d => { b += d; }); r.on('end', () => { try { res(JSON.parse(b)); } catch (_) { res(null); } }); }).on('error', () => res(null)));
  for (let i = 0; i < 40 && !(await get()); i++) await new Promise(r => setTimeout(r, 100));
  assert.strictEqual((await get()).version, '1.0.0');
  const cl = spawn(process.execPath, [cli, '--no-open', '--port', String(port)], { stdio: 'ignore', env: { ...process.env, BRIDGE_UPDATE_URL: '' } });
  let st = null; for (let i = 0; i < 80; i++) { await new Promise(r => setTimeout(r, 150)); st = await get(); if (st && st.version !== '1.0.0') break; }
  cl.kill(); old.kill();
  assert.ok(st && st.version === require('../package.json').version, 'the current server runs on the port');
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

test('installers: audio-mixer command (Windows .exe on PATH, mac / Linux link), bundled npm, install + uninstall paths on all three systems', async () => {
  const { spawnSync } = require('node:child_process');
  const root = pathx.join(__dirname, '..'), rd = (...a) => fsx.readFileSync(pathx.join(root, ...a), 'utf8');
  // client commands
  const cli = pathx.join(root, 'client', 'cli.js');
  assert.strictEqual(spawnSync(process.execPath, [cli, 'version'], { encoding: 'utf8' }).stdout.trim(), require('../package.json').version);
  assert.match(spawnSync(process.execPath, [cli, 'npm', '--version'], { encoding: 'utf8' }).stdout, /^\d+\.\d+\.\d+/);          // npm through the client
  const un = spawnSync(process.execPath, [cli, 'uninstall'], { encoding: 'utf8', input: '' });                                  // no "y": nothing is changed
  assert.match(un.stdout, /Uninstalling Audio Mixer/); assert.match(un.stdout, /Nothing was changed/);
  // Windows: console program, stub switch, MSI feature / PATH row / uninstall code
  const c = rd('platforms', 'windows', 'installer', 'audio-mixer.c');
  assert.match(c, /wmain\(void\)/); assert.match(c, /runtime\\\\node\.exe/); assert.match(c, /client\\\\cli\.js/);
  assert.ok(rd('scripts', 'build-exe.js').includes("-mconsole"));
  assert.match(rd('platforms', 'windows', 'installer', 'setup-stub.c'), /L"\/nopath"/); assert.match(rd('platforms', 'windows', 'installer', 'setup-stub.c'), /L",CommandLine"/);
  const m = require('../scripts/build-msi');
  const stage = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'cl-')); fsx.writeFileSync(pathx.join(stage, 'LICENSE'), 'MIT'); fsx.writeFileSync(pathx.join(stage, 'audio-mixer.exe'), 'x');
  const w = m.wxs({ stage, version: '1.16.0', arch: 'x64' });
  assert.match(w, /Feature Id="CommandLine"/); assert.match(w, /Component Id="CommandPath"/); assert.match(w, /Name="UninstallCode" Type="string" Value="\[ProductCode\]"/);
  if (spawnSync('wixl', ['--version']).status === 0 && spawnSync('msibuild', ['-h']).status !== null) {   // the PATH row is written into a real package
    const wk = pathx.join(stage, 'w'); fsx.mkdirSync(wk); fsx.writeFileSync(pathx.join(wk, 'License.rtf'), m.rtf('MIT')); fsx.writeFileSync(pathx.join(wk, 'a.wxs'), w);
    assert.strictEqual(spawnSync('wixl', ['--arch', 'x64', '--ext', 'ui', '-o', pathx.join(wk, 'a.msi'), pathx.join(wk, 'a.wxs')], { cwd: wk }).status, 0);
    m.addPathEntry(pathx.join(wk, 'a.msi'), wk);
    const q = pathx.join(wk, 'q'); fsx.mkdirSync(q); spawnSync('msidump', ['-t', pathx.join(wk, 'a.msi')], { cwd: q });
    assert.match(fsx.readFileSync(pathx.join(q, 'Environment.idt'), 'utf8'), /PathAudioMixer\t=\*PATH\t\[~\];\[INSTALLDIR\]\tCommandPath/);
    const seq = fsx.readFileSync(pathx.join(q, 'InstallExecuteSequence.idt'), 'utf8'); assert.match(seq, /WriteEnvironmentStrings\t\t5200/); assert.match(seq, /RemoveEnvironmentStrings\t\t3300/);
  }
  // npm of the official zip: whole tree extracted, unsafe names refused
  const bi = require('../scripts/build-installer'), zlib = require('node:zlib');
  const zipOf = entries => {
    const loc = [], cen = []; let off = 0;
    for (const [name, data] of entries) {
      const nb = Buffer.from(name), raw = zlib.deflateRawSync(Buffer.from(data));
      const h = Buffer.alloc(30); h.writeUInt32LE(0x04034b50, 0); h.writeUInt16LE(20, 4); h.writeUInt16LE(8, 8); h.writeUInt32LE(raw.length, 18); h.writeUInt32LE(Buffer.byteLength(data), 22); h.writeUInt16LE(nb.length, 26);
      const ch = Buffer.alloc(46); ch.writeUInt32LE(0x02014b50, 0); ch.writeUInt16LE(20, 4); ch.writeUInt16LE(20, 6); ch.writeUInt16LE(8, 10); ch.writeUInt32LE(raw.length, 20); ch.writeUInt32LE(Buffer.byteLength(data), 24); ch.writeUInt16LE(nb.length, 28); ch.writeUInt32LE(off, 42);
      loc.push(h, nb, raw); cen.push(ch, nb); off += 30 + nb.length + raw.length;
    }
    const cd = Buffer.concat(cen), e = Buffer.alloc(22); e.writeUInt32LE(0x06054b50, 0); e.writeUInt16LE(entries.length, 8); e.writeUInt16LE(entries.length, 10); e.writeUInt32LE(cd.length, 12); e.writeUInt32LE(off, 16);
    return Buffer.concat([...loc, cd, e]);
  };
  const dst = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'npmz-'));
  const n = bi.extractTreeFromZip(zipOf([['node-v1.0.0-win-x64/node.exe', 'N'], ['node-v1.0.0-win-x64/node_modules/npm/bin/npm-cli.js', 'cli'], ['node-v1.0.0-win-x64/node_modules/npm/lib/a/b.js', 'b']]), 'node-v1.0.0-win-x64/node_modules/npm/', dst);
  assert.strictEqual(n, 2); assert.strictEqual(fsx.readFileSync(pathx.join(dst, 'lib', 'a', 'b.js'), 'utf8'), 'b'); assert.ok(!fsx.existsSync(pathx.join(dst, 'node.exe')));
  assert.throws(() => bi.extractTreeFromZip(zipOf([['p/node_modules/npm/../../evil.js', 'x']]), 'p/node_modules/npm/', dst), /unsafe path/);
  // Linux tarball + macOS: install / uninstall scripts parse, the command resolves its own link, the dmg gets the command files
  const u = require('../scripts/build-unix');
  for (const [name, text] of [['LINUX_INSTALL', u.LINUX_INSTALL], ['LINUX_UNINSTALL', u.LINUX_UNINSTALL], ['LAUNCHER_PORTABLE', u.LAUNCHER_PORTABLE]]) {
    const f = pathx.join(stage, name + '.sh'); fsx.writeFileSync(f, text); assert.strictEqual(spawnSync('sh', ['-n', f]).status, 0, name + ' parses');
  }
  for (const [name, text] of [['MAC_INSTALL', u.MAC_INSTALL], ['MAC_UNINSTALL', u.MAC_UNINSTALL], ['MAC_COMMAND', u.MAC_COMMAND]]) {
    const f = pathx.join(stage, name + '.sh'); fsx.writeFileSync(f, text); assert.strictEqual(spawnSync('bash', ['-n', f]).status, 0, name + ' parses');
  }
  assert.match(u.LAUNCHER_PORTABLE, /while \[ -h "\$SELF" \]/); assert.match(u.LINUX_INSTALL, /ln -sf "\$DEST\/audio-mixer" "\$BIN\/audio-mixer"/); assert.match(u.LINUX_UNINSTALL, /--purge/);
  assert.match(u.MAC_INSTALL, /ln -sf "\$APP\/audio-mixer" "\$BINDIR\/audio-mixer"/); assert.match(u.MAC_UNINSTALL, /readlink "\$L"/);
  assert.match(rd('scripts', 'build-macos.js'), /Add audio-mixer command\.command/); assert.match(rd('scripts', 'build-macos.js'), /Uninstall Audio Mixer\.command/);
  // a command is not a start: no start-up screen for "audio-mixer doctor"
  const sh = pathx.join(stage, 'sp.sh'); fsx.writeFileSync(sh, `. "${pathx.join(root, 'platforms', 'linux', 'ensure-node.sh')}"\nmkdir -p "${stage}/bin"; printf '#!/bin/sh\\ntouch "${stage}/opened"\\n' > "${stage}/bin/xdg-open"; chmod +x "${stage}/bin/xdg-open"\nPATH="${stage}/bin:$PATH" DISPLAY=:0 am_splash "${root}" doctor; echo "[$AM_SPLASH_ARGS]"\nPATH="${stage}/bin:$PATH" DISPLAY=:0 am_splash "${root}" --port 8800; echo "[$AM_SPLASH_ARGS]"\n`);
  const sp = spawnSync('sh', [sh], { encoding: 'utf8' }).stdout.trim().split('\n'); assert.deepStrictEqual(sp, ['[]', '[--no-open]']);
  fsx.rmSync(stage, { recursive: true, force: true });
});

test('notifications: identical messages merge, errors stay readable, driver errors are friendly, device notices are not repeated', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  assert.match(html, /const same = Array\.from\(stack\.children\)\.find\(c => c\._msg === text/);            // x2 instead of a full stack
  assert.match(html, /NOTIFY_SLOW = \/\^\(triangle-exclamation\|microphone-slash/); assert.match(html, /Math\.min\(9000, 5000 \+ text\.length \* 25\)/);
  assert.match(html, /friendly\(msg\) \{/); assert.match(html, /probeDeviceInfo\|already open\|in use\|unavailable\|-9985/);
  assert.match(html, /once\(key, ms\) \{/); assert.match(html, /warn\(key, text\) \{/);
  assert.match(html, /self\.warn\(i\.key, i\.name \+ ': ' \+ self\.friendly\(m\.message\)\)/);          // read, duplex ...
  assert.match(html, /self\.warn\('w:' \+ a\.deviceId, \(name \|\| 'Audio output'\)/);                    // ... and write errors
  assert.match(html, /window\.notify\('Audio interface removed: ' \+ gone\.name, 'plug-circle-xmark'\)/);
  assert.ok(!/Audio devices changed:/.test(html), 'one plug event, one notice (detected / removed), not two');
  // the friendly() and once() logic itself, run on its own
  const st = html.indexOf('friendly(msg) {'), en = html.indexOf('// a notice per key', st);
  const fr = new Function('return {' + html.slice(st, en).trim().replace(/,\s*$/, '') + '}')();
  assert.match(fr.friendly('RtApiAsio::probeDeviceInfo: error (-1) initializing driver'), /In use by another program/);
  assert.match(fr.friendly('Foo has no input channels'), /no input channels/);
  assert.strictEqual(fr.friendly('X does not support 44100 Hz (supports 48000)'), 'X does not support 44100 Hz (supports 48000)');
  assert.match(fr.friendly('device not found'), /plug it in again/); assert.strictEqual(fr.friendly('weird'), 'weird');
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

test('setup program: compiled, packed, signed; hash trailer is still found after the signature is appended', { skip: !(toolOk('i686-w64-mingw32-gcc', ['--version']) && toolOk('osslsigncode', ['--version']) && toolOk('openssl', ['version'])) }, () => {
  const be = require('../scripts/build-exe'), sg = require('../scripts/sign'), v = require('../client/verify');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'exe-'));
  const msi = pathx.join(dir, 'a.msi');
  fsx.writeFileSync(msi, Buffer.concat([Buffer.from([0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1]), cryptox.randomBytes(4000)]));
  const id = sg.ensureSigningCert({ dir: pathx.join(dir, 'sign'), env: {} });
  assert.strictEqual(id.selfSigned, true); assert.match(id.thumbprint, /^[0-9A-F]{40}$/);
  const out = pathx.join(dir, 'Audio Mixer-1.0.0.exe');
  be.buildExe({ msi, productCode: '{87FE9B11-C8D7-5DEA-97C2-5C2ABE8BED40}', out, version: '1.0.0', arch: 'x64', work: pathx.join(dir, 'w'), id });
  const buf = fsx.readFileSync(out);
  assert.strictEqual(buf.subarray(0, 2).toString(), 'MZ');
  assert.ok(sg.verifySignature(out, id));                                                   // Authenticode signature verifies against the certificate
  assert.ok(be.peDataEnd(buf) < buf.length);                                                // a certificate table now follows the image
  const t = be.readTrailer(buf, be.peDataEnd(buf));
  assert.ok(t && t.payload.equals(fsx.readFileSync(msi)) && t.productCode === '{87FE9B11-C8D7-5DEA-97C2-5C2ABE8BED40}');
  assert.ok(v.checkInstallerFile(out).filter(r => r.level !== 'INFO').every(r => r.level === 'PASS'));
  buf[buf.length - 3000] ^= 0xff;                                                           // tamper inside the signed data (the payload)
  fsx.writeFileSync(out, buf);
  assert.ok(v.checkInstallerFile(out).some(r => r.level === 'FAIL'));
  assert.throws(() => sg.verifySignature(out, id), /signature check failed/);
  fsx.rmSync(dir, { recursive: true, force: true });
});

// ── Linux / macOS packages ──
test('unix packages: Debian control + FHS layout, macOS bundle, scripts parse', () => {
  const u = require('../scripts/build-unix');
  const c = u.controlFile({ version: '1.4.0', installedSizeKb: 100 });
  assert.match(c, /^Package: audio-mixer$/m); assert.match(c, /^Recommends: nodejs \(>= 18\), pipewire/m); assert.ok(!/^Depends:/m.test(c));   // Node.js is installed on first start when apt cannot provide 18+ assert.match(c, /^Architecture: all$/m);
  assert.match(u.desktopEntry(), /^Exec=audio-mixer$/m); assert.match(u.desktopEntry(), /Categories=AudioVideo;Audio;Mixer;/);
  assert.match(u.systemdUserUnit(), /ExecStart=\/usr\/bin\/env node \/opt\/audio-mixer\/bridge\/server\.js/);
  assert.match(u.LAUNCHER_LINUX, /\. "\$APP\/ensure-node\.sh"\nam_splash "\$APP" "\$@"\nam_ensure_node \|\| exit 1\nam_ensure_audio "\$APP"\nexec "\$NODE" "\$APP\/client\/cli\.js" \$AM_SPLASH_ARGS "\$@"/);
  assert.match(u.MAC_LAUNCHER, /am_ensure_node/); assert.match(u.MAC_INSTALL, /am_ensure_audio/);
  assert.match(u.infoPlist('1.4.0'), /<key>CFBundleIdentifier<\/key><string>com\.audiomixer\.app<\/string>/);
  assert.match(u.MAC_UNINSTALL, /com\.audiomixer\.bridge\.plist/);                           // same label the service installer writes
  const cp = require('node:child_process');
  for (const [name, text, sh] of [['install', u.MAC_INSTALL, 'bash'], ['uninstall', u.MAC_UNINSTALL, 'bash'], ['launcher', u.MAC_LAUNCHER, 'bash'], ['linux', u.LAUNCHER_LINUX, 'sh']]) {
    const f = pathx.join(osx.tmpdir(), `amx-${name}.sh`); fsx.writeFileSync(f, text);
    assert.strictEqual(cp.spawnSync(sh, ['-n', f]).status, 0, name + ' script has a syntax error');
    fsx.unlinkSync(f);
  }
});

test('unix packages: the .deb is built with files under /opt, /usr/bin and /usr/share (needs dpkg-deb)', { skip: !toolOk('dpkg-deb', ['--version']) }, () => {
  const u = require('../scripts/build-unix'), cp = require('node:child_process');
  const out = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'deb-'));
  const r = u.buildDeb({ out });
  const list = cp.spawnSync('dpkg-deb', ['--contents', r.deb], { encoding: 'utf8' }).stdout;
  for (const want of ['./opt/audio-mixer/client/cli.js', './opt/audio-mixer/bridge/server.js', './usr/bin/audio-mixer', './usr/share/applications/audio-mixer.desktop', './usr/lib/systemd/user/audio-mixer.service', './usr/share/doc/audio-mixer/copyright']) assert.ok(list.includes(want), want);
  assert.ok(!/\.bat/.test(list));                                                              // no Windows launchers
  assert.match(list, /-rwxr-xr-x root\/root\s+\d+ \S+ \S+ \.\/usr\/bin\/audio-mixer/);
  assert.ok(list.includes('./opt/audio-mixer/ensure-node.sh'));                               // the launcher sources it
  const info = cp.spawnSync('dpkg-deb', ['--field', r.deb, 'Recommends'], { encoding: 'utf8' }).stdout.trim();
  assert.match(info, /^nodejs \(>= 18\)/);
  const m = u.buildMac({ out });
  const tl = cp.spawnSync('tar', ['tzf', m.tar], { encoding: 'utf8' }).stdout;
  for (const want of ['AudioMixer/Audio Mixer.app/Contents/Info.plist', 'AudioMixer/Audio Mixer.app/Contents/MacOS/AudioMixer', 'AudioMixer/install.command', 'AudioMixer/uninstall.command']) assert.ok(tl.includes(want), want);
  fsx.rmSync(out, { recursive: true, force: true });
});


// ── automatic Node.js install, levels, universal ASIO driver ──
test('auto-install scripts: shell syntax, checksum verification, no network needed to parse', () => {
  const cp = require('node:child_process');
  for (const f of ['ensure-node.sh', 'start-pc-mode.sh']) assert.strictEqual(cp.spawnSync('sh', ['-n', pathx.join(__dirname, '..', 'platforms', 'linux', f)]).status, 0, f);
  const sh = fsx.readFileSync(pathx.join(__dirname, '..', 'platforms', 'linux', 'ensure-node.sh'), 'utf8');
  assert.match(sh, /SHASUMS256\.txt/); assert.match(sh, /does not match nodejs\.org's checksum/);   // refuses an unverified download
  assert.match(sh, /--strip-components=1/); assert.ok(!/sudo|chmod 777|curl[^\n]*\|\s*(ba)?sh/.test(sh), 'no sudo, no pipe-to-shell');
  // with a working Node.js on PATH nothing is downloaded
  const r = cp.spawnSync('sh', ['-c', `. "${pathx.join(__dirname, '..', 'platforms', 'linux', 'ensure-node.sh')}"; am_ensure_node && echo "$NODE"`], { encoding: 'utf8', env: { ...process.env, AUDIO_MIXER_NODE: process.execPath } });
  assert.strictEqual(r.status, 0); assert.strictEqual(r.stdout.trim(), process.execPath);
  // a too-old / broken Node.js is not accepted
  const bad = pathx.join(osx.tmpdir(), 'amx-oldnode'); fsx.writeFileSync(bad, '#!/bin/sh\necho 12\n'); fsx.chmodSync(bad, 0o755);
  const r2 = cp.spawnSync('sh', ['-c', `. "${pathx.join(__dirname, '..', 'platforms', 'linux', 'ensure-node.sh')}"; am_node_ok "${bad}"; echo $?`], { encoding: 'utf8' });
  assert.strictEqual(r2.stdout.trim(), '1'); fsx.unlinkSync(bad);
  // Windows launcher: winget first, then the checksum-verified official zip, then the Visual C++ runtime for Audify
  const bat = fsx.readFileSync(pathx.join(__dirname, '..', 'platforms', 'windows', 'start-pc-mode.bat'), 'utf8');
  for (const want of ['winget install --id OpenJS.NodeJS.LTS', 'SHASUMS256.txt', 'certutil -hashfile', 'Microsoft.VCRedist.2015+.x64', 'does not match nodejs.org']) assert.ok(bat.includes(want), want);
  assert.ok(/\r\n/.test(bat));
  // nothing is installed without an answer: the question defaults to No after the timeout and no environment switch answers for the user
  assert.match(bat, /choice \/c YN \/d N \/t 60/); assert.ok(!/AUDIO_MIXER_YES/.test(bat) && !/choice \/c YN \/d Y/.test(bat));
});

test('Windows setup installs nothing behind the user\'s back: the Visual C++ runtime is asked for (silent installs install nothing), no start at login', () => {
  const stub = fsx.readFileSync(pathx.join(__dirname, '..', 'platforms', 'windows', 'installer', 'setup-stub.c'), 'utf8');
  assert.match(stub, /static void ensure_vc_runtime\(int quiet, int optin\)/); assert.match(stub, /if \(quiet && !optin\) return;/);              // /quiet: no questions, so no install
  assert.ok(stub.indexOf('MB_YESNO') < stub.indexOf('winget.exe install'));
  assert.ok(stub.includes('ensure_vc_runtime(quiet, vcyes)') && stub.includes('L"/vcredist"'));                                           // explicit opt-in for scripted installs
  assert.ok(stub.includes('L"/noautostart")) continue;') && !/Autostart"/.test(stub) && !/\bnoauto\b/.test(stub));                          // the old switch is accepted and ignored
  for (const f of ['scripts/build-msi.js', 'platforms/windows/installer/launcher.c', 'platforms/windows/installer/setup-stub.c']) assert.ok(!/RegistryValue[^\n]*CurrentVersion\\+Run/.test(fsx.readFileSync(pathx.join(__dirname, '..', f), 'utf8')), f);
  const svc = fsx.readFileSync(pathx.join(__dirname, '..', 'client', 'service.js'), 'utf8'); assert.ok(!/reg['"], \['add'/.test(svc) && svc.includes('not offered on Windows any more'));
});

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
    const r = cp.spawnSync(t + '-g++', ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', '-I' + pathx.join(root, 'engine', 'cpp', 'include'), '-I' + pathx.join(root, 'engine', 'cpp', 'src'), '-I' + pathx.join(root, 'platforms', 'windows', 'asio-driver'), '-c', '-o', o, pathx.join(root, 'engine', 'cpp', 'src', 'backend_asio.cpp')], { encoding: 'utf8' });
    assert.strictEqual(r.status, 0, t + ': ' + r.stderr);
  }
});

test('Windows ASIO driver (windows/asio-driver): cross-compiled for x64 and x86 with -Werror, COM exports, system DLLs only, the GUID agrees everywhere', (tc) => {
  const cp = require('node:child_process'), drv = require('../scripts/build-drivers');
  if (!['x86_64-w64-mingw32-g++', 'i686-w64-mingw32-g++'].every(n => !cp.spawnSync('which', [n]).status)) return tc.skip('mingw-w64 is not installed');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'asio-')), root = pathx.join(__dirname, '..', 'platforms', 'windows', 'asio-driver');
  try {
    for (const [arch, machine] of [['x64', 0x8664], ['x86', 0x14c]]) {
      const dll = drv.buildAsio(arch, dir), b = fsx.readFileSync(dll);
      assert.strictEqual(b.toString('latin1', 0, 2), 'MZ'); const pe = b.readUInt32LE(0x3c); assert.strictEqual(b.toString('latin1', pe, pe + 4), 'PE\0\0'); assert.strictEqual(b.readUInt16LE(pe + 4), machine); assert.ok(b.readUInt16LE(pe + 22) & 0x2000, 'a DLL');
      const dump = cp.spawnSync(arch === 'x64' ? 'x86_64-w64-mingw32-objdump' : 'i686-w64-mingw32-objdump', ['-p', dll], { encoding: 'utf8' }).stdout;
      for (const e of ['DllGetClassObject', 'DllCanUnloadNow', 'DllRegisterServer', 'DllUnregisterServer']) assert.match(dump, new RegExp('\\] ' + e + '\\b'), arch + ' exports ' + e);   // undecorated names on both
      const imports = [...dump.matchAll(/DLL Name: (\S+)/g)].map(m => m[1].toLowerCase());
      assert.ok(imports.length && imports.every(n => ['kernel32.dll', 'user32.dll', 'advapi32.dll', 'ws2_32.dll', 'winmm.dll', 'msvcrt.dll', 'ole32.dll', 'oleaut32.dll'].includes(n)), arch + ' imports only Windows DLLs: ' + imports);   // no libstdc++ / libgcc / winpthread DLL to ship
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
  const hal = rd('platforms/android/hal/audio_hw.c');
  for (const s of ['HAL_MODULE_INFO_SYM', 'AUDIO_HARDWARE_MODULE_ID', 'open_output_stream', 'open_input_stream', 'am_link_open', 'am_link_play', 'out_write', 'in_read']) assert.ok(hal.includes(s), s);
  assert.match(rd('platforms/android/hal/Android.bp'), /audio\.audiomixer\.default/); assert.match(rd('platforms/android/hal/audio_policy_configuration_audiomixer.xml'), /AUDIO_DEVICE_OUT_BUS/); assert.match(rd('platforms/android/hal/sepolicy/hal_audio_audiomixer.te'), /hal_audio_default/);
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
    for (const f of ['drivers/common/am_link.c', 'platforms/windows/asio-driver/audiomixer_asio.cpp', 'platforms/linux/alsa-plugin/pcm_audiomixer.c', 'platforms/macos/coreaudio-driver/AudioMixerDriver.c', 'platforms/android/hal/audio_hw.c', 'drivers/README.md']) assert.ok(tar.includes(`audio-mixer-drivers-${v}/${f}`), f);
    assert.ok(!tar.some(f => /\.(dll|so|o)$/.test(f)), 'no binaries in the source archive');
    assert.ok(!fsx.readdirSync(out).some(x => x.endsWith('.sha256')), 'no .sha256 files next to the release files');
  } finally { fsx.rmSync(out, { recursive: true, force: true }); }
});

test('repository layout: one folder per system with its sources and release files, shared code on top, package paths mapped, the update manifest follows it', () => {
  const lay = require('../scripts/layout'), mk = require('../scripts/make-update'), root = pathx.join(__dirname, '..');
  const top = fsx.readdirSync(root, { withFileTypes: true }).filter(e => !e.name.startsWith('.') && e.name !== 'node_modules' && e.name !== 'dist');
  const dirs = top.filter(e => e.isDirectory()).map(e => e.name).sort(), files = top.filter(e => e.isFile()).map(e => e.name).sort();
  assert.deepStrictEqual(dirs, ['bridge', 'client', 'deploy', 'drivers', 'engine', 'npm-installer', 'ota-server', 'platforms', 'releases', 'scripts'], 'the top level is minimal: systems (under platforms/) + shared code');
  assert.deepStrictEqual(fsx.readdirSync(pathx.join(root, 'platforms')).sort(), ['android', 'electron', 'ios', 'linux', 'macos', 'windows'], 'one folder per system under platforms/ (electron: an additional, optional packaging, not an installed-release OS target)');
  assert.deepStrictEqual(files, ['ADMIN-LICENSE.md', 'Additional Terms & Conditions', 'LICENSE', 'LICENSE.txt', 'PROJECT-README.md', 'README.md', 'RELEASING.md', 'SECURITY.md', 'Steinberg ASIO Licensing Agreement.pdf', 'Steinberg ASIO SDK 2.3.pdf', 'Steinberg ASIO Usage Guidelines.pdf', 'boot.html', 'changes.txt', 'index.html', 'package.json'], 'only the web app, the project papers and the Steinberg ASIO SDK papers at the root');
  assert.deepStrictEqual(fsx.readdirSync(pathx.join(root, 'releases')), ['update.json'], 'releases/ holds only the manifest that installed apps fetch');
  for (const os of lay.OSES) assert.ok(fsx.statSync(pathx.join(root, 'platforms', os, 'releases')).isDirectory(), os + '/releases');
  // every file that goes into a package exists where the layout says; the package keeps its own paths
  const build = require('../scripts/build'), src = fsx.readFileSync(pathx.join(root, 'scripts', 'build.js'), 'utf8');
  const list = [...src.slice(src.indexOf('const FILES = ['), src.indexOf('];', src.indexOf('const FILES = ['))).matchAll(/'([^']+)'/g)].map(m => m[1]).filter(f => /[./]/.test(f));
  assert.ok(list.length > 30); for (const f of list) assert.ok(fsx.existsSync(pathx.join(root, lay.repoPath(f))), f + ' -> ' + lay.repoPath(f));
  assert.deepStrictEqual([lay.repoPath('start-pc-mode.bat'), lay.repoPath('start-pc-mode.sh'), lay.repoPath('ensure-node.sh'), lay.repoPath('native/win/x64/AudioDevices.exe'), lay.repoPath('native/host/x86/PluginHost.exe'), lay.repoPath('native/mac/AudioDevices.swift'), lay.repoPath('bridge/server.js')],
    ['platforms/windows/start-pc-mode.bat', 'platforms/linux/start-pc-mode.sh', 'platforms/linux/ensure-node.sh', 'platforms/windows/native/x64/AudioDevices.exe', 'platforms/windows/plugin-host/x86/PluginHost.exe', 'platforms/macos/native/AudioDevices.swift', 'bridge/server.js']);
  assert.deepStrictEqual(['win-x64-exe', 'win-x86-msi', 'linux-deb', 'linux-tar', 'macos', 'macos-dmg', 'android-apk', 'ios-project', 'nope'].map(lay.osOfKey), ['windows', 'windows', 'linux', 'linux', 'macos', 'macos', 'android', 'ios', null]);
  // the manifest builder finds each file in the folder of its system and points the URL there; a flat folder or a server keeps flat URLs
  const version = JSON.parse(fsx.readFileSync(pathx.join(root, 'package.json'), 'utf8')).version, base = 'https://raw.githubusercontent.com/o/r/main';
  const m = mk.build({ version, base, notes: [] }), win = m.files['win-x64-exe'], apk = m.files['android-apk'];
  assert.strictEqual(win.url, `${base}/platforms/windows/releases/${encodeURIComponent('Audio Mixer-' + version + '.exe')}`); assert.strictEqual(apk.url, `${base}/platforms/android/releases/AudioMixer-${version}-android.apk`);
  assert.strictEqual(m.files['linux-deb'].url, `${base}/platforms/linux/releases/audio-mixer_${version}_all.deb`); assert.strictEqual(m.files['macos-dmg'].url, `${base}/platforms/macos/releases/AudioMixer-${version}-macos.dmg`); assert.strictEqual(m.files['ios-project'].url, `${base}/platforms/ios/releases/AudioMixer-${version}-ios-xcode-project.tar.gz`);
  for (const f of Object.values(m.files)) { const file = lay.findRelease(f.name); assert.ok(file, f.name); assert.strictEqual(fsx.statSync(file).size, f.size); }
  assert.strictEqual(mk.build({ version, base: 'https://ota.test/releases', flatUrls: true, notes: [] }).files['win-x64-exe'].url, `https://ota.test/releases/${encodeURIComponent('Audio Mixer-' + version + '.exe')}`);
  assert.strictEqual(lay.findRelease('nothing-here.exe'), null);
  // the web server never serves the system folders, the installers' own helpers keep being found where they are shipped
  const sec = require('./security'); for (const d of ['platforms', 'drivers', 'ota-server']) { assert.ok(sec.BLOCKED_TOP.has(d), d); assert.strictEqual(sec.staticAllowed(root, pathx.join(root, d, 'x.json')), false, d); }
  assert.strictEqual(sec.staticAllowed(root, pathx.join(root, 'index.html')), true);
  // the launchers work from their folder in the repository and in a package
  const cp = require('node:child_process'), env = { ...process.env, AUDIO_MIXER_NODE: process.execPath, AUDIO_MIXER_NO_NATIVE: '1' };
  for (const cwd of [root, pathx.join(root, 'platforms', 'linux')]) { const r = cp.spawnSync('sh', [pathx.join(root, 'platforms', 'linux', 'start-pc-mode.sh'), '--help'], { cwd, env, encoding: 'utf8' }); assert.strictEqual(r.status, 0, r.stderr); assert.match(r.stdout, /PC-mode client/); }
  const bat = fsx.readFileSync(pathx.join(root, 'platforms', 'windows', 'start-pc-mode.bat'), 'utf8'), srv = fsx.readFileSync(pathx.join(root, 'platforms', 'windows', 'start-local-server.bat'), 'utf8');
  assert.match(bat, /if not exist client\\cli\.js if exist \.\.\\\.\.\\client\\cli\.js cd \/d \.\.\\\.\./); assert.match(srv, /if not exist bridge\\server\.js if exist \.\.\\\.\.\\bridge\\server\.js cd \/d \.\.\\\.\./); assert.match(bat, /\r\n/);
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

test('clip detection in the page: every meter (sources, channel strips, master, native streams) uses one detector with a real, saved threshold and a latched, clearable LED', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  for (const s of ['detect(s, v, now)', 'v.run >= this.RUN || v.pk >= 1', 'clearAll()', "halx_clip_db", 'id="clip-thr"', 'rt.clip.clear(m.L)', 'cs.clipLatched', 'clearClips()', "'/api/meters'", 'clipView.sent'])
    assert.ok(html.includes(s), s);
  assert.ok(!/v\.pk >= 0\.97\) s\.clipAt/.test(html), 'the old level rule is gone from the source meters');
  assert.match(html, /window\.setClipThreshold = function\(v\) \{\s*const r = window\.rtEngine/);                    // no longer a notification only
  assert.ok(!/window\.setClipThreshold = function\(v\) \{ window\.notify/.test(html));
  // the page's detector, run on its own source text: the rules of the bridge (3 in a row at the threshold, or one at / over full scale; events closer than 250 ms are one)
  const a = html.indexOf('clip: {\n            RUN: 3'), b = html.indexOf('lit(s, now)', a);
  assert.ok(a > 0 && b > a);
  const clip = new Function('return {' + html.slice(a, b).replace(/^clip: \{/, '').replace(/,\s*$/, '') + '}')();      // RUN, HOLD_MS, db, lin, detect
  const s = { clipAt: 0, clipN: 0, clipLatched: false, clipPk: 0 };
  clip.detect(s, { run: 2, pk: 0.99 }, 1000); assert.strictEqual(s.clipN, 0);                                           // 2 samples in a row: not a clip
  clip.detect(s, { run: 3, pk: 0.99 }, 1100); assert.strictEqual(s.clipN, 1); assert.strictEqual(s.clipLatched, true);
  clip.detect(s, { run: 5, pk: 1.0 }, 1200); assert.strictEqual(s.clipN, 1);                                            // within 250 ms: the same event
  clip.detect(s, { run: 1, pk: 1.3 }, 1500); assert.strictEqual(s.clipN, 2); assert.strictEqual(s.clipPk, 1.3);          // over full scale is always a clip
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
  assert.strictEqual(ok.ok, true); assert.strictEqual(ok.plan.channels, 16); assert.ok(ok.plan.features.includes('plugins'));
  assert.strictEqual(lic.verifyKey(mk({ plan: 'studio', expires: null }), { jwk, now }).plan.channels, 32);
  assert.strictEqual(lic.PLANS.basic.channels, 8);
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
    assert.strictEqual(late.state, 'expired'); assert.strictEqual(late.plan.id, 'basic'); assert.strictEqual(late.plan.channels, 8);
    assert.strictEqual(lic.status({ jwk: vendorKeys().jwk, now }).state, 'invalid');                 // key of another vendor never raises the plan
    assert.strictEqual(lic.deactivate({ file }).removed, true); assert.strictEqual(lic.status({ jwk }).state, 'basic');
    assert.strictEqual(lic.hasFeature(lic.status({ key: key, jwk, now }), 'ota'), true);
    assert.strictEqual(lic.hasFeature(lic.status({ key: null }), 'ota'), false);
  } finally { delete process.env.BRIDGE_LICENSE_FILE; }
});

test('vendor tool: key pair, issue and embed into a copy of the project', () => {
  const v = require('../scripts/license'), lic = require('./license');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'vend-'));
  const r = v.initKeys(dir);
  assert.ok(fsx.existsSync(r.priv) && fsx.existsSync(r.pub));
  assert.throws(() => v.initKeys(dir), /already exists/);                                               // never overwrites a key
  const jwk = v.loadPublic(dir);
  const { key, payload } = v.issue({ dir, plan: 'studio', name: 'Studio One', days: 365, machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' });
  assert.strictEqual(lic.verifyKey(key, { jwk, machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' }).plan.id, 'studio'); assert.ok(payload.expires > Date.now());
  assert.throws(() => v.issue({ dir, plan: 'gold', name: 'x' }), /plan must be/);
  assert.throws(() => v.issue({ dir, plan: 'pro' }), /--name/);
  const copy = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'proj-')); fsx.mkdirSync(pathx.join(copy, 'bridge'));
  fsx.writeFileSync(pathx.join(copy, 'index.html'), `x ${v.MARK_A}{"old":1}${v.MARK_B} y`);
  v.embedPublic(jwk, copy);
  assert.deepStrictEqual(JSON.parse(fsx.readFileSync(pathx.join(copy, 'bridge', 'license-public.json'), 'utf8')), jwk);
  assert.ok(fsx.readFileSync(pathx.join(copy, 'index.html'), 'utf8').includes(JSON.stringify(jwk)));
  // the shipped page and bridge carry the same public key
  const page = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  const m = new RegExp(v.MARK_A.replace(/\*/g, '\\*') + '(.*?)' + v.MARK_B.replace(/\*/g, '\\*')).exec(page);
  assert.deepStrictEqual(JSON.parse(m[1]), lic.PUBLIC_JWK);
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

test('endpoints: /api/license (BASIC by default), activation needs a valid key, updates need PRO', async () => {
  process.env.BRIDGE_LICENSE_FILE = pathx.join(fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'lep-')), 'license.json');
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const post = (p, headers = {}, body) => fetch(base + p, { method: 'POST', headers: { 'Content-Type': 'application/json', ...headers }, body });
  try {
    const st = await (await fetch(base + '/api/license')).json();
    assert.strictEqual(st.ok, true); assert.strictEqual(st.state, 'basic'); assert.strictEqual(st.plan.channels, 8); assert.match(st.machineId, /^[0-9A-F]{4}(-[0-9A-F]{4}){4}$/);
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

test('security: headers, static allow-list, limiter, redirect checks', async () => {
  const sec = require('./security');
  const root = pathx.resolve(__dirname, '..');
  assert.ok(sec.staticAllowed(root, pathx.join(root, 'index.html')));
  for (const f of ['client/cli.js', 'scripts/license.js', 'native/win/x64/AudioDevices.exe', 'package.json.bak', 'bridge/license.js', '.git/config', 'dist/x.json', 'releases/update.json']) assert.ok(!sec.staticAllowed(root, pathx.join(root, f)), f);
  assert.ok(!sec.staticAllowed(root, pathx.resolve(root, '..', 'index.html')));
  let t = 0; const lim = sec.createLimiter({ now: () => t });
  for (let i = 0; i < 3; i++) assert.ok(lim.allow('k', 3, 1000).ok);
  const blocked = lim.allow('k', 3, 1000); assert.strictEqual(blocked.ok, false); assert.ok(blocked.retryAfter >= 1);
  t = 1500; assert.ok(lim.allow('k', 3, 1000).ok);
  // every redirect hop is checked before it is requested
  const hit = [];
  const fi = async (u, o) => { hit.push([u, o.redirect]); return u.includes('github.com') ? { status: 302, headers: new Map([['location', 'https://evil.example/x.exe']]) } : { status: 200, headers: new Map() }; };
  await assert.rejects(sec.fetchChecked(fi, 'https://github.com/a', {}, u => new URL(u).hostname === 'github.com'), /untrusted host/);
  assert.deepStrictEqual(hit, [['https://github.com/a', 'manual']]);                       // evil.example was never contacted
  const ok = await sec.fetchChecked(async () => ({ status: 200, headers: new Map() }), 'https://github.com/a', {}, () => true); assert.strictEqual(ok.status, 200);
  // live server
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const idx = await fetch(base + '/');
  assert.strictEqual(idx.status, 200); assert.match(idx.headers.get('content-security-policy'), /frame-ancestors 'self'/); assert.match(idx.headers.get('permissions-policy'), /geolocation=\(\)/); assert.strictEqual(idx.headers.get('cross-origin-opener-policy'), 'same-origin');
  for (const p of ['/client/cli.js', '/scripts/license.js', '/native/win/x64/AudioDevices.exe', '/bridge/server.js', '/%2e%2e/etc/passwd']) assert.notStrictEqual((await fetch(base + p)).status, 200, p);
  server.closeAllConnections(); server.close();
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

test('android app: page transform, version code, launcher icon, sources', () => {
  const apk = require('../scripts/build-apk');
  assert.strictEqual(apk.versionCode('1.10.0'), 11000); assert.strictEqual(apk.versionCode('1.9.1'), 10901); assert.ok(apk.versionCode('2.0.0') > apk.versionCode('1.99.99'));
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  const out = apk.transformHtml(html);
  assert.ok(!/src="https:\/\/cdn\.tailwindcss\.com"/.test(out) && !/cdnjs\.cloudflare\.com/.test(out) && !/fonts\.googleapis\.com/.test(out));   // everything the page needs is bundled
  assert.ok(out.includes('href="tw.css"') && out.includes('href="fa/css/all.min.css"') && out.includes("@import url('fonts/fonts.css');"));
  assert.strictEqual(out.length > html.length - 400 && out.length < html.length + 400, true);
  const p = apk.png(48, apk.iconPixel); assert.strictEqual(p.subarray(0, 8).toString('hex'), '89504e470d0a1a0a'); assert.strictEqual(p.readUInt32BE(16), 48);
  assert.deepStrictEqual(apk.iconPixel(0, 0), [0, 0, 0, 0]);                                         // rounded corner is transparent
  const man = fsx.readFileSync(pathx.join(__dirname, '..', 'platforms', 'android', 'AndroidManifest.xml'), 'utf8');
  assert.ok(/package="com\.audiomixer\.app"/.test(man) && /RECORD_AUDIO/.test(man) && /android:exported="true"/.test(man) && !/CAMERA|READ_EXTERNAL|WRITE_EXTERNAL|READ_CONTACTS/.test(man));
  const java = fsx.readFileSync(pathx.join(__dirname, '..', 'platforms', 'android', 'src', 'com', 'audiomixer', 'app', 'MainActivity.java'), 'utf8');
  assert.ok(java.includes('file:///android_asset/www/index.html') && java.includes('setAllowUniversalAccessFromFileURLs(false)') && java.includes('RESOURCE_AUDIO_CAPTURE'));
});

test('mic EQ presets: every menu entry has 10 sane bands and the mic group is present', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  const sel = /<select id="eq-preset-select"[\s\S]*?<\/select>/.exec(html)[0];
  const values = [...sel.matchAll(/<option value="([^"]+)"/g)].map(m => m[1]);
  const block = /window\.standardEQPresets = \{([\s\S]*?)\n        \};/.exec(html)[1];
  const defs = {}; for (const m of block.matchAll(/"([^"]+)": \[([^\]]+)\]/g)) defs[m[1]] = m[2].split(',').map(Number);
  assert.ok(/<optgroup label="MICROPHONE PRESETS">/.test(sel));
  const mic = values.filter(v => v.startsWith('MIC • ')); assert.ok(mic.length >= 15);
  for (const v of values) { assert.ok(defs[v], 'preset missing: ' + v); assert.strictEqual(defs[v].length, 10, v); assert.ok(defs[v].every(g => Number.isFinite(g) && g >= -12 && g <= 12), v); }
  for (const v of mic) assert.ok(defs[v][0] <= 5, v);                                                         // no mic preset boosts the sub-bass hard except the kick
  assert.ok(defs['MIC • Male Vocal'][0] <= -9 && defs['MIC • Feedback Safe (live stage)'].slice(0, 2).every(g => g <= -8));   // vocal presets roll off the rumble
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

test('iOS app: Swift sources, XcodeGen spec, opaque icon, project bundle', async () => {
  const ios = require('../scripts/build-ios'); const root = pathx.join(__dirname, '..', 'platforms', 'ios');
  const tpl = fsx.readFileSync(pathx.join(root, 'project.yml.template'), 'utf8');
  const yml = ios.projectYml(tpl, '1.11.0'); assert.ok(!yml.includes('@VERSION@') && !yml.includes('@BUILD@') && yml.includes('MARKETING_VERSION: "1.11.0"') && yml.includes('CURRENT_PROJECT_VERSION: "11100"'));
  for (const k of ['NSMicrophoneUsageDescription', 'UIBackgroundModes: [audio]', 'NSAllowsLocalNetworking: true', 'PRODUCT_BUNDLE_IDENTIFIER: com.audiomixer.app', 'path: www']) assert.ok(yml.includes(k), k);
  assert.ok(!/NSCameraUsageDescription|NSPhotoLibrary|NSLocation/.test(yml));
  const app = fsx.readFileSync(pathx.join(root, 'AudioMixer', 'AudioMixerApp.swift'), 'utf8'), srv = fsx.readFileSync(pathx.join(root, 'AudioMixer', 'LocalServer.swift'), 'utf8');
  assert.ok(app.includes('WKWebView') && app.includes('requestMediaCapturePermissionFor') && app.includes('.microphone') && app.includes('origin.host == "127.0.0.1"') && app.includes('AVAudioSession'));
  assert.ok(srv.includes('requiredInterfaceType = .loopback') && srv.includes('NWListener') && srv.includes('path.contains("..")') && srv.includes('hasPrefix(root.path + "/")') && /method == "GET" \|\| method == "HEAD"/.test(srv));
  for (const f of [app, srv]) { assert.strictEqual((f.match(/\{/g) || []).length, (f.match(/\}/g) || []).length, 'balanced braces'); assert.strictEqual((f.match(/\(/g) || []).length, (f.match(/\)/g) || []).length, 'balanced parentheses'); }
  assert.deepStrictEqual(ios.iconPixelOpaque(0, 0).slice(3), [255]);                                          // no transparency in the App Store icon
});

test('macOS: .dmg writer (UDIF) round-trips, app icon and Info.plist are valid, the disk image holds the app', () => {
  const { makeDmg } = require('../scripts/mkdmg'); const zl = require('node:zlib');
  // a disk image with data, a zero run (stored as nothing) and incompressible bytes, not a multiple of the 1 MiB chunk
  const raw = Buffer.concat([Buffer.from('ISO-like volume '.repeat(90000)), Buffer.alloc(1500000), cryptox.randomBytes(1048576 + 700)]);
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'dmg-')), src = pathx.join(dir, 'a.img'), dst = pathx.join(dir, 'a.dmg');
  fsx.writeFileSync(src, raw);
  const info = makeDmg(src, dst);
  const f = fsx.readFileSync(dst), k = f.subarray(f.length - 512);
  assert.strictEqual(k.toString('latin1', 0, 4), 'koly'); assert.strictEqual(k.readUInt32BE(4), 4); assert.strictEqual(k.readUInt32BE(8), 512);
  const xmlOff = Number(k.readBigUInt64BE(216)), xmlLen = Number(k.readBigUInt64BE(224)), dataLen = Number(k.readBigUInt64BE(32));
  assert.strictEqual(xmlOff, dataLen); assert.strictEqual(xmlOff + xmlLen + 512, f.length); assert.strictEqual(Number(k.readBigUInt64BE(492)), Math.ceil(raw.length / 512)); assert.strictEqual(info.sectors, Math.ceil(raw.length / 512));
  const xml = f.subarray(xmlOff, xmlOff + xmlLen).toString('utf8');
  assert.ok(xml.startsWith('<?xml') && xml.includes('<key>blkx</key>'));
  const m = Buffer.from(/<data>([\s\S]*?)<\/data>/.exec(xml)[1].replace(/\s+/g, ''), 'base64');
  assert.strictEqual(m.toString('latin1', 0, 4), 'mish'); assert.strictEqual(Number(m.readBigUInt64BE(16)), info.sectors);
  const n = m.readUInt32BE(200); const out = Buffer.alloc(info.sectors * 512); let last = null;
  for (let i = 0; i < n; i++) {
    const o = 204 + 40 * i, type = m.readUInt32BE(o), start = Number(m.readBigUInt64BE(o + 8)), cnt = Number(m.readBigUInt64BE(o + 16)), off = Number(m.readBigUInt64BE(o + 24)), len = Number(m.readBigUInt64BE(o + 32));
    if (type === 0xffffffff) { last = start; break; }
    if (type === 0x80000005) zl.inflateSync(f.subarray(off, off + len)).copy(out, start * 512); else if (type === 1) f.copy(out, start * 512, off, off + len);   // 2 = zeros
    assert.ok(cnt > 0 && cnt <= 2048);
  }
  assert.strictEqual(last, info.sectors); assert.ok(out.subarray(0, raw.length).equals(raw)); assert.ok(f.length < raw.length);        // lossless, smaller
  // icon and Info.plist
  const u = require('../scripts/build-unix'); const ic = u.icns();
  assert.strictEqual(ic.toString('latin1', 0, 4), 'icns'); assert.strictEqual(ic.readUInt32BE(4), ic.length);
  let p = 8; const types = []; while (p < ic.length) { types.push(ic.toString('latin1', p, p + 4)); const len = ic.readUInt32BE(p + 4); assert.strictEqual(ic.subarray(p + 8, p + 16).toString('hex'), '89504e470d0a1a0a'); p += len; }
  assert.deepStrictEqual(types, ['ic07', 'ic08', 'ic09', 'ic10']); assert.strictEqual(p, ic.length);
  const plist = u.infoPlist('1.12.0'); for (const kx of ['CFBundleIconFile</key><string>AppIcon', 'LSApplicationCategoryType', 'NSMicrophoneUsageDescription', 'LSMinimumSystemVersion</key><string>11.0', 'CFBundleExecutable</key><string>AudioMixer']) assert.ok(plist.includes(kx), kx);
  assert.match(u.MAC_LAUNCHER, /swiftc/);
  // the full build (needs genisoimage / xorriso and zip; skipped without them)
  const mac = require('../scripts/build-macos');
  if (mac.isoTool()) {
    const outd = pathx.join(dir, 'rel'); const r = mac.buildMacApp({ out: outd });
    assert.ok(fsx.statSync(r.dmg).size > 100000 && !fsx.existsSync(r.dmg + '.sha256') && fsx.existsSync(r.zip));
    const kk = fsx.readFileSync(r.dmg); assert.strictEqual(kk.toString('latin1', kk.length - 512, kk.length - 508), 'koly');
    assert.match(mac.readme('1.12.0'), /Applications/);
  }
});

test('LIVE SOURCES: one scan at a time, a failed scan keeps the interface list, duplex failures are remembered, only openable devices; the server shares one detection', async () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  // run the page's scan logic with a stub: extract the methods and drive them
  const grab = name => { const i = html.indexOf('            ' + name); assert.ok(i > 0, name); return i; };
  assert.ok(html.includes('this._scanning = this.scanOnce()') && html.includes('this._rescan = true'));
  assert.ok(html.includes('if (!ok && this.interfaces) this.interfaces.filter(i => i.apis && i.apis.length).forEach(i => found.set(i.key, i));'));
  assert.ok(html.includes("a.inputs > 0 && a.deviceId >= 0") && html.includes("a.outputs > 0 && a.deviceId >= 0"));
  assert.ok(html.includes('duplexBad(key)') && (html.match(/!this\.duplexBad\(i\.key\)/g) || []).length >= 3);
  const m = /scan\(\) \{\n\s*if \(this\._scanning\)[\s\S]*?\n            \},\n            async scanOnce\(\) \{/.exec(html); assert.ok(m); void grab;
  // behaviour of the single-flight wrapper
  const L = { _scanning: null, _rescan: false, runs: 0, async scanOnce() { this.runs++; await new Promise(r => setTimeout(r, 20)); } };
  L.scan = function () { if (this._scanning) { this._rescan = true; return this._scanning; } this._scanning = this.scanOnce().finally(() => { this._scanning = null; if (this._rescan) { this._rescan = false; this.scan(); } }); return this._scanning; };
  await Promise.all([L.scan(), L.scan(), L.scan()]); await new Promise(r => setTimeout(r, 60));
  assert.strictEqual(L.runs, 2);                                                      // three overlapping calls: one run + one follow-up, never parallel
  // server: concurrent detections share one run and a failure is not cached
  const srv = require('./server'); let calls = 0;
  const first = srv.detectCached(true), second = srv.detectCached(); assert.strictEqual(first, second);
  assert.notStrictEqual(srv.detectCached(true), first);                              // ?force=1 starts a new run
  void calls; await first.catch(() => {});
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

test('ASIO probe: a second try on a fresh instance rescues a driver that was not ready; no probe while a driver is being opened; the reason reaches the driver-only row', () => {
  const a = require('./audify'), { groupInterfaces } = require('./interfaces');
  const foc = { id: 0, name: 'Focusrite USB ASIO', inputChannels: 18, outputChannels: 20, sampleRates: [44100, 48000], preferredSampleRate: 48000 };
  const asioNames = d => d.devices.filter(x => x.api === 'WINDOWS_ASIO').map(x => x.name);
  // 1. the first probe of the scan finds the driver "waking up" (no channels / an exception), the second answers
  for (const firstFails of ['zero', 'throw']) {
    const fa = fakeAudify(); const orig = fa.RtAudio.prototype.getDevices; let n = 0;
    fa.RtAudio.prototype.getDevices = function () {
      if (this.api !== 6) return orig.call(this);
      if (++n === 1) { if (firstFails === 'throw') throw new Error('RtApiAsio::probeDeviceInfo: error (-1) initializing driver'); return [{ ...foc, inputChannels: 0, outputChannels: 0, sampleRates: [] }]; }
      return [foc];
    };
    const r = a.listDevices(() => fa);
    assert.deepStrictEqual(asioNames(r), ['Focusrite USB ASIO'], firstFails); assert.strictEqual(r.devices.find(d => d.api === 'WINDOWS_ASIO').maxInputChannels, 18);
    assert.strictEqual((r.problems || []).length, 0, firstFails); assert.strictEqual(n, 2);                       // exactly one retry
  }
  // a driver that stays unreadable is still reported (and retried only once per scan)
  { const fa = fakeAudify(); let n = 0; fa.RtAudio.prototype.getDevices = function () { if (this.api !== 6) return []; n++; throw new Error('RtApiAsio::probeDeviceInfo: error (-1) initializing driver'); };
    const r = a.listDevices(() => fa); assert.strictEqual(n, 2); assert.strictEqual(r.problems[0].retried, true); assert.match(r.problems[0].message, /probeDeviceInfo/); assert.deepStrictEqual(asioNames(r), []); }
  // 2. a scan that runs while a driver is being opened does not probe it (two probes of a single-client driver make both fail)
  { const fa = fakeAudify(); let probes = 0, during = null; const orig = fa.RtAudio.prototype.getDevices; const open0 = fa.RtAudio.prototype.openStream;
    fa.RtAudio.prototype.getDevices = function () { if (this.api === 6) probes++; return orig.call(this); };
    const first = a.listDevices(() => fa), dev = first.devices.find(d => d.api === 'WINDOWS_ASIO'); assert.strictEqual(probes, 1);
    fa.RtAudio.prototype.openStream = function (...args) { const before = probes; during = a.listDevices(() => fa); assert.strictEqual(probes, before, 'no probe while the driver opens'); return open0.apply(this, args); };
    const s = a.openStream({ mod: fa, dev, direction: 'output', channels: 2, sampleRate: 48000 });
    assert.deepStrictEqual(asioNames(during), ['Focusrite USB ASIO']);                                        // the remembered list is shown meanwhile
    assert.strictEqual(a._asioOf(fa).opening, 0); s.close();
    probes = 0; a.listDevices(() => fa); assert.strictEqual(probes, 1); }                                      // the scans probe again once nothing is open or opening
  { const fa = fakeAudify(); const dev = a.listDevices(() => fa).devices.find(d => d.api === 'WINDOWS_ASIO'); fa.RtAudio.prototype.openStream = () => { throw new Error('driver busy'); };
    assert.throws(() => a.openStream({ mod: fa, dev, direction: 'output', channels: 2, sampleRate: 48000 }), /could not open/); assert.strictEqual(a._asioOf(fa).opening, 0); }   // a failed open does not leave the guard set
  // 3. the interface list: an installed driver the engine could not read says why
  const problems = [{ api: 'ASIO', message: 'RtApiAsio::probeDeviceInfo: error (-1) initializing driver', unprobed: ['Focusrite USB ASIO'], kept: [], retried: true }];
  const list = groupInterfaces([], ['Focusrite USB ASIO', 'ASIO4ALL v2'], problems);
  const f = list.find(i => /focusrite/i.test(i.name));
  assert.ok(f.driverOnly && f.probeNamed && f.probeRetried && /probeDeviceInfo/.test(f.probeError));
  assert.ok(list.find(i => /asio4all/i.test(i.name)).probeError);                                             // the whole probe failed: the error applies to every unlisted driver
  assert.strictEqual(groupInterfaces([], ['Focusrite USB ASIO'], []).find(i => i.driverOnly).probeError, undefined);   // nothing known: nothing claimed
  const html = require('node:fs').readFileSync(require('node:path').join(__dirname, '..', 'index.html'), 'utf8');
  assert.ok(html.includes('ls-reprobe') && html.includes("'?force=1'") && html.includes('PROBE AGAIN') && html.includes('i.probeError'));
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

test('Android engine (Java): the bridge protocol runs on a plain JVM with a fake backend: HTTP, origin / host checks, WebSocket input and output, interfaces grouping', async () => {
  const { spawnSync, spawn } = require('node:child_process');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'jv-')), src = pathx.join(__dirname, '..', 'platforms', 'android');
  const files = ['Json', 'Ws', 'AudioBackend', 'Assets', 'Interfaces', 'VirtualHub', 'NowPlaying', 'AppInfo', 'MiniBridge'].map(n => pathx.join(src, 'src', 'com', 'audiomixer', 'app', n + '.java')).concat(pathx.join(src, 'test', 'com', 'audiomixer', 'app', 'BridgeHarness.java'));
  const c = spawnSync('javac', ['--release', '8', '-Xlint:-options', '-d', dir, ...files], { encoding: 'utf8' });
  if (c.error || c.status !== 0) return;                                                      // needs a JDK (the build of the APK needs one as well)
  const www = pathx.join(dir, 'www'); fsx.mkdirSync(pathx.join(www, 'fonts'), { recursive: true }); fsx.writeFileSync(pathx.join(www, 'index.html'), '<html>mixer</html>'); fsx.writeFileSync(pathx.join(www, 'fonts', 'a.woff2'), Buffer.from([1, 2, 3])); fsx.writeFileSync(pathx.join(dir, 'secret.txt'), 'outside');
  const jv = spawn('java', ['-cp', dir, 'com.audiomixer.app.BridgeHarness', '0', www], { stdio: ['pipe', 'pipe', 'inherit'] });
  try {
    const port = await new Promise((res, rej) => { let b = ''; jv.stdout.on('data', d => { b += d; const m = /PORT (\d+)/.exec(b); if (m) res(Number(m[1])); }); jv.on('error', rej); setTimeout(() => rej(new Error('no port')), 8000); });
    const base = `http://127.0.0.1:${port}`;
    const st = await (await fetch(base + '/api/status')).json(); assert.strictEqual(st.ok, true); assert.strictEqual(st.name, 'audio-mixer-bridge'); assert.strictEqual(st.engine, 'android');
    const ifs = await (await fetch(base + '/api/interfaces')).json(); assert.strictEqual(ifs.ok, true); assert.strictEqual(ifs.portaudio, true);
    const by = n => ifs.interfaces.find(i => i.name === n);
    assert.ok(by('Scarlett 2i2 USB').usb && by('Scarlett 2i2 USB').inputs === 2 && by('Scarlett 2i2 USB').outputs === 2 && by('Scarlett 2i2 USB').read.deviceId === 7 && by('Scarlett 2i2 USB').write.deviceId === 8);   // one interface, both directions
    assert.ok(by('This device (built-in microphone and speaker)') && by('Galaxy Buds2').bluetooth && by('Galaxy Buds2').apis.length === 2);
    const drv = await (await fetch(base + '/api/drivers')).json(); assert.ok(drv.ok && drv.drivers.includes('aaudio') && drv.portaudio.devices.length === 6 && Array.isArray(drv.asio) && drv.vst.vst3.length === 0);
    assert.strictEqual((await (await fetch(base + '/api/license')).json()).ok, false);
    assert.strictEqual((await fetch(base + '/api/nope')).status, 404);
    // the bundled page is served from the engine (a real http origin for AudioWorklet, localStorage, the microphone): only files of www/
    const page = await fetch(base + '/'); assert.strictEqual(page.status, 200); assert.match(page.headers.get('content-type'), /text\/html/); assert.strictEqual(await page.text(), '<html>mixer</html>');
    const fnt = await fetch(base + '/fonts/a.woff2'); assert.strictEqual(fnt.headers.get('content-type'), 'font/woff2'); assert.deepStrictEqual([...new Uint8Array(await fnt.arrayBuffer())], [1, 2, 3]);
    assert.strictEqual((await fetch(base + '/nothing.html')).status, 404);
    for (const raw of ['/../secret.txt', '/%2e%2e/secret.txt', '/fonts/..%2fsecret.txt', '/..%5csecret.txt']) {
      const s = await new Promise(res => { const c = require('node:net').connect(port, '127.0.0.1', () => c.write(`GET ${raw} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n`)); let d = ''; c.on('data', x => d += x); c.on('close', () => res(d)); });
      assert.ok(!s.includes('outside') && /^HTTP\/1\.1 (403|404)/.test(s), raw);
    }
    // origin / host checks
    assert.strictEqual((await fetch(base + '/api/status', { headers: { Origin: 'https://evil.example' } })).status, 403);
    const nul = await fetch(base + '/api/status', { headers: { Origin: 'null' } }); assert.strictEqual(nul.status, 200); assert.strictEqual(nul.headers.get('access-control-allow-origin'), 'null');
    assert.strictEqual((await fetch(base + '/api/status', { headers: { Origin: 'http://localhost:8765' } })).status, 200);
    const pre = await fetch(base + '/api/status', { method: 'OPTIONS', headers: { Origin: 'null' } }); assert.strictEqual(pre.status, 204);
    assert.strictEqual((await fetch(base + '/api/status', { method: 'POST' })).status, 405);
    const raw = await new Promise(res => { const s = require('node:net').connect(port, '127.0.0.1', () => s.write('GET /api/status HTTP/1.1\r\nHost: evil.example\r\nConnection: close\r\n\r\n')); let d = ''; s.on('data', x => d += x); s.on('close', () => res(d)); });
    assert.match(raw, /^HTTP\/1\.1 403/);                                                         // DNS-rebinding guard
    // WebSocket input: started, PCM frames, stop
    const wsOpen = path => new Promise((res, rej) => { const w = new WebSocket(`ws://127.0.0.1:${port}${path}`); w.binaryType = 'arraybuffer'; w.onopen = () => res(w); w.onerror = () => rej(new Error('ws failed')); });
    const next = (w, pred) => new Promise(res => { const h = ev => { if (pred(ev.data)) { w.removeEventListener('message', h); res(ev.data); } }; w.addEventListener('message', h); });
    let w = await wsOpen('/ws/input');
    w.send(JSON.stringify({ type: 'start', channels: 2, sampleRate: 48000, frameSize: 'auto', deviceId: 7 }));
    const started = JSON.parse(await next(w, d => typeof d === 'string')); assert.strictEqual(started.type, 'started'); assert.strictEqual(started.engine, 'android'); assert.strictEqual(started.channels, 2); assert.strictEqual(started.sampleRate, 48000);
    const pcm = await next(w, d => typeof d !== 'string'); assert.strictEqual(pcm.byteLength, 480 * 2 * 2);
    const i16 = new Int16Array(pcm); assert.ok(Math.max(...i16) > 8000 && Math.min(...i16) < -8000);                                            // the 440 Hz sine arrives intact
    const live = await (await fetch(base + '/api/status')).json(); assert.strictEqual(live.streams.length, 1); assert.strictEqual(live.streams[0].direction, 'input');
    w.send(JSON.stringify({ type: 'stop' })); assert.strictEqual(JSON.parse(await next(w, d => typeof d === 'string')).type, 'stopped'); w.close();
    // WebSocket input with a failing device: a clear error, the socket stays usable
    w = await wsOpen('/ws/input'); w.send(JSON.stringify({ type: 'start', deviceId: 99 }));
    assert.match(JSON.parse(await next(w, d => typeof d === 'string')).message, /microphone permission/); w.close();
    // WebSocket output: started, PCM in, counted by the status endpoint
    w = await wsOpen('/ws/output'); w.send(JSON.stringify({ type: 'start', channels: 2, sampleRate: 44100, deviceId: 8 }));
    const so = JSON.parse(await next(w, d => typeof d === 'string')); assert.strictEqual(so.type, 'started'); assert.strictEqual(so.sampleRate, 44100);
    w.send(new Int16Array(960).buffer); w.send(new Int16Array(960).buffer); w.send(new Uint8Array(3).buffer);                                       // two whole blocks, one odd-sized junk frame
    await new Promise(r => setTimeout(r, 300));
    const o = (await (await fetch(base + '/api/status')).json()).streams.find(s => s.direction === 'output'); assert.strictEqual(o.bytes, 2 * 1920);
    w.close();
    // a plain HTTP request to a WebSocket path, and unknown WebSocket paths, are refused
    assert.strictEqual((await fetch(base + '/ws/input')).status, 404);
    await assert.rejects(wsOpen('/ws/other'));
  } finally { jv.stdin.end(); jv.kill(); }
});

test('Qt Android project (source only): files, CMake wiring, the routes of its server match the page\'s needs, version follows package.json', () => {
  const root = pathx.join(__dirname, '..'), rd = f => fsx.readFileSync(pathx.join(root, f), 'utf8');
  for (const f of ['CMakeLists.txt', 'version.txt', 'README.md', 'src/main.cpp', 'src/MixerServer.cpp', 'src/MixerServer.h', 'src/AudioDevices.cpp', 'src/AudioDevices.h', 'qml/Main.qml', 'android/AndroidManifest.xml']) assert.ok(fsx.existsSync(pathx.join(root, 'platforms', 'android', 'qt', f)), f);
  const cm = rd('platforms/android/qt/CMakeLists.txt');
  for (const s of ['find_package(Qt6 6.5 REQUIRED COMPONENTS', 'WebView', 'Multimedia', 'qt_add_qml_module', 'QT_ANDROID_PACKAGE_SOURCE_DIR', 'src/MixerServer.cpp', 'src/AudioDevices.cpp', 'qml/Main.qml']) assert.ok(cm.includes(s), s);
  for (const f of ['src/MixerServer.cpp', 'src/AudioDevices.cpp', 'src/main.cpp']) assert.ok(cm.includes(f), f);                      // every source is built
  const srv = rd('platforms/android/qt/src/MixerServer.cpp');
  assert.ok(srv.includes('"/api/status"') && srv.includes('"/api/interfaces"') && srv.includes('assets:/www') && srv.includes('path.contains("..")'));   // routes of the Java app; no way out of the page folder
  assert.ok(rd('platforms/android/qt/src/main.cpp').includes('start(8765, 10)') && rd('platforms/android/qt/src/main.cpp').includes('QMicrophonePermission') && rd('platforms/android/qt/src/main.cpp').includes('QtWebView::initialize()'));
  assert.ok(rd('platforms/android/qt/qml/Main.qml').includes('"http://localhost:" + mixerPort + "/index.html"'));
  const man = rd('platforms/android/qt/android/AndroidManifest.xml'); assert.ok(man.includes('RECORD_AUDIO') && man.includes('QtActivity') && !/ACCESS_FINE_LOCATION|CAMERA/.test(man));
  assert.strictEqual(rd('platforms/android/qt/version.txt').trim(), JSON.parse(rd('package.json')).version);                                       // `node scripts/prepare-qt.js` keeps it in step
  assert.match(rd('platforms/android/qt/README.md'), /source only, not built or run/);                                                               // the limit is stated
  const prep = require('../scripts/prepare-qt'); assert.strictEqual(typeof prep.main, 'function');
  assert.ok(/android\/qt\/assets\//.test(rd('.gitignore')));
});

test('Android native audio helpers (channel counts, buffer sizes, native info) and the Quick Settings tile: JVM self-test, manifest, build wiring', () => {
  const { spawnSync } = require('node:child_process');
  const root = pathx.join(__dirname, '..'), rd = f => fsx.readFileSync(pathx.join(root, f), 'utf8'), dir = fsx.mkdtempSync(pathx.join(require('node:os').tmpdir(), 'st-'));
  const src = n => pathx.join(root, 'platforms', 'android', 'src', 'com', 'audiomixer', 'app', n + '.java');
  const c = spawnSync('javac', ['--release', '8', '-Xlint:-options', '-d', dir, src('Json'), src('Interfaces'), src('NowPlaying'), src('AppInfo'), pathx.join(root, 'platforms', 'android', 'test', 'com', 'audiomixer', 'app', 'Selftest.java')], { encoding: 'utf8' });
  assert.strictEqual(c.status, 0, c.stderr);
  const r = spawnSync('java', ['-cp', dir, 'com.audiomixer.app.Selftest'], { encoding: 'utf8' }); assert.strictEqual(r.status, 0, r.stderr);
  const o = Object.fromEntries(r.stdout.split('\n').filter(l => /^[\w.]+=/.test(l)).map(l => [l.slice(0, l.indexOf('=')), l.slice(l.indexOf('=') + 1)]));
  assert.strictEqual(o['maxChannels.counts'], '2'); assert.strictEqual(o['maxChannels.usb8'], '8'); assert.strictEqual(o['maxChannels.none'], '0');
  assert.strictEqual(o['frames.requested'], '256');                                                                                     // an explicit size is kept
  assert.ok(+o['frames.tooSmall'] === +o['frames.auto'] && +o['frames.auto'] === 384);                                                  // out of range = automatic = two device bursts
  assert.ok(+o['frames.autoOdd'] % 32 === 0 && +o['frames.huge'] <= 2048 && +o['frames.noNative'] > 0);
  const nat = JSON.parse(o.native); assert.deepStrictEqual([nat.sampleRate, nat.framesPerBuffer, nat.lowLatency, nat.pro, nat.usbHost], [48000, 192, true, false, true]); assert.strictEqual(nat.burstMs, 4);
  assert.strictEqual(JSON.parse(o['native.none']).sampleRate, 0);
  const w = JSON.parse(o.with); assert.ok(w.ok === true && Array.isArray(w.interfaces) && w.native.pro === true);                      // /api/interfaces carries the native block
  // /api/nowplaying of the app: same shape as the PC bridge; classified like bridge/nowplaying.js, playing first; without the listener access it says so
  const npj = JSON.parse(o.nowplaying); assert.deepStrictEqual(npj.sessions.map(x => [x.service, x.serviceName, x.status, x.title, x.artist]), [['spotify', 'Spotify', 'playing', 'Karma Police', 'Radiohead'], ['youtube-music', 'YouTube Music', 'paused', 'Lofi', 'Chill'], ['other', 'player', 'stopped', 'Local file', '']]);
  assert.ok(npj.ok === true && npj.access === true && npj.method === 'media-session' && npj.playing.service === 'spotify' && npj.platform === 'android');
  const npn = JSON.parse(o['nowplaying.none']); assert.ok(npn.access === false && npn.playing === null && npn.sessions.length === 0 && /Notifications/.test(npn.hint));
  assert.ok(rd('platforms/android/AndroidManifest.xml').includes('BIND_NOTIFICATION_LISTENER_SERVICE') && rd('platforms/android/AndroidManifest.xml').includes('.NowPlayingListener'));
  // what the app says about itself: SHA-256 of the certificate (colon form), permissions with their reasons, now-playing state
  assert.strictEqual(o.fingerprint, 'BA:78:16:BF:8F:01:CF:EA:41:41:40:DE:5D:AE:22:23:B0:03:61:A3:96:17:7A:9C:B4:10:FF:61:F2:00:15:AD');
  const ai = JSON.parse(o.appinfo); assert.deepStrictEqual([ai.package, ai.versionCode, ai.targetSdk, ai.signer.sha256, ai.installer, ai.debuggable, ai.nowPlaying.accessGranted], ['com.audiomixer.app', 10000, 29, 'AA:BB', 'com.android.vending', false, true]);
  assert.ok(ai.permissions[0].granted === true && /microphone/i.test(ai.permissions[0].why) && ai.permissions[1].why === 'not explained');
  assert.ok(rd('platforms/android/src/com/audiomixer/app/AndroidNowPlaying.java').includes('getActiveSessions(new ComponentName(ctx, NowPlayingListener.class))'));
  // the tile: declared with the system permission and intent, compiled against stubs that never go into the app
  const man = rd('platforms/android/AndroidManifest.xml');
  assert.ok(man.includes('android:name=".EngineTileService"') && man.includes('android.permission.BIND_QUICK_SETTINGS_TILE') && man.includes('android.service.quicksettings.action.QS_TILE') && man.includes('ACTIVE_TILE'));
  const tile = rd('platforms/android/src/com/audiomixer/app/EngineTileService.java'); assert.ok(tile.includes('extends TileService') && tile.includes('EngineService.ACTION_STOP') && tile.includes('startForegroundService') && tile.includes('startActivityAndCollapse'));
  for (const f of ['platforms/android/src/com/audiomixer/app/EngineService.java', 'platforms/android/kotlin/EngineService.kt']) assert.ok(rd(f).includes('EngineState.running'), f);
  const apk = require('../scripts/build-apk'), files = apk.listFiles(pathx.join(root, 'platforms', 'android', 'src'), '.java').map(f => pathx.basename(f));
  assert.ok(files.includes('EngineTileService.java') && files.includes('EngineState.java') && !files.includes('TileService.java') && !files.includes('Selftest.java'));
  assert.ok(fsx.existsSync(pathx.join(root, 'platforms/android/stubs/android/service/quicksettings/TileService.java')));
  const bs = rd('scripts/build-apk.js'); assert.ok(bs.includes('stubs') && bs.includes('--classpath'));
  const mb = rd('platforms/android/src/com/audiomixer/app/MiniBridge.java'); assert.ok(mb.includes('nativeJson()') && mb.includes('withNative'));
});

test('Android engine (Java): virtual devices for the audio HAL - /ws/virtual, /api/virtual, the device in /api/interfaces, input and output with its id', async (tc) => {
  const { spawnSync, spawn } = require('node:child_process');
  const root = pathx.join(__dirname, '..', 'platforms', 'android'), dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'avh-'));
  const files = ['Json', 'Ws', 'AudioBackend', 'Assets', 'Interfaces', 'VirtualHub', 'NowPlaying', 'AppInfo', 'MiniBridge'].map(n => pathx.join(root, 'src', 'com', 'audiomixer', 'app', n + '.java')).concat(pathx.join(root, 'test', 'com', 'audiomixer', 'app', 'BridgeHarness.java'));
  const c = spawnSync('javac', ['--release', '8', '-Xlint:-options', '-d', dir, ...files], { encoding: 'utf8' });
  if (c.error) return tc.skip('no JDK');
  assert.strictEqual(c.status, 0, c.stderr);
  const jv = spawn('java', ['-cp', dir, 'com.audiomixer.app.BridgeHarness', '0'], { stdio: ['pipe', 'pipe', 'inherit'] }), sock = [];
  const until = async (f, ms = 5000) => { const t0 = Date.now(); while (!(await f())) { if (Date.now() - t0 > ms) throw new Error('timeout'); await new Promise(r => setTimeout(r, 10)); } };
  try {
    const port = await new Promise((res, rej) => { let b = ''; jv.stdout.on('data', d => { b += d; const m = /PORT (\d+)/.exec(b); if (m) res(Number(m[1])); }); jv.on('error', rej); setTimeout(() => rej(new Error('no port')), 8000); });
    const base = `http://127.0.0.1:${port}`, open = (path) => new Promise((res, rej) => { const ws = new WebSocket(`ws://127.0.0.1:${port}${path}`); ws.binaryType = 'arraybuffer'; ws.msgs = []; ws.bins = []; ws.onmessage = e => (typeof e.data === 'string' ? ws.msgs.push(JSON.parse(e.data)) : ws.bins.push(Buffer.from(e.data))); ws.onopen = () => res(ws); ws.onerror = () => rej(new Error('connect')); });
    const npa = await (await fetch(base + '/api/nowplaying')).json(); assert.ok(npa.ok === true && npa.access === false && npa.playing === null);      // the fake backend has no media sessions
    // GET /api/app and the switch of the optional now-playing access (custom header, local origins only)
    const app = await (await fetch(base + '/api/app')).json(); assert.ok(app.ok && app.package === 'com.audiomixer.app' && app.signer.sha256.split(':').length === 32 && app.nowPlaying.componentEnabled === false);
    assert.strictEqual((await fetch(base + '/api/app/nowplaying/enable', { method: 'POST' })).status, 400);                                     // no header: a page cannot do it by a plain request
    assert.strictEqual((await fetch(base + '/api/app/nowplaying/enable', { method: 'POST', headers: { 'X-Mixer-Action': 'nowplaying', Origin: 'https://evil.example' } })).status, 403);
    assert.strictEqual((await (await fetch(base + '/api/app/nowplaying/enable', { method: 'POST', headers: { 'X-Mixer-Action': 'nowplaying' } })).json()).enabled, true);
    assert.strictEqual((await (await fetch(base + '/api/app')).json()).nowPlaying.componentEnabled, true);
    assert.strictEqual((await (await fetch(base + '/api/app/nowplaying/disable', { method: 'POST', headers: { 'X-Mixer-Action': 'nowplaying' } })).json()).enabled, false);
    assert.deepStrictEqual(await (await fetch(base + '/api/virtual')).json(), { ok: true, devices: [] });
    const before = await (await fetch(base + '/api/interfaces')).json(); assert.ok(!before.interfaces.some(i => i.transport === 'virtual'));
    const drv = await open('/ws/virtual'); sock.push(drv);
    drv.send(JSON.stringify({ type: 'hello', name: 'Android HAL', channels: 40, rate: 48000 })); await until(() => drv.msgs.length === 1); assert.match(drv.msgs[0].message, /channels/);
    drv.send(JSON.stringify({ type: 'hello', name: 'Android HAL', channels: 2, rate: 48000 })); await until(() => drv.msgs.length === 2); assert.deepStrictEqual([drv.msgs[1].type, drv.msgs[1].id], ['ready', 9000]);
    const dup = await open('/ws/virtual'); sock.push(dup); dup.send(JSON.stringify({ type: 'hello', name: 'Android HAL', channels: 2, rate: 48000 })); await until(() => dup.msgs.length === 1); assert.match(dup.msgs[0].message, /already connected/);
    const ifs = (await (await fetch(base + '/api/interfaces')).json()).interfaces.find(i => i.transport === 'virtual');
    assert.ok(ifs && ifs.name === 'Android HAL' && ifs.inputs === 2 && ifs.outputs === 2 && ifs.read.deviceId === 9000 && ifs.write.deviceId === 9000, JSON.stringify(ifs));
    assert.ok((await (await fetch(base + '/api/interfaces')).json()).interfaces.some(i => i.name === 'Scarlett 2i2 USB'), 'the hardware interfaces are still listed');
    // what apps play reaches the page; a half frame is ignored
    const inp = await open('/ws/input'); sock.push(inp); inp.send(JSON.stringify({ type: 'start', deviceId: 9000, channels: 2, sampleRate: 48000 }));
    await until(() => inp.msgs.some(m => m.type === 'started')); const st = inp.msgs.find(m => m.type === 'started'); assert.deepStrictEqual([st.device, st.hostApi, st.sampleRate, st.channels], ['Android HAL', 'Audio Mixer Virtual', 48000, 2]);
    const pcm = Buffer.alloc(16); [100, -100, 200, -200, 300, -300, 400, -400].forEach((v, i) => pcm.writeInt16LE(v, i * 2));
    drv.send(pcm.subarray(0, 6)); drv.send(pcm); await until(() => inp.bins.length >= 1); assert.ok(Buffer.concat(inp.bins).equals(pcm));
    // what the page sends reaches the HAL
    const out = await open('/ws/output'); sock.push(out); out.send(JSON.stringify({ type: 'start', deviceId: 9000, channels: 2, sampleRate: 48000 }));
    await until(() => out.msgs.some(m => m.type === 'started')); out.send(pcm); await until(() => drv.bins.length >= 1); assert.ok(drv.bins[0].equals(pcm));
    assert.strictEqual((await (await fetch(base + '/api/virtual')).json()).devices[0].capturing, true);
    // the HAL goes away: readers are told, the device disappears
    drv.close(); await until(() => inp.msgs.some(m => m.type === 'error' && /disconnected/.test(m.message)));
    await until(async () => (await (await fetch(base + '/api/virtual')).json()).devices.length === 0);
    const gone = await open('/ws/input'); sock.push(gone); gone.send(JSON.stringify({ type: 'start', deviceId: 9000 })); await until(() => gone.msgs.some(m => m.type === 'error')); assert.match(gone.msgs.at(-1).message, /not connected/);
    // a web page cannot pose as a HAL: a connection with an Origin header is refused
    const refused = await new Promise(res => { const s = require('node:net').connect(port, '127.0.0.1', () => s.write(`GET /ws/virtual HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nOrigin: http://localhost:3000\r\n\r\n`)); let got = ''; s.on('data', d => { got += d; }); s.on('close', () => res(!/ 101 /.test(got))); setTimeout(() => s.destroy(), 800); });
    assert.strictEqual(refused, true);
  } finally { sock.forEach(s => { try { s.close(); } catch (_) { /* closed */ } }); jv.stdin.end(); jv.kill(); fsx.rmSync(dir, { recursive: true, force: true }); }
});

test('Android background engine + power-on animation: manifest, service (Java and Kotlin twin), build wiring, page hooks', () => {
  const root = pathx.join(__dirname, '..'), rd = f => fsx.readFileSync(pathx.join(root, f), 'utf8');
  const man = rd('platforms/android/AndroidManifest.xml');
  assert.ok(man.includes('android.permission.FOREGROUND_SERVICE') && man.includes('android.permission.FOREGROUND_SERVICE_MICROPHONE') && man.includes('<service android:name=".EngineService" android:exported="false" android:foregroundServiceType="microphone"/>') && /RECORD_AUDIO/.test(man) && !/ACCESS_FINE_LOCATION|CAMERA|READ_EXTERNAL/.test(man));
  const java = rd('platforms/android/src/com/audiomixer/app/EngineService.java'), kt = rd('platforms/android/kotlin/EngineService.kt');
  for (const src of [java, kt]) { assert.ok(src.includes('8765') && src.includes('MiniBridge') && src.includes('PARTIAL_WAKE_LOCK') && src.includes('STOP') && src.includes('NotificationChannel') && src.includes('START_STICKY')); }
  assert.ok(java.includes('b.start(8765, 10)') && kt.includes('b.start(8765, 10)') && java.includes('AndroidAssets') && kt.includes('AndroidAssets'));                                  // both twins look for the page's port first
  const act = rd('platforms/android/src/com/audiomixer/app/MainActivity.java'); assert.ok(act.includes('loadMixer(state)') && act.includes('"http://localhost:" + port + "/index.html"') && act.includes('startEngine()') && act.includes('startForegroundService') && act.includes('EngineService.class'));
  const aa = rd('platforms/android/src/com/audiomixer/app/AndroidAudio.java'); assert.ok(aa.includes('AudioRecord') && aa.includes('AudioTrack') && aa.includes('setPreferredDevice') && aa.includes('RECORD_AUDIO') && aa.includes('THREAD_PRIORITY_URGENT_AUDIO'));
  const apk = require('../scripts/build-apk'); const files = apk.listFiles(pathx.join(root, 'platforms', 'android', 'src'), '.java').map(f => pathx.basename(f));
  for (const n of ['MiniBridge.java', 'AndroidAudio.java', 'EngineService.java', 'MainActivity.java', 'Ws.java', 'Json.java', 'Interfaces.java', 'AudioBackend.java']) assert.ok(files.includes(n), n);
  assert.ok(!files.includes('BridgeHarness.java'));                                                                     // test code is not in the app
  const bs = rd('scripts/build-apk.js'); assert.ok(bs.includes("'--target-sdk-version', '35'") && bs.includes('--kotlin') && bs.includes('kotlin-stdlib.jar'));
  // the page: no server-side license step without a license endpoint, power-on sequence
  const html = rd('index.html');
  assert.ok(html.includes("if (bridge() && this.machineId) {"));
  assert.ok(html.includes('<script id="poweron-boot">') && html.includes('window.dismissPowerOn = finish') && html.includes('prefers-reduced-motion') && html.includes("sessionStorage.getItem('halx_poweron')"));
  assert.ok(html.includes("if (window.dismissPowerOn) window.dismissPowerOn(true);"));                                   // fastBoot ends the sequence at once
  for (const k of ['po-glow', 'po-line', 'po-led', 'po-fade-up', 'po-seg', 'po-letter', 'po-draw', 'po-bar']) assert.ok(html.includes('@keyframes ' + k) || html.includes(k), k);
  assert.ok(html.indexOf('<script id="poweron-boot">') < html.indexOf('id="setup-wizard"'));                             // plays before the console markup
});

test('Bluetooth: paired devices and discovery are read from BlueZ / Windows PnP / macOS, only valid addresses are acted on, the routes exist', async () => {
  const fs = require('node:fs'), path = require('node:path');
  const bt = require('./bluetooth');
  const devs = 'Device AA:BB:CC:DD:EE:01 WH-1000XM4\nDevice 11:22:33:44:55:66 Keyboard K380\nnoise\n';
  const info = { 'AA:BB:CC:DD:EE:01': 'Paired: yes\nConnected: yes\nTrusted: yes\nIcon: audio-headset\nRSSI: -52\nBattery Percentage: 0x5a (90)\nUUID: Audio Sink', '11:22:33:44:55:66': 'Paired: no\nConnected: no\nIcon: input-keyboard\nRSSI: 0xffffffc4 (-60)' };
  const calls = [];
  const linux = async (cmd, args) => { calls.push(args.join(' ')); return args[0] === 'show' ? 'Controller 00:11:22:33:44:55 (public)\n\tPowered: yes\n\tDiscovering: no' : args[0] === 'devices' ? devs : args[0] === 'info' ? (info[args[1]] || '') : ''; };
  const l = await bt.list({ platform: 'linux', run: linux });
  assert.ok(l.ok && l.adapter.present && l.adapter.powered && l.scanSupported && l.pairSupported);
  assert.strictEqual(l.devices.length, 2);
  assert.strictEqual(l.devices[0].name, 'WH-1000XM4');                                   // connected audio device first
  assert.ok(l.devices[0].paired && l.devices[0].connected && l.devices[0].audio && l.devices[0].battery === 90 && l.devices[0].rssi === -52);
  assert.ok(!l.devices[1].paired && l.devices[1].rssi === -60);
  const sc = await bt.scan({ seconds: 99, platform: 'linux', run: linux });
  assert.ok(sc.supported && sc.scannedSeconds === 15 && sc.nearby === 1 && calls.some(c => c === '--timeout 15 scan on'));
  assert.strictEqual((await bt.scan({ platform: 'win32' })).supported, false);
  const act = await bt.act('connect', 'aa:bb:cc:dd:ee:01', { platform: 'linux', run: linux });
  assert.ok(act.ok && act.connected && act.address === 'AA:BB:CC:DD:EE:01');
  await assert.rejects(bt.act('connect', 'not-an-address; rm -rf /', { platform: 'linux', run: linux }), /address/);
  await assert.rejects(bt.act('format', 'AA:BB:CC:DD:EE:01', { platform: 'linux', run: linux }), /unknown action/);
  await assert.rejects(bt.act('pair', 'AA:BB:CC:DD:EE:01', { platform: 'win32', run: linux }), /settings/);
  const down = await bt.list({ platform: 'linux', run: async () => '' });
  assert.ok(down.ok && !down.adapter.present && down.devices.length === 0);
  const pnp = JSON.stringify([{ FriendlyName: 'Bose QC35', Status: 'OK', InstanceId: 'BTHENUM\\DEV_0016942AC001\\7&1' }, { FriendlyName: 'Intel(R) Wireless Bluetooth(R)', Status: 'OK', InstanceId: 'USB\\VID_8087&PID_0A2B\\5' }, { FriendlyName: 'Old Mouse', Status: 'Unknown', InstanceId: 'BTHLE\\DEV_AABBCCDDEEFF\\2' }]);
  const w = await bt.list({ platform: 'win32', run: async () => pnp });
  assert.ok(w.adapter.present && w.adapter.powered && w.settingsSupported && !w.pairSupported);
  assert.deepStrictEqual(w.devices.map(d => [d.name, d.address, d.connected]), [['Bose QC35', '00:16:94:2A:C0:01', true], ['Old Mouse', 'AA:BB:CC:DD:EE:FF', false]]);
  const mac = JSON.stringify({ SPBluetoothDataType: [{ controller_properties: { controller_state: 'attrib_on' }, device_connected: [{ 'AirPods Pro': { device_address: 'A0-B1-C2-D3-E4-F5', device_minorType: 'Headphones', device_batteryLevelMain: '80%' } }], device_not_connected: [{ 'MX Keys': { device_address: '00-11-22-33-44-55', device_minorType: 'Keyboard' } }] }] });
  const m = await bt.list({ platform: 'darwin', run: async () => mac });
  assert.deepStrictEqual(m.devices.map(d => [d.name, d.address, d.connected, d.audio, d.battery]), [['AirPods Pro', 'A0:B1:C2:D3:E4:F5', true, true, 80], ['MX Keys', '00:11:22:33:44:55', false, false, null]]);
  assert.strictEqual(bt.openSettings({ platform: 'linux' }).ok, false);
  let started = null; assert.ok(bt.openSettings({ platform: 'win32', spawn: (c, a) => { started = [c, a]; } }).ok && started[1].includes('ms-settings:bluetooth'));
  // routes and packaging
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const j = await (await fetch(base + '/api/bluetooth?force=1')).json();
  assert.ok(j.ok && Array.isArray(j.devices) && j.adapter);
  assert.strictEqual((await fetch(base + '/api/bluetooth', { method: 'POST', body: '{}' })).status, 400);   // POST needs the action header
  const bad = await fetch(base + '/api/bluetooth', { method: 'POST', headers: { 'X-Mixer-Action': 'bluetooth', 'Content-Type': 'application/json' }, body: JSON.stringify({ action: 'connect', address: 'zz' }) });
  assert.strictEqual(bad.status, 400);
  server.closeAllConnections(); server.close();
  assert.ok(fs.readFileSync(path.join(__dirname, '..', 'scripts', 'build.js'), 'utf8').includes("'bridge/bluetooth.js'"));
});

test('real-time pages: LUFS is a real BS.1770 meter, dynamics / gate / mic EQ / drivers / Bluetooth read live data, no page keeps invented numbers', () => {
  const vm = require('node:vm'), fs = require('node:fs'), path = require('node:path');
  const html = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
  const script = id => { const m = new RegExp('<script id="' + id + '">([\\s\\S]*?)</script>').exec(html); assert.ok(m, id); return m[1]; };
  // the LUFS engine runs here against known signals (EBU Tech 3341 style): -23 dBFS 997 Hz in both channels reads -23 LUFS
  const win = { isSecureContext: false }; const ctx = { window: win, document: { getElementById: () => null, addEventListener() {} }, setInterval: () => 0, clearInterval() {}, Math, Float32Array, URL, Blob: class {} };
  vm.runInNewContext(script('rt-lufs'), ctx);
  const L = win.lufsMeter, tone = (fs, db, secs, fn) => { L.setRate(fs); L.reset(); L.sub = []; L.subAcc = 0; L.subN = 0; for (let n = 0; n < fs * secs; n += 2048) { const l = new Float32Array(2048); for (let i = 0; i < 2048; i++) l[i] = fn ? fn(n + i, fs) : Math.pow(10, db / 20) * Math.sin(2 * Math.PI * 997 * (n + i) / fs); L.feed(l, l); } };
  tone(48000, -23, 6); assert.ok(Math.abs(L.integrated + 23) < 0.1 && Math.abs(L.momentary + 23) < 0.1 && Math.abs(L.shortTerm + 23) < 0.1 && Math.abs(L.truePeak + 23) < 0.2, JSON.stringify([L.integrated, L.momentary, L.shortTerm, L.truePeak]));
  tone(44100, -14, 6); assert.ok(Math.abs(L.integrated + 14) < 0.1, 'any sample rate');
  tone(48000, 0, 4, (i, fs) => Math.sin(2 * Math.PI * (fs / 4) * i / fs + Math.PI / 4));            // samples at 0.707, real peak 1.0
  assert.ok(L.truePeak > -0.1 && L.truePeak < 0.3, 'true peak sees the peak between the samples: ' + L.truePeak);
  tone(48000, 0, 5, () => 0); assert.ok(!isFinite(L.integrated) && !isFinite(L.momentary), 'silence is -INF, not a number from a table');
  tone(48000, 0, 20, (i, fs) => (i < fs * 10 ? 0.0316 : 0.1) * Math.sin(2 * Math.PI * 997 * i / fs));   // -30 then -20 dBFS
  assert.ok(Math.abs(L.lra - 10) < 0.5 && L.integrated < -20 && L.integrated > -30, 'loudness range of a two-level programme: ' + L.lra);
  // the page values are written from the meter, the fixed numbers are gone
  assert.ok(!/id="lufs-live-val">-14\.2|id="lufs-short-term">-13\.8|id="lufs-momentary">-13\.5|id="lufs-true-peak">-0\.8|5\.4 LU|w-\[15%\]/.test(html));
  for (const id of ['lufs-lra-val', 'lufs-gr-bar', 'lufs-gr-val', 'lufs-engine-note', 'lufs-card-int']) assert.ok(html.includes('id="' + id + '"'), id);
  assert.ok(/masterLimiter\.reduction/.test(script('rt-lufs')) && /createScriptProcessor/.test(script('rt-lufs')) && /AudioWorkletNode/.test(script('rt-lufs')));
  // dynamics: sliders reach the audio path, gain reduction is the real one; gate: live detector
  const dyn = script('rt-dyn');
  assert.ok(/masterCompressor/.test(dyn) && /\.reduction/.test(dyn) && /setTargetAtTime/.test(dyn) && /comp-gr-meter/.test(dyn) && /gate-state-badge/.test(dyn) && /gate-attenuation-meter/.test(dyn));
  // rt engine: pages and navigation
  const rt = script('rt-engine');
  assert.ok(html.includes("id: 'miceq', text: 'MIC EQ'") && html.includes("'miceq': 'MIC EQ"));
  for (const id of ['tab-miceq', 'meq-bands', 'meq-spec', 'rt-drivers-live', 'rt-bt-live', 'src-meter-strip']) assert.ok(html.includes('id="' + id + '"') || rt.includes(id), id);
  assert.ok(/window\.rtEngine\s*=/.test(rt) && /window\.micEq\s*=/.test(rt) && /\/api\/bluetooth/.test(rt) && /navigator\.bluetooth/.test(rt));
  assert.ok(/window\.rtEngine \? window\.rtEngine\.chLevel\(i\) : 0/.test(html));                      // channel VU reads the engine
  assert.ok(!/Default system audio output<\/option>\s*<optgroup/.test(html) && !/Behringer UMC|Focusrite Scarlett 2i2 \(Driver/.test(html.split('<script id="rt-engine">')[0].slice(html.indexOf('asio-dac-select'), html.indexOf('asio-dac-select') + 800)));
});

test('Bluetooth input strip: a Bluetooth input device is found by flag, name or paired device, bluez devices group as Bluetooth, the strip is wired into the page', () => {
  const vm = require('node:vm'), fs = require('node:fs'), path = require('node:path');
  const gi = require('./interfaces').groupInterfaces([{ id: -3, name: 'bluez_input.AA_BB_CC_DD_EE_01.a2dp-source', hostApi: 'PulseAudio', inputs: 2, outputs: 0 }, { id: -4, name: 'alsa_input.usb-Scarlett', hostApi: 'ALSA', inputs: 2, outputs: 0 }]);
  assert.ok(gi.find(i => /bluez/.test(i.name)).bluetooth && !gi.find(i => /Scarlett/.test(i.name)).bluetooth);
  const html = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
  const m = /<script id="rt-btin">([\s\S]*?)<\/script>/.exec(html); assert.ok(m);
  const l = { interfaces: [{ key: 'usb', name: 'Scarlett 2i2', inputs: 2 }, { key: 'k1', name: 'Pixel 8 (Bluetooth A2DP)', inputs: 2 }, { key: 'k2', name: 'WH-1000XM4 Line', inputs: 1 }, { key: 'k3', name: 'Speakers', inputs: 0, outputs: 2 }, { key: 'k4', name: 'Hands-Free AG Audio', inputs: 1 }], caps: new Map(), status: {} };
  const rt = { bt: { data: { devices: [{ name: 'WH-1000XM4', connected: true, paired: true, battery: 80 }] } }, on() {}, meter() { return {}; } };
  const win = { liveSources: l, rtEngine: rt, licenseChannels: 8 };
  const store = {}; const ctx = { window: win, document: { getElementById: () => null, querySelector: () => null }, localStorage: { getItem: k => store[k] || null, setItem: (k, v) => { store[k] = v; } }, setInterval: () => 0, clearInterval() {}, Math, JSON, Array, Object, String };
  vm.runInNewContext(m[1], ctx);
  const B = win.btInput;
  assert.ok(B.isBt(l.interfaces[1]) && B.isBt(l.interfaces[2]) && B.isBt(l.interfaces[4]) && !B.isBt(l.interfaces[0]), 'by name or by the paired device');
  assert.ok(B.isBt({ key: 'x', name: 'Whatever', bluetooth: true }), 'by the server flag');
  assert.strictEqual(B.select(), 'k1', 'defaults to the first Bluetooth input, outputs are not inputs');
  assert.deepStrictEqual(B.inputs().map(i => i.key), ['usb', 'k1', 'k2', 'k4']);
  assert.strictEqual(B.link(l.interfaces[2]).battery, 80);
  assert.strictEqual(B.routeName(), 'CH 7 / 8', 'the plan limit (8 channels) keeps the pair inside the plan');
  win.licenseChannels = 32; assert.strictEqual(B.routeName(), 'CH 31 / 32');
  B.cfg.route = 'matrix'; assert.strictEqual(B.routeName(), 'MATRIX 1 & 2');
  // page wiring
  for (const id of ['bti-panel', 'bti-src', 'bti-on', 'bti-pol', 'bti-mono', 'bti-bal', 'bti-delay', 'bti-pair', 'bti-il', 'bti-ol']) assert.ok(m[1].includes(id), id);
  assert.ok(/createDelay\(0\.5\)/.test(m[1]) && /createChannelMerger\(2\)/.test(m[1]) && /patchCap/.test(m[1]) && /mtxIn/.test(m[1]) && /masterGain/.test(m[1]) && /channelInterpretation = 'speakers'/.test(m[1]));
  for (const f of ['setBluetoothRoute', 'setBluetoothTrim', 'toggleBluetoothMute', 'setBtTransceiverMode']) assert.ok(m[1].includes('window.' + f + ' = function'), f);   // the old, silent controls drive the strip now
  assert.ok(html.includes("(cap._bt && cap._bt.ctx === c ? cap._bt.out : cap.node).connect(hpf)"));                   // Mic EQ follows the strip
  // ducker decisions (pure): opens above the threshold, holds, closes 3 dB below it after the hold, depth when open, nothing when off
  const d = { on: true, thr: -30, depth: 12, hold: 300 }, st = { duckOpen: false, holdUntil: 0 };
  assert.strictEqual(B.duckStep(d, st, -50, 0), 0);
  assert.strictEqual(B.duckStep(d, st, -20, 100), -12);
  assert.strictEqual(B.duckStep(d, st, -50, 200), -12, 'held for 300 ms after the last loud moment');
  assert.strictEqual(B.duckStep(d, st, -31, 500), -12, 'inside the 3 dB hysteresis it stays open');
  assert.strictEqual(B.duckStep(d, st, -50, 500), 0, 'below the hysteresis after the hold: released');
  assert.strictEqual(B.duckStep(Object.assign({}, d, { on: false }), { duckOpen: true, holdUntil: 0 }, -10, 0), 0);
  assert.deepStrictEqual(Object.keys(B.duckCfg()).sort(), ['attack', 'depth', 'hold', 'on', 'release', 'src', 'thr']);
  for (const id of ['bti-sends', 'bti-dsrc', 'bti-dthr', 'bti-ddep', 'bti-datk', 'bti-dhld', 'bti-drel', 'bti-don', 'bti-dgr']) assert.ok(html.includes('id="' + id + '"'), id);
  assert.ok(/M\.busIn\[b\]/.test(m[1]) && /S\.duck\.gain\.setTargetAtTime|S\.duck\.gain/.test(m[1]) && /S\.mute\.connect\(S\.duck\); S\.duck\.connect\(S\.out\)/.test(m[1]) && /sendNodes/.test(m[1]));
  assert.ok(html.includes('<optgroup label="Bluetooth inputs">') && html.includes('>BLUETOOTH</span>') && html.includes("rt.st[k].bt ?"));
});

test('Windows installer: the app is visible after the install (icons, Start Menu entry, desktop shortcut, start now), the launcher opens a browser the robust way', () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path');
  const rd = (...p) => fs.readFileSync(path.join(__dirname, '..', ...p), 'utf8');
  // every native program carries the application icon (they showed the plain default icon before)
  for (const n of ['launcher', 'setup', 'audio-mixer']) assert.match(rd('platforms', 'windows', 'installer', n + '.rc'), /^1 ICON "AudioMixer\.ico"$/m, n);
  const ico = require('../scripts/build-exe').icoFile();
  assert.strictEqual(ico.readUInt16LE(2), 1); assert.strictEqual(ico.readUInt16LE(4), 7);                    // type icon, 7 sizes
  for (let i = 0; i < 7; i++) { const off = ico.readUInt32LE(6 + 16 * i + 12), len = ico.readUInt32LE(6 + 16 * i + 8); assert.strictEqual(ico.subarray(off + 1, off + 4).toString(), 'PNG'); assert.ok(off + len <= ico.length); }
  assert.strictEqual(ico[6], 16); assert.strictEqual(ico[6 + 16 * 6], 0);                                    // 16 px ... 256 px (stored as 0)
  // MSI: "Audio Mixer" in the Start Menu list, the desktop shortcut on by default, icons on the shortcuts and in Settings > Apps
  const m = require('../scripts/build-msi'), d = fs.mkdtempSync(path.join(os.tmpdir(), 'amx-'));
  fs.writeFileSync(path.join(d, 'index.html'), 'x'); fs.writeFileSync(path.join(d, 'AudioMixerServer.exe'), 'MZ');
  for (const [arch, scope] of [['x64', 'machine'], ['x86', 'machine'], ['x64', 'user']]) {
    const x = m.wxs({ stage: d, version: '1.4.1', arch, scope });
    assert.match(x, /<DirectoryRef Id="ProgramMenuFolder">\s*<Component Id="StartRootShortcut"[\s\S]*?Name="Audio Mixer" Target="\[INSTALLDIR\]AudioMixerServer\.exe" Arguments="\/open"/);
    assert.match(x, /Feature Id="Shortcuts"[^>]*>.*ComponentRef Id="StartRootShortcut"/); assert.match(x, /Feature Id="Desktop"[^>]*Level="1"/);
    assert.match(x, /<Icon Id="AudioMixer\.exe" SourceFile=/); assert.match(x, /Property Id="ARPPRODUCTICON" Value="AudioMixer\.exe"/); assert.match(x, /Id="ScDesk"[^>]*Icon="AudioMixer\.exe"/);
  }
  assert.doesNotMatch(m.wxs({ stage: fs.mkdtempSync(path.join(os.tmpdir(), 'amy-')), version: '1.4.1' }), /ARPPRODUCTICON/);   // no launcher in the tree: no icon reference
  // setup program: desktop shortcut by default (/nodesktop removes it), done message and start-now offer after an interactive install, started without admin rights
  const stub = rd('platforms', 'windows', 'installer', 'setup-stub.c');
  assert.match(stub, /desktop = 1, nopath = 0/); assert.ok(stub.includes('L"/nodesktop"') && stub.includes('!desktop'));
  assert.match(stub, /Audio Mixer is installed\./); assert.match(stub, /!quiet && !passive/); assert.match(stub, /Audio Mixer\.lnk/); assert.match(stub, /explorer\.exe/); assert.match(stub, /CSIDL_COMMON_PROGRAMS/);
  // launcher: default browser, then Explorer, then "start", then the address in a message
  const lau = rd('platforms', 'windows', 'installer', 'launcher.c');
  assert.match(lau, /static void open_page\(void\)/); assert.match(lau, /> 32\) return;[\s\S]*explorer\.exe[\s\S]*cmd\.exe \/c start[\s\S]*http:\/\/localhost:8765\//); assert.match(lau, /if \(show_splash\(inst\)\) open_page\(\)/);
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
    await assert.rejects(update.install({ current: '1.0.0', fetchImpl: route, url: 'https://ota.test/update.json', dir: pathx.join(tmp, 'dl3'), platform: 'android', arch: 'arm64', jwk, spawnImpl: fakeSpawn }), /no update file for this system/);
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

test('no Windows Defender scanner: the verify command, the setup program and the Start Menu shortcut no longer start a Defender scan (a false PUP detection trigger); the file / signature checks stay', async () => {
  const fs = require('node:fs'), path = require('node:path'), rd = (...p) => fs.readFileSync(path.join(__dirname, '..', ...p), 'utf8');
  const v = require('../client/verify');
  assert.ok(!('checkDefender' in v) && !('parseDefender' in v) && typeof v.checkManifest === 'function' && typeof v.checkSignature === 'function' && typeof v.checkLoopbackOnly === 'function');
  // nothing that ships can call Defender: no PowerShell Defender cmdlets, no MpCmdRun, in the client, the bridge, the installers or the MSI definition
  const code = ['client/verify.js', 'client/cli.js', 'client/service.js', 'platforms/windows/installer/setup-stub.c', 'platforms/windows/installer/launcher.c', 'platforms/windows/installer/audio-mixer.c', 'scripts/build-msi.js'].map(f => rd(...f.split('/'))).join('\n') + fs.readdirSync(path.join(__dirname)).filter(f => /\.js$/.test(f) && f !== 'test.js').map(f => rd('bridge', f)).join('\n');
  assert.ok(!/Start-MpScan|Get-MpThreat|MpCmdRun|Add-MpPreference|Set-MpPreference|Get-MpComputerStatus|Update-MpSignature/i.test(code), 'no Defender cmdlet or tool is called');
  assert.ok(!/Defender/.test(rd('scripts', 'build-msi.js')) && !/verify --scan/.test(rd('scripts', 'build-msi.js')));
  assert.match(rd('scripts', 'build-msi.js'), /Verify installation \(file check\)[^\n]*verify --pause/);
  // the setup program: no scan after the install; /scan from an old command line is accepted and not passed on to Windows Installer
  const stub = rd('platforms', 'windows', 'installer', 'setup-stub.c');
  assert.ok(!/scan = 1|&& scan\)|verify --scan/.test(stub) && /L"\/scan"\)\) continue;/.test(stub));
  // the command line: --scan is ignored with a note, the file checks still run
  const { execFileSync } = require('node:child_process');
  const out = execFileSync('node', [path.join(__dirname, '..', 'client', 'cli.js'), 'verify', '--scan'], { encoding: 'utf8' });
  assert.match(out, /Result: VERIFIED/); assert.match(out, /--scan was removed/); assert.ok(!/Defender/.test(out));
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

test('Windows installers carry the ASIO driver files (Program Files / Program Files (x86)); registering it is the optional feature AsioDriver, the setup asks (default No) or takes /asio', () => {
  const fsx = require('node:fs'), pathx = require('node:path'), osx = require('node:os'), cp = require('node:child_process');
  const m = require('../scripts/build-msi'), root = pathx.join(__dirname, '..');
  const stage = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'asiostage-')); fsx.mkdirSync(pathx.join(stage, 'asio-driver'), { recursive: true }); fsx.writeFileSync(pathx.join(stage, 'LICENSE'), 'x');
  try {
    for (const f of ['AudioMixerASIO32.dll', 'AudioMixerASIO64.dll']) fsx.writeFileSync(pathx.join(stage, 'asio-driver', f), 'x');
    const x64 = m.wxs({ stage, version: '9.0.0', arch: 'x64' }), x86 = m.wxs({ stage, version: '9.0.0', arch: 'x86' });
    const feature = w => /<Feature Id="AsioDriver"[^>]*Level="3"[^>]*>(.*?)<\/Feature>/.exec(w);          // Level 3: off unless ADDLOCAL names it
    assert.ok(feature(x64) && /AsioReg64/.test(feature(x64)[1]) && /AsioReg32/.test(feature(x64)[1]));
    assert.match(x64, /<Component Id="AsioReg64"[^>]*Win64="yes"/); assert.match(x64, /<Component Id="AsioReg32"[^>]*Win64="no"/);          // 64-bit and 32-bit registry views
    assert.match(x64, /InprocServer32" Type="string" Value="\[INSTALLDIR\]asio-driver\\AudioMixerASIO64\.dll"/); assert.match(x64, /Key="SOFTWARE\\ASIO\\Audio Mixer" Name="CLSID"[^>]*\{C1893F2F-1AD5-4344-9806-DCFD242C0D48\}/);
    assert.match(x64, /Directory Id="ProgramFiles64Folder"/); assert.match(x86, /Directory Id="ProgramFilesFolder"/);
    assert.ok(feature(x86) && !/AsioReg64/.test(feature(x86)[1]) && /AsioReg32/.test(feature(x86)[1]) && !/Value="\[INSTALLDIR\]asio-driver\\AudioMixerASIO64/.test(x86));
    assert.ok(!/AsioDriver/.test(m.wxs({ stage, version: '9.0.0', arch: 'x64', scope: 'user' })), 'HKLM\\SOFTWARE\\ASIO is per machine: the per-user package has no driver feature');
    fsx.rmSync(pathx.join(stage, 'asio-driver', 'AudioMixerASIO32.dll')); assert.ok(!/AsioDriver/.test(m.wxs({ stage, version: '9.0.0', arch: 'x64' })), 'no DLL, no feature');
    // the same keys that DllRegisterServer writes
    const cpp = fsx.readFileSync(pathx.join(root, 'platforms', 'windows', 'asio-driver', 'audiomixer_asio.cpp'), 'utf8'); assert.ok(cpp.includes('SOFTWARE\\ASIO\\Audio Mixer') && cpp.includes('InprocServer32') && cpp.includes('ThreadingModel'));
  } finally { fsx.rmSync(stage, { recursive: true, force: true }); }
  // the setup program: /asio, /noasio, a Yes / No question whose default is No, ADDLOCAL gets AsioDriver only on request
  const stub = fsx.readFileSync(pathx.join(root, 'platforms', 'windows', 'installer', 'setup-stub.c'), 'utf8');
  assert.match(stub, /L"\/asio"/); assert.match(stub, /L"\/noasio"/); assert.match(stub, /MB_YESNO \| MB_DEFBUTTON2/); assert.match(stub, /if \(asio\) wcscat\(add, L",AsioDriver"\)/);
  assert.match(stub, /!asio && !noasio && !quiet && !passive && MessageBoxW/);          // never asked (and never registered) in a silent install
  const bi = fsx.readFileSync(pathx.join(root, 'scripts', 'build-installers.js'), 'utf8'); assert.match(bi, /addAsioDriver\(staged/); assert.match(bi, /MANIFEST\.sha256/);
  const gcc = cp.spawnSync('which', ['x86_64-w64-mingw32-gcc']); if (!gcc.status) { const o = pathx.join(osx.tmpdir(), 'stub-' + process.pid + '.o'); const r = cp.spawnSync('x86_64-w64-mingw32-gcc', ['-c', '-DREQUIRE_X64', '-o', o, pathx.join(root, 'platforms', 'windows', 'installer', 'setup-stub.c')], { encoding: 'utf8' }); fsx.rmSync(o, { force: true }); assert.strictEqual(r.status, 0, r.stderr); }
});

test('phone layout: every rule is scoped to html.phone-ui (or its own #phone- elements), so tablets, laptops and desktops keep the page they had', () => {
  const html = require('node:fs').readFileSync(require('node:path').join(__dirname, '..', 'index.html'), 'utf8');
  const css = /<style id="phone-ui-css">([\s\S]*?)<\/style>/.exec(html); assert.ok(css, 'the phone layout has its own style block');
  const rules = css[1].replace(/\/\*[\s\S]*?\*\//g, '').split('}').map(r => r.split('{')[0].trim()).filter(Boolean);
  assert.ok(rules.length > 40);
  for (const sel of rules.flatMap(r => r.split(','))) assert.match(sel.trim(), /^(html\.phone-ui|#phone-bar|#phone-sheet|#phone-mon-btn)\b/, 'unscoped selector: ' + sel.trim());
  assert.ok(!/@media/.test(css[1]), 'the gate is the class, set by the script');
  const js = /<script id="phone-ui-js">([\s\S]*?)<\/script>/.exec(html); assert.ok(js);
  assert.match(js[1], /max-width: 700px\) and \(pointer: coarse\)/); assert.match(js[1], /phone=0/); assert.match(js[1], /phone=1/);
  assert.doesNotThrow(() => new Function(js[1]));
  for (const id of ['mixer', 'live', 'meter', 'fx']) assert.ok(html.includes(`id="nav-${id}"`) || html.includes(`'nav-${id}'`) || html.includes(`"nav-" + `) || html.includes('nav-'), id);
});

test('METER SOURCE: the VU, LUFS, scope and phase pages follow one chosen source; the scope shows the real samples (trigger, timebase, frequency) and Now Playing can be captured into the meters', () => {
  const fsx = require('node:fs'), pathx = require('node:path'), vm = require('node:vm');
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  const mod = /<script id="meter-source">([\s\S]*?)<\/script>/.exec(html); assert.ok(mod, 'the meter-source script is in the page');
  assert.ok(!/Math\.random\(\)[^\n]*(osc-vpp|osc-freq)|osc-vrms-val'\);\s*\n\s*if \(vrmsEl\)/.test(html), 'no simulated scope readings');
  const scopeFn = html.slice(html.indexOf('function renderOscFrame'), html.indexOf('window.toggleOscRun = function'));
  assert.ok(!/Math\.sin|Math\.random/.test(scopeFn) && scopeFn.includes('window.meterSource.drawScope'), 'the scope draws the chosen source, not simulated waves');
  // the pages read the chosen source (and still the master by default)
  assert.match(html, /window\.meterSource \? window\.meterSource\.vu\(\)/); assert.match(html, /window\.meterSource \? window\.meterSource\.analysers\(\)/); assert.match(html, /const want = \(window\.meterSource && window\.meterSource\.node\(\)\) \|\| lim/);
  assert.match(mod[1], /PAGES = \['meter', 'lufs', 'osc', 'phase'\]/); assert.match(mod[1], /window\.setOscSource = function \(val\) \{ ms\.set\(val\)/);
  // Now Playing -> capture: loopback interface or the system / tab audio picker, tagged with service and title, selected in the meters
  for (const s of ['captureNowPlaying', 'tagMusic', 'ls-np-cap', 'IN THE METERS']) assert.ok(html.includes(s), s);
  // the arithmetic of the scope, run on real sample blocks
  const win = { addEventListener() {}, localStorage: undefined }, doc = { getElementById: () => null, addEventListener() {}, querySelector: () => null, querySelectorAll: () => [] };
  vm.runInNewContext(mod[1], { window: win, document: doc, setInterval() { return 0; }, clearInterval() {}, performance: { now: () => 0 }, CustomEvent: function () {} });
  const calc = win.meterSource.scopeCalc, fs = 48000, size = 32768;
  const wave = (f, amp, fn) => { const A = new Float32Array(size); for (let i = 0; i < size; i++) A[i] = amp * fn(2 * Math.PI * f * i / fs); return A; };
  let A = wave(440, 0.5, Math.sin), r = calc(A, A, fs, { timebase: 1, triggerLevel: -12, triggerEdge: 'rise' });
  assert.ok(r.triggered && r.N === 480 && r.start + r.N <= size, JSON.stringify(r)); assert.ok(Math.abs(r.freq - 440) < 1 && Math.abs(r.p2p - 1) < 0.01 && Math.abs(r.rms - 0.3536) < 0.01 && Math.abs(r.crest - 3.01) < 0.1, JSON.stringify(r));
  assert.ok(A[r.start - 1] < 0.25 && A[r.start] >= 0.25, 'the sweep starts at a rising crossing of the trigger level (-12 dB = 0.25)');
  r = calc(A, A, fs, { timebase: 1, triggerLevel: -12, triggerEdge: 'fall' }); assert.ok(r.triggered && A[r.start - 1] > -0.25 && A[r.start] <= -0.25, 'falling edge');
  assert.strictEqual(calc(A, A, fs, { timebase: 25 }).N, 12000, '25 ms/div = 250 ms of samples');
  assert.ok(calc(A, A, fs, { timebase: 1000 }).N <= size / 2, 'never longer than half the buffer');
  r = calc(wave(1000, 0.8, x => (Math.sin(x) >= 0 ? 1 : -1)), A, fs, { timebase: 0.5, triggerLevel: -6, triggerEdge: 'rise' }); assert.ok(Math.abs(r.freq - 1000) < 2 && Math.abs(r.p2p - 1.6) < 0.01, JSON.stringify(r));
  const q = calc(new Float32Array(size), A, fs, { timebase: 1, triggerLevel: -12 }); assert.ok(!q.triggered && q.start === size - q.N && q.freq === 0 && q.p2p === 0 && q.crest === 0, 'silence: no trigger, no frequency');
  assert.ok(Math.abs(calc(wave(60, 0.3, Math.sin), A, fs, { timebase: 10 }).freq - 60) < 0.5, 'low frequencies too');
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

test('native engine in the page: the WebAssembly build is embedded, imports nothing, runs the loudness and scope vectors, replaces the JavaScript meters behind the same objects', async () => {
  const fsx = require('node:fs'), pathx = require('node:path'), vm = require('node:vm'), crypto = require('node:crypto');
  const root = pathx.join(__dirname, '..'), html = fsx.readFileSync(pathx.join(root, 'index.html'), 'utf8');
  const { parse } = require('../engine/tests/vectors'), { gen } = require('../engine/tests/signals');
  const m = /<script id="engine-wasm">([\s\S]*?)<\/script>/.exec(html); assert.ok(m, 'the engine-wasm script is in the page');
  const b64 = /AM_WASM_B64:BEGIN\*\/'([A-Za-z0-9+/=]+)'\/\*AM_WASM_B64:END/.exec(m[1]); assert.ok(b64, 'the binary is embedded');
  const bytes = Buffer.from(b64[1], 'base64'); assert.ok(bytes.length > 5000 && bytes.length < 100000, 'size ' + bytes.length);
  assert.ok(bytes.equals(fsx.readFileSync(pathx.join(root, 'engine', 'wasm', 'am_core.wasm'))), 'the page carries engine/wasm/am_core.wasm (node scripts/build-engine.js --wasm re-embeds it)');
  const mod = new WebAssembly.Module(bytes); assert.strictEqual(WebAssembly.Module.imports(mod).length, 0, 'freestanding: no imports');
  // run the script of the page: the loudness object and the scope function behind it
  const calls = []; const win = { lufsMeter: { fs: 48000, samples: 0, momentary: -Infinity, feed() { calls.push('js-feed'); }, setRate() { calls.push('js-rate'); }, reset() { calls.push('js-reset'); } }, meterSource: { scopeCalc() { return 'js'; } } };
  vm.runInNewContext(m[1], { window: win, WebAssembly, atob: s => Buffer.from(s, 'base64').toString('binary'), Float32Array, Uint8Array, Math });
  for (let i = 0; i < 100 && !win.amEngine.ready; i++) await new Promise(r => setTimeout(r, 20));
  const E = win.amEngine; assert.ok(E.ready && E.version === 1 && E.source === 'webassembly');
  assert.strictEqual(win.lufsMeter.engine, 'webassembly');
  // the vectors the page's engine must give
  let n = 0;
  for (const c of parse()) {
    if (c.kind === 'lufs') {
      const fsHz = Number(c.head[2]), block = Number(c.head[3]), len = Math.round(fsHz * Number(c.head[4])), l = gen(c.head[5], fsHz, len, 0), r = gen(c.head[5], fsHz, len, 1);
      E.lufs.init(fsHz); for (let i = 0; i < len; i += block) E.lufs.feed(l.subarray(i, i + block), r.subarray(i, i + block));
      const v = E.lufs.read(), want = c.expect; n++;
      for (const [k, w, tol] of [['momentary', want[0], 0.02], ['shortTerm', want[1], 0.02], ['integrated', want[2], 0.02], ['lra', want[3], 0.02], ['truePeak', want[4], 0.01]]) assert.ok(w === -Infinity ? v[k] === -Infinity : Math.abs(v[k] - w) <= tol, `${c.id} ${k}: ${v[k]} vs ${w}`);
    } else if (c.kind === 'scope') {
      const fsHz = Number(c.head[2]), size = Number(c.head[3]), A = gen(c.head[7], fsHz, size, 0), r = E.scopeCalc(A, fsHz, { timebase: Number(c.head[4]), triggerLevel: Number(c.head[5]), triggerEdge: c.head[6] === '1' ? 'rise' : 'fall' }), w = c.expect; n++;
      assert.ok(r.N === w[0] && r.start === w[1] && r.triggered === (w[2] === 1), c.id + ' sweep'); for (const [k, x, tol] of [['p2p', w[3], 1e-6], ['rms', w[4], 1e-6], ['crest', w[5], 1e-4], ['freq', w[6], 1e-6]]) assert.ok(Math.abs(r[k] - x) <= tol * Math.max(1, Math.abs(x)), `${c.id} ${k}: ${r[k]} vs ${x}`);
    }
  }
  assert.ok(n >= 20, 'loudness and scope vectors run in the page engine: ' + n);
  // the page's objects use it: feed -> the fields the page paints
  const left = gen('sine:1000:0.5', 48000, 4800 * 4, 0); win.lufsMeter.setRate(48000); win.lufsMeter.feed(left.subarray(0, 9600), left.subarray(0, 9600)); win.lufsMeter.feed(left.subarray(9600, 19200), left.subarray(9600, 19200));
  assert.ok(win.lufsMeter.samples === 19200 && Math.abs(win.lufsMeter.momentary + 6.01) < 0.05 && Math.abs(win.lufsMeter.truePeak + 6.02) < 0.05, 'the LUFS object follows the engine: ' + win.lufsMeter.momentary);
  assert.ok(!calls.includes('js-feed'), 'the JavaScript loudness code does not run when the engine is there');
  const sc = win.meterSource.scopeCalc(gen('sine:440:0.5', 48000, 32768, 0), null, 48000, { timebase: 1, triggerLevel: -12, triggerEdge: 'rise' }); assert.ok(Math.abs(sc.freq - 440) < 0.1, 'meterSource.scopeCalc uses the engine');
  assert.strictEqual(win.meterSource.scopeCalc(new Float32Array(40000), null, 48000, { timebase: 1 }), 'js', 'a block the engine cannot take falls back to JavaScript');
  // without WebAssembly (or with a damaged binary) nothing changes
  const win2 = { lufsMeter: { feed() { return 'js'; } }, meterSource: { scopeCalc() { return 'js'; } } };
  vm.runInNewContext(m[1], { window: win2, WebAssembly: undefined, atob: s => Buffer.from(s, 'base64').toString('binary'), Float32Array, Uint8Array, Math }); assert.strictEqual(win2.amEngine.ready, false); assert.strictEqual(win2.lufsMeter.feed(), 'js');
  const win3 = { lufsMeter: { feed() { return 'js'; } }, meterSource: { scopeCalc() { return 'js'; } } };
  vm.runInNewContext(m[1].replace(b64[1], 'AAAA'), { window: win3, WebAssembly, atob: s => Buffer.from(s, 'base64').toString('binary'), Float32Array, Uint8Array, Math }); await new Promise(r => setTimeout(r, 100)); assert.strictEqual(win3.amEngine.ready, false); assert.strictEqual(win3.lufsMeter.feed(), 'js');
  void crypto;
});

test('native engine in the bridge: the Node addon (C++) gives the numbers of the JavaScript meter, the loudness meter and the scope of the vectors', (tc) => {
  const be = require('../scripts/build-engine'), r = be.buildNode();
  if (r.skipped) return tc.skip(r.skipped); assert.strictEqual(r.ok, true, r.detail);
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

test('the page\'s OS detection (index.html otaDetectOs / otaPick) gives the same answers as bridge/osdetect.js', () => {
  const fs = require('node:fs'), path = require('node:path'), vm = require('node:vm'), od = require('./osdetect');
  const html = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
  const a = html.indexOf('const OTA_FILES = {'), b = html.indexOf('const otaOthers = ');
  assert.ok(a > 0 && b > a, 'the page carries its detector');
  const ctx = vm.createContext({}); vm.runInContext(html.slice(a, b) + '\nthis.detect = otaDetectOs; this.pick = otaPick; this.keys = otaKeysFor; this.FILES = OTA_FILES;', ctx);
  const clone = x => JSON.parse(JSON.stringify(x));
  const UAS = [
    'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/126.0.0.0 Safari/537.36', 'Mozilla/5.0 (Windows NT 6.1; WOW64; Trident/7.0; rv:11.0) like Gecko', 'Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 Chrome/49.0 Safari/537.36',
    'Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 Chrome/126.0.0.0 Mobile Safari/537.36', 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 Version/17.5 Mobile/15E148 Safari/604.1',
    'Mozilla/5.0 (iPad; CPU OS 17_5 like Mac OS X) AppleWebKit/605.1.15', 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 Safari/605.1.15', 'Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0',
    'Mozilla/5.0 (X11; Fedora; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0', 'Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 Chrome/126.0', 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/126.0',
    'Mozilla/5.0 (X11; CrOS x86_64 14541.0.0) AppleWebKit/537.36 Chrome/126.0 Safari/537.36', 'curl/8.5.0', '', undefined,
  ];
  const extras = [{}, { os: 'android' }, { os: 'win', arch: 'ia32' }, { os: 'linux', distro: 'Ubuntu' }, { os: 'ubuntu' }, { platformHint: '"Android"' }, { platformHint: '"macOS"', archHint: '"arm"' }, { platformHint: '"Windows"', archHint: '"x86"' }, { platformHint: '"Linux"' }, { platformHint: '"Chrome OS"' }];
  for (const userAgent of UAS) for (const e of extras) {
    const o = { userAgent, ...e };
    assert.deepStrictEqual(clone(ctx.detect(o)), od.detectOs(o), JSON.stringify(o));
    const det = od.detectOs(o); assert.deepStrictEqual(clone(ctx.keys(det)), od.keysFor(det));
  }
  assert.deepStrictEqual(clone(ctx.FILES), od.FILES);
  const man = { files: Object.fromEntries(Object.keys(od.FILES).map(k => [k, { name: k + '.bin', url: 'https://x.test/' + k, size: 1, sha256: 'a'.repeat(64) }])) };
  for (const os of od.OSES) for (const arch of ['x64', 'x86', 'arm64', null]) for (const distro of ['debian', 'other', null]) {
    const det = { os, arch, distro }, x = ctx.pick(man, det), y = od.pick(man, det);
    assert.deepStrictEqual(clone(x), y);
  }
});

test('Windows registry and environment: variables, App Paths, version keys, .amix + audiomixer:// handlers, optional tools PATH; setup switches', async () => {
  const { spawnSync } = require('node:child_process');
  const root = pathx.join(__dirname, '..'), m = require('../scripts/build-msi');
  const stage = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'rg-')); fsx.writeFileSync(pathx.join(stage, 'LICENSE'), 'MIT');
  for (const f of ['audio-mixer.exe', 'AudioMixerServer.exe']) fsx.writeFileSync(pathx.join(stage, f), 'x');
  fsx.mkdirSync(pathx.join(stage, 'asio-driver')); for (const f of ['AudioMixerASIO32.dll', 'AudioMixerASIO64.dll']) fsx.writeFileSync(pathx.join(stage, 'asio-driver', f), 'x');
  for (const [arch, scope] of [['x64', 'machine'], ['x86', 'machine'], ['x64', 'user']]) {
    const w = m.wxs({ stage, version: '1.10.0', arch, scope }), hive = scope === 'user' ? 'HKCU' : 'HKLM';
    assert.match(w, /<Feature Id="Registry"[^>]*Level="1"/); assert.match(w, /<Feature Id="FileTypes"[^>]*Level="1"/); assert.match(w, /<Feature Id="ToolsPath"[^>]*Level="3"/);   // tools PATH is off unless asked
    assert.ok(w.includes(`Root="${hive}" Key="Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\audio-mixer.exe"`));
    assert.ok(w.includes('Name="Version" Type="string" Value="1.10.0"') && w.includes(`Name="Arch" Type="string" Value="${arch}"`) && w.includes('Name="DriverDir"'));
    assert.ok(w.includes('Key="Software\\Classes\\.amix"') && w.includes('Name="URL Protocol"') && w.includes('Key="Software\\Classes\\audiomixer\\shell\\open\\command"'));
    assert.ok(w.includes('Value="&quot;[INSTALLDIR]audio-mixer.exe&quot; open &quot;%1&quot;"'));       // the argument is quoted, and only "open" is run
    if (spawnSync('wixl', ['--version']).status !== 0 || spawnSync('msibuild', ['-h']).status === null) continue;
    const wk = pathx.join(stage, 'w-' + arch + scope); fsx.mkdirSync(wk); fsx.writeFileSync(pathx.join(wk, 'License.rtf'), m.rtf('MIT')); fsx.writeFileSync(pathx.join(wk, 'a.wxs'), w);
    assert.strictEqual(spawnSync('wixl', ['--arch', arch, '--ext', 'ui', '-o', pathx.join(wk, 'a.msi'), pathx.join(wk, 'a.wxs')], { cwd: wk }).status, 0);
    m.addEnvironment(pathx.join(wk, 'a.msi'), wk, m.environmentRows({ stage }));
    const q = pathx.join(wk, 'q'); fsx.mkdirSync(q); spawnSync('msidump', ['-t', pathx.join(wk, 'a.msi')], { cwd: q });
    const env = fsx.readFileSync(pathx.join(q, 'Environment.idt'), 'utf8');
    assert.match(env, /PathAudioMixer\t=\*PATH\t\[~\];\[INSTALLDIR\]\tCommandPath/);                             // unchanged: audio-mixer.exe on PATH
    assert.match(env, /EnvAudioMixerDir\t=\*AUDIO_MIXER_DIR\t\[INSTALLDIR\]\tRegistryKeys/);                  // "=" set, "*" removed on uninstall
    assert.match(env, /EnvAudioMixerAsio\t=\*AUDIO_MIXER_ASIO\t\[INSTALLDIR\]asio-driver\tRegistryKeys/);
    assert.match(env, /PathAudioMixerTools\t=\*PATH\t\[~\];\[INSTALLDIR\]asio-driver\tToolsPathKey/);          // follows the (off by default) ToolsPath feature
    const feat = fsx.readFileSync(pathx.join(q, 'Feature.idt'), 'utf8'), fc = fsx.readFileSync(pathx.join(q, 'FeatureComponents.idt'), 'utf8');
    assert.match(feat, /^ToolsPath\t.*\t3\t/m); assert.match(fc, /^ToolsPath\tToolsPathKey/m); assert.match(fc, /^Registry\tRegistryKeys/m); assert.match(fc, /^FileTypes\tFileTypeKeys/m);
  }
  // AUDIO_MIXER_HOME already means "the private data folder" on Linux / macOS (ensure-node.sh): the install folder variable has another name
  assert.ok(!fsx.readFileSync(pathx.join(root, 'scripts', 'build-msi.js'), 'utf8').includes("=*AUDIO_MIXER_HOME"));
  const stub = fsx.readFileSync(pathx.join(root, 'platforms', 'windows', 'installer', 'setup-stub.c'), 'utf8');
  for (const t of ['/noregistry', '/nofiletypes', '/toolspath', ',Registry', ',FileTypes', ',ToolsPath']) assert.ok(stub.includes(t), t);
  const gcc = spawnSync('x86_64-w64-mingw32-gcc', ['-fsyntax-only', '-municode', '-Wall', '-Werror', pathx.join(root, 'platforms', 'windows', 'installer', 'setup-stub.c')]);
  if (!gcc.error) assert.strictEqual(gcc.status, 0, String(gcc.stderr));
});

test('audio-mixer open: only .amix files and audiomixer://page/<tab> links; a scene file reaches the page once through /api/handoff', async () => {
  const cli = require('../client/cli.js');
  assert.deepStrictEqual(cli.parseOpenTarget('audiomixer://page/Mixer'), { kind: 'link', query: '?page=mixer' });
  assert.deepStrictEqual(cli.parseOpenTarget('audiomixer://'), { kind: 'link', query: '' });
  for (const bad of ['audiomixer://page/../x', 'audiomixer://page/a;calc', 'audiomixer://page/' + 'a'.repeat(17), 'audiomixer://run/calc', 'notes.txt', 'x.amix.exe', '', 'file://evil/x.amix', 'file:///etc/passwd']) assert.ok(cli.parseOpenTarget(bad).error, bad);
  assert.strictEqual(cli.parseOpenTarget('file:///tmp/a%20b.amix').file, '/tmp/a b.amix');
  assert.ok(cli.openCommand('linux', 'http://localhost:8765/?page=scenes&scene=1') && !cli.openCommand('linux', 'http://localhost:8765/?page=a;calc') && !cli.openCommand('linux', 'http://localhost:8765/?x=1&y=%0a'));
  const d = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'op-'));
  const put = (n, t) => { const f = pathx.join(d, n); fsx.writeFileSync(f, t); return f; };
  assert.deepStrictEqual(cli.readScenes(put('ok.amix', '[null,{"name":"a"}]')), [null, { name: 'a' }]);
  for (const t of ['{"a":1}', '[1,2]', '[[]]', 'not json', JSON.stringify(Array(17).fill(null))]) assert.throws(() => cli.readScenes(put('bad.amix', t)), /scene file/);
  assert.throws(() => cli.readScenes(put('big.amix', '[' + '{"a":"' + 'x'.repeat(1 << 20) + '"}]')), /larger than 1 MB/);
  assert.throws(() => cli.readScenes(pathx.join(d, 'missing.amix')), /cannot read/);
  // the server: needs the header, JSON and a valid archive; the page takes it once
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`, post = (body, hdr = { 'X-Mixer-Action': 'handoff' }) => fetch(base + '/api/handoff', { method: 'POST', headers: { 'Content-Type': 'application/json', ...hdr }, body: JSON.stringify(body) });
  assert.strictEqual((await post({ scenes: [null] }, {})).status, 400);                           // no X-Mixer-Action: a foreign page cannot trigger it
  assert.strictEqual((await post({ scenes: 'x' })).status, 400); assert.strictEqual((await post({ scenes: [[1]] })).status, 400); assert.strictEqual((await post({ scenes: [1] })).status, 400);
  assert.strictEqual((await (await fetch(base + '/api/handoff')).json()).ok, false);              // nothing waiting
  const big = { scenes: [{ name: 'x'.repeat(100000) }] };                                          // above the 8 KB of other actions, below 1 MB
  assert.strictEqual((await post(big)).status, 200);
  const got = await (await fetch(base + '/api/handoff')).json();
  assert.strictEqual(got.ok, true); assert.strictEqual(got.scenes[0].name.length, 100000);
  assert.strictEqual((await (await fetch(base + '/api/handoff')).json()).ok, false);              // taken once
  assert.strictEqual((await post({ scenes: [null] }, { 'X-Mixer-Action': 'handoff', Origin: 'https://evil.example' })).status, 403);
  server.closeAllConnections(); server.close();
  // the page: ?page= must name a tab that exists, ?scene=1 asks before it replaces the scene slots
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  assert.match(html, /getElementById\('nav-' \+ page\)/); assert.match(html, /confirm\('Import the scene file you opened/);
});

test('Linux and macOS own mechanisms: profile.d / environment.d, mime type + desktop handler, paths.d + launchd; kernel module and am-pump', async () => {
  const { spawnSync } = require('node:child_process');
  const root = pathx.join(__dirname, '..'), u = require('../scripts/build-unix');
  assert.match(u.MIME_XML, /<glob pattern="\*\.amix"\/>/); assert.match(u.MIME_XML, /application\/x-audio-mixer-scene/);
  const open = u.openDesktopEntry('audio-mixer');
  assert.match(open, /^Exec=audio-mixer open %u$/m); assert.match(open, /^MimeType=application\/x-audio-mixer-scene;x-scheme-handler\/audiomixer;$/m); assert.match(open, /^NoDisplay=true$/m);
  assert.strictEqual(u.profileSh('/opt/audio-mixer'), '# Audio Mixer: where it is installed (read by login shells)\n[ -d /opt/audio-mixer ] && export AUDIO_MIXER_DIR=/opt/audio-mixer\n');
  assert.strictEqual(u.environmentConf('/opt/audio-mixer'), 'AUDIO_MIXER_DIR=/opt/audio-mixer\n');
  // the .deb carries them in the system's places and refreshes the caches
  const out = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'dbx-')), d = u.buildDeb({ out });
  const lst = spawnSync('dpkg-deb', ['-c', d.deb], { encoding: 'utf8' });
  if (!lst.error) {
    for (const f of ['./etc/profile.d/audio-mixer.sh', './usr/lib/environment.d/60-audio-mixer.conf', './usr/share/mime/packages/audio-mixer.xml', './usr/share/applications/audio-mixer-open.desktop']) assert.ok(lst.stdout.includes(f), f);
    const ctl = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'dbc-')); spawnSync('dpkg-deb', ['-e', d.deb, ctl]);
    assert.match(fsx.readFileSync(pathx.join(ctl, 'postinst'), 'utf8'), /update-mime-database \/usr\/share\/mime/); assert.match(fsx.readFileSync(pathx.join(ctl, 'postrm'), 'utf8'), /update-desktop-database/);
  }
  // tarball installer: user level (~/.config/environment.d, ~/.local/share/mime) or root level; uninstall removes both
  assert.match(u.LINUX_INSTALL, /\.config\/environment\.d/); assert.match(u.LINUX_INSTALL, /\/etc\/profile\.d\/audio-mixer\.sh/); assert.match(u.LINUX_INSTALL, /audio-mixer-open\.desktop/);
  assert.match(u.LINUX_UNINSTALL, /60-audio-mixer\.conf/); assert.match(u.LINUX_UNINSTALL, /audio-mixer\.xml/);
  for (const [n, t] of [['install', u.LINUX_INSTALL], ['uninstall', u.LINUX_UNINSTALL]]) { const f = pathx.join(out, n + '.sh'); fsx.writeFileSync(f, t); assert.strictEqual(spawnSync('sh', ['-n', f]).status, 0, n); }
  // macOS: launchd environment + /etc/paths.d (asked, needs a password); no document type is claimed because a shell-script app cannot receive Apple events
  assert.match(u.MAC_INSTALL, /com\.audiomixer\.env/); assert.match(u.MAC_INSTALL, /launchctl setenv AUDIO_MIXER_DIR/); assert.match(u.MAC_INSTALL, /\/etc\/paths\.d\/audio-mixer/);
  assert.match(u.MAC_UNINSTALL, /com\.audiomixer\.env/); assert.match(u.MAC_UNINSTALL, /\/etc\/paths\.d\/audio-mixer/);
  assert.ok(!/CFBundleDocumentTypes|CFBundleURLTypes/.test(u.infoPlist('1.0.0')));
  for (const [n, t] of [['mi', u.MAC_INSTALL], ['mu', u.MAC_UNINSTALL], ['mc', u.MAC_COMMAND]]) { const f = pathx.join(out, n + '.sh'); fsx.writeFileSync(f, t); assert.strictEqual(spawnSync('bash', ['-n', f]).status, 0, n); }
  // kernel module: compiled against kernel headers where they are installed (below), never loaded here; am-pump is plain C and is built here
  const km = pathx.join(root, 'platforms', 'linux', 'kernel-module'), ko = fsx.readFileSync(pathx.join(km, 'snd-audiomixer.c'), 'utf8');
  for (const t of ['snd_card_new', 'snd_pcm_new', 'snd_pcm_period_elapsed', 'misc_register', 'MODULE_LICENSE("GPL")', 'MISC_DYNAMIC_MINOR']) assert.ok(ko.includes(t), t);
  assert.match(ko, /NOT loaded or run/); assert.ok(!ko.includes('no_llseek'), 'no_llseek was removed from newer kernels: noop_llseek works on all');
  assert.match(fsx.readFileSync(pathx.join(km, 'dkms.conf'), 'utf8'), new RegExp('PACKAGE_VERSION="' + require('../package.json').version.replace(/\.0$/, '').replace(/\./g, '\\.') + '"'));
  const pump = pathx.join(out, 'am-pump'), cc = spawnSync('gcc', ['-std=gnu99', '-Wall', '-Wextra', '-Werror', '-O2', '-o', pump, pathx.join(km, 'am_pump.c'), pathx.join(root, 'drivers', 'common', 'am_link.c'), '-lpthread'], { encoding: 'utf8' });
  if (!cc.error) assert.strictEqual(cc.status, 0, cc.stderr);
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

test('exclusive access: ASIO is exclusive by nature, the hog flag is asked of Core Audio / OSS only, everything else says it stays shared; the page never falls back to Web Audio for an interface the server can open', () => {
  const a = require('./audify');
  const mod = { RtAudioStreamFlags: { RTAUDIO_MINIMIZE_LATENCY: 2, RTAUDIO_HOG_DEVICE: 4 } };
  assert.deepStrictEqual(a.streamFlags(mod, { api: 'WINDOWS_ASIO' }, false), { flags: 2, exclusive: true });
  assert.deepStrictEqual(a.streamFlags(mod, { api: 'MACOSX_CORE' }, true), { flags: 6, exclusive: true });
  assert.deepStrictEqual(a.streamFlags(mod, { api: 'LINUX_OSS' }, true), { flags: 6, exclusive: true });
  assert.deepStrictEqual(a.streamFlags(mod, { api: 'MACOSX_CORE' }, false), { flags: 2, exclusive: false });          // only when asked
  for (const api of ['WINDOWS_WASAPI', 'WINDOWS_DS', 'LINUX_ALSA', 'LINUX_PULSE', 'UNIX_JACK']) assert.deepStrictEqual(a.streamFlags(mod, { api }, true), { flags: 2, exclusive: false }, api + ' stays shared and says so');
  assert.deepStrictEqual(a.streamFlags({ RtAudioStreamFlags: { RTAUDIO_MINIMIZE_LATENCY: 2 } }, { api: 'MACOSX_CORE' }, true), { flags: 2, exclusive: false }, 'a build without the flag');
  // through a stream: the flags reach RtAudio, the stream reports what was applied
  const seen = [];
  const f = fakeAudify(); const Base = f.RtAudio;
  f.RtAudio = class extends Base { openStream(o, i, fm, r, fr, n, cb, fcb, flags) { seen.push(flags); return super.openStream(o, i, fm, r, fr, n, cb); } };
  f.RtAudioStreamFlags = { RTAUDIO_MINIMIZE_LATENCY: 2, RTAUDIO_HOG_DEVICE: 4 };
  const core = { id: 1000, rtId: 1, api: 'MACOSX_CORE', hostAPIName: 'Core Audio', name: 'USB Interface', maxOutputChannels: 2, maxInputChannels: 2, sampleRates: [44100, 48000] };
  assert.strictEqual(a.openStream({ mod: f, dev: core, direction: 'output', channels: 2, sampleRate: 48000, exclusive: true }).exclusive, true);
  assert.strictEqual(a.openStream({ mod: f, dev: core, direction: 'output', channels: 2, sampleRate: 48000 }).exclusive, false);
  assert.deepStrictEqual(seen, [6, 2]);
  const asio = { id: 1001, rtId: 0, api: 'WINDOWS_ASIO', hostAPIName: 'ASIO', name: 'Focusrite USB ASIO', maxOutputChannels: 20, maxInputChannels: 18, sampleRates: [44100, 48000, 96000] };
  assert.strictEqual(a.openStream({ mod: fakeAudify(), dev: asio, direction: 'output', channels: 2, sampleRate: 48000, frameSize: 128 }).exclusive, true);
  // the sessions ask for it and report it
  const root = pathx.join(__dirname, '..'), rd = f2 => fsx.readFileSync(pathx.join(root, f2), 'utf8');
  for (const f2 of ['bridge/output.js', 'bridge/input.js', 'bridge/duplex.js']) { const s = rd(f2); assert.match(s, /exclusive: opts\.exclusive === true/); assert.match(s, /exclusive: st\.exclusive/); }
  // the page: interface streams ask for exclusive access and show shared / exclusive; no silent browser fallback when the server has a native route; browser twins are replaced
  const html = rd('index.html');
  assert.strictEqual((html.match(/exclusive: self\.cfg\.exclusive !== false/g) || []).length, 3);
  assert.match(html, /exclusive: true, deviceId: devSel/); assert.match(html, /lockNote\(m\) \{ return \/asio\/i\.test\(m\.hostApi\)/);
  assert.match(html, /if \(!cap && i\.browser && i\.browser\.inId && !this\.nativeInput\(i\)\)/);
  assert.match(html, /nativeInput\(i\) \{ return !!\(this\.bridgeUrl && this\.portaudio && \(i\.apis \|\| \[\]\)\.some\(a => a\.inputs > 0\)\); \}/);
  assert.match(html, /this\.preferNative\(sel, ins\.concat\(au\), 'selectedInputDeviceId'\)/); assert.match(html, /this\.preferNative\(sel, outs\.concat\(au\), 'selectedOutputDeviceId'\)/);
});

test('music FFT spectrum: the RTA music trace and the PEQ overlay use a 4096-point analyser on the labelled frequency axis; MIC ONLY / MUSIC ONLY / DUAL work; the Live Sources music feeds the RTA', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  // a dedicated analyser: the 512-point program bus analyser stays for the scope
  assert.match(html, /const musicFftAnalyser = window\.state\.ctx\.createAnalyser\(\);\s*musicFftAnalyser\.fftSize = 4096;/);
  assert.match(html, /msOut\.connect\(musicFftAnalyser\);\s*window\.state\.nodes\.musicFftAnalyser = musicFftAnalyser;/);
  assert.match(html, /musicAnalyser\.fftSize = 512;/);
  // the RTA: one source (Live Sources music, else the program bus while powered on), the mode, the same band axis as the mic trace
  assert.match(html, /const rtaMode = \(window\.musicRtaEngine && window\.musicRtaEngine\.mode\) \|\| 'dual';/);
  assert.match(html, /const extMusic = window\.liveSources && window\.liveSources\.music && window\.liveSources\.music\.analyser;/);
  assert.match(html, /const musicAn = extMusic \|\| \(window\.state\.active \? window\.state\.nodes\.musicFftAnalyser : null\);/);
  assert.match(html, /if \(rtaMode !== 'mic' && musicAn\) \{/); assert.match(html, /if \(rtaMode !== 'music' && \(window\.state\.micActive \|\| window\.state\.micFeeds\)/);
  assert.ok(!/Math\.pow\(i \/ bars, 1\.7\)/.test(html) && !/Math\.pow\(i \/ bars, 1\.6\)/.test(html), 'the old power-law bin mapping (it did not match the labels) is gone');
  // the PEQ overlay follows the curve's own axis (band k at x = step * (k + 1))
  assert.match(html, /const hzAtX = x => \{ const t = x \/ eqStep - 1, k = Math\.max\(0, Math\.min\(EQ_BANDS\.length - 2, Math\.floor\(t\)\)\);/);
  // the same band axis, run for real: a tone must land on the bar of its label
  const AX = [20, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000, 20000], hzAt = x => { const t = Math.min(1, Math.max(0, x)) * (AX.length - 1), k = Math.min(AX.length - 2, Math.floor(t)); return AX[k] * Math.pow(AX[k + 1] / AX[k], t - k); };
  const barOf = hz => { for (let i = 0; i < 64; i++) if (hz >= hzAt(i / 64) && hz < hzAt((i + 1) / 64)) return i; return -1; };
  assert.deepStrictEqual([250, 1000, 4000, 8000].map(barOf), [19, 32, 44, 51]);
});

test('single-file server: the page, the command line and the server in one file (portable JavaScript bundle and the Node.js single-executable builds)', async () => {
  const cp = require('node:child_process'), root = pathx.join(__dirname, '..');
  const single = require('../scripts/build-single'), cache = pathx.join(root, 'dist', 'cache');
  const rd = f => fsx.readFileSync(pathx.join(root, f), 'utf8');
  // hooks in the normal code: the embedded page is served first; the command line and the autostart know they may be the executable itself
  assert.match(rd('bridge/server.js'), /global\.__AM_ASSETS && Object\.prototype\.hasOwnProperty\.call\(global\.__AM_ASSETS, rel\)/);
  assert.match(rd('client/cli.js'), /global\.__AM_SELF \? \(global\.__AM_SELF\.script \? \[global\.__AM_SELF\.script\] : \[\]\) : \[__filename\]/); assert.match(rd('client/cli.js'), /global\.__AM_CLI_HELP !== undefined/);
  assert.match(rd('scripts/build-installers.js'), /await addSingleFile\(staged/); assert.match(rd('scripts/build-installers.js'), /AudioMixer-single\.exe/);
  assert.deepStrictEqual(Object.keys(single.TARGETS), ['windows-x64', 'windows-x86', 'linux-x64', 'linux-arm64', 'macos-x64', 'macos-arm64']);
  // autostart of the single-file build: the executable (or the bundle run by node) with --no-open, no server.js
  const svc = require('../client/service'), calls = [];
  global.__AM_SELF = { sea: true, script: null };
  try {
    const home = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'sv-'));
    await svc.install({ platform: 'linux', env: {}, home, exec: (c, a, o, cb) => { calls.push(c + ' ' + a.join(' ')); cb(null); } });
    const unit = fsx.readFileSync(pathx.join(home, '.config', 'systemd', 'user', 'audio-mixer.service'), 'utf8');
    assert.ok(unit.includes(`ExecStart="${process.execPath}" "--no-open"`) && !unit.includes('server.js'), unit);
    global.__AM_SELF = { sea: false, script: '/opt/am/AudioMixerServer.js' };
    await svc.install({ platform: 'linux', env: {}, home, exec: (c, a, o, cb) => { calls.push(c); cb(null); } });
    assert.ok(fsx.readFileSync(pathx.join(home, '.config', 'systemd', 'user', 'audio-mixer.service'), 'utf8').includes(`ExecStart="${process.execPath}" "/opt/am/AudioMixerServer.js" "--no-open"`));
  } finally { delete global.__AM_SELF; }
  // the bundle itself, run for real in an empty folder (needs the build tools in dist/cache, installed by the first build)
  if (!fsx.existsSync(pathx.join(cache, 'single-tools', 'node_modules', '.complete'))) return;
  const out = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'single-')), version = require('../package.json').version;
  const [b] = await single.build({ targets: ['portable'], out, cache });
  assert.ok(fsx.statSync(b.file).size < 1.5 * 1024 * 1024, 'the whole product in about half a megabyte of JavaScript');
  const port = 21000 + Math.floor(Math.random() * 9000), env = { ...process.env, BRIDGE_UPDATE_URL: '' };
  assert.strictEqual(cp.spawnSync(process.execPath, [b.file, 'version'], { cwd: out, encoding: 'utf8', env }).stdout.trim(), version);
  assert.match(cp.spawnSync(process.execPath, [b.file, '--help'], { cwd: out, encoding: 'utf8', env }).stdout, /PC-mode client/);
  const srv = cp.spawn(process.execPath, [b.file, '--no-open', '--port', String(port)], { cwd: out, stdio: 'ignore', env });
  try {
    let st = null; for (let i = 0; i < 60 && !st; i++) { await new Promise(r => setTimeout(r, 150)); try { st = await (await fetch(`http://127.0.0.1:${port}/api/status`)).json(); } catch (_) { /* starting */ } }
    assert.ok(st && st.version === version, 'the bundle serves the API');
    const page = Buffer.from(await (await fetch(`http://127.0.0.1:${port}/`)).arrayBuffer());
    assert.ok(page.equals(fsx.readFileSync(pathx.join(root, 'index.html'))), 'the page comes out of the file, byte for byte');
    assert.strictEqual((await fetch(`http://127.0.0.1:${port}/boot.html`)).status, 200); assert.strictEqual((await fetch(`http://127.0.0.1:${port}/bridge/server.js`)).status, 404);
    // a scene file opened through the bundle: it starts nothing new (the server runs), hands the file over and the page takes it once
    fsx.writeFileSync(pathx.join(out, 's.amix'), '[null,{"name":"x"}]');
    assert.strictEqual(cp.spawnSync(process.execPath, [b.file, 'open', pathx.join(out, 's.amix'), '--port', String(port)], { cwd: out, env }).status, 0);
    assert.deepStrictEqual((await (await fetch(`http://127.0.0.1:${port}/api/handoff`)).json()).scenes, [null, { name: 'x' }]);
  } finally { srv.kill(); }
});

test('single-file Windows build: npm is staged next to the .exe (node_modules/npm, the same spot client/cli.js\'s npm command already looks for a bundled npm)', async () => {
  const single = require('../scripts/build-single');
  for (const arch of ['x64', 'x86']) {
    const zip = makeZip([[`node-v98.1.2-win-${arch}/node_modules/npm/bin/npm-cli.js`, Buffer.from('// npm ' + arch)], [`node-v98.1.2-win-${arch}/node_modules/npm/package.json`, Buffer.from('{}')]]);
    const out = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'single-npm-')), cache = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'single-npm-cache-'));
    const dest = await single.stageNpm({ out, cache, arch, version: 'v98.1.2', fetchImpl: fakeNodeFetch(zip, { arch }) });
    assert.strictEqual(dest, pathx.join(out, 'node_modules', 'npm'));
    assert.strictEqual(fsx.readFileSync(pathx.join(dest, 'bin', 'npm-cli.js'), 'utf8'), '// npm ' + arch);
    assert.ok(!fsx.existsSync(pathx.join(dest, '.complete')), 'the cache marker file is not copied into the staged npm');
    fsx.rmSync(out, { recursive: true, force: true }); fsx.rmSync(cache, { recursive: true, force: true });
  }
  // build() stages npm automatically once a Windows target was built in this call (it does not for a linux/macos/portable-only run)
  assert.match(fsx.readFileSync(pathx.join(__dirname, '..', 'scripts', 'build-single.js'), 'utf8'), /if \(windowsArch\) \{ const npmDir = await stageNpm\(\{ out, cache, arch: windowsArch, fetchImpl \}\);/);
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

test('page: REBOOT chooser, real factory reset, CALIBRATE page (measurements, loudness log, session snapshots) are wired', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  const sc = html.slice(html.indexOf('<script id="system-control">'), html.indexOf('<script id="system-calibration">')), cal = html.slice(html.indexOf('<script id="system-calibration">'));
  assert.ok(sc.length > 1000 && cal.length > 1000);
  for (const s of ["'/api/restart'", "'/api/reboot'", "confirm: 'REBOOT'", "'/api/inserts'", 'window.factoryResetConsole = factory', 'window.rebootSystem = function (mode)', 'systemControl = { run, engine, server, page, computer', 'location.reload()']) assert.ok(sc.includes(s), s);
  for (const s of ["'/ws/duplex'", 'function correlate', 'function chirp', 'measureClock', 'measureNoise', 'logCsv', 'restoreSnapshot', 'halx_snapshots', 'halx_session_open', 'testHardwareLoopback', 'calibrateDuganAmbient', "window.sessionTools"]) assert.ok(cal.includes(s), s);
  assert.ok(html.includes("id: 'calib'") && html.includes('id="tab-calib"') && html.includes('id="calib-root"'));
  assert.ok(!/LOOPBACK PASS/.test(cal));                                      // no pass is printed without a measurement
  assert.ok(html.includes('window.insertChain = chain'));
  // the inserts of the server are cleared by a factory reset, on any plan
  const srv = fsx.readFileSync(pathx.join(__dirname, 'server.js'), 'utf8'); assert.ok(srv.includes('body.clear === true') && srv.indexOf('body.clear === true') < srv.indexOf("hasFeature(license.status(), 'plugins')"));
});

test('KNOX PIN lock: default code 1XF300#, Enter / Escape, the lock survives a reload, wrong tries are remembered, available on the phone layout', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  assert.ok(html.includes('const DEFAULT_ADMIN_PIN = "1XF300#";') && html.includes('Default PIN: 1XF300#') && !html.includes('1XF2OO'));
  for (const s of ["e.key === 'Enter'", "e.key === 'Escape'", "'halx_knox'", "'halx_pin_fail'", 'Number(f.until)', 'autocapitalize="off"', 'enter the PIN first']) assert.ok(html.includes(s), s);
  assert.ok(!/html\.phone-ui #btn-knox-toggle/.test(html));                                    // the lock button is shown on phones and tablets too
});

test('license sandbox: request file, sandbox issuer with its own key, accepted only in sandbox mode, never packaged, vendor key untouched', async () => {
  const lic = require('./license'), sb = require('../scripts/license-sandbox'), crypto2 = require('node:crypto');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'sbx-')), pubFile = pathx.join(dir, 'pub.json');
  sb.init(dir, pubFile);
  const jwk = JSON.parse(fsx.readFileSync(pubFile, 'utf8'));
  assert.notDeepStrictEqual(jwk, lic.PUBLIC_JWK);                                                                   // a key pair of its own
  assert.strictEqual(lic.sandboxJwk({ env: {}, file: pubFile }), null);                                             // off without AUDIO_MIXER_SANDBOX=1
  assert.strictEqual(lic.sandboxJwk({ env: { AUDIO_MIXER_SANDBOX: '1' }, file: pathx.join(dir, 'none.json') }), null);   // off without the file
  assert.deepStrictEqual(lic.sandboxJwk({ env: { AUDIO_MIXER_SANDBOX: '1' }, file: pubFile }), jwk);
  // the request file the page and the server make
  const req = lic.request({ plan: 'studio', name: 'Jane <b>Doe</b>', email: 'j@example.com', machine: 'AAAA-BBBB-CCCC-DDDD-EEEE', now: 1 });
  assert.deepStrictEqual([req.app, req.kind, req.plan, req.machineId, req.name], ['audio-mixer', 'license-request', 'studio', 'AAAA-BBBB-CCCC-DDDD-EEEE', 'Jane bDoe/b']);
  assert.throws(() => lic.request({ plan: 'gold' }), /plan must be/);
  // issue for the request: bound to its machine, sandbox mark, the sandbox key verifies it, the vendor key does not
  const r = sb.issue({ dir, request: req, days: 5, now: Date.now() });
  assert.ok(r.payload.sandbox && r.payload.mid === 'AAAA-BBBB-CCCC-DDDD-EEEE' && r.payload.plan === 'studio');
  assert.ok(lic.verifyKey(r.key, { jwk, machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' }).ok);
  assert.strictEqual(lic.verifyKey(r.key, { machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' }).reason, 'bad-signature');            // a normal build refuses it
  assert.strictEqual(lic.verifyKey(r.key, { jwk, machine: 'ZZZZ-BBBB-CCCC-DDDD-EEEE' }).reason, 'wrong-machine');
  assert.throws(() => sb.issue({ dir, request: { kind: 'other' } }), /not an Audio Mixer license request/);
  // the real server: the request route, no sandbox without the environment variable
  const started = !server.listening, port = server.listening ? server.address().port : await require('./server').start(0), base = `http://127.0.0.1:${port}`;
  const j = await (await fetch(base + '/api/license/request?plan=pro&name=Test')).json(); assert.ok(j.ok && j.request.plan === 'pro' && j.request.machineId === lic.machineId());
  assert.strictEqual((await fetch(base + '/api/license/request?plan=gold')).status, 400);
  const st = await (await fetch(base + '/api/license')).json(); assert.strictEqual(st.sandbox, false);
  if (started) await new Promise(r => server.close(r));
  // not in the packages, not in the repository
  const build = fsx.readFileSync(pathx.join(__dirname, '..', 'scripts', 'build.js'), 'utf8'); assert.ok(!/license-sandbox/.test(build));
  assert.ok(/license-sandbox-public\.json/.test(fsx.readFileSync(pathx.join(__dirname, '..', '.gitignore'), 'utf8')));
  const page = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8'); for (const s of ['lic-req-file', 'lic-req-copy', 'LICENSE_SANDBOX_JWK', "'/api/license/request?plan='"]) assert.ok(page.includes(s), s);
  fsx.rmSync(dir, { recursive: true, force: true });
});

test('admin license key: a second signing authority for real PRO / STUDIO upgrades, held only by whoever runs it, trusted in production (unlike sandbox)', () => {
  const lic = require('./license'), adm = require('../scripts/license-admin');
  // no admin key configured: adminJwk() is null, a normal build verifies only against the vendor (and, in sandbox, the sandbox) key
  assert.strictEqual(lic.adminJwk({ file: pathx.join(osx.tmpdir(), 'no-such-admin-key.json') }), null);
  assert.strictEqual(lic.status({ key: null }).admin, false);
  // key pair + issue, exactly like the vendor tool but its own identity
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'adm-'));
  const r = adm.initKeys(dir);
  assert.ok(fsx.existsSync(r.priv) && fsx.existsSync(r.pub));
  assert.throws(() => adm.initKeys(dir), /already exists/);                                              // never overwrites a key
  const jwk = adm.loadPublic(dir);
  assert.notDeepStrictEqual(jwk, lic.PUBLIC_JWK);                                                         // not the vendor key
  const { key, payload } = adm.issue({ dir, plan: 'studio', name: 'Support Unlock', days: 30, machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' });
  assert.ok(payload.admin === true && payload.id.startsWith('ADM-') && payload.expires > Date.now() && payload.expires < Date.now() + 31 * 86400000);
  assert.throws(() => adm.issue({ dir, plan: 'gold', name: 'x' }), /plan must be/);
  assert.throws(() => adm.issue({ dir, plan: 'pro' }), /--name/);
  // verifies against the admin key explicitly, exactly like a vendor key against the vendor key (same signKey / verifyKey code path)
  assert.strictEqual(lic.verifyKey(key, { jwk, machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' }).plan.id, 'studio');
  assert.deepStrictEqual(lic.adminJwk({ file: pathx.join(dir, 'admin-public.jwk.json') }), jwk);
  // this key pair is this test's own, not the one the real repository's adminJwk() default path points at, so the default trust list still refuses it
  assert.strictEqual(lic.verifyKey(key, { machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' }).reason, 'bad-signature');
  assert.ok(lic.hasFeature({ plan: lic.PLANS.studio }, 'studio'));
  // embed into a copy of the project (never the real repository: the private key stays with whoever ran init, exactly like the vendor tool)
  const copy = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'proj-admin-')); fsx.mkdirSync(pathx.join(copy, 'bridge'));
  fsx.writeFileSync(pathx.join(copy, 'index.html'), `x ${adm.MARK_A}null${adm.MARK_B} y`);
  adm.embedPublic(jwk, copy);
  assert.deepStrictEqual(JSON.parse(fsx.readFileSync(pathx.join(copy, 'bridge', 'license-admin-public.json'), 'utf8')), jwk);
  assert.ok(fsx.readFileSync(pathx.join(copy, 'index.html'), 'utf8').includes(JSON.stringify(jwk)));
  // once embedded this way, adminJwk() pointed at that copy's file trusts it, and status() reports admin: true
  assert.deepStrictEqual(lic.adminJwk({ file: pathx.join(copy, 'bridge', 'license-admin-public.json') }), jwk);
  // the real repository: no admin key configured yet (nothing was generated for the user), but the marker and the wiring are in place
  const page = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  assert.ok(page.includes('window.LICENSE_ADMIN_JWK = /*LICENSE_ADMIN_JWK*/null/*END_LICENSE_ADMIN_JWK*/'));
  assert.ok(page.includes('window.LICENSE_ADMIN_JWK'));
  const m = new RegExp(adm.MARK_A.replace(/\*/g, '\\*') + '(.*?)' + adm.MARK_B.replace(/\*/g, '\\*')).exec(page);
  assert.strictEqual(m[1], 'null');
  assert.strictEqual(fsx.existsSync(pathx.join(__dirname, 'license-admin-public.json')), false);           // nothing was generated on this machine either
  // the admin tool and the public key slot are real build inputs once a key exists, but never required for the build to succeed without one
  const bs = fsx.readFileSync(pathx.join(__dirname, '..', 'scripts', 'build.js'), 'utf8');
  assert.ok(bs.includes("'scripts/license-admin.js'") && bs.includes('license-admin-public.json') && bs.includes('fs.existsSync'));
  assert.ok(fsx.existsSync(pathx.join(__dirname, '..', 'ADMIN-LICENSE.md')));
  fsx.rmSync(dir, { recursive: true, force: true }); fsx.rmSync(copy, { recursive: true, force: true });
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
  let r = await fetch(base + '/issue', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ plan: 'pro', name: 'Jane Doe', email: 'j@example.com', machine: 'AAAA-BBBB-CCCC-DDDD-EEEE', days: 30, seats: 2 }) });
  let j = await r.json(); assert.strictEqual(r.status, 200); assert.ok(j.ok && j.key.startsWith('AMIX1.') && /VENDOR PRO for Jane Doe/.test(j.summary));
  const vpub = require('../scripts/license.js').loadPublic(vdir);
  let v = lic.verifyKey(j.key, { jwk: vpub, machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' }); assert.ok(v.ok && v.payload.seats === 2 && v.plan.id === 'pro');
  assert.strictEqual(lic.verifyKey(j.key, { machine: 'AAAA-BBBB-CCCC-DDDD-EEEE' }).reason, 'bad-signature');            // not the real vendor key: a normal build refuses it
  // validation: bad plan, missing name (vendor requires one), oversized / non-JSON body, wrong method
  assert.strictEqual((await fetch(base + '/issue', { method: 'POST', body: JSON.stringify({ plan: 'gold' }) })).status, 400);
  assert.strictEqual((await fetch(base + '/issue', { method: 'POST', body: JSON.stringify({ plan: 'pro' }) })).status, 400);
  assert.strictEqual((await fetch(base + '/issue', { method: 'POST', body: 'not json' })).status, 400);
  assert.strictEqual((await fetch(base + '/issue', { method: 'GET' })).status, 404);
  // CSRF / DNS-rebinding guards: a foreign Origin or Host is refused, the same functions bridge/server.js itself uses
  assert.strictEqual((await fetch(base + '/issue', { method: 'POST', headers: { Origin: 'https://evil.example' }, body: JSON.stringify({ plan: 'pro', name: 'x' }) })).status, 403);
  const rawHost = await new Promise(res => { const req = http.request({ host: '127.0.0.1', port: server.address().port, path: '/issue', method: 'POST', headers: { Host: 'evil.example', 'Content-Type': 'application/json' } }, out => { out.resume(); out.on('end', () => res(out.statusCode)); }); req.end(JSON.stringify({ plan: 'pro', name: 'x' })); });
  assert.strictEqual(rawHost, 403);
  await new Promise(r => server.close(r));
  // sandbox: no name required, its own key pair, the vendor key does not verify it
  const sdir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'gen-sandbox-')), spub = pathx.join(sdir, 'pub.json');
  require('../scripts/license-sandbox.js').init(sdir, spub);
  server = gen.createServer({ kind: 'sandbox', dirArgv: ['--dir', sdir] }); await new Promise(r => server.listen(0, '127.0.0.1', r)); base = `http://127.0.0.1:${server.address().port}`;
  r = await fetch(base + '/issue', { method: 'POST', body: JSON.stringify({ plan: 'studio', days: 5 }) }); j = await r.json();
  assert.strictEqual(r.status, 200); assert.ok(j.ok && /SANDBOX STUDIO/.test(j.summary));
  v = lic.verifyKey(j.key, { jwk: JSON.parse(fsx.readFileSync(spub, 'utf8')) }); assert.ok(v.ok && v.payload.sandbox === true);
  assert.strictEqual(lic.verifyKey(j.key, { jwk: vpub }).ok, false);
  await new Promise(r => server.close(r)); fsx.rmSync(sdir, { recursive: true, force: true });
  // admin: mirrors vendor (name required, admin: true on the payload), its own key pair
  const adir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'gen-admin-'));
  require('../scripts/license-admin.js').initKeys(adir);
  server = gen.createServer({ kind: 'admin', dirArgv: ['--dir', adir] }); await new Promise(r => server.listen(0, '127.0.0.1', r)); base = `http://127.0.0.1:${server.address().port}`;
  assert.strictEqual((await fetch(base + '/issue', { method: 'POST', body: JSON.stringify({ plan: 'studio' }) })).status, 400);   // name required
  r = await fetch(base + '/issue', { method: 'POST', body: JSON.stringify({ plan: 'studio', name: 'Reseller Client', days: 15 }) }); j = await r.json();
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

test('bus & matrix outputs: out sends to a real interface, speaker processor, amp calculator, mic protector, aux / FX returns, send regulator are wired', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  const sc = html.slice(html.indexOf('<script id="bus-outs">'), html.indexOf('<script id="rt-engine">'));
  assert.ok(sc.length > 5000);
  for (const s of ["'/ws/output'", "registerProcessor('mx-out'", 'createChannelMerger', 'ampCalc', 'scanFeedback', 'applyAdvice', 'regulate', 'applyFx', 'createConvolver', 'createDynamicsCompressor', "'halx_mix_outs'", 'M.render = function', 'M.ensureGraph = function']) assert.ok(sc.includes(s), s);
  for (const id of ['data-page="aux"', 'data-page="outs"', 'data-page="reg"', 'id="mx-page-aux"', 'id="mx-page-outs"', 'id="mx-page-reg"']) assert.ok(html.includes(id), id);
  // the calculator, run on its own
  const src = sc.slice(sc.indexOf('function ampCalc(o) {'), sc.indexOf('// ── graph ──')), ampCalc = new Function(src + '\nreturn ampCalc;')();
  const r = ampCalc({ ampW: 1000, ampOhm: 8, spkW: 300, spkOhm: 8, count: 1, sens: 100, dist: 10 });
  assert.ok(Math.abs(r.vmax - 89.44) < 0.01 && Math.abs(r.limDb + 5.23) < 0.01 && Math.abs(r.spl1m - 124.77) < 0.01 && Math.abs(r.splAt - 104.77) < 0.01, JSON.stringify(r));
  const two = ampCalc({ ampW: 500, ampOhm: 8, spkW: 300, spkOhm: 8, count: 2, sens: 98, dist: 1 });                     // two speakers in parallel: 4 ohm, the amp's voltage gives 1000 W, 500 W each
  assert.ok(Math.abs(two.load - 4) < 1e-9 && Math.abs(two.pLoad - 1000) < 1e-6 && Math.abs(two.pEach - 500) < 1e-6 && two.limDb < 0 && two.warn === 'The amp can give each speaker 500 W: use the protection limiter.', JSON.stringify(two));
  const safe = ampCalc({ ampW: 100, ampOhm: 8, spkW: 300, spkOhm: 8, count: 1, sens: 96, dist: 1 });
  assert.strictEqual(safe.limDb, 0); assert.ok(safe.headroom > 4.7 && safe.warn === '');                                  // a small amp cannot hurt the speaker: no limiter needed
});

test('low power mode: throttles the always-running meter loop to 1 frame in 4 (the real skip line, run for real), persists across reloads, never touches mixing/EQ/FX', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  for (const s of ["lowPowerMode: false", "window.setLowPowerMode = function(on, silent)", "localStorage.setItem('halx_low_power', on ? '1' : '0')",
    'id="low-power-toggle"', 'onclick="window.setLowPowerMode(!window.state.lowPowerMode)"', "localStorage.getItem('halx_low_power') === '1'"]) assert.ok(html.includes(s), s);
  // the exact skip line that ships, run for real (not a reimplementation): 1000 frames with it OFF must all run the per-channel body, and with it
  // ON only every 4th must (the other 3 of 4 skip straight to the next requestAnimationFrame, which is where the CPU/battery saving comes from -
  // mixing, EQ, FX and the audio graph itself are untouched by this: only this loop's own DOM writes are throttled)
  const m = /if \((window\.state\.lowPowerMode && \(\+\+lowPowerFrame % 4 !== 0\))\) \{ requestAnimationFrame\(updateMeters\); return; \}/.exec(html);
  assert.ok(m, 'the skip line was not found as expected');
  let lowPowerFrame = 0, bodyRuns = 0;
  const window = { state: { lowPowerMode: false } };
  const condition = m[1];
  const runFrame = () => { if (eval(condition)) return; bodyRuns++; };
  for (let i = 0; i < 1000; i++) runFrame();
  assert.strictEqual(bodyRuns, 1000, 'every frame must update the meters when low power mode is off');
  bodyRuns = 0; lowPowerFrame = 0; window.state.lowPowerMode = true;
  for (let i = 0; i < 1000; i++) runFrame();
  assert.strictEqual(bodyRuns, 250, 'exactly 1 frame in 4 must update the meters when low power mode is on');
});

test('auto fader: rides a channel toward -18 dBFS from its real pre-fader input level, holds its own state outside the step-quantized fader element, hands control back on a manual drag; every meter now agrees on one -90 dBFS silence floor', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  for (const s of ['channelAutoFader: Array.from({length: 32}, () => false)', 'id="btn-autofader-${i}"', 'onclick="window.toggleChannelAuto(${i})"', 'window.toggleChannelAuto = function', 'window.rideAutoFader = function', 'if (window.state.channelAutoFader[channelIndex]) window.toggleChannelAuto(channelIndex);'])
    assert.ok(html.includes(s), s);

  // the glide itself, run for real (the same technique the bus & matrix outputs test above uses for ampCalc): extract its source, mock just
  // enough of document/window to drive it, and prove it actually converges. This is the exact bug that shipped once already: the glide read
  // its own running value back from the fader element between frames, but the element has step="0.5" and the DOM rounds every .value
  // assignment to the nearest step - a small per-frame nudge got rounded straight back to where it started, every frame, forever.
  const start = html.indexOf('const AUTO_TARGET_DB'), end = html.indexOf('window.stripFaderNode = function');
  assert.ok(start > 0 && end > start);
  const src = html.slice(start, end);
  assert.ok(src.includes('autoFaderDb'), 'the glide must keep its own unrounded running value, not only the step-quantized DOM element\'s');
  // a fader-shaped mock: min="-60" step="0.5", with the browser's own value-sanitization (every .value assignment rounds to the nearest step) -
  // without this rounding the mock cannot reproduce the bug at all (a plain {value} object never clips a tiny per-frame nudge back to zero)
  const mkFaderEl = seed => { let v = seed; const el = { innerText: '' }; Object.defineProperty(el, 'value', { get: () => String(v), set: x => { v = Math.round((Number(x) + 60) / 0.5) * 0.5 - 60; } }); return el; };
  const mkDoc = seed => { const els = {}; return { getElementById: id => els[id] || (els[id] = mkFaderEl(seed)) }; };
  const run = (pk, frames) => {
    const applyCalls = [];
    const window_ = { rtEngine: { ch: [{ pk }] }, applyChannelGain: (i, db) => applyCalls.push(db) };
    const document_ = mkDoc(0);
    const rideAutoFader = new Function('window', 'document', src + '\nreturn window.rideAutoFader;')(window_, document_);
    for (let i = 0; i < frames; i++) rideAutoFader(0);
    return { fader: parseFloat(document_.getElementById('ch-fader-0').value), applyCalls };
  };
  const r = run(0.2, 400);                                             // -14 dBFS in, target -18 dBFS -> settles at -4 dB (-18 - (-14) = -4)
  assert.ok(Math.abs(r.fader - -4) < 0.05, 'expected the fader to settle near -4 dB, got ' + r.fader);
  assert.ok(Math.abs(r.applyCalls[r.applyCalls.length - 1] - -4) < 0.05);
  assert.ok(r.applyCalls.length === 400, 'a live signal must drive the gain every frame');
  // a silent channel (no signal yet) is left exactly where it is: no gain change at all
  const window2 = { rtEngine: { ch: [{ pk: 0 }] }, applyChannelGain: () => assert.fail('must not touch the gain of a silent channel') };
  const document2 = mkDoc(3);
  const rideAutoFader2 = new Function('window', 'document', src + '\nreturn window.rideAutoFader;')(window2, document2);
  rideAutoFader2(0);
  assert.strictEqual(document2.getElementById('ch-fader-0').value, '3');

  // every meter's own silence-floor threshold now agrees (the fix: 4 independent copies of dbOf() used to disagree - one at ~-180 dBFS,
  // one at ~-100 dBFS, two already at -90 dBFS - so the same near-silent input could read "-INF" on one page and a real number on another)
  const floors = [...html.matchAll(/const dbOf = v => \(v < ([0-9.e-]+) \?/g)].map(m => Number(m[1]));
  assert.strictEqual(floors.length, 4, 'expected 4 independent dbOf() copies (speaker processor, rt-engine, dynamics/gate, BT input)');
  assert.ok(floors.every(f => Math.abs(f - 0.00003) < 1e-12), 'every dbOf() must share the same -90 dBFS floor as bridge/levels.js: ' + JSON.stringify(floors));
});

test('auto volume control (dual mode) + dynamic EQ: master-bus gain riders wired as their own nodes, never fighting the manual master knob or the static EQ bands; subwoofer outputs get a real-time protection credit from ampCalc', () => {
  const html = fsx.readFileSync(pathx.join(__dirname, '..', 'index.html'), 'utf8');
  for (const s of [
    "autoVolume: { enabled: false, mode: 'peak', ceilingDb: -1, gainDb: 0 }",
    "dynamicEq: { enabled: false, freq: 3000, q: 1.4, threshold: -12, ratio: 2, gainDb: 0 }",
    'window.rideAutoVolume = function', 'window.rideDynamicEq = function',
    'window.setAutoVolume = function', 'window.setAutoVolumeMode = function', 'window.setDynamicEq = function',
    'const autoVolumeGain = window.state.ctx.createGain()', 'masterGain.connect(autoVolumeGain)', 'autoVolumeGain.connect(masterCompressor)',
    'const dynamicEqFilter = window.state.ctx.createBiquadFilter()', 'window.state.nodes.dynamicEqFilter = dynamicEqFilter',
    'window.state.nodes.autoVolumeGain = autoVolumeGain',
    'id="auto-volume-toggle"', 'id="auto-volume-mode"', 'id="dynamic-eq-toggle"', 'id="auto-volume-gr"', 'id="dynamic-eq-gr"',
    'id="mo-sub"', "chk('mo-sub', v => { this.setSpk(m, { sub: v });",
  ]) assert.ok(html.includes(s), s);

  // the EQ filters array built by EQ_BANDS.forEach must stay untouched (10 bands, nothing else pushed into it) - the dynamic EQ band is a
  // separate node, created and connected after that loop's own closing brace, never inside it
  const loopStart = html.indexOf('EQ_BANDS.forEach(freq => {'), loopEnd = html.indexOf('});', loopStart);
  const loopBody = html.slice(loopStart, loopEnd);
  assert.ok(loopBody.includes('window.state.nodes.eqFilters.push(bq)') && !loopBody.includes('dynamicEqFilter'), 'the static EQ loop body must not be touched by the new dynamic EQ node');
  assert.ok(html.indexOf('const dynamicEqFilter') > loopEnd, 'the dynamic EQ node must be created after the static EQ loop, not inside it');

  // rideAutoVolume / rideDynamicEq, run for real (same extraction technique as the auto-fader test above)
  const start = html.indexOf('const AUTO_TARGET_DB'), end = html.indexOf('window.stripFaderNode = function');
  assert.ok(start > 0 && end > start);
  const src = html.slice(start, end);
  const mkWindow = (autoVolume, dynamicEq) => ({ state: { autoVolume, dynamicEq }, localStorage: undefined, notify: () => {} });
  const mkLS = () => ({ store: {}, setItem(k, v) { this.store[k] = v; }, getItem(k) { return this.store[k]; } });
  const run = (window_, fn, args, frames) => {
    const document_ = { getElementById: () => null };
    const localStorage_ = mkLS();
    const f = new Function('window', 'document', 'localStorage', src + '\nreturn window.' + fn + ';')(window_, document_, localStorage_);
    let last; for (let i = 0; i < frames; i++) last = f(...args);
    return last;
  };

  // PEAK mode: ceiling -1 dBFS, fed a steady 0 dBFS true peak - gain reduction must converge to -1 dB (protects the ceiling), never overshoot
  const av1 = { enabled: true, mode: 'peak', ceilingDb: -1, gainDb: 0 };
  const peakResult = run(mkWindow(av1, null), 'rideAutoVolume', [0, null, null], 200);
  assert.ok(Math.abs(peakResult - -1) < 0.05, 'expected peak-mode gain reduction to converge to -1 dB, got ' + peakResult);
  assert.ok(peakResult <= 0.0001, 'auto volume must never add positive gain in peak mode');

  // LOUDNESS mode: a too-loud program (-8 LUFS short-term) against a -14 LUFS target must converge to -6 dB of reduction
  const av2 = { enabled: true, mode: 'loudness', ceilingDb: -1, gainDb: 0 };
  const loudResult = run(mkWindow(av2, null), 'rideAutoVolume', [null, -8, -14], 200);
  assert.ok(Math.abs(loudResult - -6) < 0.05, 'expected loudness-mode gain reduction to converge to -6 dB, got ' + loudResult);
  // a too-quiet program never gets boosted above 0 dB (clamped) - auto volume only ever protects, never adds gain
  const av3 = { enabled: true, mode: 'loudness', ceilingDb: -1, gainDb: 0 };
  const quietResult = run(mkWindow(av3, null), 'rideAutoVolume', [null, -20, -14], 50);
  assert.strictEqual(quietResult, 0);

  // disabled always resets to exactly 0, even with nonzero gainDb left over from before
  const av4 = { enabled: false, mode: 'peak', ceilingDb: -1, gainDb: -7 };
  assert.strictEqual(run(mkWindow(av4, null), 'rideAutoVolume', [0, null, null], 1), 0);
  assert.strictEqual(av4.gainDb, 0);

  // DYNAMIC EQ: the standard compressor formula, -(over) * (1 - 1/ratio); 6 dB over a -12 dB threshold at ratio 2 -> -3 dB reduction
  const deq1 = { enabled: true, freq: 3000, q: 1.4, threshold: -12, ratio: 2, gainDb: 0 };
  const deqResult = run(mkWindow(null, deq1), 'rideDynamicEq', [-6], 200);
  assert.ok(Math.abs(deqResult - -3) < 0.05, 'expected dynamic EQ reduction to converge to -3 dB, got ' + deqResult);
  // under threshold: no reduction at all
  const deq2 = { enabled: true, freq: 3000, q: 1.4, threshold: -12, ratio: 2, gainDb: 0 };
  assert.strictEqual(run(mkWindow(null, deq2), 'rideDynamicEq', [-20], 1), 0);
  // disabled always resets to exactly 0
  const deq3 = { enabled: false, freq: 3000, q: 1.4, threshold: -12, ratio: 2, gainDb: -4 };
  assert.strictEqual(run(mkWindow(null, deq3), 'rideDynamicEq', [-6], 1), 0);
  assert.strictEqual(deq3.gainDb, 0);

  // ampCalc's subwoofer protection credit (the same function the bus & matrix outputs test above exercises) -
  // scoped to the bus-outs script block, which has its own "// ── graph ──" marker elsewhere in the page too
  const busOutsSc = html.slice(html.indexOf('<script id="bus-outs">'), html.indexOf('<script id="rt-engine">'));
  const ampSrc = busOutsSc.slice(busOutsSc.indexOf('function ampCalc(o) {'), busOutsSc.indexOf('// ── graph ──'));
  const { ampCalc, ampCalcLive } = new Function('window', ampSrc + '\nreturn { ampCalc, ampCalcLive };')({ state: { autoVolume: { gainDb: -4 } } });
  const base = { ampW: 1000, ampOhm: 8, spkW: 300, spkOhm: 8, count: 1, sens: 100, dist: 10 };
  const notSub = ampCalc({ ...base, sub: false, avGainDb: -4 });
  assert.strictEqual(notSub.avReduction, 0, 'a non-subwoofer output must get no credit regardless of avGainDb');
  assert.strictEqual(notSub.effHeadroom, notSub.headroom);
  const sub = ampCalc({ ...base, sub: true, avGainDb: -4 });
  assert.ok(Math.abs(sub.avReduction - 4) < 1e-9, 'a subwoofer output must be credited the full 4 dB auto volume is already pulling down');
  assert.ok(Math.abs(sub.effHeadroom - (sub.headroom + 4)) < 1e-9);
  // ampCalcLive reads the live auto-volume state instead of taking avGainDb from the caller
  const live = ampCalcLive({ ...base, sub: true });
  assert.ok(Math.abs(live.avReduction - 4) < 1e-9, 'ampCalcLive must read window.state.autoVolume.gainDb on its own');
});

test('Android APK safety: permission allowlist, listener shipped off, audit tool catches additions, fingerprint published, APP SAFETY card wired', () => {
  const audit = require('../scripts/apk-audit'), root = pathx.join(__dirname, '..'), rd = f => fsx.readFileSync(pathx.join(root, f), 'utf8');
  const man = rd('platforms/android/AndroidManifest.xml'), ok = audit.auditManifest(man);
  assert.deepStrictEqual(ok.problems, []); assert.deepStrictEqual(ok.permissions.slice().sort(), audit.ALLOWED.slice().sort());
  assert.ok(/<service android:name="\.NowPlayingListener"[^>]*android:enabled="false"/.test(man));
  // each thing a scanner dislikes is caught
  const bad = s => audit.auditManifest(man.replace('<application', s + '<application')).problems.join(' | ');
  assert.match(bad('<uses-permission android:name="android.permission.READ_SMS"/>'), /suspicious permission: android.permission.READ_SMS/);
  assert.match(bad('<uses-permission android:name="android.permission.REQUEST_INSTALL_PACKAGES"/>'), /suspicious/);
  assert.match(bad('<uses-permission android:name="android.permission.VIBRATE"/>'), /not in the explained list/);
  assert.match(audit.auditManifest(man.replace('android:label="Audio Mixer" android:enabled="false"', 'android:label="Audio Mixer"')).problems.join(), /switched off/);
  assert.match(audit.auditManifest(man.replace('android:allowBackup="false"', 'android:allowBackup="true"')).problems.join(), /allowBackup/);
  assert.match(audit.auditManifest(man.replace('</application>', '<receiver android:name=".Boot"/></application>')).problems.join(), /broadcast receiver/);
  assert.match(audit.auditManifest(man.replace('android:exported="true" android:launchMode', 'android:launchMode')).problems.join(), /explicit android:exported/);
  // no blanket "cleartext to any host": a network security config restricts it to this device (127.0.0.1 / localhost / ::1) only
  assert.ok(!/android:usesCleartextTraffic/.test(man)); assert.match(man, /android:networkSecurityConfig="@xml\/network_security_config"/);
  assert.match(audit.auditManifest(man.replace('android:networkSecurityConfig="@xml/network_security_config"', 'android:usesCleartextTraffic="true"')).problems.join(), /usesCleartextTraffic="true" permits unencrypted traffic to any host/);
  assert.match(audit.auditManifest(man.replace('android:networkSecurityConfig="@xml/network_security_config" ', '')).problems.join(), /must set android:networkSecurityConfig/);
  const net = rd('platforms/android/res/xml/network_security_config.xml');
  assert.deepStrictEqual(audit.auditNetworkConfig(net), []);
  assert.deepStrictEqual([...audit.LOCAL_ONLY_HOSTS].sort(), ['127.0.0.1', '::1', 'localhost']);
  assert.match(audit.auditNetworkConfig(net.replace('cleartextTrafficPermitted="false"', 'cleartextTrafficPermitted="true"')).join(), /base-config must set cleartextTrafficPermitted="false"/);
  assert.match(audit.auditNetworkConfig(net.replace('<domain includeSubdomains="false">localhost</domain>', '<domain includeSubdomains="true">evil.example</domain>')).join(), /cleartext is permitted for "evil\.example", not just this device/);
  assert.match(audit.auditNetworkConfig(net.replace(/<domain[^>]*>[^<]*<\/domain>\s*/g, '')).join(), /lists no <domain>/);
  // the build packages the same file under res/xml/, and fails the build the same way the manifest audit does
  const buildSrc = rd('scripts/build-apk.js'); assert.ok(buildSrc.includes('network_security_config.xml') && buildSrc.includes('auditNetworkConfig'));
  assert.deepStrictEqual(audit.auditApk(['AndroidManifest.xml', 'classes.dex', 'resources.arsc', 'assets/www/index.html', 'res/mipmap-hdpi-v4/ic_launcher.png', 'res/xml/network_security_config.xml', 'META-INF/X.RSA']), []);
  assert.match(audit.auditApk(['classes.dex', 'lib/arm64-v8a/libx.so']).join(), /native library/);
  assert.match(audit.auditApk(['classes.dex', 'classes2.dex']).join(), /unexpected file/);
  // classes.dex must never reference a dynamic class loader (the one string that turns "WebView app" into "dropper" for a scanner)
  assert.deepStrictEqual(audit.auditDex(Buffer.from('nothing suspicious here', 'latin1')), []);
  assert.match(audit.auditDex(Buffer.from('...dalvik/system/DexClassLoader...', 'latin1')).join(), /dynamic class loader/);
  assert.match(audit.auditDex(Buffer.from('...dalvik/system/InMemoryDexClassLoader...', 'latin1')).join(), /dynamic class loader/);
  // the false-positive appeal report is generated from the real build, not hand-edited, and refuses an APK signed with the wrong key
  const appeal = require('../scripts/play-protect-appeal');
  assert.throws(() => appeal.build.call(null, '/nonexistent.apk'));
  // the published fingerprint matches the release the repository carries, and every explained permission has a reason in the app
  const fp = rd('platforms/android/signing-fingerprint.txt').trim(); assert.match(fp, /^([0-9A-F]{2}:){31}[0-9A-F]{2}$/);
  const infoJava = rd('platforms/android/src/com/audiomixer/app/AppInfo.java'); for (const p of audit.ALLOWED) assert.ok(infoJava.includes('"' + p + '"'), p);
  const bs = rd('scripts/build-apk.js'); assert.ok(bs.includes('apk-audit') && bs.includes('signing-fingerprint.txt') && bs.includes('certSha256'));
  assert.ok(fsx.existsSync(pathx.join(root, 'platforms', 'android', 'PLAY-PROTECT.md')));
  const page = rd('index.html'); for (const s of ['id="app-safety"', "'/api/app'", "'/api/app/nowplaying/'", 'APP SAFETY', 'Send app for scanning', 'app-safety-card']) assert.ok(page.includes(s), s);
  const mb = rd('platforms/android/src/com/audiomixer/app/MiniBridge.java'); assert.ok(mb.includes('"/api/app"') && mb.includes('x-mixer-action'));
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
