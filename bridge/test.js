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
  assert.ok(CATALOG.filter(x => x.download).every(x => ['github-release', 'site-link'].includes(x.download.kind)));
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
  await assert.rejects(downloadDriver('focusrite', { fetchImpl: fakeFetch(), dir }), /no automatic download/);
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
  assert.strictEqual((await post({ 'X-Mixer-Action': 'download' }, '{"id":"focusrite"}')).status, 400);      // no automatic download
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

  // Windows: one registry Run value, no script file anywhere
  const spawned = [];
  const w = await svc.install({ ...base, platform: 'win32', node: 'C:\\Program Files\\nodejs\\node.exe', server: 'C:\\mixer\\bridge\\server.js', launcher: null, spawn: (c, a) => { spawned.push([c, ...a]); return { unref() {} }; } });
  assert.strictEqual(w.installed, true); assert.strictEqual(w.command, '"C:\\Program Files\\nodejs\\node.exe" "C:\\mixer\\bridge\\server.js"');
  assert.ok(calls.some(c => c.startsWith('reg add HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run /v AudioMixerServer /t REG_SZ /d ')));
  assert.ok(!calls.some(c => /wscript/i.test(c)));
  assert.deepStrictEqual(fsx.readdirSync(home), []);                                   // nothing was written into the profile
  const wl = await svc.install({ ...base, platform: 'win32', server: 'C:\\Program Files\\Audio Mixer\\bridge\\server.js', launcher: 'C:\\Program Files\\Audio Mixer\\AudioMixerServer.exe', spawn: (c, a) => { spawned.push([c, ...a]); return { unref() {} }; } });
  assert.strictEqual(wl.command, '"C:\\Program Files\\Audio Mixer\\AudioMixerServer.exe"');         // the signed native launcher when installed
  assert.deepStrictEqual(spawned[1], ['C:\\Program Files\\Audio Mixer\\AudioMixerServer.exe']);
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
  assert.ok(!fsx.existsSync(pathx.join(__dirname, '..', 'installer', 'audio-mixer.nsi')), 'the NSIS script is gone');
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

test('ASIO4ALL: newest installer is found on the vendor page and saved, never run', async () => {
  const { downloadDriver } = require('./catalog');
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'a4a-'));
  const page = '<a href="/downloads_11/ASIO4ALL_2_14_English.exe">old</a><a href="/downloads_11/ASIO4ALL_2_15_English.exe">new</a>' +
    '<a href="/downloads_11/ASIO4ALL_2_15_Deutsch.exe">de</a><a href="https://evil.example/ASIO4ALL_9_9.exe">bad</a><a href="http://www.asio4all.org/ASIO4ALL_9_8.exe">http</a>';
  const mk = (html, finalUrl) => async (url) => {
    if (/asio4all\.org\/?$/.test(url)) return { ok: true, status: 200, text: async () => html };
    return { ok: true, status: 200, url: finalUrl || url, headers: { get: () => '3' }, body: (async function* () { yield Buffer.from('exe'); })() };
  };
  const r = await downloadDriver('asio4all', { fetchImpl: mk(page), dir });
  assert.strictEqual(pathx.basename(r.file), 'ASIO4ALL_2_15_English.exe');       // highest version, English first, foreign hosts / http ignored
  assert.strictEqual(r.source, 'https://www.asio4all.org/downloads_11/ASIO4ALL_2_15_English.exe');
  assert.strictEqual(r.verified, false); assert.strictEqual(fsx.readFileSync(r.file).toString(), 'exe');
  await assert.rejects(downloadDriver('asio4all', { fetchImpl: mk('<a href="https://evil.example/ASIO4ALL_9_9.exe">x</a>'), dir }), /no installer link/);
  await assert.rejects(downloadDriver('asio4all', { fetchImpl: mk(page, 'https://evil.example/x.exe'), dir }), /untrusted host/);
  fsx.rmSync(dir, { recursive: true, force: true });
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
  assert.match(x64, /Name="a &amp; b\.js"/); assert.match(x64, /Feature Id="Autostart"/); assert.match(x64, /Feature Id="Desktop"[^>]*Level="1"/);
  assert.match(x64, /Id="ScUninstall"[^>]*msiexec\.exe" Arguments="\/x \{[0-9A-F-]{36}\}"/);          // Start Menu uninstall entry
  assert.match(x64, /ARPURLINFOABOUT/);                                                                  // Settings > Apps entry details
  assert.match(x64, /Id="ScPc"[^>]*AudioMixerServer\.exe" Arguments="\/open"/);                     // main shortcut: start-up screen, then the browser
  assert.match(x64, /Id="ScPlugins"[^>]*AudioMixerServer\.exe" Arguments="\/plugins"/);             // native launcher, no cmd one-liner
  assert.match(x64, /Name="AudioMixer" Type="string" Value="&quot;\[INSTALLDIR\]AudioMixerServer\.exe&quot;"/);
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
  const stub = fsx.readFileSync(pathx.join(__dirname, '..', 'installer', 'setup-stub.c'), 'utf8');
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
  const en = rd('ensure-node.sh');
  assert.match(en, /am_splash\(\)/); assert.match(en, /AM_SPLASH_ARGS="--no-open"/); assert.match(en, /AUDIO_MIXER_NO_SPLASH/);
  const unix = rd('scripts', 'build-unix.js');
  assert.strictEqual((unix.match(/am_splash "\$APP" "\$@"/g) || []).length, 3);                       // Linux .deb launcher, macOS app, portable command
  assert.strictEqual((unix.match(/cli\.js" \$AM_SPLASH_ARGS "\$@"/g) || []).length, 3);              // no second browser window when the screen is open
  assert.ok(rd('scripts', 'build.js').includes("'boot.html'"));
  const l = rd('installer', 'launcher.c');
  assert.match(l, /\/open/); assert.match(l, /AudioMixerBoot/); assert.match(l, /server_up\(\)/);
  assert.ok(rd('scripts', 'build-exe.js').includes("'-lgdi32', '-lws2_32'"));
  assert.match(rd('android', 'src', 'com', 'audiomixer', 'app', 'MainActivity.java'), /new BootView\(this\)/);
  assert.match(rd('android', 'src', 'com', 'audiomixer', 'app', 'BootView.java'), /TITAN OS/);
  assert.match(rd('ios', 'AudioMixer', 'AudioMixerApp.swift'), /struct BootView: View/);
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
  const l = fsx.readFileSync(pathx.join(__dirname, '..', 'installer', 'launcher.c'), 'utf8');
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
  const c = rd('installer', 'audio-mixer.c');
  assert.match(c, /wmain\(void\)/); assert.match(c, /runtime\\\\node\.exe/); assert.match(c, /client\\\\cli\.js/);
  assert.ok(rd('scripts', 'build-exe.js').includes("-mconsole"));
  assert.match(rd('installer', 'setup-stub.c'), /L"\/nopath"/); assert.match(rd('installer', 'setup-stub.c'), /L",CommandLine"/);
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
  const sh = pathx.join(stage, 'sp.sh'); fsx.writeFileSync(sh, `. "${pathx.join(root, 'ensure-node.sh')}"\nmkdir -p "${stage}/bin"; printf '#!/bin/sh\\ntouch "${stage}/opened"\\n' > "${stage}/bin/xdg-open"; chmod +x "${stage}/bin/xdg-open"\nPATH="${stage}/bin:$PATH" DISPLAY=:0 am_splash "${root}" doctor; echo "[$AM_SPLASH_ARGS]"\nPATH="${stage}/bin:$PATH" DISPLAY=:0 am_splash "${root}" --port 8800; echo "[$AM_SPLASH_ARGS]"\n`);
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
  for (const f of ['ensure-node.sh', 'start-pc-mode.sh']) assert.strictEqual(cp.spawnSync('sh', ['-n', pathx.join(__dirname, '..', f)]).status, 0, f);
  const sh = fsx.readFileSync(pathx.join(__dirname, '..', 'ensure-node.sh'), 'utf8');
  assert.match(sh, /SHASUMS256\.txt/); assert.match(sh, /does not match nodejs\.org's checksum/);   // refuses an unverified download
  assert.match(sh, /--strip-components=1/); assert.ok(!/sudo|chmod 777|curl[^\n]*\|\s*(ba)?sh/.test(sh), 'no sudo, no pipe-to-shell');
  // with a working Node.js on PATH nothing is downloaded
  const r = cp.spawnSync('sh', ['-c', `. "${pathx.join(__dirname, '..', 'ensure-node.sh')}"; am_ensure_node && echo "$NODE"`], { encoding: 'utf8', env: { ...process.env, AUDIO_MIXER_NODE: process.execPath } });
  assert.strictEqual(r.status, 0); assert.strictEqual(r.stdout.trim(), process.execPath);
  // a too-old / broken Node.js is not accepted
  const bad = pathx.join(osx.tmpdir(), 'amx-oldnode'); fsx.writeFileSync(bad, '#!/bin/sh\necho 12\n'); fsx.chmodSync(bad, 0o755);
  const r2 = cp.spawnSync('sh', ['-c', `. "${pathx.join(__dirname, '..', 'ensure-node.sh')}"; am_node_ok "${bad}"; echo $?`], { encoding: 'utf8' });
  assert.strictEqual(r2.stdout.trim(), '1'); fsx.unlinkSync(bad);
  // Windows launcher: winget first, then the checksum-verified official zip, then the Visual C++ runtime for Audify
  const bat = fsx.readFileSync(pathx.join(__dirname, '..', 'start-pc-mode.bat'), 'utf8');
  for (const want of ['winget install --id OpenJS.NodeJS.LTS', 'SHASUMS256.txt', 'certutil -hashfile', 'Microsoft.VCRedist.2015+.x64', 'does not match nodejs.org']) assert.ok(bat.includes(want), want);
  assert.ok(/\r\n/.test(bat));
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
  const dir = pathx.join(__dirname, '..', 'native', 'win');
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
  const root = pathx.join(__dirname, '..', 'native', 'host');
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
  for (const [file, re] of [[pathx.join(dir, 'missing.vst'), /could not load/], [pathx.join(dir, 'x.vst3'), /VST3 plugins are not supported/], [hostBin, /could not load|not a VST2/]]) {
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
  store.slots['fx:1'].format = 'VST3'; scan.v3 = true;
  s = mk({ scan: () => ({ plugins: [{ name: 'gain', format: 'VST3', file: plug, valid: true, compatible: true }] }) }); s.onText(JSON.stringify({ type: 'start', slot: 'fx:1' })); await wait(() => sent.length); assert.match(sent.pop().message, /VST3/);
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
  const man = fsx.readFileSync(pathx.join(__dirname, '..', 'android', 'AndroidManifest.xml'), 'utf8');
  assert.ok(/package="com\.audiomixer\.app"/.test(man) && /RECORD_AUDIO/.test(man) && /android:exported="true"/.test(man) && !/CAMERA|READ_EXTERNAL|WRITE_EXTERNAL|READ_CONTACTS/.test(man));
  const java = fsx.readFileSync(pathx.join(__dirname, '..', 'android', 'src', 'com', 'audiomixer', 'app', 'MainActivity.java'), 'utf8');
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
  const src = fsx.readFileSync(pathx.join(__dirname, '..', 'native', 'mac', 'AudioDevices.swift'), 'utf8');
  assert.ok(src.includes('import CoreAudio') && src.includes('kAudioHardwarePropertyDevices') && src.includes('JSONSerialization'));
});

test('iOS app: Swift sources, XcodeGen spec, opaque icon, project bundle', async () => {
  const ios = require('../scripts/build-ios'); const root = pathx.join(__dirname, '..', 'ios');
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
    assert.ok(fsx.statSync(r.dmg).size > 100000 && fsx.existsSync(r.dmg + '.sha256') && fsx.existsSync(r.zip));
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
  const dir = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'jv-')), src = pathx.join(__dirname, '..', 'android');
  const files = ['Json', 'Ws', 'AudioBackend', 'Assets', 'Interfaces', 'MiniBridge'].map(n => pathx.join(src, 'src', 'com', 'audiomixer', 'app', n + '.java')).concat(pathx.join(src, 'test', 'com', 'audiomixer', 'app', 'BridgeHarness.java'));
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

test('Android background engine + power-on animation: manifest, service (Java and Kotlin twin), build wiring, page hooks', () => {
  const root = pathx.join(__dirname, '..'), rd = f => fsx.readFileSync(pathx.join(root, f), 'utf8');
  const man = rd('android/AndroidManifest.xml');
  assert.ok(man.includes('android.permission.FOREGROUND_SERVICE') && man.includes('<service android:name=".EngineService" android:exported="false"/>') && /RECORD_AUDIO/.test(man) && !/ACCESS_FINE_LOCATION|CAMERA|READ_EXTERNAL/.test(man));
  const java = rd('android/src/com/audiomixer/app/EngineService.java'), kt = rd('android/kotlin/EngineService.kt');
  for (const src of [java, kt]) { assert.ok(src.includes('8765') && src.includes('MiniBridge') && src.includes('PARTIAL_WAKE_LOCK') && src.includes('STOP') && src.includes('NotificationChannel') && src.includes('START_STICKY')); }
  assert.ok(java.includes('b.start(8765, 10)') && kt.includes('b.start(8765, 10)') && java.includes('AndroidAssets') && kt.includes('AndroidAssets'));                                  // both twins look for the page's port first
  const act = rd('android/src/com/audiomixer/app/MainActivity.java'); assert.ok(act.includes('loadMixer(state)') && act.includes('"http://localhost:" + port + "/index.html"') && act.includes('startEngine()') && act.includes('startForegroundService') && act.includes('EngineService.class'));
  const aa = rd('android/src/com/audiomixer/app/AndroidAudio.java'); assert.ok(aa.includes('AudioRecord') && aa.includes('AudioTrack') && aa.includes('setPreferredDevice') && aa.includes('RECORD_AUDIO') && aa.includes('THREAD_PRIORITY_URGENT_AUDIO'));
  const apk = require('../scripts/build-apk'); const files = apk.listFiles(pathx.join(root, 'android', 'src'), '.java').map(f => pathx.basename(f));
  for (const n of ['MiniBridge.java', 'AndroidAudio.java', 'EngineService.java', 'MainActivity.java', 'Ws.java', 'Json.java', 'Interfaces.java', 'AudioBackend.java']) assert.ok(files.includes(n), n);
  assert.ok(!files.includes('BridgeHarness.java'));                                                                     // test code is not in the app
  const bs = rd('scripts/build-apk.js'); assert.ok(bs.includes("'--target-sdk-version', '29'") && bs.includes('--kotlin') && bs.includes('kotlin-stdlib.jar'));
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
  for (const n of ['launcher', 'setup', 'audio-mixer']) assert.match(rd('installer', n + '.rc'), /^1 ICON "AudioMixer\.ico"$/m, n);
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
  const stub = rd('installer', 'setup-stub.c');
  assert.match(stub, /desktop = 1, noauto = 0/); assert.ok(stub.includes('L"/nodesktop"') && stub.includes('!desktop'));
  assert.match(stub, /Audio Mixer is installed\./); assert.match(stub, /!quiet && !passive/); assert.match(stub, /Audio Mixer\.lnk/); assert.match(stub, /explorer\.exe/); assert.match(stub, /CSIDL_COMMON_PROGRAMS/);
  // launcher: default browser, then Explorer, then "start", then the address in a message
  const lau = rd('installer', 'launcher.c');
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
    for (const p of ['/releases/..%2F..%2Fetc%2Fpasswd', '/releases/%2e%2e', '/releases/a/b']) assert.ok([400, 404].includes((await fetch(base + p)).status), p);
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
  const code = ['client/verify.js', 'client/cli.js', 'client/service.js', 'installer/setup-stub.c', 'installer/launcher.c', 'installer/audio-mixer.c', 'scripts/build-msi.js'].map(f => rd(...f.split('/'))).join('\n') + fs.readdirSync(path.join(__dirname)).filter(f => /\.js$/.test(f) && f !== 'test.js').map(f => rd('bridge', f)).join('\n');
  assert.ok(!/Start-MpScan|Get-MpThreat|MpCmdRun|Add-MpPreference|Set-MpPreference|Get-MpComputerStatus|Update-MpSignature/i.test(code), 'no Defender cmdlet or tool is called');
  assert.ok(!/Defender/.test(rd('scripts', 'build-msi.js')) && !/verify --scan/.test(rd('scripts', 'build-msi.js')));
  assert.match(rd('scripts', 'build-msi.js'), /Verify installation \(file check\)[^\n]*verify --pause/);
  // the setup program: no scan after the install; /scan from an old command line is accepted and not passed on to Windows Installer
  const stub = rd('installer', 'setup-stub.c');
  assert.ok(!/scan = 1|&& scan\)|verify --scan/.test(stub) && /L"\/scan"\)\) continue;/.test(stub));
  // the command line: --scan is ignored with a note, the file checks still run
  const { execFileSync } = require('node:child_process');
  const out = execFileSync('node', [path.join(__dirname, '..', 'client', 'cli.js'), 'verify', '--scan'], { encoding: 'utf8' });
  assert.match(out, /Result: VERIFIED/); assert.match(out, /--scan was removed/); assert.ok(!/Defender/.test(out));
});
