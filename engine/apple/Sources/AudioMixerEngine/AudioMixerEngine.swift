// Swift API of the Audio Mixer engine (a thin layer over the embedded C core: no copies of the algorithms).
import CAudioMixerCore

/// Peak / RMS / hold of blocks of samples with clip detection (3 samples in a row at the threshold, or one at full scale; latched).
public final class LevelMeter {
    private var state = amc_meter_t()
    private var clip = amc_clip_cfg_t()
    public init(clipThresholdDb: Double = -0.1) { amc_meter_init(&state); amc_clip_cfg_init(&clip, clipThresholdDb) }
    /// One reading window; `nowMs` is a monotonic time in milliseconds.
    public func push(_ samples: [Float], nowMs: Double) { samples.withUnsafeBufferPointer { amc_meter_push(&state, $0.baseAddress, $0.count, nowMs, &clip) } }
    public var peak: Double { state.pk }
    public var rms: Double { state.rms }
    public var hold: Double { state.hold }
    public var clipCount: Int { Int(state.clip_n) }
    public var isClipLatched: Bool { state.latched != 0 }
    public func isClipLit(nowMs: Double) -> Bool { amc_meter_lit(&state, nowMs, &clip) != 0 }
    public func clearClip() { amc_meter_clear_clip(&state) }
}

/// ITU-R BS.1770-4 / EBU R128 loudness: momentary, short-term, integrated, loudness range and 4x true peak.
public final class LoudnessMeter {
    private let p = UnsafeMutablePointer<amc_loudness_t>.allocate(capacity: 1)
    public init(sampleRate: Double) { amc_loudness_init(p, sampleRate) }
    deinit { p.deallocate() }
    public func feed(left: [Float], right: [Float]) {
        let n = min(left.count, right.count)
        left.withUnsafeBufferPointer { l in right.withUnsafeBufferPointer { r in amc_loudness_feed(p, l.baseAddress, r.baseAddress, n) } }
    }
    public func reset() { amc_loudness_reset(p) }
    public var momentary: Double { p.pointee.momentary }      // LUFS
    public var shortTerm: Double { p.pointee.short_term }     // LUFS
    public var integrated: Double { p.pointee.integrated }    // LUFS, gated
    public var range: Double { p.pointee.lra }                // LU
    public var truePeak: Double { p.pointee.true_peak }       // dBTP
}

public struct PhaseResult { public var correlation: Double, peak: Double, rmsLeft: Double, rmsRight: Double, silent: Bool }
public func phaseCorrelation(left: [Float], right: [Float], widthPercent: Double = 100, invertLeft: Bool = false, invertRight: Bool = false) -> PhaseResult {
    var out = amc_phase_t()
    let n = min(left.count, right.count)
    left.withUnsafeBufferPointer { l in right.withUnsafeBufferPointer { r in amc_phase(l.baseAddress, r.baseAddress, n, widthPercent, invertLeft ? 1 : 0, invertRight ? 1 : 0, &out) } }
    return PhaseResult(correlation: out.corr, peak: out.peak, rmsLeft: out.rms_l, rmsRight: out.rms_r, silent: out.silent != 0)
}

public struct ScopeSweep { public var length: Int, start: Int, triggered: Bool, peakToPeak: Double, rms: Double, crestDb: Double, frequency: Double }
public func scopeSweep(samples: [Float], sampleRate: Double, timebaseMs: Double = 1, triggerDb: Double = -12, rising: Bool = true) -> ScopeSweep {
    var cfg = amc_scope_cfg_t(timebase_ms: timebaseMs, trigger_db: triggerDb, rising: rising ? 1 : 0)
    var out = amc_scope_t()
    samples.withUnsafeBufferPointer { amc_scope_calc($0.baseAddress, $0.count, sampleRate, &cfg, &out) }
    return ScopeSweep(length: Int(out.n), start: Int(out.start), triggered: out.triggered != 0, peakToPeak: out.p2p, rms: out.rms, crestDb: out.crest_db, frequency: out.freq)
}

/// Up to 32 mono strips (gain, pan, mute, solo) into a stereo master (gain, limiter) with meters, loudness and phase.
public final class Mixer {
    private let p = UnsafeMutablePointer<amc_mixer_t>.allocate(capacity: 1)
    public let strips: Int
    public init(strips: Int, sampleRate: Double) { amc_mixer_init(p, Int32(strips), sampleRate); self.strips = Int(p.pointee.n_strips) }
    deinit { p.deallocate() }
    public func setGain(strip: Int, db: Double) { amc_mixer_set_gain_db(p, Int32(strip), db) }
    public func setPan(strip: Int, pan: Double) { amc_mixer_set_pan(p, Int32(strip), pan) }
    public func setMute(strip: Int, _ on: Bool) { amc_mixer_set_mute(p, Int32(strip), on ? 1 : 0) }
    public func setSolo(strip: Int, _ on: Bool) { amc_mixer_set_solo(p, Int32(strip), on ? 1 : 0) }
    public func setMaster(db: Double) { amc_mixer_set_master_db(p, db) }
    public func setLimiter(on: Bool, ceilingDb: Double = -0.5, releaseMs: Double = 100) { amc_mixer_set_limiter(p, on ? 1 : 0, ceilingDb, releaseMs) }
    public func setClip(thresholdDb: Double) { amc_mixer_set_clip_db(p, thresholdDb) }
    /// `inputs[strip]` -> (left, right); every array has the same number of frames. Meters, loudness and phase are updated once per call.
    public func process(inputs: [[Float]]) -> (left: [Float], right: [Float]) {
        let frames = inputs.first?.count ?? 0
        var left = [Float](repeating: 0, count: frames), right = [Float](repeating: 0, count: frames)
        var pointers: [UnsafePointer<Float>?] = []
        var keep: [UnsafeMutablePointer<Float>] = []
        for i in 0..<strips {                                   // a strip without an input is silent
            let src = i < inputs.count ? inputs[i] : [Float](repeating: 0, count: frames)
            let buf = UnsafeMutablePointer<Float>.allocate(capacity: max(frames, 1)); buf.initialize(from: src, count: min(src.count, frames)); keep.append(buf); pointers.append(UnsafePointer(buf))
        }
        pointers.withUnsafeBufferPointer { ins in left.withUnsafeMutableBufferPointer { l in right.withUnsafeMutableBufferPointer { r in amc_mixer_process(p, ins.baseAddress, frames, l.baseAddress, r.baseAddress) } } }
        keep.forEach { $0.deallocate() }
        return (left, right)
    }
    public func stripPeak(_ strip: Int) -> Double { amc_mixer_strip_peak(p, Int32(strip)) }
    public var masterPeak: (left: Double, right: Double) { (amc_mixer_master_peak(p, 0), amc_mixer_master_peak(p, 1)) }
    public var momentaryLoudness: Double { p.pointee.loudness.momentary }
    public var integratedLoudness: Double { p.pointee.loudness.integrated }
    public var truePeak: Double { p.pointee.loudness.true_peak }
    public var correlation: Double { p.pointee.phase.corr }
}
