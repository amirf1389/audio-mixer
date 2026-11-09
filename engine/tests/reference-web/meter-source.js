// Reference code of the retired web console (AmF X1 1.22.1.0 Beta, git tag html-app-final, index.html), kept so engine/tests/gen-vectors.js can still
// produce engine/tests/vectors.txt. Do not edit: the native engines are held to these numbers.

/* METER SOURCE: the VU, LUFS, SCOPE and PHASE pages measure the source you choose, in real time: the mixer master (as before), a channel strip, any LIVE SOURCES
   interface (ASIO / WASAPI / Core Audio / ALSA / virtual drivers / Bluetooth), the microphone, a patched live input, or the music (loopback interface or shared system /
   tab audio: what Now Playing detected). The choice is saved and shared by the four pages; a source that is not there (an unplugged interface) is measured as the master
   until it returns. The SCOPE page used to draw simulated waves: it now draws the real samples (trigger, timebase, scale, X-Y and wave + FFT all act on them).
   Nothing here changes the audio path: the measuring nodes only listen (a silent sink keeps them running). */
(function () {
    'use strict';
    const $ = id => document.getElementById(id);
    const KEY = 'halx_meter_src', PAGES = ['meter', 'lufs', 'osc', 'phase'];
    const LS = () => window.liveSources, RT = () => window.rtEngine;
    const esc = v => (window.escapeHtml ? window.escapeHtml(v) : String(v == null ? '' : v).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])));
    const store = {
        get(k, d) { try { const v = JSON.parse(localStorage.getItem(k)); return v == null ? d : v; } catch (e) { return d; } },
        set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* storage blocked */ } },
    };
    const visible = id => { const t = $('tab-' + id); return !!t && !t.classList.contains('tab-hidden'); };

    // L / R analysers on a node (a mono source is up-mixed, so both sides show it)
    function makeTap(node, ctx, fft, sink) {
        const pre = ctx.createGain(); pre.channelCount = 2; pre.channelCountMode = 'explicit'; pre.channelInterpretation = 'speakers';
        const split = ctx.createChannelSplitter(2), l = ctx.createAnalyser(), r = ctx.createAnalyser();
        l.fftSize = r.fftSize = fft; l.smoothingTimeConstant = r.smoothingTimeConstant = 0;
        node.connect(pre); pre.connect(split); split.connect(l, 0); split.connect(r, 1); l.connect(sink); r.connect(sink);
        return { node, ctx, l, r, drop() { try { node.disconnect(pre); } catch (e) { /* gone */ } try { pre.disconnect(); split.disconnect(); l.disconnect(); r.disconnect(); } catch (e) { /* gone */ } } };
    }

    // The scope arithmetic on two blocks of samples (pure: bin/test.js runs it): where the sweep starts (trigger), its length, and the readings.
    //   A, B: the newest samples (the last element is the newest); st: oscState; returns { N, start, triggered, p2p, rms, crest, freq }
    function scopeCalc(A, B, fs, st) {
        const size = A.length, N = Math.max(64, Math.min(size >> 1, Math.round(fs * (st.timebase || 1) * 10 / 1000)));        // 10 divisions
        const lvl = Math.pow(10, (st.triggerLevel == null ? -12 : st.triggerLevel) / 20), rise = st.triggerEdge !== 'fall';
        let start = size - N, triggered = false;
        for (let i = size - 2 * N + 1; i < size - N; i++) {                      // the newest sweep that begins with a crossing and still has N samples after it
            if (i < 1) continue;
            if (rise ? (A[i - 1] < lvl && A[i] >= lvl) : (A[i - 1] > -lvl && A[i] <= -lvl)) { start = i; triggered = true; }
        }
        let mn = Infinity, mx = -Infinity, ss = 0, pk = 0;
        for (let i = start; i < start + N; i++) { const v = A[i]; if (v < mn) mn = v; if (v > mx) mx = v; ss += v * v; const a = v < 0 ? -v : v; if (a > pk) pk = a; }
        const rms = Math.sqrt(ss / N);
        // frequency from rising zero crossings over the newest 8192 samples (with a little hysteresis against noise)
        const from = Math.max(1, size - 8192), hy = Math.max(0.002, pk * 0.05); let first = -1, last = -1, n = 0, armed = false;
        for (let i = from; i < size; i++) {
            if (A[i] < -hy) armed = true;
            else if (armed && A[i - 1] < 0 && A[i] >= 0) { const t = i - 1 + (-A[i - 1]) / (A[i] - A[i - 1] || 1); if (first < 0) first = t; last = t; n++; armed = false; }
        }
        return { N, start, triggered, p2p: isFinite(mx - mn) ? mx - mn : 0, rms, crest: rms > 1e-6 ? 20 * Math.log10(pk / rms) : 0, freq: n >= 2 ? (n - 1) * fs / (last - first) : 0 };
    }

    const ms = window.meterSource = {
        key: store.get(KEY, 'master'), eff: 'master', tapA: null, tapS: null, sel: null, sig: '', scopeScratch: null,
        scopeCalc,
        // everything that can be measured right now
        entries() {
            const out = [], st = window.state, c = st && st.ctx, n = (st && st.nodes) || {}, rt = RT(), l = LS();
            if (!c) return out;
            if (n.masterLimiter) out.push({ key: 'master', group: 'Mixer', name: 'Master (mix bus)', node: n.masterLimiter });
            const gains = n.channelPostGains || [];
            for (let i = 0; i < gains.length; i++) { const lane = $('mixer-lane-' + i); if (lane && lane.classList.contains('lane-locked')) continue; out.push({ key: 'ch:' + i, group: 'Mixer', name: 'CH ' + (i + 1) + ' (channel strip)', node: gains[i] }); }
            if (rt) rt.sources().forEach(s => { const node = s.node || s.an; if (node) out.push({ key: s.key, group: 'Sources', name: (s.music ? '♫ ' : '') + s.name, how: s.how, node, music: !!s.music }); });
            if (l && l.music && l.music.node && l.music.node.context === c && !out.some(e => e.key === l.music.key)) out.push({ key: l.music.key, group: 'Sources', name: '♫ ' + (l.music.name || 'Music'), how: l.music.how, node: l.music.node, music: true });
            return out;
        },
        entry() { const e = this.entries(); return e.find(x => x.key === this.key) || e.find(x => x.key === 'master') || null; },
        node() { const e = this.entry(); return e ? e.node : null; },       // the node the LUFS meter listens to
        isMaster() { const e = this.entry(); return !e || e.key === 'master'; },
        name() { const e = this.entry(); return e ? e.name : 'Master (mix bus)'; },
        set(key, quiet) {
            if (key === this.key) return;
            this.key = key; store.set(KEY, key);
            this.tick(performance.now());
            if (!quiet && window.notify) window.notify('Meters follow: ' + this.name(), 'gauge');
        },
        // L / R analysers of the chosen source for the phase page and the VU meter (the master keeps its own)
        analysers() {
            const n = window.state && window.state.nodes;
            if (this.isMaster() || !this.tapA) return n && n.phaseAnL ? { L: n.phaseAnL, R: n.phaseAnR } : null;
            return { L: this.tapA.l, R: this.tapA.r };
        },
        // smoothed meter state for the VU page
        vu() { const rt = RT(); return this.isMaster() || !this.sel ? (rt && rt.master) : this.sel; },
        vuLabel(side) { if (this.isMaster()) return null; const nm = this.name().replace(/^♫\s*/, ''); return (nm.length > 16 ? nm.slice(0, 15) + '…' : nm) + ' / ' + side; },
        // the scope listens with longer analysers (32768 samples)
        scopeTap() {
            const c = window.state && window.state.ctx, e = this.entry(), rt = RT(); if (!c || !e || !rt) return null;
            if (this.tapS && (this.tapS.ctx !== c || this.tapS.node !== e.node)) { this.tapS.drop(); this.tapS = null; }
            if (!this.tapS) { try { this.tapS = makeTap(e.node, c, 32768, rt.sink()); } catch (err) { this.tapS = null; } }
            return this.tapS;
        },
        rewire() {
            if (this.tapA) { this.tapA.drop(); this.tapA = null; }
            if (this.tapS) { this.tapS.drop(); this.tapS = null; }
            this.sel = null;
            const c = window.state && window.state.ctx, e = this.entry(), rt = RT();
            if (!c || !e || !rt || e.key === 'master') return;
            try { this.tapA = makeTap(e.node, c, 2048, rt.sink()); this.sel = { L: rt.meter(), R: rt.meter() }; } catch (err) { this.tapA = null; }
        },
        tick(now) {
            const e = this.entry(), eff = e ? e.key : 'master', c = window.state && window.state.ctx, rt = RT();
            if (eff !== this.eff || (this.tapA && this.tapA.ctx !== c)) { this.eff = eff; this.rewire(); document.dispatchEvent(new CustomEvent('metersourcechange', { detail: { key: eff, name: this.name() } })); }
            if (this.tapA && this.sel && rt) { try { rt.smooth(this.sel.L, rt.read(this.tapA.l), now); rt.smooth(this.sel.R, rt.read(this.tapA.r), now); } catch (err) { /* node closed */ } }
            this.ui();
        },
        // the source row on each page and the select of the scope
        ui() {
            if (!PAGES.some(visible)) return;
            const list = this.entries(), sig = list.map(e => e.key + '|' + e.name).join(';') + '#' + this.key + '#' + this.eff;
            PAGES.forEach(id => {
                const inner = document.querySelector('#tab-' + id + ' .page-scroll-inner');
                if (!inner || inner.querySelector('.meter-src-row')) return;
                const row = document.createElement('div');
                row.className = 'meter-src-row glass-panel px-3 py-2 rounded-xl flex flex-wrap items-center gap-2 text-[8px] font-mono';
                row.innerHTML = '<span class="text-cyan-400 font-black uppercase tracking-widest whitespace-nowrap"><i class="fa-solid fa-diagram-project mr-1"></i>METER SOURCE</span><select class="meter-src-sel flex-1 min-w-[160px] bg-black text-slate-200 border border-white/20 rounded px-1.5 py-1 text-[8px] font-bold" aria-label="Meter source"></select><span class="meter-src-note text-slate-400"></span>';
                row.querySelector('select').onchange = ev => ms.set(ev.target.value);
                const first = inner.querySelector(':scope > .glass-panel'); inner.insertBefore(row, first || null);
                this.sig = '';
            });
            const sels = [...document.querySelectorAll('.meter-src-sel'), $('osc-source-sel')].filter(Boolean);
            if (sig !== this.sig || sels.some(s => !s.options.length)) {
                this.sig = sig;
                const group = g => { const items = list.filter(e => e.group === g); return items.length ? '<optgroup label="' + g + '">' + items.map(e => '<option value="' + esc(e.key) + '">' + esc(e.name) + (e.how ? ' (' + esc(e.how) + ')' : '') + '</option>').join('') + '</optgroup>' : ''; };
                const missing = !list.some(e => e.key === this.key) ? '<option value="' + esc(this.key) + '" disabled>' + esc(this.key) + ' (not available now: measuring the master)</option>' : '';
                const html = missing + group('Mixer') + group('Sources');
                sels.forEach(s => { s.innerHTML = html; s.value = this.key; });
                document.querySelectorAll('.meter-src-note').forEach(n => { n.textContent = list.length > 1 ? list.length - 1 + ' to choose from' : 'start the audio engine'; });
            }
            sels.forEach(s => { if (s.value !== this.key && s !== document.activeElement) s.value = this.key; });
        },

        // ── SCOPE: real traces ──
        drawScope(ctx, w, h, st) {
            const c = window.state && window.state.ctx, say = t => { ctx.fillStyle = '#84cc16'; ctx.font = '12px monospace'; ctx.textAlign = 'center'; ctx.fillText(t, w / 2, h / 2); ctx.textAlign = 'left'; };
            const tap = c ? this.scopeTap() : null;
            if (!tap) return say('AUDIO ENGINE OFF: start it to see the real signal');
            const fs = c.sampleRate, size = tap.l.fftSize, sc = this.scopeScratch && this.scopeScratch.A.length === size ? this.scopeScratch : (this.scopeScratch = { A: new Float32Array(size), B: new Float32Array(size), F: new Float32Array(size >> 1) });
            tap.l.getFloatTimeDomainData(sc.A); tap.r.getFloatTimeDomainData(sc.B);
            let f = st._frozen, calc;
            if (f && st.triggerMode === 'single') { calc = f.calc; sc.A.set(f.A); sc.B.set(f.B); }                       // SINGLE: the captured sweep stays
            else {
                calc = ms.scopeCalc(sc.A, sc.B, fs, st);
                if (st.triggerMode === 'norm' && !calc.triggered) { if (st._last) { calc = st._last.calc; sc.A.set(st._last.A); sc.B.set(st._last.B); } else return say('WAITING FOR TRIGGER'); }   // NORM: wait for a trigger, keep the last sweep
                else if (st.triggerMode === 'single') { if (!calc.triggered) return say('ARMED: WAITING FOR TRIGGER'); st._frozen = { calc, A: sc.A.slice(), B: sc.B.slice() }; }
                else if (st.triggerMode === 'norm') st._last = { calc, A: sc.A.slice(), B: sc.B.slice() };   // calc.triggered here
            }
            const { N, start } = calc, cy = h / 2, volts = st.voltageScale === 'auto' ? Math.max(0.02, calc.p2p / 2 / 0.9) : parseFloat(st.voltageScale) || 1, pxFs = (h / 2) * 0.9 / volts;
            const glow = st.displayMode === 'phosphor' ? 8 : 3;
            const trace = (arr, col, lw) => {
                ctx.beginPath(); ctx.lineWidth = lw; ctx.strokeStyle = col; ctx.shadowColor = col; ctx.shadowBlur = glow;
                const per = N / w;
                if (per > 2) { for (let x = 0; x < w; x++) { let lo = Infinity, hi = -Infinity; const a = start + Math.floor(x * per), b = Math.min(start + N, start + Math.floor((x + 1) * per) + 1); for (let i = a; i < b; i++) { const v = arr[i]; if (v < lo) lo = v; if (v > hi) hi = v; } if (lo <= hi) { ctx.moveTo(x, cy - hi * pxFs); ctx.lineTo(x, cy - lo * pxFs); } } }
                else for (let i = 0; i < N; i++) { const x = i / (N - 1) * w, y = cy - arr[start + i] * pxFs; if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); }
                ctx.stroke();
            };
            if (st.displayMode === 'xy') {                                                                                // Lissajous: L against R
                ctx.beginPath(); ctx.lineWidth = 1.2; ctx.strokeStyle = '#a855f7'; ctx.shadowColor = '#a855f7'; ctx.shadowBlur = 6;
                const m = Math.min(N, 4096), k = Math.min(w, h) / 2 * 0.9 / volts;
                for (let i = 0; i < m; i++) { const x = w / 2 + sc.A[start + i] * k, y = cy - sc.B[start + i] * k; if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); }
                ctx.stroke();
            } else {
                trace(sc.A, '#f59e0b', 1.6); trace(sc.B, '#06b6d4', 1.3);
                if (st.displayMode === 'fft') {                                                                              // wave + the real spectrum of the left side
                    tap.l.getFloatFrequencyData(sc.F);
                    ctx.beginPath(); ctx.lineWidth = 1; ctx.strokeStyle = '#a3e635'; ctx.shadowBlur = 0;
                    const fmin = 20, fmax = Math.min(20000, fs / 2), bin = fs / size;
                    for (let x = 0; x < w; x++) { const fr = fmin * Math.pow(fmax / fmin, x / (w - 1)), v = sc.F[Math.min(sc.F.length - 1, Math.round(fr / bin))], y = h - Math.max(0, Math.min(1, (v + 100) / 100)) * h * 0.5; if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); }
                    ctx.stroke();
                }
            }
            ctx.shadowBlur = 0;
            const set = (id, t) => { const e = $(id); if (e && e.textContent !== t) e.textContent = t; };
            const nm = this.name().replace(/^♫\s*/, '');
            set('osc-ch-a-label', nm + ' L'); set('osc-ch-b-label', nm + ' R');
            const now = performance.now();
            if (!st._tel || now - st._tel > 150) {                                                                            // readings 6 times a second: readable
                st._tel = now;
                set('osc-vpp-val', calc.p2p.toFixed(2) + ' FS'); set('osc-vrms-val', calc.rms.toFixed(3) + ' FS');
                set('osc-freq-val', calc.freq ? (calc.freq >= 1000 ? (calc.freq / 1000).toFixed(2) + ' kHz' : calc.freq.toFixed(1) + ' Hz') : '-- Hz'); set('osc-crest-val', calc.rms > 1e-6 ? calc.crest.toFixed(1) + ' dB' : '-- dB');
                set('osc-trig-status', st.triggerMode.toUpperCase() + (calc.triggered ? ' (LOCKED)' : st.triggerMode === 'auto' ? ' (FREE RUN)' : ' (WAITING)'));
            }
            return true;
        },
    };
    window.setOscSource = function (val) { ms.set(val); };                         // the scope's own select: the same choice as the other pages
    const waitRt = setInterval(() => { if (window.rtEngine) { clearInterval(waitRt); window.rtEngine.on(now => { try { ms.tick(now); } catch (e) { /* a page failed: the others go on */ } }); } }, 200);
})();
    