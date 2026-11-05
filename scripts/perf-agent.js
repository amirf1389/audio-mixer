'use strict';
// Loaded into the profiled process with --require by scripts/perf-controller.js. It reports V8 heap facts to the controller over IPC and
// answers two requests: a garbage collection (needs --expose-gc) for a clean post-GC baseline, and a heap snapshot written to a given path.
// Nothing here runs unless the process was started by the controller (an IPC channel is present).
const v8 = require('node:v8');

if (typeof process.send === 'function') {
  const stats = () => {
    const h = v8.getHeapStatistics(), m = process.memoryUsage();
    return { rss: m.rss, heapUsed: m.heapUsed, heapTotal: m.heapTotal, external: m.external, arrayBuffers: m.arrayBuffers,
      heapLimit: h.heap_size_limit, detachedContexts: h.number_of_detached_contexts, nativeContexts: h.number_of_native_contexts };
  };
  process.on('message', msg => {
    if (!msg || typeof msg !== 'object' || !msg.perf) return;
    try {
      if (msg.perf === 'sample') {
        let gc = false;
        if (msg.gc && typeof global.gc === 'function') { global.gc(); gc = true; }   // post-collection baseline: only what is really still referenced
        process.send({ perf: 'sample', id: msg.id, gc, at: Date.now(), ...stats() });
      } else if (msg.perf === 'exit') {
        setTimeout(() => process.exit(0), 50);   // a normal exit, so --heap-prof writes its .heapprofile
      } else if (msg.perf === 'snapshot') {
        const file = v8.writeHeapSnapshot(msg.path);
        process.send({ perf: 'snapshot', id: msg.id, file });
      }
    } catch (e) { process.send({ perf: 'error', id: msg.id, error: String(e && e.message || e) }); }
  });
  process.send({ perf: 'ready', pid: process.pid, node: process.version, ...stats() });
}
