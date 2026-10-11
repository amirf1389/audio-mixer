'use strict';
// The reference implementations: the code that runs in the page (index.html) and in the bridge, taken from there and run on blocks of samples.
// The engines in engine/ (C++, embedded C, Rust, WebAssembly) must give the same numbers; gen-vectors.js writes what these return.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const ROOT = path.resolve(__dirname, '..', '..');
const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
const slice = (a, b, from = 0) => { const i = html.indexOf(a, from), j = html.indexOf(b, i + a.length); if (i < 0 || j < 0) throw new Error('reference code not found: ' + a); return html.slice(i, j); };
const script = id => { const m = new RegExp('<script id="' + id + '">([\\s\\S]*?)</script>').exec(html); if (!m) throw new Error('script ' + id + ' not found'); return m[1]; };

const ctx = (extra = {}) => vm.createContext({ window: { state: {}, rtEngine: null }, document: { getElementById: () => null, addEventListener() {}, querySelector: () => null, querySelectorAll: () => [] },
  localStorage: { getItem: () => null, setItem() {} }, setInterval: () => 0, clearInterval() {}, requestAnimationFrame() {}, performance: { now: () => 0 }, Math, Float32Array, Float64Array, console, CustomEvent: function () {}, ...extra });

// ── loudness (rt-lufs): feed 2-channel blocks, read the results ──
function lufs(fsHz, blocks) {
  const c = ctx(); c.window.rtEngine = { on() {}, sink() { return null; } };
  vm.runInContext(script('rt-lufs'), c);
  const L = c.window.lufsMeter; L.setRate(fsHz); L.reset(); L.truePeak = -Infinity;
  for (const [l, r] of blocks) L.feed(l, r);
  return { momentary: L.momentary, shortTerm: L.shortTerm, integrated: L.integrated, lra: L.lra, truePeak: L.truePeak };
}

// ── level meters with clip detection (rt-engine: read / smooth / clip.detect) ──
function meters() {
  const body = slice('const rt = window.rtEngine = {', "    try { const s = localStorage.getItem('halx_clip_db')");
  const c = ctx({ LS: () => null, $: () => null, pct: v => v, dbOf: v => v, visible: () => false, esc: v => v });
  vm.runInContext(body + '\nthis.__rt = rt;', c);
  return c.__rt;
}

// ── phase correlation (readStereoPhase) ──
function phase(L, R, ps) {
  const fn = slice('window.readStereoPhase = function() {', '\n        };', html.indexOf('window.readStereoPhase = function() {')) + '\n        };';
  const size = L.length, mk = a => ({ fftSize: size, getFloatTimeDomainData(b) { b.set(a); } });
  const c = ctx(); c.window.state = { nodes: { phaseAnL: mk(L), phaseAnR: mk(R) } }; c.window.phaseScopeState = ps || {}; c.window.meterSource = null;
  vm.runInContext(fn, c);
  const r = c.window.readStereoPhase();
  return { corr: r.corr, peak: r.peak, rmsL: r.rmsL, rmsR: r.rmsR };
}

// ── scope sweep / trigger / readings (meter-source) ──
function scope(A, B, fsHz, st) {
  const c = ctx(); vm.runInContext(script('meter-source'), c);
  return c.window.meterSource.scopeCalc(A, B, fsHz, st);
}

// ── Int16 interleaved meter of the bridge (bridge/levels.js) ──
function s16() { return require(path.join(ROOT, 'bridge', 'levels.js')); }

module.exports = { lufs, meters, phase, scope, s16 };
