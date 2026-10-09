'use strict';
// Read-only system volume / mute of the default output and input devices (per OS, fails soft).
const { execFile } = require('node:child_process');

function run(cmd, args, timeout = 3000) {
  return new Promise(resolve => execFile(cmd, args, { timeout, windowsHide: true }, (err, out) => resolve(err ? '' : String(out))));
}

// ---- parsers (pure, unit-tested) ----
function parsePactlVolume(text) {       // "Volume: front-left: 42000 /  64% / -11.9 dB, ..."
  const m = /(\d+)%/.exec(text || '');
  return m ? Math.min(1.5, parseInt(m[1], 10) / 100) : null;
}
function parsePactlMute(text) { const m = /Mute:\s*(yes|no)/i.exec(text || ''); return m ? m[1].toLowerCase() === 'yes' : null; }
function parseMacVolume(text) {          // "output volume:50, input volume:75, alert volume:100, output muted:false"
  const g = k => { const m = new RegExp(k + ':(\\w+)').exec(text || ''); return m ? m[1] : null; };
  const num = v => (v && /^\d+$/.test(v) ? parseInt(v, 10) / 100 : null);
  return { output: { volume: num(g('output volume')), muted: g('output muted') === null ? null : g('output muted') === 'true' },
           input: { volume: num(g('input volume')), muted: null } };
}
function parseWinVolume(text) {          // "out 0.65 False" / "in 0.8 True" lines
  const res = { output: { volume: null, muted: null }, input: { volume: null, muted: null } };
  String(text || '').split(/\r?\n/).forEach(l => {
    const m = /^(out|in)\s+([\d.]+)\s+(True|False)/i.exec(l.trim());
    if (m) res[m[1].toLowerCase() === 'out' ? 'output' : 'input'] = { volume: parseFloat(m[2]), muted: m[3].toLowerCase() === 'true' };
  });
  return res;
}

const WIN_PS = `
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
[Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioEndpointVolume { int f(); int g(); int h(); int i(); int SetMasterVolumeLevelScalar(float fLevel, System.Guid pguidEventContext); int j(); int GetMasterVolumeLevelScalar(out float pfLevel); int k(); int l(); int m(); int n(); int SetMute([MarshalAs(UnmanagedType.Bool)] bool bMute, System.Guid pguidEventContext); int GetMute(out bool pbMute); }
[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice { int Activate(ref System.Guid id, int clsCtx, int activationParams, out IAudioEndpointVolume aev); }
[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator { int f(); int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice endpoint); }
[ComImport, Guid("BCDE0395-E52F-46C5-B0DE-53D3F5A83D9F")] class MMDeviceEnumeratorComObject { }
public class AudioVol {
  static IAudioEndpointVolume Ep(int flow) { var e = new MMDeviceEnumeratorComObject() as IMMDeviceEnumerator; IMMDevice d = null; Marshal.ThrowExceptionForHR(e.GetDefaultAudioEndpoint(flow, 1, out d)); IAudioEndpointVolume v = null; var id = typeof(IAudioEndpointVolume).GUID; Marshal.ThrowExceptionForHR(d.Activate(ref id, 23, 0, out v)); return v; }
  public static string Get(int flow) { try { var v = Ep(flow); float lv; bool m; v.GetMasterVolumeLevelScalar(out lv); v.GetMute(out m); return lv.ToString(System.Globalization.CultureInfo.InvariantCulture) + " " + m; } catch { return ""; } }
}
'@
$o = [AudioVol]::Get(0); if ($o) { "out $o" }
$i = [AudioVol]::Get(1); if ($i) { "in $i" }
`;

let winCache = { t: 0, v: null };
async function readVolume() {
  const p = process.platform;
  const empty = { output: { volume: null, muted: null }, input: { volume: null, muted: null } };
  if (p === 'win32') {
    if (Date.now() - winCache.t < 800 && winCache.v) return winCache.v;   // PowerShell start-up is slow; throttle
    const v = parseWinVolume(await run('powershell', ['-NoProfile', '-Command', WIN_PS], 8000));
    winCache = { t: Date.now(), v };
    return { ...v, source: 'wasapi-endpoint' };
  }
  if (p === 'darwin') {
    return { ...parseMacVolume(await run('osascript', ['-e', 'get volume settings'])), source: 'osascript' };
  }
  if (p === 'linux') {
    const [ov, om, iv, im] = await Promise.all([
      run('pactl', ['get-sink-volume', '@DEFAULT_SINK@']), run('pactl', ['get-sink-mute', '@DEFAULT_SINK@']),
      run('pactl', ['get-source-volume', '@DEFAULT_SOURCE@']), run('pactl', ['get-source-mute', '@DEFAULT_SOURCE@']),
    ]);
    return { output: { volume: parsePactlVolume(ov), muted: parsePactlMute(om) }, input: { volume: parsePactlVolume(iv), muted: parsePactlMute(im) }, source: 'pactl' };
  }
  return { ...empty, source: 'unsupported' };
}

module.exports = { readVolume, parsePactlVolume, parsePactlMute, parseMacVolume, parseWinVolume };
