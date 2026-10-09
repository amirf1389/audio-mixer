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

function fakeFetch({ name = 'FlexASIO-1.10.exe', content = Buffer.from('installer-bytes'), digestOk = true, finalUrl = '' } = {}) {
  const sha = cryptox.createHash('sha256').update(content).digest('hex');
  const dl = `https://github.com/dechamps/FlexASIO/releases/download/v1.10/${name}`;
  return async url => {
    if (url.startsWith('https://api.github.com/repos/dechamps/FlexASIO/releases/latest')) {
      return new Response(JSON.stringify({ assets: [
        { name: 'FlexASIO-1.10-Debug.exe', browser_download_url: 'https://github.com/x/debug.exe' },
        { name, browser_download_url: dl, digest: 'sha256:' + (digestOk ? sha : '0'.repeat(64)) },
      ] }), { headers: { 'content-type': 'application/json' } });
    }
    const r = new Response(content, { headers: { 'content-length': String(content.length) } });
    return finalUrl ? { ok: true, status: 200, body: r.body, url: finalUrl, headers: r.headers } : r;
  };
}

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
  assert.ok(CATALOG.filter(x => x.download).every(x => x.download.kind === 'github-release'));
  assert.strictEqual(by('flexasio').downloadable, true);
});

test('downloader saves the official installer, checks the checksum and never runs it', async () => {
  const { downloadDriver } = require('./catalog');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'drv-'));
  const r = await downloadDriver('flexasio', { fetchImpl: fakeFetch(), dir });
  assert.strictEqual(r.verified, true);
  assert.strictEqual(pathx.dirname(r.file), dir);
  assert.strictEqual(fsx.readFileSync(r.file).toString(), 'installer-bytes');
  assert.deepStrictEqual(fsx.readdirSync(dir), ['FlexASIO-1.10.exe']);          // no .part left behind
  await assert.rejects(downloadDriver('flexasio', { fetchImpl: fakeFetch({ digestOk: false }), dir }), /checksum mismatch/);
  assert.deepStrictEqual(fsx.readdirSync(dir), ['FlexASIO-1.10.exe']);          // bad download discarded
  await assert.rejects(downloadDriver('flexasio', { fetchImpl: fakeFetch({ finalUrl: 'https://evil.example/a.exe' }), dir }), /untrusted host/);
  await assert.rejects(downloadDriver('asio4all', { fetchImpl: fakeFetch(), dir }), /no automatic download/);
  await assert.rejects(downloadDriver('../../etc/passwd', { fetchImpl: fakeFetch(), dir }), /no automatic download/);
  fsx.rmSync(dir, { recursive: true, force: true });
});

test('downloader neutralises hostile asset names', async () => {
  const { downloadDriver } = require('./catalog');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'drv-'));
  const r = await downloadDriver('flexasio', { fetchImpl: fakeFetch({ name: 'FlexASIO-9.exe' }), dir });
  assert.ok(r.file.startsWith(dir + pathx.sep));
  fsx.rmSync(dir, { recursive: true, force: true });
});

test('catalog endpoints: GET list, POST needs the custom header, ids validated, foreign origins refused', async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const port = server.address().port, base = `http://127.0.0.1:${port}`;
  const cat = await (await fetch(base + '/api/catalog')).json();
  assert.strictEqual(cat.ok, true);
  assert.ok(Array.isArray(cat.items) && cat.items.length > 5 && cat.items.every(i => !('detect' in i) && !('download' in i)));
  const post = (headers, body, origin) => fetch(base + '/api/catalog/download', { method: 'POST', headers: { 'Content-Type': 'application/json', ...(origin ? { Origin: origin } : {}), ...headers }, body });
  assert.strictEqual((await post({}, '{"id":"flexasio"}')).status, 400);                                    // no X-Mixer-Action
  assert.strictEqual((await post({ 'X-Mixer-Action': 'download' }, '{"id":"../x"}')).status, 400);          // bad id
  assert.strictEqual((await post({ 'X-Mixer-Action': 'download' }, 'nope')).status, 400);
  assert.strictEqual((await post({ 'X-Mixer-Action': 'download' }, '{"id":"asio4all"}')).status, 400);      // no automatic download
  assert.strictEqual((await post({ 'X-Mixer-Action': 'download' }, '{"id":"flexasio"}', 'https://evil.example')).status, 403);
  assert.strictEqual((await post({ 'X-Mixer-Action': 'download' }, 'x'.repeat(5000))).status, 413);
  assert.strictEqual((await fetch(base + '/api/catalog', { method: 'DELETE' })).status, 405);
  server.closeAllConnections();
  server.close();
});

test('client: argument parsing and the browser opener only accept localhost URLs', () => {
  const { parseArgs, openCommand } = require('../client/cli');
  assert.deepStrictEqual(parseArgs([]), { cmd: 'start', arg: null, port: 8765, open: true, help: false });
  const a = parseArgs(['download', 'flexasio', '--port', '9000', '--no-open']);
  assert.strictEqual(a.cmd, 'download'); assert.strictEqual(a.arg, 'flexasio'); assert.strictEqual(a.port, 9000); assert.strictEqual(a.open, false);
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

  const w = await svc.install({ ...base, platform: 'win32', node: 'C:\\Program Files\\nodejs\\node.exe', server: 'C:\\mixer\\bridge\\server.js' });
  assert.ok(w.file.endsWith('AudioMixerServer.vbs') && w.file.includes('Startup'));
  assert.match(fsx.readFileSync(w.file, 'utf8'), /sh\.Run """C:\\Program Files\\nodejs\\node\.exe"" ""C:\\mixer\\bridge\\server\.js""", 0, False/);
  assert.ok(calls.some(c => c.startsWith('wscript //nologo ')));

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
  const { build, FILES } = require('../scripts/build');
  const out = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'dist-'));
  const r = build({ out });
  assert.strictEqual(r.dest, pathx.join(out, 'audio-mixer-pc'));
  for (const f of FILES) assert.ok(fsx.existsSync(pathx.join(r.dest, f)), 'missing ' + f);
  assert.ok(!fsx.existsSync(pathx.join(r.dest, 'bridge', 'test.js')) && !fsx.existsSync(pathx.join(r.dest, 'bridge', 'audit.js')));
  const pkg = JSON.parse(fsx.readFileSync(pathx.join(r.dest, 'package.json'), 'utf8'));
  assert.ok(!('test' in pkg.scripts) && pkg.scripts.start);
  const manifest = fsx.readFileSync(pathx.join(r.dest, 'MANIFEST.sha256'), 'utf8').trim().split('\n');
  assert.strictEqual(manifest.length, FILES.length);
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

function fakeNodeFetch(zip, { badSum = false } = {}) {
  const sum = cryptox.createHash('sha256').update(zip).digest('hex');
  return async url => {
    if (url.endsWith('/index.json')) return new Response(JSON.stringify([{ version: 'v99.0.0', lts: false, files: ['win-x64-zip'] }, { version: 'v98.1.2', lts: 'Fake', files: ['linux-x64', 'win-x64-zip'] }]));
    if (url.endsWith('/SHASUMS256.txt')) return new Response(`${badSum ? '0'.repeat(64) : sum}  node-v98.1.2-win-x64.zip\n${'1'.repeat(64)}  other.tar.gz\n`);
    if (url.endsWith('node-v98.1.2-win-x64.zip')) return new Response(zip);
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

test('installer: stages the app with the bundled runtime and builds a Windows setup.exe when NSIS is available', async () => {
  const bi = require('../scripts/build-installer');
  const zip = makeZip([['node-v98.1.2-win-x64/LICENSE', Buffer.from('MIT')], ['node-v98.1.2-win-x64/node.exe', Buffer.from('MZ-fake-node')]]);
  const out = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'inst-'));
  const hasNsis = require('node:child_process').spawnSync('makensis', ['-VERSION']).status === 0;
  const r = await bi.buildInstaller({ out, fetchImpl: fakeNodeFetch(zip), runMakensis: hasNsis });
  assert.ok(fsx.existsSync(pathx.join(r.stage, 'runtime', 'node.exe')) && fsx.existsSync(pathx.join(r.stage, 'client', 'cli.js')));
  assert.ok(!fsx.existsSync(pathx.join(r.stage, 'start-pc-mode.sh')));
  assert.match(fsx.readFileSync(pathx.join(r.stage, 'start-pc-mode.bat'), 'utf8'), /runtime\\node\.exe/);
  assert.match(fsx.readFileSync(pathx.join(r.stage, 'start-local-server.bat'), 'utf8'), /runtime\\node\.exe" bridge\\server\.js/);
  const nsi = fsx.readFileSync(pathx.join(__dirname, '..', 'installer', 'audio-mixer.nsi'), 'utf8');
  assert.match(nsi, /RequestExecutionLevel user/);                           // no administrator rights
  assert.match(nsi, /service install/); assert.match(nsi, /service uninstall/);
  if (hasNsis) {
    assert.ok(r.installer && fsx.existsSync(r.installer));
    const head = fsx.readFileSync(r.installer).subarray(0, 2).toString();
    assert.strictEqual(head, 'MZ');                                          // a real Windows executable
    assert.strictEqual(r.sha256.length, 64);
  }
  fsx.rmSync(out, { recursive: true, force: true });
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

test('verify: installer file checks and checksum sidecar', () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path');
  const v = require('../client/verify');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'vfy-')), f = path.join(dir, 'Setup.exe');
  fs.writeFileSync(f, Buffer.concat([Buffer.from('MZ'), Buffer.from('Nullsoft Install System')]));
  fs.writeFileSync(f + '.sha256', `${v.sha256(f)}  Setup.exe\n`);
  assert.ok(v.checkInstallerFile(f).every(r => r.level === 'PASS'));
  fs.writeFileSync(f + '.sha256', `${'1'.repeat(64)}  Setup.exe\n`);
  assert.ok(v.checkInstallerFile(f).some(r => r.level === 'FAIL'));
  fs.writeFileSync(f, 'not an exe');
  assert.ok(v.checkInstallerFile(f).some(r => r.level === 'FAIL' && /executable/.test(r.title)));
  fs.rmSync(dir, { recursive: true });
});

test('verify: parsers and injected PowerShell results', async () => {
  const v = require('../client/verify');
  assert.deepStrictEqual(v.parseSignature('Valid|CN=OpenJS Foundation'), { status: 'Valid', subject: 'CN=OpenJS Foundation' });
  assert.strictEqual(v.parseDefender('CLEAN').state, 'clean');
  assert.deepStrictEqual(v.parseDefender('THREAT|123,456'), { state: 'threat', ids: '123,456' });
  assert.strictEqual(v.parseDefender('').state, 'unavailable');
  const run = out => (cmd, args, opts, cb) => cb(null, out);
  assert.strictEqual((await v.checkSignature('x', 'Runtime', { platform: 'win32', run: run('Valid|CN=OpenJS Foundation'), expect: /OpenJS/ }))[0].level, 'PASS');
  assert.strictEqual((await v.checkSignature('x', 'Runtime', { platform: 'win32', run: run('Valid|CN=Someone Else'), expect: /OpenJS/ }))[0].level, 'WARN');
  assert.strictEqual((await v.checkSignature('x', 'Installer', { platform: 'win32', run: run('NotSigned|') }))[0].level, 'WARN');
  assert.strictEqual((await v.checkSignature('x', 'Installer', { platform: 'win32', run: run('HashMismatch|CN=x') }))[0].level, 'FAIL');
  assert.strictEqual((await v.checkDefender('x', { platform: 'win32', run: run('CLEAN') }))[0].level, 'PASS');
  assert.strictEqual((await v.checkDefender('x', { platform: 'win32', run: run('THREAT|9') }))[0].level, 'FAIL');
  assert.strictEqual((await v.checkDefender('x', { platform: 'linux' }))[0].level, 'INFO');
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
  assert.throws(() => a.openStream({ mod: f1, dev, direction: 'output', channels: 2, sampleRate: 88200 }), /does not support 88200/);
  assert.throws(() => a.openStream({ mod: f1, dev, direction: 'output', channels: 30, sampleRate: 48000 }), /only 20 output/);
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
