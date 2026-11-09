// Reference code of the retired web console (AmF X1 1.22.1.0 Beta, git tag html-app-final, index.html), kept so engine/tests/gen-vectors.js can still
// produce engine/tests/vectors.txt. Do not edit: the native engines are held to these numbers.

/* REAL LOUDNESS METER (LUFS page): ITU-R BS.1770-4 / EBU R128 measured on the real master output (after the master limiter).
   K-weighting (high shelf + high-pass, any sample rate), momentary 400 ms, short-term 3 s, gated integrated (-70 LUFS absolute / -10 LU relative),
   loudness range (-70 / -20 LU gates, 10th..95th percentile), 4x oversampled true peak, and the real gain reduction of the master limiter.
   The LUFS page used to show fixed numbers; every value here is computed from the audio that leaves the mixer. */
(function () {
    'use strict';
    const $ = id => document.getElementById(id);
    const rt = () => window.rtEngine;
    const L = window.lufsMeter = {
        fs: 48000, ctx: null, node: null, mode: '',
        momentary: -Infinity, shortTerm: -Infinity, integrated: -Infinity, lra: 0, truePeak: -Infinity, samplePeak: -Infinity,
        blocks: [], shorts: [], sub: [], blockSum: 0, subFill: 0, subAcc: 0, subN: 0, subSize: 4800, since: 0, running: false, samples: 0,
    };
    const lk = ms => (ms > 0 ? -0.691 + 10 * Math.log10(ms) : -Infinity);
    const meanLoud = a => { let s = 0; for (let i = 0; i < a.length; i++) s += a[i]; return a.length ? lk(s / a.length) : -Infinity; };
    L.setRate = function (fs) {
        L.fs = fs; L.subSize = Math.round(fs * 0.1);
        const pre = (f0, G, Q) => { const K = Math.tan(Math.PI * f0 / fs), Vh = Math.pow(10, G / 20), Vb = Math.pow(Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
            return { b0: (Vh + Vb * K / Q + K * K) / a0, b1: 2 * (K * K - Vh) / a0, b2: (Vh - Vb * K / Q + K * K) / a0, a1: 2 * (K * K - 1) / a0, a2: (1 - K / Q + K * K) / a0 }; };
        const hp = (f0, Q) => { const K = Math.tan(Math.PI * f0 / fs), a0 = 1 + K / Q + K * K; return { b0: 1, b1: -2, b2: 1, a1: 2 * (K * K - 1) / a0, a2: (1 - K / Q + K * K) / a0 }; };
        L.c1 = pre(1681.974450955533, 3.999843853973347, 0.7071752369554196);
        L.c2 = hp(38.13547087602444, 0.5003270373238773);
        L.z = [0, 0, 0, 0, 0, 0, 0, 0];                       // two channels x (two stages x two states)
        L.h = [new Float32Array(11), new Float32Array(11)];   // true-peak history per channel
    };
    // 4x oversampling: 12-tap windowed sinc, phases 1/4, 2/4, 3/4 (phase 0 is the sample itself)
    L.tp = [1, 2, 3].map(p => {
        const f = p / 4, c = [];
        for (let k = -5; k <= 6; k++) { const t = f - k, s = t === 0 ? 1 : Math.sin(Math.PI * t) / (Math.PI * t), w = 0.5 + 0.5 * Math.cos(Math.PI * t / 6); c.push(s * w); }
        const sum = c.reduce((a, b) => a + b, 0);
        return c.map(v => v / sum);
    });
    L.reset = function () {
        L.blocks = []; L.shorts = []; L.integrated = -Infinity; L.lra = 0; L.truePeak = -Infinity; L.samplePeak = -Infinity;
    };
    // one block of master audio (left / right) -> K-weighted energy per 100 ms
    L.feed = function (l, r) {
        const n = l.length, ch = [l, r], c1 = L.c1, c2 = L.c2, z = L.z;
        let e = 0, pk = 0;
        for (let c = 0; c < 2; c++) {
            const x = ch[c], o = c * 4, h = L.h[c], buf = new Float32Array(11 + n);
            buf.set(h); buf.set(x, 11);
            // true peak: original samples + 3 interpolated phases
            for (let m = 5; m + 6 < buf.length; m++) {
                const a = buf[m] < 0 ? -buf[m] : buf[m]; if (a > pk) pk = a;
                for (let p = 0; p < 3; p++) { const k = L.tp[p]; let s = 0; for (let j = 0; j < 12; j++) s += buf[m - 5 + j] * k[j]; if (s < 0) s = -s; if (s > pk) pk = s; }
            }
            h.set(buf.subarray(buf.length - 11));
            // K-weighting (direct form II transposed)
            let a1s = z[o], a2s = z[o + 1], b1s = z[o + 2], b2s = z[o + 3], sub = 0;
            for (let i = 0; i < n; i++) {
                const v = x[i];
                const y1 = c1.b0 * v + a1s; a1s = c1.b1 * v - c1.a1 * y1 + a2s; a2s = c1.b2 * v - c1.a2 * y1;
                const y2 = c2.b0 * y1 + b1s; b1s = c2.b1 * y1 - c2.a1 * y2 + b2s; b2s = c2.b2 * y1 - c2.a2 * y2;
                e += y2 * y2;
            }
            z[o] = a1s; z[o + 1] = a2s; z[o + 2] = b1s; z[o + 3] = b2s; void sub;
        }
        // distribute the energy of this block into 100 ms sub-blocks (blocks are shorter than a sub-block in practice)
        L.subAcc += e; L.subN += n; L.samples += n;
        const tpDb = pk > 0 ? 20 * Math.log10(pk) : -Infinity;
        if (tpDb > L.truePeak) L.truePeak = tpDb;
        if (L.subN >= L.subSize) {
            const ms = L.subAcc / L.subN;                      // L + R, both with weight 1.0
            L.subAcc = 0; L.subN = 0;
            L.sub.push(ms); if (L.sub.length > 30) L.sub.shift();
            if (L.sub.length >= 4) {
                const last4 = L.sub.slice(-4), m = last4.reduce((a, b) => a + b, 0) / 4;
                L.momentary = lk(m);
                L.blocks.push(m); if (L.blocks.length > 216000) L.blocks.shift();     // 6 hours
            } else L.momentary = lk(ms);
            if (L.sub.length >= 30) {
                const s = L.sub.reduce((a, b) => a + b, 0) / 30;
                L.shortTerm = lk(s); L.shorts.push(L.shortTerm); if (L.shorts.length > 216000) L.shorts.shift();
            } else L.shortTerm = meanLoud(L.sub);
            L.gate();
        }
    };
    L.gate = function () {
        // integrated: absolute gate -70, then relative gate -10 LU below the mean of the blocks that passed
        const abs = L.blocks.filter(b => lk(b) > -70);
        if (!abs.length) { L.integrated = -Infinity; }
        else {
            const rel = meanLoud(abs) - 10, pass = abs.filter(b => lk(b) > rel);
            L.integrated = pass.length ? meanLoud(pass) : -Infinity;
        }
        // loudness range from the short-term values (absolute gate -70, relative gate -20, 10th..95th percentile)
        const sa = L.shorts.filter(v => v > -70);
        if (sa.length > 1) {
            let s = 0; sa.forEach(v => { s += Math.pow(10, (v + 0.691) / 10); });
            const rel = -0.691 + 10 * Math.log10(s / sa.length) - 20, v = sa.filter(x => x > rel).sort((a, b) => a - b);
            L.lra = v.length > 1 ? Math.max(0, v[Math.min(v.length - 1, Math.floor(0.95 * (v.length - 1) + 0.5))] - v[Math.floor(0.10 * (v.length - 1) + 0.5)]) : 0;
        } else L.lra = 0;
    };
    // tap the master output
    const WORKLET = `class P extends AudioWorkletProcessor{constructor(){super();this.l=new Float32Array(2048);this.r=new Float32Array(2048);this.n=0}
process(i){const x=i[0];if(!x||!x[0])return true;const a=x[0],b=x[1]||x[0];for(let k=0;k<a.length;k++){this.l[this.n]=a[k];this.r[this.n]=b[k];if(++this.n===2048){this.port.postMessage({l:this.l,r:this.r});this.l=new Float32Array(2048);this.r=new Float32Array(2048);this.n=0}}return true}}
registerProcessor('am-lufs',P)`;
    async function attach() {
        const st = window.state, c = st && st.ctx, n = st && st.nodes, lim = n && n.masterLimiter;
        if (!c || !lim || !rt()) return;
        const want = (window.meterSource && window.meterSource.node()) || lim;           // the master, or the source chosen in METER SOURCE
        if (L.ctx === c && L.src === want) return;
        if (L.pending === c) return;
        if (L.ctx === c && L.node && L.src) {                                              // only the source changes: same meter, new input, measurement restarted
            try { L.src.disconnect(L.node); } catch (e) { /* already gone */ }
            try { want.connect(L.node); L.src = want; L.reset(); } catch (e) { L.src = null; }
            return;
        }
        L.ctx = c; L.src = want; L.setRate(c.sampleRate); L.sub = []; L.subAcc = 0; L.subN = 0;
        try { if (L.node) { L.node.disconnect(); if (L.node.port) L.node.port.onmessage = null; } } catch (e) { /* already gone */ }
        L.node = null; L.mode = '';
        const sink = rt().sink();
        try {
            if (c.audioWorklet && window.isSecureContext !== false) {
                L.pending = c;
                const url = URL.createObjectURL(new Blob([WORKLET], { type: 'application/javascript' }));
                try { await c.audioWorklet.addModule(url); } finally { URL.revokeObjectURL(url); }
                L.pending = null;
                if (L.ctx !== c) return;
                const w = new AudioWorkletNode(c, 'am-lufs', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1], channelCount: 2, channelCountMode: 'explicit' });
                w.port.onmessage = e => L.feed(e.data.l, e.data.r);
                want.connect(w); w.connect(sink); L.node = w; L.mode = 'worklet';
                return;
            }
        } catch (e) { L.pending = null; /* no worklet here: use the script processor below */ }
        try {
            const sp = c.createScriptProcessor(4096, 2, 2);
            sp.onaudioprocess = e => L.feed(new Float32Array(e.inputBuffer.getChannelData(0)), new Float32Array(e.inputBuffer.getChannelData(e.inputBuffer.numberOfChannels > 1 ? 1 : 0)));
            want.connect(sp); sp.connect(sink); L.node = sp; L.mode = 'script';
        } catch (e) { L.mode = 'none'; }
    }
    const fmt = (v, u) => (isFinite(v) && v > -90 ? v.toFixed(1) + ' ' + u : '-INF ' + u);
    function paint() {
        const set = (id, t) => { const e = $(id); if (e && e.textContent !== t) e.textContent = t; };
        const tab = $('tab-lufs'); if (!tab || tab.classList.contains('tab-hidden')) return;
        set('lufs-live-val', isFinite(L.integrated) ? L.integrated.toFixed(1) + ' LUFS' : '-- LUFS');
        set('lufs-short-term', fmt(L.shortTerm, 'LUFS'));
        set('lufs-momentary', fmt(L.momentary, 'LUFS'));
        set('lufs-true-peak', fmt(L.truePeak, 'dBTP'));
        set('lufs-lra-val', L.lra.toFixed(1) + ' LU');
        const tg = window.state && window.state.lufsTarget != null ? window.state.lufsTarget : -14;
        set('lufs-target-disp', 'PROGRAM TARGET: ' + tg.toFixed(1) + ' LUFS' + (isFinite(L.integrated) ? ' (' + (L.integrated - tg >= 0 ? '+' : '') + (L.integrated - tg).toFixed(1) + ' LU)' : ''));
        const note = $('lufs-lra-note');
        if (note) set('lufs-lra-note', !isFinite(L.integrated) ? '(measuring)' : L.lra < 3 ? '(Very compressed)' : L.lra < 8 ? '(Dynamic Master)' : L.lra < 14 ? '(Wide)' : '(Very wide)');
        const red = window.state && window.state.nodes && window.state.nodes.masterLimiter ? -window.state.nodes.masterLimiter.reduction : 0;
        const bar = $('lufs-gr-bar'); if (bar) bar.style.width = Math.max(0, Math.min(100, (red || 0) / 12 * 100)).toFixed(1) + '%';
        set('lufs-gr-val', (red > 0.05 ? '-' + red.toFixed(1) : '0.0') + ' dB');
        const eng = window.lufsEngine;
        if (eng) { eng.integrated = L.integrated; eng.shortTerm = L.shortTerm; eng.momentary = L.momentary; eng.lra = L.lra; eng.truePeakDbTp = L.truePeak; eng.gainReduction = red || 0; }
        const tp = $('lufs-true-peak'); if (tp) tp.style.color = L.truePeak > -1 ? '#f43f5e' : '';
        set('lufs-engine-note', L.mode === 'none' ? 'METER UNAVAILABLE (no audio engine)' : L.samples ? 'LIVE • ' + (L.fs / 1000).toFixed(1) + ' kHz' + (L.engine === 'webassembly' ? ' • WASM' : '') : 'WAITING FOR AUDIO ENGINE');
    }
    const wait = setInterval(() => { if (window.rtEngine) { clearInterval(wait); rt().on(() => { attach(); paint(); }); } }, 200);
    document.addEventListener('click', e => { const b = e.target.closest && e.target.closest('#lufs-card-int'); if (b) { L.reset(); if (window.notify) window.notify('Loudness meter reset (integrated, range, true peak)', 'rotate-left'); } });
})();
    