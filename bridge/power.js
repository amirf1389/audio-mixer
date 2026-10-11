'use strict';
// Reboot system: restart this server (the process starts itself again), or reboot the computer.
//   restart:  the server closes its port and sockets, starts a copy of itself (same program, same arguments, no browser window) and exits
//   reboot:   Windows `shutdown /r /t 10` (10 s to cancel with `shutdown /a`), Linux `systemctl reboot`, macOS `System Events` restart; Android / iOS: not possible
// BRIDGE_POWER_DRY_RUN=1 only reports what would run (used by the tests).
const { execFile, spawn } = require('node:child_process');

function rebootCommand(platform = process.platform) {
  if (platform === 'win32') return { cmd: 'shutdown.exe', args: ['/r', '/t', '10', '/c', 'Audio Mixer: restart requested from the mixer (shutdown /a cancels it)'] };
  if (platform === 'linux') return { cmd: 'systemctl', args: ['reboot'], fallback: { cmd: 'shutdown', args: ['-r', 'now'] } };
  if (platform === 'darwin') return { cmd: 'osascript', args: ['-e', 'tell application "System Events" to restart'] };
  return null;
}

function cancelCommand(platform = process.platform) {
  if (platform === 'win32') return { cmd: 'shutdown.exe', args: ['/a'] };
  if (platform === 'linux') return { cmd: 'shutdown', args: ['-c'] };
  return null;
}

function runCmd(cmd, args, timeout = 8000) {
  return new Promise(resolve => execFile(cmd, args, { timeout, windowsHide: true }, (err, out, errOut) => resolve({ ok: !err, out: String(out || ''), err: String((err && err.message) || errOut || '') })));
}

function capabilities({ platform = process.platform } = {}) { return { restart: true, reboot: !!rebootCommand(platform), cancelReboot: !!cancelCommand(platform), platform }; }

async function rebootOs({ platform = process.platform, run = runCmd, dry = process.env.BRIDGE_POWER_DRY_RUN === '1', cancel = false } = {}) {
  const c = cancel ? cancelCommand(platform) : rebootCommand(platform);
  if (!c) { const e = new Error('this system cannot be rebooted from the app'); e.status = 501; throw e; }
  if (dry) return { ok: true, dry: true, command: [c.cmd, ...c.args].join(' ') };
  let r = await run(c.cmd, c.args);
  if (!r.ok && c.fallback) r = await run(c.fallback.cmd, c.fallback.args);
  if (!r.ok) { const e = new Error('the reboot command was refused by the system' + (r.err ? ': ' + r.err.split('\n')[0] : '') + ' (administrator rights may be needed)'); e.status = 403; throw e; }
  return { ok: true, dry: false, command: [c.cmd, ...c.args].join(' '), cancelled: cancel };
}

// The command that starts this same server again: a normal Node.js run (`node bridge/server.js`, `node client/cli.js`) or a single-file executable.
function relaunch({ execPath = process.execPath, execArgv = process.execArgv, argv = process.argv, self = global.__AM_SELF } = {}) {
  const user = argv.slice(2);                                    // the arguments after the script (a single-file executable has its own path at argv[1] too)
  const script = self ? (self.script ? [self.script] : []) : [argv[1]];
  const args = [...(self ? [] : execArgv), ...script, ...user.filter(a => a !== '--no-open'), '--no-open'];
  return { cmd: execPath, args };
}

// Closes the port, then starts the copy: the new process must find the port free. `exit` and `spawnImpl` are injectable.
function restartSelf({ server, sockets = new Set(), spawnImpl = spawn, exit = code => process.exit(code), plan = relaunch(), dry = process.env.BRIDGE_POWER_DRY_RUN === '1', delay = 250 } = {}) {
  if (dry) return { dry: true, command: [plan.cmd, ...plan.args].join(' ') };
  setTimeout(() => {
    let done = false;
    const go = () => {
      if (done) return; done = true;
      try { spawnImpl(plan.cmd, plan.args, { detached: true, stdio: 'ignore', windowsHide: true, env: process.env }).unref(); } catch (_) { /* nothing more to try */ }
      exit(0);
    };
    try { server.close(go); } catch (_) { go(); }
    for (const s of sockets) { try { s.destroy(); } catch (_) { /* gone */ } }
    try { server.closeAllConnections && server.closeAllConnections(); } catch (_) { /* old Node.js */ }
    setTimeout(go, 2500).unref();       // a socket that will not close must not keep the restart waiting
  }, delay);
  return { dry: false, command: [plan.cmd, ...plan.args].join(' ') };
}

module.exports = { rebootCommand, cancelCommand, capabilities, rebootOs, relaunch, restartSelf };
