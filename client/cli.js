#!/usr/bin/env node
'use strict';
// Audio Mixer PC-mode client: starts the local system server (bridge), opens the mixer in your browser,
// and manages the official audio drivers. Zero dependencies, needs Node.js 18+.
//   node client/cli.js                 start the server and open the mixer
//   node client/cli.js drivers         list official drivers for this PC (installed / not found)
//   node client/cli.js download <id>   save an official installer (e.g. flexasio); it is never run for you
//   node client/cli.js doctor          check Node.js, ports, PortAudio, ASIO drivers, download folder
//   node client/cli.js verify [installer.exe] [--scan]   verification scan: file hashes, signatures, loopback-only, Defender scan
//   node client/cli.js setup [--user]  install the native audio modules (Audify, PortAudio) for ASIO / WASAPI; --user: Audify only, into your home folder
//   node client/cli.js service install|uninstall|status   start the server automatically when you log in
const http = require('node:http');
const fs = require('node:fs');
const { execFile } = require('node:child_process');

const MIN_NODE = 18;

function parseArgs(argv) {
  const o = { cmd: 'start', arg: null, port: Number(process.env.BRIDGE_PORT) || 8765, open: true, help: false };
  const rest = [];
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--no-open') o.open = false;
    else if (a === '--help' || a === '-h') o.help = true;
    else if (a === '--port') o.port = Number(argv[++i]);
    else if (a.startsWith('--port=')) o.port = Number(a.slice(7));
    else if (a.startsWith('--')) continue; // command flags (--scan, --pause) are read from argv by the command
    else rest.push(a);
  }
  if (rest[0]) o.cmd = rest[0];
  if (rest[1]) o.arg = rest[1];
  if (!Number.isInteger(o.port) || o.port < 1 || o.port > 65535) o.port = 8765;
  return o;
}

// Never passes anything but a localhost URL to the OS opener.
function openCommand(platform, url) {
  if (!/^http:\/\/localhost:\d{1,5}\/?$/.test(url)) return null;
  if (platform === 'win32') return { cmd: 'rundll32', args: ['url.dll,FileProtocolHandler', url] };
  if (platform === 'darwin') return { cmd: 'open', args: [url] };
  return { cmd: 'xdg-open', args: [url] };
}

function openBrowser(url) {
  const c = openCommand(process.platform, url);
  if (!c) return false;
  try { execFile(c.cmd, c.args, { windowsHide: true }, () => {}); return true; } catch (_) { return false; }
}

function probe(port) {
  return new Promise(resolve => {
    const req = http.get({ host: '127.0.0.1', port, path: '/api/status', timeout: 1500 }, res => {
      let b = ''; res.on('data', d => { b += d; }); res.on('end', () => { try { resolve(JSON.parse(b).name === 'audio-mixer-bridge'); } catch (_) { resolve(false); } });
    });
    req.on('error', () => resolve(false)); req.on('timeout', () => { req.destroy(); resolve(false); });
  });
}

function status(installed) { return installed === true ? 'INSTALLED' : installed === false ? 'NOT FOUND' : 'n/a'; }

function formatDrivers(items, all) {
  const rows = items.filter(i => all || i.forThisPc);
  const w = Math.max(...rows.map(r => r.id.length), 2);
  return rows.map(r => `${r.id.padEnd(w)}  ${status(r.installed).padEnd(9)}  ${r.name}\n${' '.repeat(w + 2)}${r.downloadable ? 'download: node client/cli.js download ' + r.id + '\n' + ' '.repeat(w + 2) : ''}${r.url}${r.install ? '\n' + ' '.repeat(w + 2) + 'install: ' + Object.values(r.install)[0] : ''}`).join('\n');
}

async function catalogForThisPc() {
  const { detect } = require('../bridge/detect');
  const { listCatalog, downloadDir } = require('../bridge/catalog');
  const info = await detect();
  return { info, items: listCatalog(info), dir: downloadDir() };
}

async function cmdDrivers() {
  const { info, items, dir } = await catalogForThisPc();
  console.log(`Official audio drivers for this PC (${info.platform}):\n`);
  console.log(formatDrivers(items, false));
  console.log(`\nDownloads are saved to ${dir} and are never run automatically.`);
}

async function cmdDownload(id) {
  if (!id) { console.error('usage: node client/cli.js download <id>   (see: node client/cli.js drivers)'); return 2; }
  const { downloadDriver } = require('../bridge/catalog');
  try {
    console.log(`Downloading ${id} from its official release ...`);
    const r = await downloadDriver(id);
    console.log(`Saved ${r.file} (${(r.bytes / 1048576).toFixed(2)} MB)`);
    console.log(`SHA-256 ${r.sha256} ${r.verified ? '(matches the release checksum)' : '(no checksum published; compare it yourself)'}`);
    console.log('Run the installer yourself after checking it.');
    return 0;
  } catch (e) { console.error('Download failed: ' + e.message); return 1; }
}

async function cmdDoctor(port) {
  let bad = 0;
  const line = (ok, msg) => { if (!ok) bad++; console.log(`${ok ? 'OK  ' : 'FAIL'} ${msg}`); };
  const major = Number(process.versions.node.split('.')[0]);
  line(major >= MIN_NODE, `Node.js ${process.versions.node} (need ${MIN_NODE}+; get it from https://nodejs.org/)`);
  const { info, items, dir } = await catalogForThisPc();
  line(true, `Platform ${info.platform} ${info.arch}`);
  const running = await probe(port);
  line(true, running ? `Server already running on port ${port}` : `Port ${port} is free for the server`);
  line(!!info.portaudio, info.portaudio ? `${info.portaudio.engine === 'audify' ? 'Audify (RtAudio)' : 'PortAudio'} module present (${info.portaudio.hostApis.join(', ')})${info.engines && info.engines.naudiodon && info.engines.audify ? ' + Audify' : ''}` : 'Audio engine missing: ASIO/WASAPI output needs "cd bridge && npm install" (naudiodon2 or audify)');
  if (!info.portaudio) { const pr = require('../bridge/audify').loadProblem(); if (pr) console.log(`HINT Audify: ${pr.error}\n     ${pr.hint}`); }
  console.log(`INFO ASIO drivers installed: ${info.asio.length ? info.asio.join(', ') : 'none'}`);
  console.log(`INFO Native stacks found: ${info.drivers.join(', ') || 'none'}`);
  try { fs.mkdirSync(dir, { recursive: true }); fs.accessSync(dir, fs.constants.W_OK); line(true, `Download folder writable: ${dir}`); } catch (_) { line(false, `Download folder not writable: ${dir}`); }
  if (info.platform === 'win32' && !info.asio.length) console.log('HINT No ASIO driver: install FlexASIO or ASIO4ALL (node client/cli.js drivers), or use WASAPI.');
  return bad ? 1 : 0;
}

// Installs the native audio modules. Default: bridge/ dependencies (Audify and naudiodon2). --user: only Audify (prebuilt binary, no compiler),
// into ~/.local/share/audio-mixer/modules, which the launchers put on NODE_PATH (the app folder may be read-only, e.g. /opt/audio-mixer).
function cmdSetup(argv = []) {
  const { spawn } = require('node:child_process');
  const path = require('node:path'), os = require('node:os');
  let cwd = path.resolve(__dirname, '..', 'bridge'), args = ['install'];
  if (argv.includes('--user')) {
    cwd = path.join(process.env.AUDIO_MIXER_HOME || path.join(os.homedir(), '.local', 'share', 'audio-mixer'), 'modules');
    fs.mkdirSync(cwd, { recursive: true });
    if (!fs.existsSync(path.join(cwd, 'package.json'))) fs.writeFileSync(path.join(cwd, 'package.json'), '{"name":"audio-mixer-modules","private":true}\n');
    args = ['install', '--no-audit', '--no-fund', 'audify'];
    console.log(`Installing the Audify native audio module in ${cwd} ...`);
  } else console.log('Installing the native audio modules (Audify, PortAudio) in bridge/ (this can take a minute) ...');
  return new Promise(resolve => {
    const c = process.platform === 'win32' ? spawn('cmd', ['/c', 'npm', ...args], { cwd, stdio: 'inherit' }) : spawn('npm', args, { cwd, stdio: 'inherit' });
    c.on('error', e => { console.error('Cannot run npm: ' + e.message + ' (install Node.js from https://nodejs.org/)'); resolve(1); });
    c.on('exit', code => {
      if (code === 0) console.log('Done. Restart the server; "node client/cli.js doctor" shows the detected ASIO / WASAPI devices.');
      else console.error('npm install failed. Web mode still works. Audify needs no compiler (prebuilt); PortAudio (naudiodon2) needs a C++ toolchain (Windows: Visual Studio Build Tools, macOS: Xcode CLT, Linux: build-essential).');
      resolve(code === 0 ? 0 : 1);
    });
  });
}

async function cmdVerify(o, argv) {
  const verify = require('./verify');
  const r = await verify.verify({ target: o.arg, scan: argv.includes('--scan'), port: o.port });
  console.log(verify.format(r));
  if (argv.includes('--pause')) { console.log('\nPress Enter to close.'); await new Promise(res => { process.stdin.resume(); process.stdin.once('data', res); }); }
  return r.ok ? 0 : 1;
}

async function cmdService(action, port) {
  const svc = require('./service');
  try {
    if (action === 'install') {
      const r = await svc.install();
      console.log(`Autostart enabled: ${r.file}`);
      console.log(r.started ? 'The local system server was started now and will start at every login.' : 'It will start at your next login (starting it now failed: run "node client/cli.js" or log in again).');
      console.log(`The mixer finds it at http://localhost:${port}/ ; remove it with: node client/cli.js service uninstall`);
      return 0;
    }
    if (action === 'uninstall') { const r = await svc.uninstall(); console.log(r.removed ? `Autostart removed: ${r.file}` : 'Autostart was not installed.'); return 0; }
    if (action === 'status') {
      const st = svc.status(), up = await probe(port);
      console.log(`Autostart: ${st.installed ? 'ENABLED' : 'not enabled'} (${st.file})`);
      console.log(`Server on port ${port}: ${up ? 'RUNNING' : 'not running'}`);
      return 0;
    }
  } catch (e) { console.error('Service error: ' + e.message); return 1; }
  console.error('usage: node client/cli.js service install|uninstall|status');
  return 2;
}

async function cmdStart(o) {
  const server = require('../bridge/server');
  let port = o.port;
  try { port = await server.start(o.port); }
  catch (e) {
    if (e.code === 'EADDRINUSE' && await probe(o.port)) console.log(`The system server is already running on port ${o.port}; reusing it.`);
    else { console.error(`Cannot start the server on port ${o.port}: ${e.message}`); return 1; }
  }
  const url = `http://localhost:${port}/`;
  const { detect } = require('../bridge/detect');
  const info = await detect();
  console.log(`Audio Mixer PC mode is running: ${url}`);
  console.log(`  Platform: ${info.platform}   Native stacks: ${info.drivers.join(', ') || 'none'}`);
  console.log(`  ASIO drivers: ${info.asio.length ? info.asio.join(', ') : 'none'}   PortAudio: ${info.portaudio ? 'yes' : 'no (cd bridge && npm install)'}`);
  if (info.platform === 'win32' && !info.asio.length) console.log('  No ASIO driver found. See: node client/cli.js drivers');
  if (o.open) openBrowser(url);
  console.log('Press Ctrl+C to stop.');
  await new Promise(() => {});
  return 0;
}

async function main(argv) {
  const o = parseArgs(argv);
  const major = Number(process.versions.node.split('.')[0]);
  if (major < MIN_NODE) { console.error(`Node.js ${MIN_NODE}+ is required (you have ${process.versions.node}). Download it from https://nodejs.org/`); return 1; }
  if (o.help) { const head = []; for (const l of fs.readFileSync(__filename, 'utf8').split('\n').slice(2)) { if (!l.startsWith('//')) break; head.push(l.slice(3)); } console.log(head.join('\n')); return 0; }
  if (o.cmd === 'start') return cmdStart(o);
  if (o.cmd === 'drivers') return (await cmdDrivers(), 0);
  if (o.cmd === 'download') return cmdDownload(o.arg);
  if (o.cmd === 'doctor') return cmdDoctor(o.port);
  if (o.cmd === 'service') return cmdService(o.arg, o.port);
  if (o.cmd === 'setup') return cmdSetup(argv);
  if (o.cmd === 'verify') return cmdVerify(o, argv);
  console.error(`Unknown command "${o.cmd}". Use: start | drivers | download <id> | doctor | verify | setup | service install|uninstall|status`);
  return 2;
}

if (require.main === module) main(process.argv.slice(2)).then(code => process.exit(code), e => { console.error(e.message); process.exit(1); });
module.exports = { parseArgs, openCommand, formatDrivers, probe, main };
