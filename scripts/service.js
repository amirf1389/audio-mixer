#!/usr/bin/env node
'use strict';
// Runs the Audio Mixer server as a managed background service.
//   node scripts/service.js nssm install|remove|start|stop|restart|status [--name AudioMixer] [--port 8765] [--nssm <path to nssm.exe>] [--user DOMAIN\name] [--dry-run]
//   node scripts/service.js pm2   start|stop|restart|delete|status|logs|save|startup [--dry-run]
// nssm: a Windows service through NSSM (https://nssm.cc, put nssm.exe on PATH or pass --nssm). The service starts with Windows, restarts the server when it exits
//       (2 s back-off), writes rotated logs to %ProgramData%\AudioMixer\logs. A service runs in Windows' session 0: it is right for the server, the page, the OTA
//       and remote control; audio hardware that belongs to the logged-in user (WASAPI default devices, many ASIO drivers) may not be reachable from there. For that, run the
//       service as the user (--user DOMAIN\name; the password is read from the AM_SERVICE_PASSWORD environment variable, never typed on the command line; NSSM itself takes it as an argument while it runs) or use pm2 / autostart.
// pm2:  the Node.js process manager with deploy/pm2/ecosystem.config.js (npm i -g pm2).
// --dry-run prints the commands and runs nothing. Installing a service needs an administrator prompt.
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 && a[i + 1] ? a[i + 1] : d; };

// the NSSM commands (argument lists) for one action
function nssmCommands(action, o = {}) {
  const name = o.name || 'AudioMixer', nssm = o.nssm || 'nssm';
  const dir = o.dir || ROOT, node = o.node || process.execPath, script = o.script || path.join(dir, 'bridge', 'server.js');
  const logs = o.logs || path.join(process.env.ProgramData || 'C:\\ProgramData', 'AudioMixer', 'logs');
  const set = (k, ...v) => [nssm, 'set', name, k, ...v];
  switch (action) {
    case 'install': return [
      [nssm, 'install', name, node, script],
      set('AppDirectory', dir),
      set('DisplayName', 'Audio Mixer'),
      set('Description', 'Audio Mixer server: the local mixer, its page, OTA updates and remote control'),
      set('Start', 'SERVICE_AUTO_START'),
      set('AppEnvironmentExtra', 'NODE_ENV=production', 'BRIDGE_PORT=' + (o.port || 8765)),
      set('AppExit', 'Default', 'Restart'),
      set('AppRestartDelay', '2000'),
      set('AppThrottle', '10000'),
      set('AppStopMethodConsole', '3000'),
      set('AppStdout', path.win32.join(logs, name + '.out.log')),
      set('AppStderr', path.win32.join(logs, name + '.err.log')),
      set('AppRotateFiles', '1'), set('AppRotateOnline', '1'), set('AppRotateBytes', String(5 * 1024 * 1024)),
      ...(o.user ? [set('ObjectName', o.user, o.password || '')] : []),
      [nssm, 'start', name],
    ];
    case 'remove': return [[nssm, 'stop', name], [nssm, 'remove', name, 'confirm']];
    case 'start': case 'stop': case 'restart': case 'status': return [[nssm, action, name]];
    default: throw new Error('unknown nssm action: ' + action);
  }
}
function pm2Commands(action) {
  const eco = path.join(ROOT, 'deploy', 'pm2', 'ecosystem.config.js');
  switch (action) {
    case 'start': return [['pm2', 'start', eco]];
    case 'stop': case 'restart': case 'delete': return [['pm2', action, 'audio-mixer']];
    case 'status': return [['pm2', 'status']];
    case 'logs': return [['pm2', 'logs', 'audio-mixer', '--lines', '50', '--nostream']];
    case 'save': return [['pm2', 'save']];
    case 'startup': return [['pm2', 'startup']];
    default: throw new Error('unknown pm2 action: ' + action);
  }
}

function main(argv) {
  const [kind, action = 'status'] = argv;
  const dry = argv.includes('--dry-run');
  let cmds;
  if (kind === 'nssm') {
    if (process.platform !== 'win32' && !dry) throw new Error('NSSM services are Windows only (use --dry-run to see the commands)');
    cmds = nssmCommands(action, { name: arg(argv, '--name'), port: arg(argv, '--port'), nssm: arg(argv, '--nssm'), user: arg(argv, '--user'), password: process.env.AM_SERVICE_PASSWORD });
  } else if (kind === 'pm2') cmds = pm2Commands(action);
  else throw new Error('usage: node scripts/service.js nssm|pm2 <action> [--dry-run]   (see the header of scripts/service.js)');
  for (const c of cmds) {
    const shown = c.map((x, i) => (c[1] === 'set' && c[3] === 'ObjectName' && i === 5 ? '********' : (/\s/.test(x) ? `"${x}"` : x))).join(' ');
    console.log((dry ? '[dry run] ' : '> ') + shown);
    if (dry) continue;
    const r = spawnSync(c[0], c.slice(1), { stdio: 'inherit', windowsHide: true });
    if (r.error) throw new Error('cannot run ' + c[0] + ': ' + r.error.message + (kind === 'nssm' ? ' (download NSSM from https://nssm.cc and put nssm.exe on PATH or pass --nssm)' : ' (npm i -g pm2)'));
    if (r.status !== 0 && !(action === 'remove' && c[1] === 'stop')) throw new Error(c[0] + ' ' + c[1] + ' exited with ' + r.status);
  }
}

if (require.main === module) { try { main(process.argv.slice(2)); } catch (e) { console.error('Error: ' + e.message); process.exit(1); } }
module.exports = { nssmCommands, pm2Commands };
