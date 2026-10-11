#!/usr/bin/env node
'use strict';
// Audio Mixer PC-mode client: starts the local system server (bridge), opens the mixer in your browser,
// and manages the official audio drivers. Zero dependencies, needs Node.js 18+.
//   node client/cli.js                 start the server and open the mixer (an older Audio Mixer server still running on the port is ended first)
//   node client/cli.js drivers         list official drivers for this PC (installed / not found)
//   node client/cli.js doctor          check Node.js, ports, PortAudio, ASIO drivers, download folder
//   node client/cli.js verify [installer.exe]   verification: file hashes, signatures, loopback-only
//   node client/cli.js setup [--user]  install the native audio modules (Audify, PortAudio) for ASIO / WASAPI; --user: Audify only, into your home folder
//   node client/cli.js service install|uninstall|status   start the server automatically when you log in
//   node client/cli.js npm <args>      run npm (the bundled one in the Windows install, else the system's) inside the app's bridge folder
//   node client/cli.js uninstall [--yes]   stop the server, remove the autostart entry and run this installation's uninstaller
//   node client/cli.js version         print the version
//   (installed packages put these on your PATH as "audio-mixer": audio-mixer doctor, audio-mixer npm install audify ...)
//   node client/cli.js license [status|activate <key>|deactivate]   show / activate / remove the license key (works offline)
//   node client/cli.js plugins         list the VST3 / VST2 plugins found on this PC and the plugin folder
//   node client/cli.js update [download]   check the signed update manifest; download saves the verified installer (never run for you)
//   (drivers, verify, license, plugins and update accept --pause: wait for Enter before closing, used by the Start Menu shortcuts)
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
    else if (a.startsWith('--')) continue; // command flags (--pause) are read from argv by the command
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

// The Audio Mixer server on this port, or null: { name, version, pid } from /api/status.
function serverInfo(port) {
  return new Promise(resolve => {
    const req = http.get({ host: '127.0.0.1', port, path: '/api/status', timeout: 1500 }, res => {
      let b = ''; res.on('data', d => { b += d; }); res.on('end', () => { try { const j = JSON.parse(b); resolve(j && j.name === 'audio-mixer-bridge' ? j : null); } catch (_) { resolve(null); } });
    });
    req.on('error', () => resolve(null)); req.on('timeout', () => { req.destroy(); resolve(null); });
  });
}

// An OLDER Audio Mixer server still running (started before an upgrade, or at login) keeps serving its old page and code: whoever starts the new version
// would just reuse it and show no change. It is verified as an Audio Mixer server on this PC's loopback (name + pid from /api/status), then ended.
async function replaceStaleServer(port, log = console.log) {
  const info = await serverInfo(port);
  if (!info) return 'none';
  const own = require('../package.json').version, upd = require('../bridge/update');
  if (info.version === own) return 'current';
  if (upd.cmpVersion(info.version, own) > 0) return 'newer';     // never replace a newer one (a downgrade is the user's decision)
  if (!Number.isInteger(info.pid) || info.pid <= 1 || info.pid === process.pid) return 'stale';
  try { process.kill(info.pid); } catch (e) { log(`An older Audio Mixer server (${info.version}, pid ${info.pid}) is still running on port ${port} and could not be ended (${e.code || e.message}). End "node.exe" in Task Manager, then start Audio Mixer again.`); return 'stale'; }
  for (let i = 0; i < 40 && await serverInfo(port); i++) await new Promise(r => setTimeout(r, 150));
  if (await serverInfo(port)) { log(`The older server (${info.version}) did not stop: end "node.exe" in Task Manager, then start Audio Mixer again.`); return 'stale'; }
  log(`Replaced the older Audio Mixer server (${info.version}) with ${own}.`);
  return 'replaced';
}

function status(installed) { return installed === true ? 'INSTALLED' : installed === false ? 'NOT FOUND' : 'n/a'; }

function formatDrivers(items, all) {
  const rows = items.filter(i => all || i.forThisPc);
  const w = Math.max(...rows.map(r => r.id.length), 2);
  return rows.map(r => `${r.id.padEnd(w)}  ${status(r.installed).padEnd(9)}  ${r.name}\n${' '.repeat(w + 2)}${r.url}${r.install ? '\n' + ' '.repeat(w + 2) + 'install: ' + Object.values(r.install)[0] : ''}`).join('\n');
}

async function catalogForThisPc() {
  const { detect } = require('../bridge/detect');
  const { listCatalog } = require('../bridge/catalog');
  const info = await detect();
  return { info, items: listCatalog(info) };
}

async function cmdDrivers() {
  const { info, items } = await catalogForThisPc();
  console.log(`Official audio drivers for this PC (${info.platform}):\n`);
  console.log(formatDrivers(items, false));
  console.log('\nEach driver is installed from its official site (the link is shown above); Audio Mixer does not download drivers.');
}

async function cmdDoctor(port) {
  let bad = 0;
  const line = (ok, msg) => { if (!ok) bad++; console.log(`${ok ? 'OK  ' : 'FAIL'} ${msg}`); };
  const major = Number(process.versions.node.split('.')[0]);
  line(major >= MIN_NODE, `Node.js ${process.versions.node} (need ${MIN_NODE}+; get it from https://nodejs.org/)`);
  const { info, items } = await catalogForThisPc();
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
    const n = findNpm(), env = { ...process.env, PATH: path.dirname(process.execPath) + path.delimiter + (process.env.PATH || '') };
    const c = spawn(n.cmd, [...n.pre, ...args], { cwd, stdio: 'inherit', env });
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
  const r = await verify.verify({ target: o.arg, port: o.port });
  console.log(verify.format(r));
  if (argv.includes('--scan')) console.log('\nNote: --scan was removed from this command. Nothing else changed.');
  await pause(argv);
  return r.ok ? 0 : 1;
}

async function pause(argv) {
  if (!argv.includes('--pause')) return;
  console.log('\nPress Enter to close.');
  await new Promise(res => { process.stdin.resume(); process.stdin.once('data', res); });
}

async function cmdLicense(o, argv) {
  const lic = require('../bridge/license');
  const key = argv.filter(a => !a.startsWith('--'));   // [license, activate, <key>]
  let code = 0;
  if (o.arg === 'activate') {
    if (!key[2]) { console.error('usage: node client/cli.js license activate <key>'); code = 2; }
    else { const r = lic.activate(key.slice(2).join('')); if (r.ok) console.log(`License activated: plan ${r.plan.name || r.plan.id || ''}`); else { console.error(`Key refused: ${r.message || r.reason}`); code = 1; } }
  } else if (o.arg === 'deactivate') { const r = lic.deactivate(); console.log(r.removed ? 'License removed; Audio Mixer runs as BASIC (8 channels).' : 'No license was stored.'); }
  else if (o.arg && o.arg !== 'status') { console.error('usage: node client/cli.js license [status|activate <key>|deactivate]'); code = 2; }
  if (code === 0) {
    const st = lic.status();
    console.log(`License: ${st.state.toUpperCase()}   plan: ${st.plan.name || st.plan.id || 'basic'}   channels: ${st.plan.channels}`);
    console.log(`Machine ID: ${st.machineId}  (give it to the seller for a key bound to this PC)`);
    if (st.license && st.license.expires) console.log(`Expires: ${new Date(st.license.expires).toISOString().slice(0, 10)}`);
    if (st.message) console.log(st.message);
  }
  await pause(argv);
  return code;
}

async function cmdPlugins(argv) {
  const pl = require('../bridge/plugins');
  let r;
  try { r = pl.scan(); } catch (e) { console.error('Plugin scan failed: ' + e.message); await pause(argv); return 1; }
  console.log(`Plugin folder: ${r.appDir}${r.appDirExists ? '' : ' (not created yet)'}`);
  const list = r.plugins || [];
  console.log(list.length ? `${list.length} plugin(s) found:` : 'No VST3 / VST2 plugins found. Copy .vst3 files or VST2 .dll files into the plugin folder.');
  for (const p of list) console.log(`  ${(p.format || p.ext || '').toString().toUpperCase().padEnd(5)} ${p.name || p.file}  ${p.file}`);
  await pause(argv);
  return 0;
}

async function cmdUpdate(o, argv) {
  const upd = require('../bridge/update');
  const current = require('../package.json').version;
  let code = 0;
  try {
    if (o.arg === 'download') { const r = await upd.download({ current }); console.log(`Saved and verified ${r.version}: ${r.file}\nSHA-256 ${r.sha256}\n${r.note}`); }
    else {
      const r = await upd.check({ current });
      console.log(`Installed ${current}   latest ${r.latest}   ${r.updateAvailable ? 'UPDATE AVAILABLE' : 'up to date'}   (signed manifest)`);
      for (const n of r.notes) console.log('  - ' + n);
      if (r.updateAvailable) console.log('Download it with: node client/cli.js update download');
    }
  } catch (e) { console.error('Update: ' + e.message); code = 1; }
  await pause(argv);
  return code;
}

// npm: the one bundled next to the Windows runtime (runtime/node_modules/npm), else the system's npm.
function findNpm() {
  const path = require('node:path');
  const bundled = path.join(path.dirname(process.execPath), 'node_modules', 'npm', 'bin', 'npm-cli.js');
  if (fs.existsSync(bundled)) return { cmd: process.execPath, pre: [bundled], bundled: true };
  return process.platform === 'win32' ? { cmd: 'cmd', pre: ['/c', 'npm'], bundled: false } : { cmd: 'npm', pre: [], bundled: false };
}

function cmdNpm(argv) {
  const { spawn } = require('node:child_process'), path = require('node:path');
  const i = argv.indexOf('npm'), args = i >= 0 ? argv.slice(i + 1) : [];
  const n = findNpm();
  const env = { ...process.env, PATH: path.dirname(process.execPath) + path.delimiter + (process.env.PATH || '') };   // npm scripts find this Node.js first
  return new Promise(resolve => {
    const c = spawn(n.cmd, [...n.pre, ...(args.length ? args : ['--version'])], { cwd: path.resolve(__dirname, '..', 'bridge'), stdio: 'inherit', env });
    c.on('error', e => { console.error('Cannot run npm: ' + e.message + ' (install Node.js from https://nodejs.org/)'); resolve(1); });
    c.on('exit', code => resolve(code === null ? 1 : code));
  });
}

// Uninstall: ends the running server and the autostart entry, then hands over to what installed this copy.
async function cmdUninstall(o, argv) {
  const { spawnSync, spawn } = require('node:child_process'), path = require('node:path'), readline = require('node:readline');
  const root = path.resolve(__dirname, '..');
  const yes = argv.includes('--yes') || argv.includes('-y');
  const sh = (c, a) => { try { return spawnSync(c, a, { encoding: 'utf8', windowsHide: true }); } catch (_) { return { status: 1, stdout: '' }; } };
  let step = null;
  if (process.platform === 'win32') {
    for (const hive of ['HKLM', 'HKCU']) {
      const r = sh('reg', ['query', `${hive}\\Software\\Audio Mixer`, '/v', 'UninstallCode']);
      const m = r.status === 0 && /UninstallCode\s+REG_SZ\s+(\{[0-9A-Fa-f-]{36}\})/.exec(r.stdout || '');
      if (m) { step = { text: `Windows Installer removes Audio Mixer (${hive === 'HKLM' ? 'all users' : 'this user'})`, run: () => spawn('msiexec.exe', ['/x', m[1]], { detached: true, stdio: 'ignore' }).unref() }; break; }
    }
    if (!step) step = { text: 'Windows Settings opens: Apps > Installed apps > Audio Mixer > Uninstall', run: () => spawn('cmd', ['/c', 'start', '', 'ms-settings:appsfeatures'], { detached: true, stdio: 'ignore' }).unref() };
  } else {
    const script = ['uninstall.sh'].map(f => path.join(root, f)).find(f => fs.existsSync(f));
    if (script) step = { text: `${script} removes the program, the menu entry and the command`, run: () => spawnSync('sh', [script], { stdio: 'inherit' }) };
    else if (process.platform === 'linux' && sh('dpkg', ['-S', root]).status === 0) step = { text: 'this copy belongs to the Debian package audio-mixer: run  sudo apt remove audio-mixer', run: null };
    else step = { text: `no uninstaller found for ${root}: delete that folder`, run: null };
  }
  console.log('Uninstalling Audio Mixer:\n  1. stop the local server on port ' + o.port + '\n  2. remove the start-at-login entry\n  3. ' + step.text);
  console.log('Your downloaded drivers (~/AudioMixerDrivers), plugins (~/AudioMixerPlugins) and license key stay.');
  if (!yes) {
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
    const a = await new Promise(res => { rl.once('close', () => res('')); rl.question('Continue? [y/N] ', res); });   // input that ends without an answer is a no
    rl.close(); if (!process.stdin.isTTY) console.log();
    if (!/^y/i.test(String(a).trim())) { console.log('Nothing was changed.'); return 0; }
  }
  const info = await serverInfo(o.port);
  if (info && Number.isInteger(info.pid) && info.pid > 1 && info.pid !== process.pid) { try { process.kill(info.pid); console.log('Server stopped.'); } catch (_) { console.log('Could not stop the server: end it in Task Manager / Activity Monitor.'); } }
  try { const r = await require('./service').uninstall(); if (r.removed) console.log('Start-at-login entry removed.'); } catch (_) { /* none */ }
  if (step.run) { step.run(); console.log('Started the uninstaller.'); } else console.log(step.text);
  return 0;
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

async function cmdStart(o, argv = []) {
  const server = require('../bridge/server');
  let port = o.port;
  const stale = await replaceStaleServer(o.port);
  try { port = await server.start(o.port); }
  catch (e) {
    if (e.code === 'EADDRINUSE' && await probe(o.port)) {
      console.log(`The system server is already running on port ${o.port}${stale === 'newer' ? ' (a newer version than this one)' : stale === 'stale' ? ' (an OLDER version: you see its old page)' : ''}; reusing it.`);
      if (argv.includes('--ensure')) return 0;   // the Windows launcher only wants a current server running
    }
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
  if (o.help && o.cmd !== 'npm') { const head = []; for (const l of fs.readFileSync(__filename, 'utf8').split('\n').slice(2)) { if (!l.startsWith('//')) break; head.push(l.slice(3)); } console.log(head.join('\n')); return 0; }
  if (o.cmd === 'start') return cmdStart(o, argv);
  if (o.cmd === 'drivers') return (await cmdDrivers(), await pause(argv), 0);
  if (o.cmd === 'license') return cmdLicense(o, argv);
  if (o.cmd === 'plugins') return cmdPlugins(argv);
  if (o.cmd === 'update') return cmdUpdate(o, argv);
  if (o.cmd === 'doctor') { const c = await cmdDoctor(o.port); await pause(argv); return c; }
  if (o.cmd === 'service') return cmdService(o.arg, o.port);
  if (o.cmd === 'setup') return cmdSetup(argv);
  if (o.cmd === 'npm') return cmdNpm(argv);
  if (o.cmd === 'uninstall') return cmdUninstall(o, argv);
  if (o.cmd === 'version') return (console.log(require('../package.json').version), 0);
  if (o.cmd === 'verify') return cmdVerify(o, argv);
  console.error(`Unknown command "${o.cmd}". Use: start | drivers | doctor | verify | setup | service install|uninstall|status | license | plugins | update | npm | uninstall | version`);
  return 2;
}

if (require.main === module) main(process.argv.slice(2)).then(code => process.exit(code), e => { console.error(e.message); process.exit(1); });
module.exports = { parseArgs, openCommand, formatDrivers, probe, main };
