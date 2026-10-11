'use strict';
// Deterministic test signals: every engine (JavaScript, C, C++, Rust) builds the same samples from the same spec string.
//   sine:F:A          A sin(2 pi F n / fs), both channels         sinelr:F1:A1:F2:A2   different left and right
//   inv:F:A           right = -left                                square:F:A           A sign(sin)
//   noise:SEED:A      A (2 u - 1), u from a 32-bit linear congruential generator (seed + channel)
//   clipsine:F:A      the sine clamped to -1..1 (flat tops)        silence
//   alt:F:A1:A2:SEG   sine whose amplitude alternates between A1 and A2 every SEG seconds (A1 first)
//   fade:F:A1:A2:SEC  sine whose amplitude runs from A1 to A2 over SEC seconds, then stays
// A float is the nearest float32 (Math.fround): the engines cast the same double.
const TWO_PI = 2 * Math.PI;

function gen(spec, fs, n, ch) {
  const p = String(spec).split(':'), kind = p[0], out = new Float32Array(n), num = i => Number(p[i]);
  const sine = (f, a) => { for (let i = 0; i < n; i++) out[i] = a * Math.sin(TWO_PI * f * i / fs); };
  switch (kind) {
    case 'silence': break;
    case 'sine': sine(num(1), num(2)); break;
    case 'sinelr': sine(ch ? num(3) : num(1), ch ? num(4) : num(2)); break;
    case 'inv': sine(num(1), ch ? -num(2) : num(2)); break;
    case 'square': for (let i = 0; i < n; i++) out[i] = num(2) * (Math.sin(TWO_PI * num(1) * i / fs) >= 0 ? 1 : -1); break;
    case 'clipsine': for (let i = 0; i < n; i++) { const v = num(2) * Math.sin(TWO_PI * num(1) * i / fs); out[i] = v > 1 ? 1 : v < -1 ? -1 : v; } break;
    case 'noise': { let s = (num(1) + ch) >>> 0; for (let i = 0; i < n; i++) { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; out[i] = num(2) * ((s >>> 8) / 16777216 * 2 - 1); } break; }
    case 'alt': for (let i = 0; i < n; i++) out[i] = (Math.floor(i / (num(4) * fs)) % 2 ? num(3) : num(2)) * Math.sin(TWO_PI * num(1) * i / fs); break;
    case 'fade': for (let i = 0; i < n; i++) { const t = Math.min(1, i / (num(4) * fs)); out[i] = (num(2) + (num(3) - num(2)) * t) * Math.sin(TWO_PI * num(1) * i / fs); } break;
    default: throw new Error('unknown signal ' + spec);
  }
  return out;
}
// Int16 version (the bridge's streams): the float, scaled by 32767, rounded half away from zero
function genS16(spec, fs, frames, channels) {
  const out = new Int16Array(frames * channels);
  for (let c = 0; c < channels; c++) { const x = gen(spec, fs, frames, c & 1); for (let i = 0; i < frames; i++) { const v = x[i] * 32767; out[i * channels + c] = Math.trunc(v + (v >= 0 ? 0.5 : -0.5)); } }
  return out;
}
module.exports = { gen, genS16 };
