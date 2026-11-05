'use strict';
// Which devices are the SAME hardware seen through different drivers (an ASIO driver and its Windows endpoints). Used by the interface
// grouping (interfaces.js), the engine's ASIO -> WASAPI fallback (native-engine.js), the silence fallback (input.js) and the simulator.
//
// A chip / product family wins first, then shared name words. Families exist because a driver and its Windows endpoint often share NO word:
// the Behringer UM2 / UCA202 / UCA222 / Xenyx USB mixers (Burr-Brown PCM290x chip) are "BEHRINGER USB AUDIO" through ASIO but
// "Microphone (USB Audio CODEC)" through Windows, so a word match never paired them and the UM2 never got its WASAPI fallback.
const FAMILIES = [
  ['realtek', /realtek/i], ['conexant', /conexant|synaptics/i], ['idt', /\bidt\b|sigmatel|tempo/i], ['via', /\bvia\b|vinyl/i], ['cirrus', /cirrus/i],
  ['intel', /intel.*(smart sound|display audio)/i], ['amd', /\bamd\b.*(high definition|audio)/i],
  ['behringer', /behringer|\bumc ?\d|\bum ?2\b|\buca ?2\d\d|usb audio codec|burr[- ]?brown|\bpcm ?29\d\d/i],
  ['focusrite', /focusrite|scarlett/i], ['steinberg', /steinberg|yamaha ur\b|\bur\d{2}/i], ['presonus', /presonus|audiobox/i],
  ['motu', /\bmotu\b/i], ['rme', /\brme\b|fireface|babyface/i], ['audient', /audient|\bid4\b|\bid14\b/i], ['m-audio', /m-audio|m-track/i],
];
const WRAPPERS = /asio4all|flexasio|generic low latency|asio2wasapi/i;   // ASIO drivers that open some Windows endpoint themselves
const GENERIC = new Set(['asio', 'audio', 'driver', 'usb', 'device', 'speakers', 'microphone', 'line', 'input', 'output', 'high', 'definition', 'the', 'and', 'for',
  'generic', 'low', 'latency', 'array', 'headphones', 'codec', 'wdm', 'analogue', 'analog', 'digital', 'interface', 'sound', 'card']);

function family(name) { for (const [f, re] of FAMILIES) if (re.test(String(name || ''))) return f; return null; }
const isWrapper = name => WRAPPERS.test(String(name || ''));
const words = n => new Set(String(n || '').toLowerCase().split(/[^a-z0-9]+/).filter(w => w.length >= 3 && !GENERIC.has(w)));
// 100 for the same family, +10 per shared meaningful word; 0 = not the same hardware as far as names can tell
function score(a, b) {
  if (isWrapper(a) || isWrapper(b)) return 0;   // a wrapper is not tied to one device
  const fa = family(a), wa = words(a);
  return (fa && family(b) === fa ? 100 : 0) + [...words(b)].filter(x => wa.has(x)).length * 10;
}
// the best twin of `name` in `pool` (objects with .name), or null when nothing really matches (never just "the default device")
function bestTwin(name, pool, min = 10) {
  let best = null, s = 0;
  for (const d of pool || []) { const v = score(name, d.name); if (v > s) { s = v; best = d; } }
  return s >= min ? best : null;
}

module.exports = { FAMILIES, WRAPPERS, family, isWrapper, score, bestTwin, words };
