// A mix OS DSP core in the browser or Node: load amix_dsp.wasm and mix blocks of Float32Array.
//   import { loadAmix } from './amix.mjs'; const mix = await loadAmix(fetch('amix_dsp.wasm')); mix.init(2, 48000); mix.setGain(0, -6);
//   mix.input(0).set(samples); mix.process(256); const left = mix.output(0);
export async function loadAmix(source) {
  const bytes = source instanceof ArrayBuffer ? source : source instanceof Uint8Array ? source : await (await source).arrayBuffer();
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const e = instance.exports; const mem = () => e.memory.buffer;
  const view = (ptr, n) => new Float32Array(mem(), ptr, n);
  const max = e.amix_max_frames();
  return {
    maxFrames: max,
    init: (strips, fs) => e.amix_init(strips, fs),
    input: (strip, frames = max) => view(e.amix_in(strip), frames),
    output: (ch, frames = max) => view(e.amix_out(ch), frames),
    process: (frames) => e.amix_process(frames),
    setGain: (s, db) => e.amix_set_gain(s, db), setPan: (s, p) => e.amix_set_pan(s, p), setMute: (s, on) => e.amix_set_mute(s, on ? 1 : 0), setSolo: (s, on) => e.amix_set_solo(s, on ? 1 : 0),
    setEq: (s, band, type, f, q, gdb) => e.amix_set_eq(s, band, type, f, q, gdb), setMaster: (db) => e.amix_set_master(db), setLimiter: (on, ceilDb, relMs) => e.amix_set_limiter(on ? 1 : 0, ceilDb, relMs),
    peak: (s) => e.amix_peak(s), masterPeak: (ch) => e.amix_master_peak(ch),
  };
}
