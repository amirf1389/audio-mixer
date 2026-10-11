'use strict';
// Level metering of the audio that is really sent to / received from a native (ASIO, WASAPI, ...) device.
// Peak and RMS per channel in dBFS, reported to the page about 12 times a second as { type: 'levels', ... } on the stream's own WebSocket.
const FLOOR = -90;
// Clip detection (the same rules as the page's meters): a sample counts as "over" when it reaches the clip threshold (default -0.1 dBFS, set from the page);
// a channel CLIPS when CLIP_RUN samples in a row are over, or when one sample is at digital full scale (a flat-topped wave is clipping, a single peak is not).
const CLIP_RUN = 3;
const config = { clipDb: -0.1 };
const limitOf = db => Math.max(1, Math.min(32767, Math.ceil(32768 * Math.pow(10, db / 20))));
function setClipDb(db) {
  const n = Number(db);
  if (!Number.isFinite(n) || n > 0 || n < -12) throw Object.assign(new Error('clipDb must be a number from -12 to 0 (dBFS)'), { status: 400 });
  config.clipDb = Math.round(n * 10) / 10;
  return config.clipDb;
}
const dbfs = v => { if (v <= 0) return FLOOR; const r = +(20 * Math.log10(v / 32768)).toFixed(1); return r === 0 ? 0 : Math.max(FLOOR, r); };   // never "-0"

class Meter {
  constructor(channels) {
    this.ch = Math.max(1, Math.min(channels | 0, 64));
    this.run = new Int32Array(this.ch);                  // consecutive over-threshold samples per channel (carried from one block to the next)
    this.reset();
  }
  reset() { this.peak = new Int32Array(this.ch); this.sumSq = new Float64Array(this.ch); this.frames = 0; this.clips = 0; this.runs = 0; this.clipCh = new Uint8Array(this.ch); }
  // buf: interleaved Int16 little-endian PCM; partial trailing frames are ignored
  push(buf) {
    const ch = this.ch, n = Math.floor(buf.length / 2 / ch) * ch, thr = limitOf(config.clipDb);
    for (let i = 0, c = 0; i < n; i++) {
      const v = buf.readInt16LE(i * 2), a = v < 0 ? -v : v;
      if (a > this.peak[c]) this.peak[c] = a;
      this.sumSq[c] += v * v;
      if (a >= thr) { this.clips++; if (++this.run[c] === CLIP_RUN) { this.runs++; this.clipCh[c] = 1; } } else this.run[c] = 0;
      if (a >= 32767) this.clipCh[c] = 1;
      if (++c === ch) c = 0;
    }
    this.frames += n / ch;
  }
  // levels since the last snapshot, then start over
  snapshot() {
    const peak = [], rms = [];
    for (let c = 0; c < this.ch; c++) {
      peak.push(dbfs(this.peak[c]));
      rms.push(this.frames ? dbfs(Math.sqrt(this.sumSq[c] / this.frames)) : FLOOR);
    }
    // clip: samples over the threshold (as before); clipping: some channel clipped; clipCh: which ones; clipRuns: separate clipping events; clipDb: the threshold in use
    const out = { peak, rms, clip: this.clips, clipping: this.clipCh.some(x => x), clipCh: Array.from(this.clipCh, x => !!x), clipRuns: this.runs, clipDb: config.clipDb, frames: this.frames };
    this.reset();
    return out;
  }
}

// Starts reporting for one open stream. `info` = what streams.add() got (direction, device, hostApi, engine, frameSize, latencyMs, sampleRate, channels).
function attach(conn, info, { interval = 80 } = {}) {
  const meter = new Meter(info.channels || 2);
  const id = `${info.sid || 0}:${info.direction}:${info.hostApi}:${info.device}`;   // one id per open stream
  const timer = setInterval(() => {
    try { conn.send(JSON.stringify({ type: 'levels', id, direction: info.direction, device: info.device, hostApi: info.hostApi, engine: info.engine, frameSize: info.frameSize || null, latencyMs: info.latencyMs || null, sampleRate: info.sampleRate, channels: meter.ch, ...meter.snapshot() })); } catch (_) { /* connection closing */ }
  }, interval);
  if (timer.unref) timer.unref();
  return { push: buf => meter.push(buf), stop: () => clearInterval(timer), meter };
}

module.exports = { Meter, attach, dbfs, FLOOR, CLIP_RUN, config, setClipDb, limitOf };
