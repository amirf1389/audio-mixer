//! Audio Mixer engine (Rust): the measuring and mixing code of Audio Mixer.
//!
//! * [`Meter`] / [`MeterState`]: peak / RMS / peak hold of blocks with CLIP detection (3 samples in a row at the threshold, or one at full scale; latched).
//! * [`Loudness`]: ITU-R BS.1770-4 / EBU R128: momentary, short-term, integrated, loudness range, 4x true peak.
//! * [`phase_correlation`], [`scope_sweep`], [`VuMeter`], [`S16Meter`] and the [`Mixer`] (32 mono strips, constant-power pan, mute / solo, master gain and limiter).
//!
//! The numbers are those of the page and of the bridge; `engine/tests/vectors.txt` holds them and `tests/vectors.rs` holds this crate to them, like the C++ engine,
//! the embedded C core and the WebAssembly build. Samples are `f32` (-1..1), levels are linear unless named `_db`; minus infinity is `f64::NEG_INFINITY`.
//! Uses `std` (no dependencies).

use std::f64::consts::PI;

pub const MAX_STRIPS: usize = 32;
const NEG_INF: f64 = f64::NEG_INFINITY;

fn lin_of(db: f64) -> f64 { 10f64.powf(db / 20.0) }
fn clampd(v: f64, lo: f64, hi: f64) -> f64 { if v < lo { lo } else if v > hi { hi } else { v } }

// ───────── level meter ─────────
#[derive(Clone, Copy, Debug)]
pub struct ClipConfig { pub lin: f64, pub run: u32, pub hold_ms: f64, pub merge_ms: f64, pub lit_ms: f64 }
impl ClipConfig {
    /// threshold in dBFS (-12..0); the other values are the page's: 3 samples, 1400 ms hold, 250 ms merge, 1500 ms badge
    pub fn from_db(db: f64) -> Self { ClipConfig { lin: lin_of(clampd(db, -12.0, 0.0)), run: 3, hold_ms: 1400.0, merge_ms: 250.0, lit_ms: 1500.0 } }
}
impl Default for ClipConfig { fn default() -> Self { Self::from_db(-0.1) } }

#[derive(Clone, Copy, Debug, Default)]
pub struct MeterState { pub pk: f64, pub rms: f64, pub hold: f64, pub hold_at: f64, pub clip_at: f64, pub clip_pk: f64, pub clip_n: u32, pub latched: bool }
impl MeterState {
    pub fn lit(&self, now_ms: f64, c: &ClipConfig) -> bool { now_ms - self.clip_at < c.lit_ms }
    pub fn clear_clip(&mut self) { self.clip_n = 0; self.latched = false; self.clip_pk = 0.0; self.clip_at = 0.0; }
}

/// A reading window may come in pieces: `add` any number of times, then `end`.
#[derive(Clone, Debug)]
pub struct Meter { cfg: ClipConfig, s: MeterState, w_pk: f64, w_ss: f64, w_n: usize, run: u32, max_run: u32 }
impl Meter {
    pub fn new(cfg: ClipConfig) -> Self { Meter { cfg, s: MeterState::default(), w_pk: 0.0, w_ss: 0.0, w_n: 0, run: 0, max_run: 0 } }
    pub fn set_clip(&mut self, cfg: ClipConfig) { self.cfg = cfg; }
    pub fn add(&mut self, x: &[f32]) {
        for &v in x {
            let v = v as f64; let a = v.abs();
            if a > self.w_pk { self.w_pk = a; }
            self.w_ss += v * v;
            if a >= self.cfg.lin { self.run += 1; if self.run > self.max_run { self.max_run = self.run; } } else { self.run = 0; }
        }
        self.w_n += x.len();
    }
    pub fn end(&mut self, now_ms: f64) {
        let rms = if self.w_n > 0 { (self.w_ss / self.w_n as f64).sqrt() } else { 0.0 };
        let s = &mut self.s;
        s.pk = if self.w_pk >= s.pk { self.w_pk } else { s.pk * 0.9 + self.w_pk * 0.1 };   // fast attack, slow release
        s.rms = if rms >= s.rms { rms } else { s.rms * 0.9 + rms * 0.1 };
        if self.w_pk >= s.hold || now_ms - s.hold_at > self.cfg.hold_ms { s.hold = self.w_pk; s.hold_at = now_ms; }
        if self.max_run >= self.cfg.run || self.w_pk >= 1.0 {                              // clip: a flat top, or full scale; events closer than merge_ms are one
            if now_ms - s.clip_at > self.cfg.merge_ms { s.clip_n += 1; }
            s.clip_at = now_ms; s.latched = true;
            if self.w_pk > s.clip_pk { s.clip_pk = self.w_pk; }
        }
        self.w_pk = 0.0; self.w_ss = 0.0; self.w_n = 0; self.run = 0; self.max_run = 0;
    }
    pub fn push(&mut self, x: &[f32], now_ms: f64) { self.add(x); self.end(now_ms); }
    pub fn state(&self) -> &MeterState { &self.s }
    pub fn state_mut(&mut self) -> &mut MeterState { &mut self.s }
}

// ───────── loudness ─────────
fn lk(ms: f64) -> f64 { if ms > 0.0 { -0.691 + 10.0 * ms.log10() } else { NEG_INF } }
fn mean_loud(a: &[f64]) -> f64 { if a.is_empty() { NEG_INF } else { lk(a.iter().sum::<f64>() / a.len() as f64) } }
fn k_pre(fs: f64, f0: f64, g: f64, q: f64) -> [f64; 5] {
    let k = (PI * f0 / fs).tan(); let vh = 10f64.powf(g / 20.0); let vb = vh.powf(0.4996667741545416); let a0 = 1.0 + k / q + k * k;
    [(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0]
}
fn k_hp(fs: f64, f0: f64, q: f64) -> [f64; 5] {
    let k = (PI * f0 / fs).tan(); let a0 = 1.0 + k / q + k * k;
    [1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0]
}

pub struct Loudness {
    cap: usize, sub_size: i32, c1: [f64; 5], c2: [f64; 5], z: [f64; 8], tp: [[f64; 12]; 3], h: [[f32; 11]; 2],
    sub_acc: f64, sub_n: i32, sub: Vec<f64>, blocks: Vec<f64>, shorts: Vec<f64>,
    momentary: f64, short_term: f64, integrated: f64, lra: f64, true_peak: f64,
}
impl Loudness {
    /// `max_blocks`: 100 ms gating blocks kept (the page keeps 6 hours = 216000)
    pub fn new(sample_rate: f64, max_blocks: usize) -> Self {
        let mut tp = [[0.0; 12]; 3];
        for p in 0..3 {
            let f = (p as f64 + 1.0) / 4.0; let mut sum = 0.0;
            for k in -5i32..=6 { let t = f - k as f64; let s = if t == 0.0 { 1.0 } else { (PI * t).sin() / (PI * t) }; let w = 0.5 + 0.5 * (PI * t / 6.0).cos(); tp[p][(k + 5) as usize] = s * w; sum += s * w; }
            for k in 0..12 { tp[p][k] /= sum; }
        }
        Loudness { cap: max_blocks, sub_size: (sample_rate * 0.1 + 0.5).floor() as i32, c1: k_pre(sample_rate, 1681.974450955533, 3.999843853973347, 0.7071752369554196), c2: k_hp(sample_rate, 38.13547087602444, 0.5003270373238773),
            z: [0.0; 8], tp, h: [[0.0; 11]; 2], sub_acc: 0.0, sub_n: 0, sub: Vec::new(), blocks: Vec::new(), shorts: Vec::new(),
            momentary: NEG_INF, short_term: NEG_INF, integrated: NEG_INF, lra: 0.0, true_peak: NEG_INF }
    }
    pub fn reset(&mut self) { self.blocks.clear(); self.shorts.clear(); self.integrated = NEG_INF; self.lra = 0.0; self.true_peak = NEG_INF; }
    pub fn momentary(&self) -> f64 { self.momentary }
    pub fn short_term(&self) -> f64 { self.short_term }
    pub fn integrated(&self) -> f64 { self.integrated }
    pub fn range(&self) -> f64 { self.lra }
    pub fn true_peak(&self) -> f64 { self.true_peak }

    fn gate(&mut self) {
        let abs: Vec<f64> = self.blocks.iter().cloned().filter(|&b| lk(b) > -70.0).collect();
        if abs.is_empty() { self.integrated = NEG_INF; } else {
            let rel = mean_loud(&abs) - 10.0; let pass: Vec<f64> = abs.iter().cloned().filter(|&b| lk(b) > rel).collect();
            self.integrated = if pass.is_empty() { NEG_INF } else { lk(pass.iter().sum::<f64>() / pass.len() as f64) };
        }
        let sa: Vec<f64> = self.shorts.iter().cloned().filter(|&v| v > -70.0).collect(); self.lra = 0.0;
        if sa.len() > 1 {                                                  // loudness range: gates -70 / -20 LU, 10th..95th percentile
            let sum: f64 = sa.iter().map(|&v| 10f64.powf((v + 0.691) / 10.0)).sum(); let rel = -0.691 + 10.0 * (sum / sa.len() as f64).log10() - 20.0;
            let mut v: Vec<f64> = sa.into_iter().filter(|&x| x > rel).collect(); v.sort_by(|a, b| a.partial_cmp(b).unwrap());
            if v.len() > 1 { let m = (v.len() - 1) as f64; self.lra = (v[(m.min((0.95 * m + 0.5).floor())) as usize] - v[(0.10 * m + 0.5).floor() as usize]).max(0.0); }
        }
    }
    pub fn feed(&mut self, left: &[f32], right: &[f32]) {
        let n = left.len().min(right.len()); let (mut e, mut pk) = (0.0f64, 0.0f64);
        for c in 0..2 {
            let x = if c == 0 { &left[..n] } else { &right[..n] };
            let mut buf = Vec::with_capacity(11 + n); buf.extend_from_slice(&self.h[c]); buf.extend_from_slice(x);
            for m in 5..n + 5 {                                            // true peak: the samples and 3 interpolated phases between them
                let a = (buf[m] as f64).abs(); if a > pk { pk = a; }
                for p in 0..3 { let mut s = 0.0; for j in 0..12 { s += buf[m - 5 + j] as f64 * self.tp[p][j]; } let s = s.abs(); if s > pk { pk = s; } }
            }
            self.h[c].copy_from_slice(&buf[n..n + 11]);
            let z = &mut self.z[c * 4..c * 4 + 4]; let (mut a1, mut a2, mut b1, mut b2) = (z[0], z[1], z[2], z[3]);
            for &s in x {                                                  // K-weighting, direct form II transposed
                let v = s as f64; let y1 = self.c1[0] * v + a1;
                a1 = self.c1[1] * v - self.c1[3] * y1 + a2; a2 = self.c1[2] * v - self.c1[4] * y1;
                let y2 = self.c2[0] * y1 + b1; b1 = self.c2[1] * y1 - self.c2[3] * y2 + b2; b2 = self.c2[2] * y1 - self.c2[4] * y2;
                e += y2 * y2;
            }
            z[0] = a1; z[1] = a2; z[2] = b1; z[3] = b2;
        }
        self.sub_acc += e; self.sub_n += n as i32;
        let tpdb = if pk > 0.0 { 20.0 * pk.log10() } else { NEG_INF }; if tpdb > self.true_peak { self.true_peak = tpdb; }
        if self.sub_n >= self.sub_size {
            let ms = self.sub_acc / self.sub_n as f64; self.sub_acc = 0.0; self.sub_n = 0;
            if self.sub.len() >= 30 { self.sub.remove(0); } self.sub.push(ms);
            if self.sub.len() >= 4 {
                let m4 = self.sub[self.sub.len() - 4..].iter().sum::<f64>() / 4.0; self.momentary = lk(m4);
                if self.blocks.len() >= self.cap { self.blocks.remove(0); } self.blocks.push(m4);
            } else { self.momentary = lk(ms); }
            if self.sub.len() >= 30 { self.short_term = lk(self.sub.iter().sum::<f64>() / 30.0); if self.shorts.len() >= self.cap { self.shorts.remove(0); } self.shorts.push(self.short_term); }
            else { self.short_term = mean_loud(&self.sub); }
            self.gate();
        }
    }
}

// ───────── phase, scope, VU ─────────
#[derive(Clone, Copy, Debug)]
pub struct PhaseResult { pub corr: f64, pub peak: f64, pub rms_l: f64, pub rms_r: f64, pub silent: bool }
pub fn phase_correlation(l: &[f32], r: &[f32], width_percent: f64, invert_l: bool, invert_r: bool) -> PhaseResult {
    let n = l.len().min(r.len()); let width = width_percent / 100.0; let (mut slr, mut sll, mut srr, mut peak) = (0.0f64, 0.0f64, 0.0f64, 0.0f64);
    for i in 0..n {
        let mut a = if invert_l { -(l[i] as f64) } else { l[i] as f64 }; let mut b = if invert_r { -(r[i] as f64) } else { r[i] as f64 };
        let mid = (a + b) * 0.5; let side = (a - b) * 0.5 * width; a = mid + side; b = mid - side;
        slr += a * b; sll += a * a; srr += b * b; let m = a.abs().max(b.abs()); if m > peak { peak = m; }
    }
    let den = (sll * srr).sqrt(); let silent = peak < 0.0005;
    PhaseResult { corr: if silent || den == 0.0 { 0.0 } else { clampd(slr / den, -1.0, 1.0) }, peak, rms_l: if n > 0 { (sll / n as f64).sqrt() } else { 0.0 }, rms_r: if n > 0 { (srr / n as f64).sqrt() } else { 0.0 }, silent }
}

#[derive(Clone, Copy, Debug)]
pub struct ScopeConfig { pub timebase_ms: f64, pub trigger_db: f64, pub rising: bool }
impl Default for ScopeConfig { fn default() -> Self { ScopeConfig { timebase_ms: 1.0, trigger_db: -12.0, rising: true } } }
#[derive(Clone, Copy, Debug, Default)]
pub struct ScopeResult { pub n: usize, pub start: usize, pub triggered: bool, pub p2p: f64, pub rms: f64, pub crest_db: f64, pub freq: f64 }
pub fn scope_sweep(a: &[f32], sample_rate: f64, cfg: &ScopeConfig) -> ScopeResult {
    let size = a.len(); let tb = if cfg.timebase_ms > 0.0 { cfg.timebase_ms } else { 1.0 }; let lvl = lin_of(cfg.trigger_db);
    let n = (64.0f64).max(((size >> 1) as f64).min((sample_rate * tb * 10.0 / 1000.0 + 0.5).floor())) as usize;
    let (mut start, mut triggered) = (size - n, false);
    let mut i = if size >= 2 * n { size - 2 * n + 1 } else { 1 };
    while i < size - n {                                                   // the newest sweep that begins with a crossing and still has n samples after it
        let hit = if cfg.rising { (a[i - 1] as f64) < lvl && (a[i] as f64) >= lvl } else { (a[i - 1] as f64) > -lvl && (a[i] as f64) <= -lvl };
        if hit { start = i; triggered = true; }
        i += 1;
    }
    let (mut mn, mut mx, mut ss, mut pk) = (f64::INFINITY, f64::NEG_INFINITY, 0.0f64, 0.0f64);
    for &s in &a[start..start + n] { let v = s as f64; mn = mn.min(v); mx = mx.max(v); ss += v * v; pk = pk.max(v.abs()); }
    let rms = (ss / n as f64).sqrt();
    let from = if size > 8192 { size - 8192 } else { 1 }; let hy = (0.002f64).max(pk * 0.05);   // frequency: rising zero crossings with hysteresis
    let (mut first, mut last, mut cnt, mut armed) = (-1.0f64, -1.0f64, 0u32, false);
    for i in from..size {
        if (a[i] as f64) < -hy { armed = true; }
        else if armed && a[i - 1] < 0.0 && a[i] >= 0.0 {
            let d = a[i] as f64 - a[i - 1] as f64; let t = (i - 1) as f64 + (-(a[i - 1] as f64)) / if d == 0.0 { 1.0 } else { d };
            if first < 0.0 { first = t; }
            last = t; cnt += 1; armed = false;
        }
    }
    ScopeResult { n, start, triggered, p2p: if (mx - mn).is_finite() { mx - mn } else { 0.0 }, rms, crest_db: if rms > 1e-6 { 20.0 * (pk / rms).log10() } else { 0.0 },
        freq: if cnt >= 2 { (cnt - 1) as f64 * sample_rate / (last - first) } else { 0.0 } }
}

#[derive(Clone, Copy, Debug, Default)]
pub struct VuMeter { needle: f64, vel: f64, led: bool }
impl VuMeter {
    /// once per display frame; 0 VU = `ref_dbfs` (-18 EBU, -20 SMPTE)
    pub fn step(&mut self, rms: f64, ref_dbfs: f64) {
        let db = if rms > 0.00003 { (20.0 * rms.log10()).max(-60.0) } else { -60.0 }; let target = clampd(db - ref_dbfs, -20.0, 3.0);
        self.vel = (self.vel + (target - self.needle) * 0.08) * 0.82; self.needle += self.vel; self.led = target >= 0.5;
    }
    pub fn needle(&self) -> f64 { self.needle }
    pub fn velocity(&self) -> f64 { self.vel }
    pub fn peak_led(&self) -> bool { self.led }
}

// ───────── Int16 meter ─────────
#[derive(Clone, Debug, Default)]
pub struct S16Snapshot { pub peak_db: Vec<f64>, pub rms_db: Vec<f64>, pub clip: u32, pub clip_runs: u32, pub frames: u32, pub clipping: bool, pub clip_ch: Vec<bool> }
pub struct S16Meter { ch: usize, limit: i32, peak: Vec<i32>, run: Vec<i32>, sum_sq: Vec<f64>, clip_ch: Vec<bool>, frames: u32, clips: u32, runs: u32 }
impl S16Meter {
    pub fn new(channels: usize, clip_db: f64) -> Self {
        let ch = channels.max(1); let limit = clampd((32768.0 * lin_of(clampd(clip_db, -12.0, 0.0))).ceil(), 1.0, 32767.0) as i32;
        S16Meter { ch, limit, peak: vec![0; ch], run: vec![0; ch], sum_sq: vec![0.0; ch], clip_ch: vec![false; ch], frames: 0, clips: 0, runs: 0 }
    }
    /// interleaved samples (frames x channels); a partial last frame is ignored
    pub fn push(&mut self, x: &[i16]) {
        let n = x.len() / self.ch * self.ch; let mut c = 0;
        for &s in &x[..n] {
            let v = s as i32; let a = v.abs();
            if a > self.peak[c] { self.peak[c] = a; }
            self.sum_sq[c] += v as f64 * v as f64;
            if a >= self.limit { self.clips += 1; self.run[c] += 1; if self.run[c] == 3 { self.runs += 1; self.clip_ch[c] = true; } } else { self.run[c] = 0; }
            if a >= 32767 { self.clip_ch[c] = true; }
            c += 1; if c == self.ch { c = 0; }
        }
        self.frames += (n / self.ch) as u32;
    }
    /// levels since the last snapshot, then start over (the clip runs carry over)
    pub fn snapshot(&mut self) -> S16Snapshot {
        let mut s = S16Snapshot::default();
        for c in 0..self.ch {
            s.peak_db.push(if self.peak[c] > 0 { (20.0 * (self.peak[c] as f64 / 32768.0).log10()).max(-90.0) } else { -90.0 });
            s.rms_db.push(if self.frames > 0 && self.sum_sq[c] > 0.0 { (20.0 * ((self.sum_sq[c] / self.frames as f64).sqrt() / 32768.0).log10()).max(-90.0) } else { -90.0 });
            s.clip_ch.push(self.clip_ch[c]); s.clipping = s.clipping || self.clip_ch[c];
        }
        s.clip = self.clips; s.clip_runs = self.runs; s.frames = self.frames;
        for c in 0..self.ch { self.peak[c] = 0; self.sum_sq[c] = 0.0; self.clip_ch[c] = false; }
        self.frames = 0; self.clips = 0; self.runs = 0; s
    }
}

// ───────── mixer ─────────
#[derive(Clone, Debug)]
pub struct Strip { pub gain_db: f64, pub gain_lin: f64, pub pan: f64, pub mute: bool, pub solo: bool, pub meter: Meter }
pub struct Mixer {
    n: usize, fs: f64, time_ms: f64, strips: Vec<Strip>, master_lin: f64, lim_on: bool, lim_ceil: f64, lim_gain: f64, lim_release_ms: f64,
    clip: ClipConfig, master: [Meter; 2], loudness: Loudness, phase: PhaseResult,
}
impl Mixer {
    pub fn new(strips: usize, sample_rate: f64) -> Self {
        let n = strips.clamp(1, MAX_STRIPS); let clip = ClipConfig::default();
        Mixer { n, fs: sample_rate, time_ms: 0.0, strips: (0..n).map(|_| Strip { gain_db: 0.0, gain_lin: 1.0, pan: 0.0, mute: false, solo: false, meter: Meter::new(clip) }).collect(),
            master_lin: 1.0, lim_on: false, lim_ceil: lin_of(-0.5), lim_gain: 1.0, lim_release_ms: 100.0, clip, master: [Meter::new(clip), Meter::new(clip)],
            loudness: Loudness::new(sample_rate, 216000), phase: PhaseResult { corr: 0.0, peak: 0.0, rms_l: 0.0, rms_r: 0.0, silent: true } }
    }
    pub fn strips(&self) -> usize { self.n }
    /// -60 and below = silence, +10 at most
    pub fn set_gain_db(&mut self, s: usize, db: f64) { if s < self.n { let db = db.min(10.0); self.strips[s].gain_db = db; self.strips[s].gain_lin = if db <= -60.0 { 0.0 } else { lin_of(db) }; } }
    /// -1 left .. +1 right, constant power (-3 dB in the middle)
    pub fn set_pan(&mut self, s: usize, pan: f64) { if s < self.n { self.strips[s].pan = clampd(pan, -1.0, 1.0); } }
    pub fn set_mute(&mut self, s: usize, on: bool) { if s < self.n { self.strips[s].mute = on; } }
    /// with any solo on, only the solo strips are heard
    pub fn set_solo(&mut self, s: usize, on: bool) { if s < self.n { self.strips[s].solo = on; } }
    pub fn set_master_db(&mut self, db: f64) { let db = db.min(10.0); self.master_lin = if db <= -60.0 { 0.0 } else { lin_of(db) }; }
    /// brick-wall peak limiter on the master: instant attack
    pub fn set_limiter(&mut self, on: bool, ceiling_db: f64, release_ms: f64) { self.lim_on = on; self.lim_ceil = lin_of(clampd(ceiling_db, -24.0, 0.0)); self.lim_release_ms = release_ms.max(1.0); }
    pub fn set_clip_db(&mut self, db: f64) { self.clip = ClipConfig::from_db(db); for s in &mut self.strips { s.meter.set_clip(self.clip); } self.master[0].set_clip(self.clip); self.master[1].set_clip(self.clip); }
    pub fn strip(&self, i: usize) -> &Strip { &self.strips[i] }
    pub fn master_meter(&self, ch: usize) -> &MeterState { self.master[ch].state() }
    pub fn loudness(&self) -> &Loudness { &self.loudness }
    pub fn phase(&self) -> &PhaseResult { &self.phase }
    pub fn clear_clips(&mut self) { for s in &mut self.strips { s.meter.state_mut().clear_clip(); } self.master[0].state_mut().clear_clip(); self.master[1].state_mut().clear_clip(); }
    /// `inputs[strip]` -> `out_l` / `out_r` (all slices the same length). Meters, loudness and phase are updated once per call.
    pub fn process(&mut self, inputs: &[&[f32]], out_l: &mut [f32], out_r: &mut [f32]) {
        const CHUNK: usize = 512; let frames = out_l.len().min(out_r.len());
        let any_solo = self.strips.iter().any(|s| s.solo); let rel = 1.0 - (-1.0 / (self.lim_release_ms * 0.001 * self.fs)).exp();
        let mut tmp = [0.0f32; CHUNK]; let mut done = 0;
        while done < frames {
            let n = CHUNK.min(frames - done);
            for i in 0..n { out_l[done + i] = 0.0; out_r[done + i] = 0.0; }
            for s in 0..self.n {
                let st = &mut self.strips[s]; let ang = (st.pan + 1.0) * PI * 0.25; let (gl, gr) = (ang.cos(), ang.sin()); let audible = !st.mute && (!any_solo || st.solo);
                for i in 0..n { tmp[i] = (inputs[s][done + i] as f64 * st.gain_lin) as f32; }      // post-fader signal: what the strip meter shows
                st.meter.add(&tmp[..n]);
                if audible { for i in 0..n { out_l[done + i] += (tmp[i] as f64 * gl) as f32; out_r[done + i] += (tmp[i] as f64 * gr) as f32; } }
            }
            for i in 0..n {
                let mut l = out_l[done + i] as f64 * self.master_lin; let mut r = out_r[done + i] as f64 * self.master_lin;
                if self.lim_on {
                    let pk = l.abs().max(r.abs()); let need = if pk > self.lim_ceil { self.lim_ceil / pk } else { 1.0 };
                    if need < self.lim_gain { self.lim_gain = need; } else { self.lim_gain += (1.0 - self.lim_gain) * rel; }
                    if self.lim_gain > need { self.lim_gain = need; }                              // instant attack: never above what this sample needs
                    l *= self.lim_gain; r *= self.lim_gain;
                }
                out_l[done + i] = l as f32; out_r[done + i] = r as f32;
            }
            self.master[0].add(&out_l[done..done + n]); self.master[1].add(&out_r[done..done + n]);
            self.loudness.feed(&out_l[done..done + n], &out_r[done..done + n]);
            self.phase = phase_correlation(&out_l[done..done + n], &out_r[done..done + n], 100.0, false, false);
            done += n;
        }
        self.time_ms += frames as f64 * 1000.0 / self.fs;
        for s in &mut self.strips { s.meter.end(self.time_ms); }
        self.master[0].end(self.time_ms); self.master[1].end(self.time_ms);
    }
}
