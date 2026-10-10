'use strict';
// Autostart ("enable") for the local system server: starts bridge/server.js when you log in, no admin rights needed.
//   Windows: a Run entry in the registry (HKCU, no scripts and nothing written to the Startup folder)   macOS: LaunchAgent   Linux: systemd user service
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFile, spawn, spawnSync } = require('node:child_process');

const NAME = 'audio-mixer';
const SERVER = path.resolve(__dirname, '..', 'bridge', 'server.js');

// Paths end up inside quoted arguments of generated files: refuse anything that could break out of them.
function safePath(p) {
  if (typeof p !== 'string' || !p || /["'\r\n\0%$`]/.test(p)) throw new Error('path contains unsupported characters: ' + p);
  return p;
}
const xml = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&apos;' }[c]));

// Windows autostart is one registry value. With the installed native launcher (AudioMixerServer.exe, next to runtime\\ and bridge\\) the server starts
// without a console window; a plain Node.js checkout starts node.exe directly (its console window is visible: nothing is hidden).
const WIN_RUN_KEY = 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run';
const WIN_VALUE = 'AudioMixerServer';
function windowsCommand(node, server, launcher) {
  return launcher ? `"${safePath(launcher)}"` : `"${safePath(node)}" "${safePath(server)}"`;
}
function macPlist(node, server, log) {
  return `<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0">\n<dict>\n` +
    `  <key>Label</key><string>com.audiomixer.bridge</string>\n  <key>ProgramArguments</key>\n  <array><string>${xml(safePath(node))}</string><string>${xml(safePath(server))}</string></array>\n` +
    `  <key>RunAtLoad</key><true/>\n  <key>KeepAlive</key><true/>\n  <key>StandardOutPath</key><string>${xml(safePath(log))}</string>\n  <key>StandardErrorPath</key><string>${xml(safePath(log))}</string>\n</dict>\n</plist>\n`;
}
function systemdUnit(node, server) {
  return `[Unit]\nDescription=Audio Mixer local system server\nAfter=network.target\n\n[Service]\nExecStart="${safePath(node)}" "${safePath(server)}"\nRestart=on-failure\n\n[Install]\nWantedBy=default.target\n`;
}

function targets(platform = process.platform, env = process.env, home = os.homedir()) {
  if (platform === 'win32') {
    return { platform, key: WIN_RUN_KEY, value: WIN_VALUE, file: WIN_RUN_KEY + '\\' + WIN_VALUE };
  }
  if (platform === 'darwin') return { platform, file: path.join(home, 'Library', 'LaunchAgents', 'com.audiomixer.bridge.plist'), log: path.join(home, 'Library', 'Logs', 'audio-mixer.log') };
  if (platform === 'linux') return { platform, file: path.join((env.XDG_CONFIG_HOME || path.join(home, '.config')), 'systemd', 'user', NAME + '.service') };
  throw new Error('autostart is not supported on ' + platform);
}

const run = (exec, cmd, args) => new Promise(res => exec(cmd, args, { windowsHide: true }, err => res(!err)));

// `opts` is injectable for tests: { platform, env, home, node, server, launcher, exec, spawn, reg }
async function install(opts = {}) {
  const node = opts.node || process.execPath, server = opts.server || SERVER, exec = opts.exec || execFile;
  const t = targets(opts.platform, opts.env, opts.home);
  if (t.platform === 'win32') {
    const launcher = opts.launcher !== undefined ? opts.launcher : (fs.existsSync(path.join(path.dirname(server), '..', 'AudioMixerServer.exe')) ? path.resolve(path.dirname(server), '..', 'AudioMixerServer.exe') : null);
    const command = windowsCommand(node, server, launcher);
    const added = await run(exec, 'reg', ['add', t.key, '/v', t.value, '/t', 'REG_SZ', '/d', command, '/f']);
    let started = false;
    if (added) {
      try { (opts.spawn || spawn)(launcher || node, launcher ? [] : [server], { detached: true, stdio: 'ignore', windowsHide: true }).unref(); started = true; } catch (_) { /* starts at the next login */ }
    }
    return { file: t.file, started, installed: added, command };
  }
  const content = t.platform === 'darwin' ? macPlist(node, server, t.log) : systemdUnit(node, server);
  fs.mkdirSync(path.dirname(t.file), { recursive: true });
  fs.writeFileSync(t.file, content, { mode: 0o644 });
  let started;
  if (t.platform === 'darwin') started = await run(exec, 'launchctl', ['load', '-w', t.file]);
  else { await run(exec, 'systemctl', ['--user', 'daemon-reload']); started = await run(exec, 'systemctl', ['--user', 'enable', '--now', NAME + '.service']); }
  return { file: t.file, started };
}

async function uninstall(opts = {}) {
  const exec = opts.exec || execFile, t = targets(opts.platform, opts.env, opts.home);
  if (t.platform === 'win32') {
    const existed = status(opts).installed;
    await run(exec, 'reg', ['delete', t.key, '/v', t.value, '/f']);
    return { file: t.file, removed: existed };
  }
  if (t.platform === 'darwin') await run(exec, 'launchctl', ['unload', '-w', t.file]);
  else if (t.platform === 'linux') await run(exec, 'systemctl', ['--user', 'disable', '--now', NAME + '.service']);
  const existed = fs.existsSync(t.file);
  try { fs.unlinkSync(t.file); } catch (_) { /* not installed */ }
  if (t.platform === 'linux') await run(exec, 'systemctl', ['--user', 'daemon-reload']);
  return { file: t.file, removed: existed };
}

function status(opts = {}) {
  const t = targets(opts.platform, opts.env, opts.home);
  if (t.platform === 'win32') {
    const q = (opts.reg || ((args) => spawnSync('reg', args, { windowsHide: true })))(['query', t.key, '/v', t.value]);
    return { file: t.file, installed: !!q && q.status === 0 };
  }
  return { file: t.file, installed: fs.existsSync(t.file) };
}

module.exports = { install, uninstall, status, targets, windowsCommand, macPlist, systemdUnit, safePath };
