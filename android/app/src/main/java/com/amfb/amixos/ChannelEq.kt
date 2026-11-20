package com.amfb.amixos

import android.content.SharedPreferences

/** THE 3-BAND EQ of each channel of the phone's own mixer (LOW, MID, HIGH, +-12 dB) and the TONE presets of the preset pack on it.
 *  It runs on the engine's ISO EQ 10 in the strip's own EQ slot ([Engine.EQ_SLOT], beside the four rack slots): LOW lifts the bands up to
 *  125 Hz, MID 500 Hz .. 2 kHz, HIGH 8 kHz and up (250 Hz and 4 kHz take half of their neighbours). A tone preset's filters are laid on
 *  the same ten bands (a bell on the nearest band, a shelf on every band past its corner, a cut filter as -12 dB past it); LOW, MID and HIGH
 *  then move from there. A flat channel has no EQ loaded at all. Channels as the mixer names them: input s = s, deck d = 100 + d. Pure. */
object ChannelEq {
    const val RANGE = 12.0
    val ISO = doubleArrayOf(31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0)
    val IDS = listOf("b31", "b63", "b125", "b250", "b500", "b1k", "b2k", "b4k", "b8k", "b16k")
    private val LOW = doubleArrayOf(1.0, 1.0, 1.0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
    private val MID = doubleArrayOf(0.0, 0.0, 0.0, 0.5, 1.0, 1.0, 1.0, 0.5, 0.0, 0.0)
    private val HIGH = doubleArrayOf(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.5, 1.0, 1.0)
    data class Band(val freq: Double, val gainDb: Double, val q: Double, val type: Int)
    // the filter types of a preset (the console's own numbering): 1 bell, 2 low shelf, 3 high shelf, 4 low pass, 5 high pass
    private fun nearest(f: Double) = ISO.indices.minByOrNull { Math.abs(Math.log(f / ISO[it])) } ?: 0
    /** a tone preset laid on the ten bands (dB each, not yet limited) */
    fun toneBands(bands: List<Band>, hpHz: Double = 0.0, lpHz: Double = 0.0): DoubleArray {
        val o = DoubleArray(10)
        for (b in bands) { if (b.freq <= 0) continue
            when (b.type) {
                1 -> o[nearest(b.freq)] += b.gainDb
                2 -> for (i in 0 until 10) if (ISO[i] <= b.freq) o[i] += b.gainDb
                3 -> for (i in 0 until 10) if (ISO[i] >= b.freq) o[i] += b.gainDb
                4 -> for (i in 0 until 10) if (ISO[i] > b.freq * 1.4) o[i] -= RANGE
                5 -> for (i in 0 until 10) if (ISO[i] < b.freq / 1.4) o[i] -= RANGE
            } }
        if (hpHz > 0) for (i in 0 until 10) if (ISO[i] < hpHz / 1.4) o[i] -= RANGE
        if (lpHz > 0) for (i in 0 until 10) if (ISO[i] > lpHz * 1.4) o[i] -= RANGE
        return o
    }
    /** the ten band gains a channel gets: its tone preset (or none) with LOW, MID and HIGH on top, each within +-12 dB */
    fun bands(low: Double, mid: Double, high: Double, tone: DoubleArray? = null): DoubleArray =
        DoubleArray(10) { i -> ((tone?.getOrNull(i) ?: 0.0) + low * LOW[i] + mid * MID[i] + high * HIGH[i]).coerceIn(-RANGE, RANGE) }
    fun flat(b: DoubleArray) = b.all { Math.abs(it) < 0.05 }
    /** a gain as the ISO EQ's slider (0 .. 1, 0.5 = 0 dB) */
    fun knob(db: Double) = (0.5 + db.coerceIn(-RANGE, RANGE) / (2 * RANGE))
    fun encode(b: DoubleArray) = b.joinToString(",") { String.format(java.util.Locale.US, "%.2f", it) }
    fun decode(s: String?): DoubleArray? = s?.split(',')?.mapNotNull { it.toDoubleOrNull() }?.takeIf { it.size == 10 }?.toDoubleArray()

    // ---- kept in the phone mixer's prefs, per channel
    fun low(p: SharedPreferences, ch: Int) = p.getFloat("eqLow$ch", 0f).toDouble()
    fun mid(p: SharedPreferences, ch: Int) = p.getFloat("eqMid$ch", 0f).toDouble()
    fun high(p: SharedPreferences, ch: Int) = p.getFloat("eqHigh$ch", 0f).toDouble()
    fun toneName(p: SharedPreferences, ch: Int) = p.getString("toneName$ch", "").orEmpty()
    fun set(p: SharedPreferences, ch: Int, low: Double, mid: Double, high: Double) { p.edit().putFloat("eqLow$ch", low.coerceIn(-RANGE, RANGE).toFloat()).putFloat("eqMid$ch", mid.coerceIn(-RANGE, RANGE).toFloat()).putFloat("eqHigh$ch", high.coerceIn(-RANGE, RANGE).toFloat()).apply() }
    /** a tone preset onto a channel (null: the tone taken off); LOW, MID and HIGH stay as they are */
    fun setTone(p: SharedPreferences, ch: Int, t: PresetPack.Tone?) {
        if (t == null) p.edit().remove("toneName$ch").remove("toneEq$ch").apply()
        else p.edit().putString("toneName$ch", t.name).putString("toneEq$ch", encode(toneBands(t.bands, t.hpHz, t.lpHz))).apply() }
    /** FLAT: LOW, MID, HIGH to 0 and no tone */
    fun reset(p: SharedPreferences, ch: Int) { p.edit().remove("eqLow$ch").remove("eqMid$ch").remove("eqHigh$ch").remove("toneName$ch").remove("toneEq$ch").apply() }
    fun of(p: SharedPreferences, ch: Int): DoubleArray = bands(low(p, ch), mid(p, ch), high(p, ch), decode(p.getString("toneEq$ch", null)))
}
