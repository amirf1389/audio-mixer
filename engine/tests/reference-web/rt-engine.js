// Reference code of the retired web console (AmF X1 1.22.1.0 Beta, git tag html-app-final, index.html), kept so engine/tests/gen-vectors.js can still
// produce engine/tests/vectors.txt. Do not edit: the native engines are held to these numbers.
const rt = window.rtEngine = {
        subs: [], st: {}, ch: [], chAn: [], ctxFor: null, last: 0, frames: 0, hz: 0,
        sink() { const l = LS(); if (l && l.sink && l.sink.context === window.state.ctx) return l.sink; const c = window.state.ctx; if (!this._sink || this._sinkCtx !== c) { this._sink = c.createGain(); this._sink.gain.value = 0; this._sink.connect(c.destination); this._sinkCtx = c; } return this._sink; },
        // stereo analysers on a node (a mono source shows the same level on both sides)
        tap(node) {
            const c = window.state.ctx;
            if (node._rtTap && node._rtTap.ctx === c) return node._rtTap;
            const split = c.createChannelSplitter(2), l = c.createAnalyser(), r = c.createAnalyser();
            l.fftSize = r.fftSize = 512; l.smoothingTimeConstant = r.smoothingTimeConstant = 0;
            node.connect(split); split.connect(l, 0); split.connect(r, 1);
            l.connect(this.sink()); r.connect(this.sink());
            return (node._rtTap = { ctx: c, split, l, r });
        },
        read(an) {
            const b = an._rtBuf && an._rtBuf.length === an.fftSize ? an._rtBuf : (an._rtBuf = new Float32Array(an.fftSize));
            an.getFloatTimeDomainData(b);
            // clip detection: samples at / over the clip threshold, and the longest run of them in a row (a flat-topped wave clips, a lone peak does not)
            let pk = 0, ss = 0, run = 0, maxRun = 0, over = 0;
            const lin = this.clip.lin;
            for (let i = 0; i < b.length; i++) {
                const a = b[i] < 0 ? -b[i] : b[i];
                if (a > pk) pk = a;
                ss += b[i] * b[i];
                if (a >= lin) { over++; if (++run > maxRun) maxRun = run; } else run = 0;
            }
            return { pk, rms: Math.sqrt(ss / b.length), run: maxRun, over };
        },
        // smoothed meter state of one reading: fast attack, slow release, peak hold, clip hold
        smooth(s, v, now) {
            s.pk = v.pk >= s.pk ? v.pk : s.pk * 0.9 + v.pk * 0.1;
            s.rms = v.rms >= s.rms ? v.rms : s.rms * 0.9 + v.rms * 0.1;
            if (v.pk >= s.hold || now - s.holdAt > 1400) { s.hold = v.pk; s.holdAt = now; }
            this.clip.detect(s, v, now);
            return s;
        },
        meter() { return { pk: 0, rms: 0, hold: 0, holdAt: 0, clipAt: 0, clipN: 0, clipLatched: false, clipPk: 0 }; },
        // ── clip detection for every meter (sources, channel strips, master) ──
        // A reading CLIPS when CLIP_RUN samples in a row reach the threshold, or one sample reaches digital full scale (>= 1.0).
        // Each clip is counted (events closer than 250 ms are one event), stays latched until it is cleared, and lights the CLIP badge for 1.5 s.
        clip: {
            RUN: 3, HOLD_MS: 1500, db: -0.1, lin: Math.pow(10, -0.1 / 20),
            detect(s, v, now) {
                if (v.run >= this.RUN || v.pk >= 1) {
                    if (now - s.clipAt > 250) s.clipN++;
                    s.clipAt = now; s.clipLatched = true;
                    if (v.pk > s.clipPk) s.clipPk = v.pk;
                }
            },
            lit(s, now) { return !!s && now - s.clipAt < this.HOLD_MS; },
            clear(s) { if (s) { s.clipN = 0; s.clipLatched = false; s.clipPk = 0; s.clipAt = 0; } },
            set(db) {
                const n = Math.max(-12, Math.min(0, Number(db)));
                if (!isFinite(n)) return this.db;
                this.db = n; this.lin = Math.pow(10, n / 20);
                try { localStorage.setItem('halx_clip_db', String(n)); } catch (e) { /* storage blocked */ }
                return n;
            },
            // every meter state that exists: [label, state]
            all() {
                const out = [], r = window.rtEngine;
                Object.keys(r.st).forEach(k => { out.push([r.st[k].name + ' L', r.st[k].L]); out.push([r.st[k].name + ' R', r.st[k].R]); });
                r.ch.forEach((s, i) => { if (s) out.push(['CH ' + (i + 1), s]); });
                if (r.master) { out.push(['MASTER L', r.master.L]); out.push(['MASTER R', r.master.R]); }
                return out;
            },
            total() { return this.all().reduce((n, x) => n + x[1].clipN, 0); },
            clearAll() { this.all().forEach(x => this.clear(x[1])); if (window.asioMeters && window.asioMeters.clearClips) window.asioMeters.clearClips(); },
        },
        // every source input that exists right now
        sources() {
            const out = [], l = LS(), c = window.state && window.state.ctx;
            if (!c) return out;
            if (l) l.caps.forEach((cap, key) => { if (cap && cap.node && cap.node.context === c) out.push({ key, name: cap.name || key, how: cap.how || '', node: cap.node, cap, music: !!cap.music }); });
            const n = window.state.nodes || {};
            if (window.state.micActive && n.micAnalyser) out.push({ key: 'mic:default', name: 'Default microphone', how: 'Web Audio', an: n.micAnalyser });
            const R = window.routingEngine;
            if (R && Array.isArray(R.live)) R.live.forEach(x => { if (x && x.src && !x.shared && x.src.context === c) out.push({ key: 'live:' + x.ch, name: x.label + ' (CH ' + (x.ch + 1) + ')', how: 'Live input patch', node: x.src }); });
            return out;
        },
        // the channel strips: level at the strip's input (where a patched source enters)
        chLevel(i) { const s = this.ch[i]; return s ? pct(dbOf(s.pk)) / 100 : 0; },
        frame(now) {
            requestAnimationFrame(t => rt.frame(t));
            if (now - this.last < 30) return;
            this.hz = this.hz * 0.9 + (1000 / Math.max(1, now - this.last)) * 0.1; this.last = now; this.frames++;
            const c = window.state && window.state.ctx, n = (window.state && window.state.nodes) || {};
            if (!c) { this.subs.forEach(f => { try { f(now); } catch (e) { /* a page failed: the others go on */ } }); return; }
            // sources
            const seen = {};
            this.sources().forEach(s => {
                seen[s.key] = 1;
                const m = this.st[s.key] || (this.st[s.key] = { L: this.meter(), R: this.meter() });
                m.name = s.name; m.how = s.how; m.music = s.music; m.cap = s.cap; m.bt = !!(window.btInput && window.btInput.isBtKey(s.key));
                try {
                    if (s.an) { const v = this.read(s.an); this.smooth(m.L, v, now); this.smooth(m.R, v, now); }
                    else { const t = this.tap(s.node), a = this.read(t.l), b = this.read(t.r); this.smooth(m.L, a, now); this.smooth(m.R, (b.pk === 0 && a.pk > 0) ? a : b, now); }
                } catch (e) { /* node closed */ }
            });
            Object.keys(this.st).forEach(k => { if (!seen[k]) delete this.st[k]; });
            // channel strips and master: only while a page that shows them is open (cheap, but no reason to spend it otherwise)
            const needCh = visible('mixer') || visible('meter') || visible('bus') || this.guardNeeded;
            if (needCh && n.channelPostGains) {
                for (let i = 0; i < n.channelPostGains.length; i++) {
                    if (!this.chAn[i] || this.chAn[i].ctx !== c) { const a = c.createAnalyser(); a.fftSize = 256; a.smoothingTimeConstant = 0; n.channelPostGains[i].connect(a); a.connect(this.sink()); a.ctx = c; this.chAn[i] = a; this.ch[i] = this.meter(); }
                    this.smooth(this.ch[i], this.read(this.chAn[i]), now);
                }
            }
            if (n.phaseAnL && n.phaseAnR) {
                const a = this.read(n.phaseAnL), b = this.read(n.phaseAnR);
                this.master = this.master || { L: this.meter(), R: this.meter() };
                this.smooth(this.master.L, a, now); this.smooth(this.master.R, b, now);
            }
            this.subs.forEach(f => { try { f(now); } catch (e) { /* a page failed: the others go on */ } });
        },
        on(f) { this.subs.push(f); },
        start() { if (!this.started) { this.started = true; requestAnimationFrame(t => rt.frame(t)); } },
    };
