// node test.mjs: a -6 dB fader on a 0.5 peak tone, centre pan -> 0.177; EQ boost at 1 kHz; mute; limiter ceiling.
import { readFileSync } from 'node:fs'; import { loadAmix } from './amix.mjs';
const mix = await loadAmix(readFileSync(new URL('./amix_dsp.wasm', import.meta.url)));
mix.init(2, 48000); mix.setLimiter(false, -0.5, 100); mix.setGain(0, -6); mix.setPan(0, 0);
const tone = Float32Array.from({ length: 256 }, (_, i) => 0.5 * Math.sin(2 * Math.PI * 1000 * i / 48000));
let pk = 0; for (let k = 0; k < 20; ++k) { mix.input(0, 256).set(tone); mix.process(256); for (const v of mix.output(0, 256)) pk = Math.max(pk, Math.abs(v)); }
const check = (c, m) => { if (!c) { console.error('FAIL', m); process.exit(1); } };
check(Math.abs(pk - 0.177) < 0.01, `fader + pan peak ${pk}`);
check(Math.abs(mix.peak(0) - 0.25) < 0.01, `strip meter ${mix.peak(0)}`);
mix.setEq(0, 0, 1, 1000, 1.0, 6); let pk2 = 0; for (let k = 0; k < 20; ++k) { mix.input(0, 256).set(tone); mix.process(256); for (const v of mix.output(0, 256)) pk2 = Math.max(pk2, Math.abs(v)); }
check(pk2 > pk * 1.8 && pk2 < pk * 2.1, `+6 dB peaking at the tone: ${pk2} vs ${pk}`);
mix.setEq(0, 0, 0, 1000, 1, 0); mix.setLimiter(true, -6, 50); mix.setGain(0, 10); let pk3 = 0; for (let k = 0; k < 40; ++k) { mix.input(0, 256).set(tone); mix.process(256); for (const v of mix.output(0, 256)) pk3 = Math.max(pk3, Math.abs(v)); }
check(pk3 <= 0.5012 + 1e-3, `limiter ceiling -6 dB: ${pk3}`);
mix.setMute(0, true); mix.input(0, 256).set(tone); mix.process(256); check(Math.abs(mix.output(0, 256)[100]) < 1e-7, 'mute');
console.log(`amix_dsp.wasm OK: fader/pan ${pk.toFixed(3)}, eq ${pk2.toFixed(3)}, limited ${pk3.toFixed(3)}, ${mix.maxFrames} max frames`);
