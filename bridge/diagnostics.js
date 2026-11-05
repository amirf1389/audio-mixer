'use strict';
// AUDIO DIAGNOSTICS: every driver step in plain language with a time, so a bug report can be READ ("bugs can be read, especially for ASIO").
// A ring buffer in memory (GET /api/diagnostics, the DIAGNOSTICS panel) plus a size-capped daily file in the app's log folder:
//   Windows %APPDATA%\AmF X1\logs\audio-YYYYMMDD.log, elsewhere ~/.audio-mixer/logs/ (or BRIDGE_LOG_DIR). Files above 1 MB roll to .1,
//   at most 7 files are kept. Personal data is redacted before anything is stored: user folder names, e-mail addresses, machine IDs.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const MAX_ENTRIES = 600, MAX_FILE = 1024 * 1024, KEEP_FILES = 7;
const ring = [];
let seq = 0, fileOff = process.env.BRIDGE_LOG_DIR === 'off';

// what each problem means and what to do: matched against the message, shown next to errors in the panel and used by the support assistant
const ADVICE = [
  [/in use by another program|busy|ASE_NotPresent|single[- ]client|held by/i, 'The ASIO driver serves one program at a time and another program (or another line of this mixer, or ASIO4ALL) holds it.', 'Close the other audio program, or use the Windows (WASAPI) twin of the same device: the mixer switches to it by itself when it can.'],
  [/delivered silence|all[- ]zero|no signal/i, 'The device opened but sends only silence.', 'Usually the ASIO driver is held elsewhere (same fix as above); otherwise check the input channel (the UM2 mic is on input 1 only), the gain knob and the cable.'],
  [/sample rate/i, 'The device does not run at the sample rate that was asked.', 'The mixer opens it at its own rate and converts; or set the same rate in the device control panel (UM2: 44.1 or 48 kHz).'],
  [/channels?/i, 'The device has fewer channels than were asked.', 'The mixer opens the channels the device has.'],
  [/unplugged|unavailable|disconnected/i, 'The device went away (unplugged, USB power saving, driver reset).', 'Plug it back in and press SCAN NOW; other devices keep playing.'],
  [/asio4all|wrapper/i, 'ASIO4ALL wraps Windows devices that may already be open through their own driver.', 'Prefer the device\'s own ASIO driver; the mixer does not open ASIO4ALL for a device that has one.'],
  [/xrun|dropout|underrun|overrun/i, 'Audio blocks were lost (the computer was too busy or the buffer is too small).', 'Raise the buffer size, close heavy programs, or turn on ECO mode.'],
];
function advise(msg) { for (const [re, means, todo] of ADVICE) if (re.test(msg)) return { means, todo }; return null; }

// user folders, e-mail addresses, machine IDs (XXXX-XXXX-XXXX-XXXX-XXXX) and long hex IDs never reach the log
function redact(s) {
  return String(s == null ? '' : s)
    .replace(/([A-Za-z]:\\Users\\)[^\\\s"']+/gi, '$1<user>').replace(/(\/(?:home|Users)\/)[^/\s"']+/g, '$1<user>')
    .replace(/[\w.+-]+@[\w-]+\.[\w.-]+/g, '<email>')
    .replace(/\b([0-9A-F]{4})(?:-[0-9A-F]{4}){4}\b/gi, '$1-…')
    .slice(0, 600);
}

function logDir() {
  if (process.env.BRIDGE_LOG_DIR && process.env.BRIDGE_LOG_DIR !== 'off') return process.env.BRIDGE_LOG_DIR;
  return process.platform === 'win32' && process.env.APPDATA ? path.join(process.env.APPDATA, 'AmF X1', 'logs') : path.join(os.homedir(), '.audio-mixer', 'logs');
}
function fileFor(d = new Date()) { return path.join(logDir(), 'audio-' + d.toISOString().slice(0, 10).replace(/-/g, '') + '.log'); }
function writeFile(line) {
  if (fileOff) return;
  try {
    const dir = logDir(); fs.mkdirSync(dir, { recursive: true });
    const f = fileFor();
    try { if (fs.statSync(f).size > MAX_FILE) fs.renameSync(f, f + '.1'); } catch (_) { /* new file */ }
    fs.appendFileSync(f, line + '\n', { mode: 0o600 });
    const old = fs.readdirSync(dir).filter(n => /^audio-\d{8}\.log(\.1)?$/.test(n)).sort().reverse();
    for (const n of old.slice(KEEP_FILES * 2)) fs.rmSync(path.join(dir, n), { force: true });
  } catch (_) { fileOff = true; /* a read-only disk never breaks the audio */ }
}

// level: 'info' | 'warn' | 'error'; area: 'scan' | 'asio' | 'wasapi' | 'stream' | 'fallback' | 'signal' | 'device' | 'sim' ...
function log(level, area, message, data) {
  const msg = redact(message), e = { seq: ++seq, at: new Date().toISOString(), level, area, message: msg };
  if (data && typeof data === 'object') { const d = {}; for (const [k, v] of Object.entries(data)) if (v !== undefined) d[k] = typeof v === 'string' ? redact(v) : v; e.data = d; }
  if (level !== 'info') { const a = advise(msg); if (a) e.advice = a; }
  ring.push(e); if (ring.length > MAX_ENTRIES) ring.shift();
  writeFile(`${e.at} ${level.toUpperCase().padEnd(5)} [${area}] ${msg}${e.data ? ' ' + JSON.stringify(e.data) : ''}${e.advice ? '  -> ' + e.advice.todo : ''}`);
  return e;
}
// filter: 'all' | 'errors' (warn + error) | an area or host API name ('asio', 'wasapi', ...); since: only entries after this seq
function list({ filter = 'all', since = 0, limit = 300 } = {}) {
  const f = String(filter || 'all').toLowerCase();
  return ring.filter(e => e.seq > since && (f === 'all' || (f === 'errors' ? e.level !== 'info' : e.area === f || (e.data && String(e.data.hostApi || '').toLowerCase().includes(f)) || e.message.toLowerCase().includes(f)))).slice(-limit);
}
// a plain-text report for a bug report (no personal data: everything in the ring is already redacted)
function report({ version = '', platform = process.platform } = {}) {
  const lines = [`AmF X1 ${version} audio diagnostics — ${new Date().toISOString()} — ${platform} ${os.release()} ${process.arch}`, ''];
  for (const e of ring) lines.push(`${e.at} ${e.level.toUpperCase().padEnd(5)} [${e.area}] ${e.message}${e.data ? ' ' + JSON.stringify(e.data) : ''}${e.advice ? '\n      what it means: ' + e.advice.means + '\n      what to do: ' + e.advice.todo : ''}`);
  return lines.join('\n') + '\n';
}
function clear() { ring.length = 0; }

module.exports = { log, list, report, clear, redact, advise, logDir, fileFor, info: (a, m, d) => log('info', a, m, d), warn: (a, m, d) => log('warn', a, m, d), error: (a, m, d) => log('error', a, m, d) };
