'use strict';
// Autostart ("enable") for the local system server: starts bridge/server.js when you log in, no admin rights needed.
//   Windows: Startup-folder script (hidden window)   macOS: LaunchAgent   Linux: systemd user service
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFile } = require('node:child_process');

const NAME = 'audio-mixer';
const SERVER = path.resolve(__dirname, '..', 'bridge', 'server.js');

// Paths end up inside quoted arguments of generated files: refuse anything that could break out of them.
function safePath(p) {
  if (typeof p !== 'string' || !p || /["'\r\n\0%$`]/.test(p)) throw new Error('path contains unsupported characters: ' + p);
  return p;
}
const xml = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&apos;' }[c]));

function windowsScript(node, server) {
  return `' Starts the Audio Mixer local system server at login (hidden). Remove with: node client/cli.js service uninstall\r\n` +
    `Set sh = CreateObject("WScript.Shell")\r\nsh.Run """${safePath(node)}"" ""${safePath(server)}""", 0, False\r\n`;
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
    const appdata = env.APPDATA || path.join(home, 'AppData', 'Roaming');
    return { platform, file: path.join(appdata, 'Microsoft', 'Windows', 'Start Menu', 'Programs', 'Startup', 'AudioMixerServer.vbs') };
  }
  if (platform === 'darwin') return { platform, file: path.join(home, 'Library', 'LaunchAgents', 'com.audiomixer.bridge.plist'), log: path.join(home, 'Library', 'Logs', 'audio-mixer.log') };
  if (platform === 'linux') return { platform, file: path.join((env.XDG_CONFIG_HOME || path.join(home, '.config')), 'systemd', 'user', NAME + '.service') };
  throw new Error('autostart is not supported on ' + platform);
}

const run = (exec, cmd, args) => new Promise(res => exec(cmd, args, { windowsHide: true }, err => res(!err)));

// `opts` is injectable for tests: { platform, env, home, node, server, exec }
async function install(opts = {}) {
  const node = opts.node || process.execPath, server = opts.server || SERVER, exec = opts.exec || execFile;
  const t = targets(opts.platform, opts.env, opts.home);
  const content = t.platform === 'win32' ? windowsScript(node, server) : t.platform === 'darwin' ? macPlist(node, server, t.log) : systemdUnit(node, server);
  fs.mkdirSync(path.dirname(t.file), { recursive: true });
  fs.writeFileSync(t.file, content, { mode: 0o644 });
  let started;
  if (t.platform === 'darwin') started = await run(exec, 'launchctl', ['load', '-w', t.file]);
  else if (t.platform === 'linux') { await run(exec, 'systemctl', ['--user', 'daemon-reload']); started = await run(exec, 'systemctl', ['--user', 'enable', '--now', NAME + '.service']); }
  else started = await run(exec, 'wscript', ['//nologo', t.file]);
  return { file: t.file, started };
}

async function uninstall(opts = {}) {
  const exec = opts.exec || execFile, t = targets(opts.platform, opts.env, opts.home);
  if (t.platform === 'darwin') await run(exec, 'launchctl', ['unload', '-w', t.file]);
  else if (t.platform === 'linux') await run(exec, 'systemctl', ['--user', 'disable', '--now', NAME + '.service']);
  const existed = fs.existsSync(t.file);
  try { fs.unlinkSync(t.file); } catch (_) { /* not installed */ }
  if (t.platform === 'linux') await run(exec, 'systemctl', ['--user', 'daemon-reload']);
  return { file: t.file, removed: existed };
}

function status(opts = {}) { const t = targets(opts.platform, opts.env, opts.home); return { file: t.file, installed: fs.existsSync(t.file) }; }

module.exports = { install, uninstall, status, targets, windowsScript, macPlist, systemdUnit, safePath };
