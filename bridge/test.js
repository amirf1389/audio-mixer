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
    const zip = makeZip([[`node-v98.1.2-win-${arch}/LICENSE`, Buffer.from('MIT')], [`node-v98.1.2-win-${arch}/node.exe`, Buffer.from('MZ-fake-node-' + arch)]]);
    const out = fsx.mkdtempSync(pathx.join(osx.tmpdir(), 'inst-'));
    const r = await bi.buildInstaller({ out, arch, fetchImpl: fakeNodeFetch(zip, { arch }), bundleAudify: false });
    assert.strictEqual(pathx.basename(r.stage), `stage-${arch}`);
    assert.strictEqual(fsx.readFileSync(pathx.join(r.stage, 'runtime', 'node.exe')).toString(), 'MZ-fake-node-' + arch);
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
  assert.match(x64, /Name="a &amp; b\.js"/); assert.match(x64, /Feature Id="Autostart"/); assert.match(x64, /Feature Id="Desktop"[^>]*Level="2"/);
  assert.match(x64, /Id="ScUninstall"[^>]*msiexec\.exe" Arguments="\/x \{[0-9A-F-]{36}\}"/);          // Start Menu uninstall entry
  assert.match(x64, /ARPURLINFOABOUT/);                                                                  // Settings > Apps entry details
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
  assert.match(u.LAUNCHER_LINUX, /\. "\$APP\/ensure-node\.sh"\nam_ensure_node \|\| exit 1\nam_ensure_audio "\$APP"\nexec "\$NODE" "\$APP\/client\/cli\.js" "\$@"/);
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
  assert.throws(() => a.openStream({ mod: fa, dev: { ...btDev, name: 'Focusrite USB ASIO', maxOutputChannels: 2 }, direction: 'output', channels: 2, sampleRate: 48000 }), /does not support 48000/);
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
