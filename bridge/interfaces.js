'use strict';
// Groups the PortAudio devices of every host API (ASIO, WASAPI, DirectSound, WDM-KS, MME, Core Audio, ALSA, JACK ...)
// into physical audio interfaces, so each interface shows once with all the ways it can be opened.
const API_RANK = [/asio/i, /wasapi/i, /core ?audio/i, /jack/i, /alsa/i, /wdm|kernel/i, /direct ?sound/i, /mme/i];
const apiRank = a => { const i = API_RANK.findIndex(r => r.test(a || '')); return i < 0 ? API_RANK.length : i; };
const LOOPBACK = /stereo mix|what u hear|wave out mix|monitor of|loopback|virtual cable|cable output|blackhole|soundflower|voicemeeter out/i;

// DirectSound's "Primary Sound Capture Driver" / "Primary Sound Driver" are Windows' default-device mappers, not hardware: they duplicate
// the real default device and often fail to open. They are listed last as the system default and never enabled automatically.
const PRIMARY = /^primary sound (capture )?driver$/i;
const isPrimary = n => PRIMARY.test(String(n || '').trim());

// "Microphone (Focusrite USB Audio)" -> "Focusrite USB Audio"; "Focusrite USB ASIO" -> key "focusrite"
function baseName(n) {
  const m = /^(?:microphone|speakers|headphones|headset|line in|line out|line|input|output|digital audio(?: \(s\/pdif\))?|stereo mix|analog|aux)\s*\((.+)\)\s*$/i.exec(String(n || '').trim());
  return (m ? m[1] : String(n || '')).trim();
}
function keyOf(n) {
  const k = baseName(n).toLowerCase().replace(/\b(asio|wasapi|audio|driver|usb|input|output|microphone|speakers?|headphones?|line|in|out|device|class|compliant|\d+-?\d*)\b/g, ' ').replace(/[^a-z0-9]+/g, ' ').trim();
  return k || baseName(n).toLowerCase();
}

function groupInterfaces(devices = [], asioNames = []) {
  const groups = new Map();
  for (const d of devices) {
    const key = keyOf(d.name) + (LOOPBACK.test(d.name) ? ':loopback' : '');   // a loopback device is never merged into a real interface
    const g = groups.get(key) || { key, names: [], apis: [], inputs: 0, outputs: 0, loopback: false, systemDefault: false };
    if (isPrimary(d.name)) g.systemDefault = true;
    g.names.push(baseName(d.name));
    g.apis.push({ api: d.hostApi || d.hostAPIName || 'default', deviceId: d.id, inputs: d.inputs || 0, outputs: d.outputs || 0, sampleRate: d.sampleRate || null });
    g.inputs = Math.max(g.inputs, d.inputs || 0); g.outputs = Math.max(g.outputs, d.outputs || 0);
    if (LOOPBACK.test(d.name)) g.loopback = true;
    groups.set(key, g);
  }
  const out = [];
  for (const g of groups.values()) {
    g.apis.sort((a, b) => apiRank(a.api) - apiRank(b.api));
    const read = g.apis.find(a => a.inputs > 0) || null, write = g.apis.find(a => a.outputs > 0) || null;
    const asioDev = g.apis.find(a => /asio/i.test(a.api));
    const name = (asioDev ? g.names[g.apis.indexOf(asioDev)] : g.names.slice().sort((a, b) => b.length - a.length)[0]) || g.key;
    out.push({ key: g.key, name: g.systemDefault ? 'System default ' + (g.inputs ? 'input' : 'output') + ' (' + name + ')' : name, inputs: g.inputs, outputs: g.outputs, loopback: g.loopback, asio: !!asioDev, systemDefault: g.systemDefault, apis: g.apis, read, write });
  }
  // Installed ASIO drivers that PortAudio does not list (not installed yet, device unplugged, or PortAudio missing).
  for (const n of asioNames) {
    const k = keyOf(n);
    if (!groups.has(k)) out.push({ key: k, name: baseName(n), inputs: 0, outputs: 0, loopback: false, asio: true, apis: [], read: null, write: null, driverOnly: true });
  }
  return out.sort((a, b) => (a.systemDefault - b.systemDefault) || (a.loopback - b.loopback) || (b.asio - a.asio) || a.name.localeCompare(b.name));
}

module.exports = { isPrimary, groupInterfaces, keyOf, baseName, apiRank };
