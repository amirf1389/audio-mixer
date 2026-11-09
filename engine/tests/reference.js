'use strict';
// The reference implementations: the code that ran in the web console (index.html, retired at git tag html-app-final; the four pieces are kept verbatim in
// reference-web/) and in the bridge, run on blocks of samples.
// The engines in engine/ (C++, embedded C, Rust, WebAssembly) must give the same numbers; gen-vectors.js writes what these return.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const ROOT = path.resolve(__dirname, '..', '..');
const web = name => fs.readFileSync(path.join(__dirname, 'reference-web', name + '.js'), 'utf8');

const ctx = (extra = {}) => vm.createContext({ window: { state: {}, rtEngine: null }, document: { getElementById: () => null, addEventListener() {}, querySelector: () => null, querySelectorAll: () => [] },
  localStorage: { getItem: () => null, setItem() {} }, setInterval: () => 0, clearInterval() {}, requestAnimationFrame() {}, performance: { now: () => 0 }, Math, Float32Array, Float64Array, console, CustomEvent: function () {}, ...extra });

// ── loudness (rt-lufs): feed 2-channel blocks, read the results ──
function lufs(fsHz, blocks) {
  const c = ctx(); c.window.rtEngine = { on() {}, sink() { return null; } };
  vm.runInContext(web('rt-lufs'), c);
  const L = c.window.lufsMeter; L.setRate(fsHz); L.reset(); L.truePeak = -Infinity;
  for (const [l, r] of blocks) L.feed(l, r);
  return { momentary: L.momentary, shortTerm: L.shortTerm, integrated: L.integrated, lra: L.lra, truePeak: L.truePeak };
}

// ── level meters with clip detection (rt-engine: read / smooth / clip.detect) ──
function meters() {
  const body = web('rt-engine');
  const c = ctx({ LS: () => null, $: () => null, pct: v => v, dbOf: v => v, visible: () => false, esc: v => v });
  vm.runInContext(body + '\nthis.__rt = rt;', c);
  return c.__rt;
}

// ── phase correlation (readStereoPhase) ──
function phase(L, R, ps) {
  const fn = web('phase');
  const size = L.length, mk = a => ({ fftSize: size, getFloatTimeDomainData(b) { b.set(a); } });
  const c = ctx(); c.window.state = { nodes: { phaseAnL: mk(L), phaseAnR: mk(R) } }; c.window.phaseScopeState = ps || {}; c.window.meterSource = null;
  vm.runInContext(fn, c);
  const r = c.window.readStereoPhase();
  return { corr: r.corr, peak: r.peak, rmsL: r.rmsL, rmsR: r.rmsR };
}

// ── scope sweep / trigger / readings (meter-source) ──
function scope(A, B, fsHz, st) {
  const c = ctx(); vm.runInContext(web('meter-source'), c);
  return c.window.meterSource.scopeCalc(A, B, fsHz, st);
}

// ── Int16 interleaved meter of the bridge (bridge/levels.js) ──
function s16() { return require(path.join(ROOT, 'bridge', 'levels.js')); }

module.exports = { lufs, meters, phase, scope, s16 };
