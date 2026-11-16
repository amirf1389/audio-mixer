package com.amfb.amixos

/** INPUT / OUTPUT INDICATOR of the phone mixer: what one side (microphone in, speaker / headphones out) is doing right now.
 *  STOPPED   the mixer is not running
 *  NOT_OPEN  running, but this side did not open (no microphone permission, the device refused, the plain Java path has no stereo input ...)
 *  SILENT    open, and nothing above -60 dBFS arrives (a muted microphone, nothing playing)
 *  SIGNAL    open and carrying sound
 *  CLIP      at or over full scale */
object IoLamp {
    enum class State { STOPPED, NOT_OPEN, SILENT, SIGNAL, CLIP }
    const val FLOOR_DB = -60.0

    fun db(peak: Float): Double = if (peak > 1e-6f) 20 * Math.log10(peak.toDouble()) else -120.0
    fun of(running: Boolean, open: Boolean, peak: Float): State = when {
        !running -> State.STOPPED
        !open -> State.NOT_OPEN
        peak >= 1f -> State.CLIP
        db(peak) > FLOOR_DB -> State.SIGNAL
        else -> State.SILENT
    }
    /** the meter position 0..1000 over the last 60 dB */
    fun pos(peak: Float): Int = (((db(peak) - FLOOR_DB) / -FLOOR_DB) * 1000).toInt().coerceIn(0, 1000)
    /** the lamp colour (ARGB) */
    fun color(s: State): Int = when (s) {
        State.STOPPED -> 0xFF4B5563.toInt()
        State.NOT_OPEN -> 0xFFEF4444.toInt()
        State.SILENT -> 0xFFFBBF24.toInt()
        State.SIGNAL -> 0xFF34D399.toInt()
        State.CLIP -> 0xFFEF4444.toInt()
    }
}
