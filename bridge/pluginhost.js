'use strict';
// Native plugin host: runs a VST 2.x effect (.dll / .vst) in its own process (windows/plugin-host/src/PluginHost.cpp) and moves audio through it.
// A plugin that crashes takes down only that process. Only plugins found by the bridge's own scan are ever started (see createInsertSession).
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const plugins = require('./plugins');
const inserts = require('./inserts');
const license = require('./license');

const ROOT = path.resolve(__dirname, '..');
const MAX_HOSTS = 12;                 // plugin processes at the same time
const MAX_PENDING = 3;                // audio blocks in flight per plugin: more are dropped, which keeps latency bounded
let running = 0;

// Windows ships PluginHost.exe for both architectures; macOS / Linux build it (windows/plugin-host/src/PluginHost.cpp) or point BRIDGE_PLUGIN_HOST at it.
function hostPath({ platform = process.platform, arch = process.arch, env = process.env, exists = fs.existsSync } = {}) {
  if (env.BRIDGE_PLUGIN_HOST) return exists(env.BRIDGE_PLUGIN_HOST) ? env.BRIDGE_PLUGIN_HOST : null;
  // an installed package keeps the host in native/host, the repository in windows/plugin-host
  for (const dir of [path.join(ROOT, 'native', 'host'), path.join(ROOT, 'windows', 'plugin-host')]) {
    const p = platform === 'win32' ? path.join(dir, arch === 'ia32' ? 'x86' : 'x64', 'PluginHost.exe') : path.join(dir, `${platform}-${arch}`, 'PluginHost');
    if (exists(p)) return p;
  }
  return null;
}

class PluginHostProcess {
  constructor({ host, plugin, sampleRate = 48000, blockSize = 512, spawnImpl = spawn, timeoutMs = 4000 }) { Object.assign(this, { host, plugin, sampleRate, blockSize, spawnImpl, timeoutMs }); this.buf = Buffer.alloc(0); this.q = { A: [], G: [] }; this.dead = false; this.err = ''; }

  start() {
    return new Promise((resolve, reject) => {
      let done = false;
      const finish = (fn, v) => { if (!done) { done = true; clearTimeout(timer); fn(v); } };
      const timer = setTimeout(() => { finish(reject, new Error('the plugin host did not answer')); this.stop(); }, this.timeoutMs);
      // no shell, no inherited secrets: the plugin is third-party native code
      this.child = this.spawnImpl(this.host, [this.plugin, String(this.sampleRate), String(this.blockSize)], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true, env: { PATH: process.env.PATH || '', SystemRoot: process.env.SystemRoot || '' } });
      this.child.stdin.on('error', () => { /* the host already exited: reported by 'close' */ });
      this.child.on('error', e => { this.dead = true; finish(reject, new Error('cannot start the plugin host: ' + e.message)); });
      this.child.stderr.on('data', d => { this.err = (this.err + d).slice(-400); });
      this.child.stdout.on('data', d => this.onData(d, finish, resolve, reject));
      this.child.on('close', code => {
        this.dead = true;
        const e = new Error(this.err.trim() || ('the plugin host stopped (exit ' + code + ')'));
        finish(reject, e); [...this.q.A, ...this.q.G].forEach(w => { clearTimeout(w.t); w.reject(e); }); this.q = { A: [], G: [] };
        if (this.onExit) this.onExit(e);
      });
    });
  }

  onData(d, finish, resolve, reject) {
    this.buf = Buffer.concat([this.buf, d]);
    for (;;) {
      if (this.buf.length < 8) return;
      const len = this.buf.readUInt32LE(4);
      if (len > (32 << 20)) { this.stop(); return; }
      if (this.buf.length < 8 + len) return;
      const type = String.fromCharCode(this.buf[0]), body = Buffer.from(this.buf.subarray(8, 8 + len));
      this.buf = this.buf.subarray(8 + len);
      if (type === 'H') {
        let j; try { j = JSON.parse(body.toString('utf8')); } catch (_) { j = { ok: false, error: 'bad answer from the plugin host' }; }
        if (j.ok) { this.info = j; finish(resolve, j); } else { finish(reject, new Error(j.error || 'the plugin could not be loaded')); this.stop(); }
      } else if (this.q[type] && this.q[type].length) {
        const w = this.q[type].shift(); clearTimeout(w.t);
        w.resolve(type === 'G' ? (() => { try { return JSON.parse(body.toString('utf8')); } catch (_) { return []; } })() : body);
      }
    }
  }

  frame(type, payload = Buffer.alloc(0)) {
    const h = Buffer.alloc(8); h[0] = type.charCodeAt(0); h.writeUInt32LE(payload.length, 4);
    if (!this.dead && this.child.stdin.writable) this.child.stdin.write(Buffer.concat([h, payload])); else throw new Error('the plugin host is not running');
  }
  ask(type, payload) {
    return new Promise((resolve, reject) => {
      const w = { resolve, reject, t: setTimeout(() => { const i = this.q[type === 'A' ? 'A' : 'G'].indexOf(w); if (i >= 0) this.q[type === 'A' ? 'A' : 'G'].splice(i, 1); reject(new Error('the plugin did not answer in time')); }, this.timeoutMs) };
      this.q[type === 'A' ? 'A' : 'G'].push(w);
      try { this.frame(type, payload); } catch (e) { clearTimeout(w.t); this.q[type === 'A' ? 'A' : 'G'].pop(); reject(e); }
    });
  }
  get pending() { return this.q.A.length; }
  // interleaved stereo float32 in -> same out
  process(buf) { return this.ask('A', buf); }
  params() { return this.ask('G'); }
  setParam(i, v) { const b = Buffer.alloc(8); b.writeUInt32LE(i >>> 0, 0); b.writeFloatLE(Math.max(0, Math.min(1, Number(v) || 0)), 4); this.frame('S', b); }
  stop() {
    if (!this.child || this.stopped) return; this.stopped = true;
    try { this.frame('Q'); } catch (_) { /* gone */ }
    const k = setTimeout(() => { try { this.child.kill(); } catch (_) { /* gone */ } }, 500); if (k.unref) k.unref();
  }
}

// WebSocket /ws/insert: the page sends { type: 'start', slot, sampleRate } and then audio blocks (binary float32, interleaved stereo); processed blocks come back.
function createInsertSession(conn, { scan = () => plugins.scan(), read = () => inserts.read(), status = () => license.status(), hostFile = () => hostPath(), spawnImpl = spawn } = {}) {
  let host = null, bypass = false, counted = false;
  const err = message => conn.send(JSON.stringify({ type: 'error', message }));
  const stop = () => { if (host) { host.onExit = null; host.stop(); host = null; } if (counted) { counted = false; running--; } };

  async function start(m) {
    stop();
    if (!license.hasFeature(status(), 'plugins')) return err('Plugin inserts need the PRO or STUDIO plan');
    const slot = read().slots[m && m.slot];
    if (!slot) return err('that slot is empty');
    const file = hostFile(); if (!file) return err('the native plugin host is not installed on this system (windows/plugin-host: build PluginHost, or set BRIDGE_PLUGIN_HOST)');
    const found = scan().plugins.find(p => p.name === slot.plugin && p.format === slot.format);
    if (!found || !found.valid || !found.compatible) return err('the plugin is not available any more: scan again');
    if (found.format === 'VST3') return err('VST3 plugins are not supported by the native host yet (VST2 .dll / .vst only)');
    if (running >= MAX_HOSTS) return err('too many plugins are running');
    running++; counted = true; bypass = !!slot.bypass;
    const h = new PluginHostProcess({ host: file, plugin: found.file, sampleRate: Number(m.sampleRate) || 48000, blockSize: 512, spawnImpl });
    host = h;
    h.onExit = e => { if (host === h) { err('The plugin stopped: ' + e.message); stop(); } };
    try {
      const info = await h.start();
      if (host !== h) return;                                    // stopped while starting
      conn.send(JSON.stringify({ type: 'started', name: info.name || slot.plugin, vendor: info.vendor || '', inputs: info.inputs, outputs: info.outputs, params: info.params, latency: info.latency || 0, bypass }));
    } catch (e) { if (host === h) { stop(); err(e.message); } }
  }

  return {
    onText(text) {
      let m; try { m = JSON.parse(text); } catch (_) { return; }
      if (!m || typeof m !== 'object') return;
      if (m.type === 'start') start(m);
      else if (m.type === 'stop') { stop(); conn.send(JSON.stringify({ type: 'stopped' })); }
      else if (m.type === 'bypass') bypass = m.on === true;
      else if (host && m.type === 'param' && Number.isInteger(m.i) && m.i >= 0) { try { host.setParam(m.i, m.v); } catch (e) { err(e.message); } }
      else if (host && m.type === 'params') host.params().then(list => conn.send(JSON.stringify({ type: 'params', list })), e => err(e.message));
    },
    onBinary(buf) {
      if (!host || host.dead || !host.info) return;
      if (buf.length % 8 !== 0 || !buf.length) return;           // whole stereo float32 frames only
      if (bypass || host.pending >= MAX_PENDING) { conn.sendBinary(buf); return; }   // dry signal instead of a growing delay
      const h = host;
      h.process(buf).then(out => { if (host === h) conn.sendBinary(out); }, e => { if (host === h) { err(e.message); stop(); } });
    },
    onClose: stop,
  };
}

module.exports = { PluginHostProcess, createInsertSession, hostPath, MAX_HOSTS };
