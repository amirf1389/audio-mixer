// Reference code of the retired web console (AmF X1 1.22.1.0 Beta, git tag html-app-final, index.html), kept so engine/tests/gen-vectors.js can still
// produce engine/tests/vectors.txt. Do not edit: the native engines are held to these numbers.
window.readStereoPhase = function() {
            const n = window.state && window.state.nodes;
            const an = window.meterSource ? window.meterSource.analysers() : (n && n.phaseAnL ? { L: n.phaseAnL, R: n.phaseAnR } : null);   // the master, or the source chosen in METER SOURCE
            if (!an) return null;
            const size = an.L.fftSize;
            if (!window._phaseBufL || window._phaseBufL.length !== size) {
                window._phaseBufL = new Float32Array(size);
                window._phaseBufR = new Float32Array(size);
            }
            const L = window._phaseBufL, R = window._phaseBufR;
            an.L.getFloatTimeDomainData(L);
            an.R.getFloatTimeDomainData(R);
            // In the page the analysers sit AFTER the real width / polarity stages (window.applyPolarity, setMsWidth), so the meter reads the
            // signal as heard and applies nothing itself (neutral settings). Without those stages (the engine reference / native-engine vectors)
            // the same math still simulates width and polarity, so every engine stays held to the same numbers.
            const real = !!(window.state && window.state.nodes && window.state.nodes.msMidHalfL);
            const ps = real ? {} : (window.phaseScopeState || {});
            const width = (ps.stereoWidth == null ? 100 : ps.stereoWidth) / 100;
            let sLR = 0, sLL = 0, sRR = 0, peak = 0;
            for (let i = 0; i < size; i++) {
                let l = ps.invertL ? -L[i] : L[i];
                let r = ps.invertR ? -R[i] : R[i];
                const mid = (l + r) * 0.5, side = (l - r) * 0.5 * width;
                l = mid + side; r = mid - side;
                L[i] = l; R[i] = r;
                sLR += l * r; sLL += l * l; sRR += r * r;
                const a = Math.max(Math.abs(l), Math.abs(r));
                if (a > peak) peak = a;
            }
            const den = Math.sqrt(sLL * sRR);
            const corr = peak < 0.0005 || den === 0 ? 0 : Math.max(-1, Math.min(1, sLR / den));
            // 10-frame moving average (~0.17 s) steadies the reading; angle = equivalent L/R phase offset in degrees
            const hist = window._phaseCorrHist || (window._phaseCorrHist = []);
            if (peak >= 0.0005) { hist.push(corr); if (hist.length > 10) hist.shift(); } else hist.length = 0;
            const avg = hist.length ? hist.reduce((a, b) => a + b, 0) / hist.length : 0;
            return { l: L, r: R, corr: corr, corrAvg: avg, angle: Math.acos(Math.max(-1, Math.min(1, avg))) * 180 / Math.PI, peak: peak, silent: peak < 0.0005, rmsL: Math.sqrt(sLL / size), rmsR: Math.sqrt(sRR / size) };
        };