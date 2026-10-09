'use strict';
// "What is playing?": reads the operating system's media sessions and recognises the music service.
//   Windows: System Media Transport Controls (SMTC) via PowerShell + browser window titles
//   macOS:   Spotify / Music apps and the active tab of Chrome / Safari / Edge / Brave via osascript
//   Linux:   MPRIS over D-Bus (busctl): Spotify, TIDAL, YouTube / YouTube Music in Chrome, Firefox, VLC, ...
// Read-only. No accounts, tokens or network access are used.
const { execFile } = require('node:child_process');

// Order matters: the first match wins (YouTube Music before YouTube).
const SERVICES = [
  { id: 'youtube-music', name: 'YouTube Music', test: /music\.youtube\.com|youtube music|youtubemusic|youtube-music/i },
  { id: 'youtube', name: 'YouTube', test: /youtube\.com|youtu\.be|\byoutube\b/i },
  { id: 'spotify', name: 'Spotify', test: /spotify/i },
  { id: 'tidal', name: 'TIDAL', test: /tidal/i },
  { id: 'apple-music', name: 'Apple Music', test: /apple ?music|applemusic|music\.apple\.com|com\.apple\.music|^music$|itunes/i },
  { id: 'amazon-music', name: 'Amazon Music', test: /amazon ?music|music\.amazon/i },
  { id: 'deezer', name: 'Deezer', test: /deezer/i },
  { id: 'soundcloud', name: 'SoundCloud', test: /soundcloud/i },
  { id: 'qobuz', name: 'Qobuz', test: /qobuz/i },
  { id: 'pandora', name: 'Pandora', test: /pandora/i },
  { id: 'vlc', name: 'VLC', test: /\bvlc\b/i },
  { id: 'foobar2000', name: 'foobar2000', test: /foobar/i },
  { id: 'musicbee', name: 'MusicBee', test: /musicbee/i },
  { id: 'winamp', name: 'Winamp', test: /winamp/i },
];
const BROWSER = /chrome|chromium|msedge|edge|firefox|brave|opera|vivaldi|safari|arc/i;

function classify(parts) {
  const text = parts.filter(Boolean).join(' ');
  const hit = SERVICES.find(s => s.test.test(text));
  return hit ? { id: hit.id, name: hit.name } : { id: 'other', name: null };
}

function cleanApp(app) {
  return String(app || '').replace(/^org\.mpris\.MediaPlayer2\./, '').replace(/\.instance\d+$/, '').replace(/\.exe$/i, '').replace(/^com\.squirrel\./i, '').slice(0, 40) || 'unknown';
}

function mk(app, status, title, artist, album, extra = []) {
  const svc = classify([app, title, ...extra]);
  return {
    service: svc.id, serviceName: svc.name || cleanApp(app), app: cleanApp(app),
    status: /^play/i.test(status) ? 'playing' : /^paus/i.test(status) ? 'paused' : /^stop/i.test(status) ? 'stopped' : String(status || 'unknown').toLowerCase(),
    title: String(title || '').slice(0, 200), artist: String(artist || '').slice(0, 200), album: String(album || '').slice(0, 200),
  };
}

// ── parsers (pure, unit-tested) ──
function parseBusctlValue(text) { try { const j = JSON.parse(text); return j && j.data !== undefined ? j.data : null; } catch (_) { return null; } }

function mprisSession(busName, statusText, metaText) {
  const status = parseBusctlValue(statusText), meta = parseBusctlValue(metaText) || {};
  const val = k => (meta[k] && meta[k].data !== undefined ? meta[k].data : null);
  const artist = val('xesam:artist');
  return mk(busName, status || 'unknown', val('xesam:title') || '', Array.isArray(artist) ? artist.join(', ') : artist || '', val('xesam:album') || '', [val('xesam:url') || '']);
}

// Lines: SESSION|app|status|title|artist|album   and   WINDOW|process|title
function parseSmtc(text) {
  const wins = [], sess = [];
  for (const line of String(text || '').split(/\r?\n/)) {
    const p = line.split('|');
    if (p[0] === 'WINDOW' && p.length >= 3) wins.push({ proc: p[1], title: p.slice(2).join('|') });
    else if (p[0] === 'SESSION' && p.length >= 6) sess.push({ app: p[1], status: p[2], title: p[3], artist: p[4], album: p.slice(5).join('|') });
  }
  return sess.map(s => {
    // A browser session does not say which site: use the window titles ("... - YouTube - Google Chrome").
    const extra = BROWSER.test(s.app) ? wins.filter(w => BROWSER.test(w.proc) && (!s.title || w.title.includes(s.title.slice(0, 30)))).map(w => w.title) : [];
    return mk(s.app, s.status, s.title, s.artist, s.album, extra);
  });
}

// osascript lines: APP|state|title|artist|album   and   TAB|browser|url|title
function parseOsa(text) {
  const out = [];
  for (const line of String(text || '').split(/\r?\n/)) {
    const p = line.split('|');
    if (p[0] === 'APP' && p.length >= 5) out.push(mk(p[1], p[2], p[3], p[4], p.slice(5).join('|')));
    else if (p[0] === 'TAB' && p.length >= 4) {
      const svc = classify([p[2], p[3]]);
      if (svc.id !== 'other') out.push(mk(p[1], 'playing', p.slice(3).join('|').replace(/\s*[-|–]\s*(YouTube Music|YouTube|Spotify|TIDAL).*$/i, ''), '', '', [p[2], p[3]]));
    }
  }
  return out;
}

const SMTC_PS = `
$ErrorActionPreference = 'SilentlyContinue'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation\`1' })[0]
function Await($op, $type) { $t = $asTask.MakeGenericMethod($type).Invoke($null, @($op)); $t.Wait(-1) | Out-Null; $t.Result }
[Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType = WindowsRuntime] | Out-Null
$mgr = Await ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager])
foreach ($s in $mgr.GetSessions()) {
  $p = Await ($s.TryGetMediaPropertiesAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties])
  $st = $s.GetPlaybackInfo().PlaybackStatus
  "SESSION|$($s.SourceAppUserModelId)|$st|$($p.Title)|$($p.Artist)|$($p.AlbumTitle)"
}
Get-Process | Where-Object { $_.MainWindowTitle } | ForEach-Object { "WINDOW|$($_.ProcessName)|$($_.MainWindowTitle)" }
`;

const OSA = `
set out to ""
tell application "System Events" to set procs to name of every process
repeat with a in {"Spotify", "Music"}
  if procs contains a then
    try
      tell application a to set out to out & "APP|" & a & "|" & (player state as string) & "|" & (name of current track) & "|" & (artist of current track) & "|" & (album of current track) & linefeed
    end try
  end if
end repeat
repeat with b in {"Google Chrome", "Brave Browser", "Microsoft Edge"}
  if procs contains b then
    try
      tell application b to set out to out & "TAB|" & b & "|" & (URL of active tab of front window) & "|" & (title of active tab of front window) & linefeed
    end try
  end if
end repeat
if procs contains "Safari" then
  try
    tell application "Safari" to set out to out & "TAB|Safari|" & (URL of current tab of front window) & "|" & (name of current tab of front window) & linefeed
  end try
end if
return out
`;

function runCmd(cmd, args, timeout = 4000) {
  return new Promise(resolve => execFile(cmd, args, { timeout, windowsHide: true, maxBuffer: 2 * 1024 * 1024 }, (err, out) => resolve(err ? '' : String(out))));
}

async function readLinux(run) {
  const list = await run('busctl', ['--user', '--no-legend', 'list']);
  const names = list.split('\n').map(l => l.split(/\s+/)[0]).filter(n => /^org\.mpris\.MediaPlayer2\./.test(n)).slice(0, 8);
  const out = [];
  for (const n of names) {
    const base = ['--user', '--json=short', 'get-property', n, '/org/mpris/MediaPlayer2', 'org.mpris.MediaPlayer2.Player'];
    const [st, meta] = await Promise.all([run('busctl', [...base, 'PlaybackStatus']), run('busctl', [...base, 'Metadata'])]);
    if (st || meta) out.push(mprisSession(n, st, meta));
  }
  return { method: 'mpris', sessions: out };
}

async function readNowPlaying({ platform = process.platform, run = runCmd } = {}) {
  let r;
  if (platform === 'linux') r = await readLinux(run);
  else if (platform === 'darwin') r = { method: 'osascript', sessions: parseOsa(await run('osascript', ['-e', OSA], 6000)) };
  else if (platform === 'win32') r = { method: 'smtc', sessions: parseSmtc(await run('powershell', ['-NoProfile', '-Command', SMTC_PS], 9000)) };
  else r = { method: 'unsupported', sessions: [] };
  const rank = s => (s.status === 'playing' ? 0 : s.status === 'paused' ? 1 : 2);
  r.sessions = r.sessions.filter(s => s.title || s.service !== 'other').sort((a, b) => rank(a) - rank(b));
  return { platform, ...r, playing: r.sessions.find(s => s.status === 'playing') || null };
}

let cache = { t: 0, v: null };
async function cachedNowPlaying(opts) {
  if (Date.now() - cache.t < 1500 && cache.v) return cache.v;
  const v = await readNowPlaying(opts);
  cache = { t: Date.now(), v };
  return v;
}

module.exports = { SERVICES, classify, parseBusctlValue, mprisSession, parseSmtc, parseOsa, readNowPlaying, cachedNowPlaying, SMTC_PS, OSA };
