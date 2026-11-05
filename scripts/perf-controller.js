#!/usr/bin/env node
'use strict';
// Node.js V8 + hardware performance profile controller for AmF X1 (Windows first, works on Linux and macOS).
//   node scripts/perf-controller.js [--entry bridge/server.js] [--interval 5000] [--growth 15] [--samples 3] [--heap 8192] [--out ./profiles] [--duration 0]
// What it does:
//   1. Detects the processor (AMD or Intel) and picks the matching hardware profile: AMD uProf (core P-states, L3 cache) or Intel VTune / PCM
//      (cache lines, DDR channel bandwidth).
//   2. Starts the app with NODE_OPTIONS="--max-old-space-size=<heap> --heap-prof --heap-prof-dir=<out> --expose-gc" and the perf agent
//      (scripts/perf-agent.js, --require), which reports v8.getHeapStatistics() over IPC.
//   3. Every interval: forces a GC in the app (global.gc) and reads the post-GC heap. A MEMORY TRAP fires when the post-GC heap grew more than
//      --growth % over the first baseline for --samples samples in a row, or when number_of_detached_contexts rises (a closure / iframe leak).
//      On a trap it writes ./profiles/<Engine>-<timestamp>.heapsnapshot and logs the report below.
//   4. Hardware telemetry comes from the native addon hw_perf_engine.node when it is present (build/Release/, native/, or next to this script),
//      which must export sample() -> { l3MissRate, busBandwidthMBs, busStalls } using the vendor driver (uProf / PCM). Without it the hardware
//      line says "not available": no number is ever estimated. A HARDWARE TRAP fires on an L3 miss rate above 3 %.
// Report format:
//   Runtime Metrics: RSS: X MB | Heap Used: Y MB / Max MB | Detached Contexts: N
//   Hardware Telemetry: Engine Active: AMD|Intel | L3 Miss Rate: X% | Bus Bandwidth: Y MB/s | Bus Stalls: N
//   Artifact Path: Saved to ./profiles/<Engine>-<Timestamp>.heapsnapshot
// The --heap-prof sampling profile (.heapprofile) is also written by Node into the same folder when the app exits.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { fork } = require('node:child_process');

const ROOT = path.resolve(__dirname, '..');
const arg = (a, n, d) => { const i = a.indexOf(n); return i >= 0 && a[i + 1] !== undefined ? a[i + 1] : d; };
const MB = b => (b / 1048576).toFixed(1);

// AMD or Intel, from the CPU model string (Windows also gives PROCESSOR_IDENTIFIER)
function detectCpu({ cpus = os.cpus(), env = process.env } = {}) {
  const model = String((cpus[0] && cpus[0].model) || '') + ' ' + String(env.PROCESSOR_IDENTIFIER || '');
  const vendor = /AMD|Ryzen|EPYC|Threadripper|AuthenticAMD/i.test(model) ? 'AMD' : /Intel|GenuineIntel|Xeon|Core\(TM\)/i.test(model) ? 'Intel' : 'Unknown';
  const profile = vendor === 'AMD' ? 'AMD uProf (core P-states, L3 cache alignment)' : vendor === 'Intel' ? 'Intel VTune / PCM (cache lines, DDR channel bandwidth)' : 'none (unknown processor)';
  return { vendor, model: model.trim(), cores: cpus.length, profile };
}

// the optional native hardware binding; null when it is not built on this machine
function loadHardware({ dirs = [path.join(ROOT, 'build', 'Release'), path.join(ROOT, 'native'), __dirname], req = require } = {}) {
  for (const d of dirs) {
    const f = path.join(d, 'hw_perf_engine.node');
    if (!fs.existsSync(f)) continue;
    try { const m = req(f); if (m && typeof m.sample === 'function') return { file: f, sample: () => m.sample() }; } catch (_) { /* a binding that does not load counts as absent */ }
  }
  return null;
}

// post-GC growth and detached-context traps over a series of samples
function makeTraps({ growthPct = 15, samples = 3 } = {}) {
  let base = null, over = 0, lastDetached = 0;
  return s => {
    const reasons = [];
    if (base === null) { base = s.heapUsed; lastDetached = s.detachedContexts; return reasons; }
    if (s.heapUsed > base * (1 + growthPct / 100)) { if (++over >= samples) { reasons.push(`post-GC heap grew ${(((s.heapUsed / base) - 1) * 100).toFixed(0)}% over its baseline for ${over} samples`); over = 0; base = s.heapUsed; } }
    else over = 0;
    if (s.detachedContexts > lastDetached) reasons.push(`detached contexts rose to ${s.detachedContexts}`);
    lastDetached = s.detachedContexts;
    return reasons;
  };
}

function formatReport({ s, cpu, hw, artifact }) {
  const lines = [`Runtime Metrics: RSS: ${MB(s.rss)} MB | Heap Used: ${MB(s.heapUsed)} MB / ${MB(s.heapLimit)} MB | Detached Contexts: ${s.detachedContexts}`];
  lines.push(hw && !hw.error
    ? `Hardware Telemetry: Engine Active: ${cpu.vendor} | L3 Miss Rate: ${Number(hw.l3MissRate).toFixed(2)}% | Bus Bandwidth: ${Math.round(hw.busBandwidthMBs)} MB/s | Bus Stalls: ${hw.busStalls}`
    : `Hardware Telemetry: Engine Active: ${cpu.vendor} | L3 Miss Rate: not available | Bus Bandwidth: not available | Bus Stalls: not available (${hw && hw.error ? hw.error : 'no hw_perf_engine.node: build it against ' + (cpu.vendor === 'AMD' ? 'AMD uProf' : 'Intel PCM / VTune')})`);
  if (artifact) lines.push(`Artifact Path: Saved to ${artifact}`);
  return lines.join('\n');
}

function run({ argv = process.argv.slice(2), log = console.log } = {}) {
  const entry = path.resolve(ROOT, arg(argv, '--entry', 'bridge/server.js'));
  const out = path.resolve(arg(argv, '--out', path.join(ROOT, 'profiles')));
  const interval = Math.max(500, Number(arg(argv, '--interval', 5000)));
  const duration = Number(arg(argv, '--duration', 0));
  const heap = Math.max(256, Number(arg(argv, '--heap', 8192)));
  fs.mkdirSync(out, { recursive: true });
  const cpu = detectCpu(), hwMod = loadHardware();
  log(`perf-controller: ${cpu.vendor} processor (${cpu.model || 'unknown model'}, ${cpu.cores} threads) -> hardware profile: ${cpu.profile}`);
  log(`perf-controller: hardware binding: ${hwMod ? hwMod.file : 'not found (V8 profiling only)'}`);
  const nodeOptions = [process.env.NODE_OPTIONS || '', `--max-old-space-size=${heap}`, '--heap-prof', `--heap-prof-dir=${out}`, '--expose-gc'].join(' ').trim();
  const child = fork(entry, argv.slice(argv.indexOf('--') >= 0 ? argv.indexOf('--') + 1 : argv.length), {
    env: { ...process.env, NODE_OPTIONS: nodeOptions }, execArgv: ['--require', path.join(__dirname, 'perf-agent.js')], stdio: ['inherit', 'inherit', 'inherit', 'ipc'],
  });
  log(`perf-controller: started ${path.relative(ROOT, entry)} (pid ${child.pid}) with NODE_OPTIONS="${nodeOptions}"`);
  const trap = makeTraps({ growthPct: Number(arg(argv, '--growth', 15)), samples: Number(arg(argv, '--samples', 3)) });
  let id = 0, timer = null, stopAt = duration ? Date.now() + duration : 0;
  const pending = new Map();
  const ask = (msg) => new Promise(res => { const k = ++id; pending.set(k, res); child.send({ ...msg, id: k }); });
  child.on('message', m => { if (m && m.perf && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } });
  const tick = async () => {
    const s = await ask({ perf: 'sample', gc: true });
    if (s.perf !== 'sample') return;
    let hw = null;
    if (hwMod) { try { hw = hwMod.sample(); } catch (e) { hw = { error: String(e.message || e) }; } }
    const reasons = trap(s);
    if (hw && !hw.error && hw.l3MissRate > 3) reasons.push(`L3 miss rate ${Number(hw.l3MissRate).toFixed(2)}% (> 3%): unaligned structures or cross-thread cache bouncing`);
    if (reasons.length) {
      const file = path.join(out, `${cpu.vendor}-${new Date().toISOString().replace(/[:.]/g, '-')}.heapsnapshot`);
      const r = await ask({ perf: 'snapshot', path: file });
      log(`\nTRAP: ${reasons.join('; ')}\n` + formatReport({ s, cpu, hw, artifact: r.file ? (path.resolve(r.file).startsWith(process.cwd() + path.sep) ? './' + path.relative(process.cwd(), r.file).split(path.sep).join('/') : r.file) : null }));
    } else log(formatReport({ s, cpu, hw }));
    if (stopAt && Date.now() >= stopAt) stop();
  };
  // stop: ask the app to exit normally (so Node writes the --heap-prof profile), force it after 5 s
  const stop = () => { clearInterval(timer); try { child.send({ perf: 'exit' }); } catch (_) { /* gone */ } setTimeout(() => { try { child.kill(); } catch (_) { /* gone */ } }, 5000).unref(); };
  process.once('SIGINT', stop);
  child.on('message', m => { if (m && m.perf === 'ready' && !timer) { tick(); timer = setInterval(tick, interval); } });
  child.on('exit', code => { clearInterval(timer); log(`perf-controller: app exited (${code}); V8 heap profiles (.heapprofile) are in ${out}`); });
  return child;
}

if (require.main === module) run();
module.exports = { detectCpu, loadHardware, makeTraps, formatReport, run };
