package com.amfb.amixos

import kotlin.math.abs
import kotlin.math.log2
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sqrt

/** SOUND SCORE on the phone: the console's own rules (engine/cpp/app/measure/score.cpp), so the phone shows the score the console does.
 *  Six parts, 0..100: LOUDNESS 20, PEAKS 20, DYNAMICS 15, TONE 20, STEREO 15, STABILITY 10. Silence (below -60 LUFS) gives no score.
 *  The spectrum is given with its band frequencies (the console's RTA: 1/6-octave bands, 20 Hz .. 20 kHz). Pure, tested on the JVM. */
object SoundScore {
    data class Input(val shortTerm: Double = -120.0, val target: Double = -14.0, val truePeak: Double = -120.0, val peakDb: Double = -120.0, val rmsDb: Double = -120.0,
                     val correlation: Double = 1.0, val freqs: List<Double> = emptyList(), val spectrum: List<Double> = emptyList(),
                     val clipStrips: Int = 0, val overCompStrips: Int = 0, val xruns: Long = 0, val masterClip: Boolean = false)
    data class Part(val name: String, val points: Double, val max: Double, val tip: String)
    data class Score(val valid: Boolean, val total: Double, val grade: String, val parts: List<Part>, val tilt: Double, val roughness: Double)

    private fun clamp(v: Double, lo: Double, hi: Double) = if (v < lo) lo else if (v > hi) hi else v
    private fun f1(v: Double) = String.format("%.1f", v)

    fun score(i: Input): Score {
        if (i.shortTerm < -60) return Score(false, 0.0, "", emptyList(), 0.0, 0.0)
        val parts = ArrayList<Part>()
        // LOUDNESS
        run { val off = abs(i.shortTerm - i.target); val p = clamp(20 - max(0.0, off - 1) * 4, 0.0, 20.0)
            parts += Part("LOUDNESS", p, 20.0, if (off <= 1) "On target." else if (i.shortTerm > i.target) "${f1(off)} LU too loud: bring the master down or lower the limiter's gain." else "${f1(off)} LU too quiet: raise the master or use AUTO LEVEL.") }
        // PEAKS
        run { var p = 20.0; var tip = "Peaks have room."
            if (i.masterClip) { p = 0.0; tip = "The master clipped: lower the master or put the MAXIMIZER last with its ceiling at -1 dB." }
            else if (i.truePeak > -1) { p = clamp(20 - (i.truePeak + 1) * 10, 0.0, 20.0); tip = "True peak ${f1(i.truePeak)} dBTP: keep it under -1 dBTP so encoders (MP3, AAC, streaming) do not distort." }
            parts += Part("PEAKS", p, 20.0, tip) }
        // DYNAMICS
        run { val crest = i.peakDb - i.rmsDb; var p = 15.0; var tip = "Crest factor ${f1(crest)} dB: natural."
            if (crest < 8) { p = clamp(15 - (8 - crest) * 3, 0.0, 15.0); tip = "Crest factor ${f1(crest)} dB: squashed. Ease the compressors or use DE-SQUASH." }
            else if (crest > 14) { p = clamp(15 - (crest - 14) * 1.5, 5.0, 15.0); tip = "Crest factor ${f1(crest)} dB: very spiky; a little bus compression (GLUE COMP) would hold it together." }
            if (i.overCompStrips > 0) { p = max(0.0, p - 2.0 * i.overCompStrips); tip += " ${i.overCompStrips} channel(s) over-compressed." }
            parts += Part("DYNAMICS", p, 15.0, tip) }
        // TONE: a straight line through the bands that carry the music (40 Hz .. 16 kHz), then how far the spectrum strays from it
        var tilt = 0.0; var rough = 0.0
        run { var p = 10.0; var tip = "Not enough spectrum to judge."
            if (i.spectrum.size >= 20 && i.freqs.size == i.spectrum.size) {
                val x = ArrayList<Double>(); val y = ArrayList<Double>()
                for (k in i.spectrum.indices) { val f = i.freqs[k]; if (f < 40 || f > 16000 || i.spectrum[k] < -100) continue; x += log2(f); y += i.spectrum[k] }
                if (x.size >= 10) {
                    val mx = x.average(); val my = y.average(); var sxy = 0.0; var sxx = 0.0
                    for (k in x.indices) { sxy += (x[k] - mx) * (y[k] - my); sxx += (x[k] - mx) * (x[k] - mx) }
                    val slope = if (sxx > 0) sxy / sxx else 0.0; var r2 = 0.0; var worst = 0.0; var wi = 0
                    for (k in x.indices) { val r = y[k] - (my + slope * (x[k] - mx)); r2 += r * r; if (abs(r) > abs(worst)) { worst = r; wi = k } }
                    rough = sqrt(r2 / x.size); tilt = slope
                    val tiltOff = if (slope > -1) slope + 1 else if (slope < -5) -5 - slope else 0.0
                    p = clamp(20 - tiltOff * 3 - max(0.0, rough - 3) * 2.5 - max(0.0, abs(worst) - 6) * 0.8, 0.0, 20.0)
                    val wf = 2.0.pow(x[wi]); val where = if (wf >= 1000) "${f1(wf / 1000)} kHz" else "${wf.roundToInt()} Hz"
                    tip = if (slope > -1) "Bright / thin (${f1(slope)} dB per octave): lower the top or raise the low end."
                        else if (slope < -5) "Dark / heavy (${f1(slope)} dB per octave): open the top (AIR) or ease the bass."
                        else if (rough > 3) (if (worst > 0) "A bump" else "A hole") + " around $where (${f1(abs(worst))} dB): a wide, gentle EQ move there will smooth it."
                        else "Smooth and balanced."
                } }
            parts += Part("TONE", p, 20.0, tip) }
        // STEREO
        run { val c = i.correlation; var p = 15.0; var tip = "Healthy stereo."
            if (c < 0) { p = clamp(5 + c * 5, 0.0, 5.0); tip = "Out of phase: parts of the mix vanish in mono. Check polarity (Ø) and wide effects." }
            else if (c < 0.3) { p = 5 + c / 0.3 * 10; tip = "Very wide: check it in mono; narrow the low end (STEREO IMAGER)." }
            parts += Part("STEREO", p, 15.0, tip) }
        // STABILITY
        run { var p = 10.0; var tip = "No dropouts, no clipping channels."
            if (i.xruns > 0) { p -= minOf(6.0, 2.0 * i.xruns); tip = "Dropouts: raise the buffer size in SETTINGS." }
            if (i.clipStrips > 0) { p -= minOf(4.0, 1.0 * i.clipStrips); tip += " ${i.clipStrips} channel(s) clipping: lower their TRIM." }
            parts += Part("STABILITY", max(0.0, p), 10.0, tip) }
        val total = parts.sumOf { it.points }.roundToInt().toDouble()
        val grade = if (total >= 90) "A" else if (total >= 80) "B" else if (total >= 70) "C" else if (total >= 60) "D" else "E"
        return Score(true, total, grade, parts, tilt, rough)
    }
}
