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

// Bluetooth headsets show up as TWO Windows devices: "Headphones (X Stereo)" (A2DP, stereo output) and "Headset (X Hands-Free AG Audio)"
// (HFP, mono 8 / 16 kHz microphone + speaker). They are one physical device: group them, write through A2DP, read through the HFP microphone.
const HFP = /hands[- ]?free|\bhfp\b|\bag audio\b/i;
const BT_HINT = /bluetooth|\ba2dp\b/i;
const btBase = (n, keepCase) => {
  const m = /\(([^()]+)\)\s*$/.exec(String(n || '').trim());
  const b = (m ? m[1] : String(n || '')).replace(/\s*(hands[- ]?free(\s+ag(\s+audio)?)?|ag audio|hfp|a2dp|stereo|bluetooth)\s*$/i, '').trim();
  return keepCase ? b : b.toLowerCase();
};
const isBluetooth = n => HFP.test(n || '') || BT_HINT.test(n || '');
const btProfile = n => (HFP.test(n || '') ? 'hfp' : 'a2dp');

// "Microphone (Focusrite USB Audio)" -> "Focusrite USB Audio"; "Focusrite USB ASIO" -> key "focusrite"
function baseName(n) {
  const m = /^(?:microphone|speakers|headphones|headset|line in|line out|line|input|output|digital audio(?: \(s\/pdif\))?|stereo mix|analog|aux)\s*\((.+)\)\s*$/i.exec(String(n || '').trim());
  return (m ? m[1] : String(n || '')).trim();
}
function keyOf(n) {
  const k = baseName(n).toLowerCase().replace(/\b(asio|wasapi|audio|driver|usb|input|output|microphone|speakers?|headphones?|line|in|out|device|class|compliant|\d+-?\d*)\b/g, ' ').replace(/[^a-z0-9]+/g, ' ').trim();
  return k || baseName(n).toLowerCase();
}

// MME and DirectSound cut device names at 31 characters ("Microphone (Focusrite USB Audi"), so the same interface listed by WASAPI under its full
// name ("Microphone (Focusrite USB Audio)") showed up a second time. A cut name is read as the longer name it is the start of.
const CUT = 28;
function untruncate(devices) {
  const names = [...new Set(devices.map(d => String(d.name || '')))];
  return devices.map(d => {
    const n = String(d.name || '');
    if (n.length < CUT || /\)\s*$/.test(n)) return d;
    const full = names.filter(m => m.length > n.length && m.toLowerCase().startsWith(n.toLowerCase())).sort((a, b) => a.length - b.length)[0];
    return full ? { ...d, name: full } : d;
  });
}

// An engine device (id >= 0) that could not be probed has no channels; the operating system's endpoint list knows the same device by name together with
// its direction and channel count: take the mode from there, so READ / WRITE are offered instead of "no input, no output".
function inferProbeFailed(devices) {
  return devices.map(d => {
    if (!d.probeFailed || d.inputs || d.outputs) return d;
    const n = String(d.name || '').toLowerCase();
    const e = devices.find(x => x !== d && !x.probeFailed && (x.inputs || x.outputs) && String(x.name || '').toLowerCase() === n);
    return e ? { ...d, inputs: e.inputs, outputs: e.outputs, inferred: true } : d;
  });
}

function groupInterfaces(devices = [], asioNames = []) {
  devices = inferProbeFailed(untruncate(devices));
  const groups = new Map();
  const hfpBases = new Set(devices.filter(d => HFP.test(d.name || '')).map(d => btBase(d.name)));
  for (const d of devices) {
    const isStereoBt = !HFP.test(d.name || '') && !LOOPBACK.test(d.name || '') && /\bstereo\)\s*$/i.test(d.name || '') && hfpBases.has(btBase(d.name));
    const bt = HFP.test(d.name || '') || BT_HINT.test(d.name || '') || isStereoBt;
    const key = bt && btBase(d.name) ? 'bt:' + btBase(d.name) : keyOf(d.name) + (LOOPBACK.test(d.name) ? ':loopback' : '');   // a loopback device is never merged into a real interface
    const g = groups.get(key) || { key, names: [], apis: [], inputs: 0, outputs: 0, loopback: false, systemDefault: false };
    if (isPrimary(d.name)) g.systemDefault = true;
    g.names.push(baseName(d.name));
    if (bt) g.bluetooth = true;
    g.apis.push({ api: d.hostApi || d.hostAPIName || 'default', deviceId: d.id, ...(bt ? { profile: btProfile(d.name) } : {}), inputs: d.inputs || 0, outputs: d.outputs || 0, sampleRate: d.sampleRate || null });
    g.inputs = Math.max(g.inputs, d.inputs || 0); g.outputs = Math.max(g.outputs, d.outputs || 0);
    if (LOOPBACK.test(d.name)) g.loopback = true;
    groups.set(key, g);
  }
  const out = [];
  for (const g of groups.values()) {
    // same host API: the A2DP stereo endpoint before the hands-free one (opening hands-free drops the headset to phone quality)
    g.apis.sort((a, b) => (apiRank(a.api) - apiRank(b.api)) || ((a.profile === 'hfp') - (b.profile === 'hfp')));
    const read = g.apis.find(a => a.inputs > 0) || null, write = g.apis.find(a => a.outputs > 0) || null;
    const asioDev = g.apis.find(a => /asio/i.test(a.api));
    const name0 = (asioDev ? g.names[g.apis.indexOf(asioDev)] : g.names.slice().sort((a, b) => b.length - a.length)[0]) || g.key;
    const name = g.bluetooth ? btBase(g.names[0], true) || name0 : name0;
    out.push({ key: g.key, name: g.systemDefault ? 'System default ' + (g.inputs ? 'input' : 'output') + ' (' + name + ')' : name, inputs: g.inputs, outputs: g.outputs, loopback: g.loopback, asio: !!asioDev, systemDefault: g.systemDefault, bluetooth: !!g.bluetooth, apis: g.apis, read, write });
  }
  // Installed ASIO drivers that PortAudio does not list (not installed yet, device unplugged, or PortAudio missing).
  for (const n of asioNames) {
    const k = keyOf(n);
    if (!groups.has(k)) out.push({ key: k, name: baseName(n), inputs: 0, outputs: 0, loopback: false, asio: true, apis: [], read: null, write: null, driverOnly: true });
  }
  return out.sort((a, b) => (a.systemDefault - b.systemDefault) || (a.loopback - b.loopback) || (b.asio - a.asio) || a.name.localeCompare(b.name));
}

module.exports = { inferProbeFailed, untruncate, isBluetooth, btBase, isPrimary, groupInterfaces, keyOf, baseName, apiRank };
