//! Holds the crate to engine/tests/vectors.txt (the numbers of the page and of the bridge).
use audiomixer_engine::*;
use std::f64::consts::PI;

const PI2: f64 = 2.0 * PI;
static VECTORS: &str = include_str!("../../tests/vectors.txt");

fn gen(spec: &str, fs: f64, n: usize, ch: u32) -> Vec<f32> {
    let p: Vec<&str> = spec.split(':').collect(); let num = |k: usize| p[k].parse::<f64>().unwrap(); let mut out = vec![0.0f32; n];
    match p[0] {
        "silence" => {}
        "sine" => for i in 0..n { out[i] = (num(2) * (PI2 * num(1) * i as f64 / fs).sin()) as f32; },
        "sinelr" => for i in 0..n { out[i] = ((if ch == 1 { num(4) } else { num(2) }) * (PI2 * (if ch == 1 { num(3) } else { num(1) }) * i as f64 / fs).sin()) as f32; },
        "inv" => for i in 0..n { out[i] = ((if ch == 1 { -num(2) } else { num(2) }) * (PI2 * num(1) * i as f64 / fs).sin()) as f32; },
        "square" => for i in 0..n { out[i] = (num(2) * if (PI2 * num(1) * i as f64 / fs).sin() >= 0.0 { 1.0 } else { -1.0 }) as f32; },
        "clipsine" => for i in 0..n { let v = num(2) * (PI2 * num(1) * i as f64 / fs).sin(); out[i] = (if v > 1.0 { 1.0 } else if v < -1.0 { -1.0 } else { v }) as f32; },
        "alt" => for i in 0..n { let seg = (i as f64 / (num(4) * fs)).floor() as i64; out[i] = ((if seg % 2 != 0 { num(3) } else { num(2) }) * (PI2 * num(1) * i as f64 / fs).sin()) as f32; },
        "fade" => for i in 0..n { let t = (i as f64 / (num(4) * fs)).min(1.0); out[i] = ((num(2) + (num(3) - num(2)) * t) * (PI2 * num(1) * i as f64 / fs).sin()) as f32; },
        "noise" => { let mut s = (num(1) as i64 as u32).wrapping_add(ch); for i in 0..n { s = s.wrapping_mul(1664525).wrapping_add(1013904223); out[i] = (num(2) * ((s >> 8) as f64 / 16777216.0 * 2.0 - 1.0)) as f32; } }
        other => panic!("unknown signal {}", other),
    }
    out
}
fn gen16(spec: &str, fs: f64, frames: usize, ch: usize) -> Vec<i16> {
    let mut out = vec![0i16; frames * ch];
    for c in 0..ch { let x = gen(spec, fs, frames, (c & 1) as u32); for i in 0..frames { let v = x[i] as f64 * 32767.0; out[i * ch + c] = (v + if v >= 0.0 { 0.5 } else { -0.5 }) as i32 as i16; } }
    out
}
struct Tally { checked: u32, failed: Vec<String> }
impl Tally {
    fn check(&mut self, id: &str, what: &str, got: f64, want: f64, tol: f64, rel: bool) {
        self.checked += 1;
        if want.is_infinite() || got.is_infinite() { if got != want { self.failed.push(format!("{} {}: got {} want {}", id, what, got, want)); } return; }
        let lim = if rel { tol * want.abs().max(1.0) } else { tol };
        if !((got - want).abs() <= lim) { self.failed.push(format!("{} {}: got {} want {} (tolerance {})", id, what, got, want, lim)); }
    }
}
fn nums(s: &str) -> Vec<f64> { s.split_whitespace().map(|w| w.parse::<f64>().unwrap()).collect() }

#[test]
fn vectors() {
    let mut t = Tally { checked: 0, failed: Vec::new() }; let mut cases = 0; let mut lines = VECTORS.lines();
    while let Some(line) = lines.next() {
        if line.is_empty() || line.starts_with('#') { continue; }
        let (head, tail) = match line.find('|') { Some(i) => (&line[..i], &line[i + 1..]), None => (line, "") };
        let e = nums(tail); let h: Vec<&str> = head.split_whitespace().collect(); if h.len() < 2 { continue; } let id = h[1];
        match h[0] {
            "lufs" => {
                cases += 1; let fs: f64 = h[2].parse().unwrap(); let block: usize = h[3].parse().unwrap(); let n = (fs * h[4].parse::<f64>().unwrap() + 0.5) as usize;
                let (l, r) = (gen(h[5], fs, n, 0), gen(h[5], fs, n, 1)); let mut m = Loudness::new(fs, 216000); let mut i = 0;
                while i < n { let j = (i + block).min(n); m.feed(&l[i..j], &r[i..j]); i += block; }
                t.check(id, "momentary", m.momentary(), e[0], 0.02, false); t.check(id, "short-term", m.short_term(), e[1], 0.02, false); t.check(id, "integrated", m.integrated(), e[2], 0.02, false);
                t.check(id, "lra", m.range(), e[3], 0.02, false); t.check(id, "true peak", m.true_peak(), e[4], 0.01, false);
            }
            "meter" => {
                cases += 1; let fs: f64 = h[2].parse().unwrap(); let win: usize = h[3].parse().unwrap(); let n = (fs * h[4].parse::<f64>().unwrap() + 0.5) as usize;
                let x = gen(h[6], fs, n, 0); let mut m = Meter::new(ClipConfig::from_db(h[5].parse().unwrap())); let mut tm = 0.0; let mut i = 0;
                while i + win <= n { tm += win as f64 * 1000.0 / fs; m.push(&x[i..i + win], tm); i += win; }
                let s = m.state();
                t.check(id, "pk", s.pk, e[0], 1e-6, true); t.check(id, "rms", s.rms, e[1], 1e-6, true); t.check(id, "hold", s.hold, e[2], 1e-6, true); t.check(id, "clipN", s.clip_n as f64, e[3], 0.0, false);
                t.check(id, "latched", s.latched as u8 as f64, e[4], 0.0, false); t.check(id, "clipPk", s.clip_pk, e[5], 1e-6, true);
            }
            "phase" => {
                cases += 1; let n: usize = h[2].parse().unwrap(); let (l, r) = (gen(h[6], 48000.0, n, 0), gen(h[6], 48000.0, n, 1));
                let p = phase_correlation(&l, &r, h[3].parse().unwrap(), h[4] == "1", h[5] == "1");
                t.check(id, "corr", p.corr, e[0], 1e-5, false); t.check(id, "peak", p.peak, e[1], 1e-6, true); t.check(id, "rmsL", p.rms_l, e[2], 1e-6, true); t.check(id, "rmsR", p.rms_r, e[3], 1e-6, true);
            }
            "scope" => {
                cases += 1; let fs: f64 = h[2].parse().unwrap(); let n: usize = h[3].parse().unwrap(); let a = gen(h[7], fs, n, 0);
                let s = scope_sweep(&a, fs, &ScopeConfig { timebase_ms: h[4].parse().unwrap(), trigger_db: h[5].parse().unwrap(), rising: h[6] == "1" });
                t.check(id, "N", s.n as f64, e[0], 0.0, false); t.check(id, "start", s.start as f64, e[1], 0.0, false); t.check(id, "triggered", s.triggered as u8 as f64, e[2], 0.0, false); t.check(id, "p2p", s.p2p, e[3], 1e-6, true);
                t.check(id, "rms", s.rms, e[4], 1e-6, true); t.check(id, "crest", s.crest_db, e[5], 1e-4, false); t.check(id, "freq", s.freq, e[6], 1e-6, true);
            }
            "vu" => {
                cases += 1; let mut v = VuMeter::default(); let reff: f64 = h[2].parse().unwrap();
                for _ in 0..h[4].parse::<u32>().unwrap() { v.step(h[3].parse().unwrap(), reff); } for _ in 0..h[6].parse::<u32>().unwrap() { v.step(h[5].parse().unwrap(), reff); }
                t.check(id, "needle", v.needle(), e[0], 1e-9, true); t.check(id, "vel", v.velocity(), e[1], 1e-9, true); t.check(id, "led", v.peak_led() as u8 as f64, e[2], 0.0, false);
            }
            "s16" => {
                cases += 1; let ch: usize = h[2].parse().unwrap(); let frames: usize = h[4].parse().unwrap(); let half = frames / 2; let x = gen16(h[5], 48000.0, frames, ch);
                let mut m = S16Meter::new(ch, h[3].parse().unwrap()); let mut k = 0;
                for snap in 0..2 {
                    let (a, b) = if snap == 1 { (half, frames) } else { (0, half) }; m.push(&x[a * ch..b * ch]); let s = m.snapshot();
                    for c in 0..ch { t.check(id, "peak", s.peak_db[c], e[k], 0.06, false); k += 1; } for c in 0..ch { t.check(id, "rms", s.rms_db[c], e[k], 0.06, false); k += 1; }
                    t.check(id, "clip", s.clip as f64, e[k], 0.0, false); t.check(id, "clipRuns", s.clip_runs as f64, e[k + 1], 0.0, false); t.check(id, "clipping", s.clipping as u8 as f64, e[k + 2], 0.0, false); t.check(id, "frames", s.frames as f64, e[k + 3], 0.0, false); k += 4;
                }
            }
            "mixer" => {
                cases += 1; let fs: f64 = h[2].parse().unwrap(); let blocks: usize = h[3].parse().unwrap(); let ns: usize = h[8].parse().unwrap(); let n = blocks * 512;
                let mut m = Mixer::new(ns, fs); m.set_master_db(h[4].parse().unwrap()); m.set_limiter(h[5] == "1", h[6].parse().unwrap(), h[7].parse().unwrap()); let mut input: Vec<Vec<f32>> = Vec::new();
                for s in 0..ns { let sl = lines.next().unwrap(); let w: Vec<&str> = sl.split_whitespace().collect(); input.push(gen(w[1], fs, n, 0)); m.set_gain_db(s, w[2].parse().unwrap()); m.set_pan(s, w[3].parse().unwrap()); m.set_mute(s, w[4] == "1"); m.set_solo(s, w[5] == "1"); }
                let ex = { let xl = lines.next().unwrap(); nums(&xl[xl.find('|').unwrap() + 1..]) }; let (mut ol, mut or_) = (vec![0.0f32; n], vec![0.0f32; n]);
                for b in 0..blocks { let r = b * 512..(b + 1) * 512; let ins: Vec<&[f32]> = input.iter().map(|v| &v[r.clone()]).collect(); m.process(&ins, &mut ol[r.clone()], &mut or_[r.clone()]); }
                let (mut pkl, mut pkr, mut ssl, mut ssr) = (0.0f64, 0.0f64, 0.0f64, 0.0f64); for i in 0..n { pkl = pkl.max((ol[i] as f64).abs()); pkr = pkr.max((or_[i] as f64).abs()); ssl += ol[i] as f64 * ol[i] as f64; ssr += or_[i] as f64 * or_[i] as f64; }
                let mut k = 0; t.check(id, "peak L", pkl, ex[k], 2e-4, true); k += 1; t.check(id, "peak R", pkr, ex[k], 2e-4, true); k += 1; t.check(id, "rms L", (ssl / n as f64).sqrt(), ex[k], 2e-4, true); k += 1; t.check(id, "rms R", (ssr / n as f64).sqrt(), ex[k], 2e-4, true); k += 1;
                for s in 0..ns { let st = m.strip(s).meter.state(); t.check(id, "strip pk", st.pk, ex[k], 2e-4, true); t.check(id, "strip rms", st.rms, ex[k + 1], 2e-4, true); t.check(id, "strip hold", st.hold, ex[k + 2], 2e-4, true); t.check(id, "strip clipN", st.clip_n as f64, ex[k + 3], 0.0, false); k += 4; }
                for c in 0..2 { let st = m.master_meter(c); t.check(id, "master pk", st.pk, ex[k], 2e-4, true); t.check(id, "master rms", st.rms, ex[k + 1], 2e-4, true); t.check(id, "master clipN", st.clip_n as f64, ex[k + 2], 0.0, false); k += 3; }
                t.check(id, "momentary", m.loudness().momentary(), ex[k], 0.02, false); t.check(id, "integrated", m.loudness().integrated(), ex[k + 1], 0.02, false); t.check(id, "true peak", m.loudness().true_peak(), ex[k + 2], 0.01, false);
            }
            "strip" | "expect" => {}
            other => panic!("unknown case {}", other),
        }
    }
    println!("Rust crate: {} cases, {} numbers checked, {} failed", cases, t.checked, t.failed.len());
    assert!(t.failed.is_empty(), "{}", t.failed.join("\n"));
}
