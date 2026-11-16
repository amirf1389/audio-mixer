package com.amfb.amixos

/** HIGH-RESOLUTION AUDIO from the engine (libamix): files written block by block as FLAC (16 / 24 / 32-bit) or WAV (16 / 24 / 32-bit,
 *  32 / 64-bit float) through libFLAC and dr_wav, with dither for the integer formats; a file's header read without decoding it.
 *  Formats: "flac16" "flac24" "flac32" "wav16" "wav24" "wav32" "wavf32" "wavf64". Dither: "none" "tpdf" "shaped". */
object HiRes {
    init { System.loadLibrary("amix") }
    data class Info(val rate: Int, val channels: Int, val frames: Long, val bits: Int, val isFloat: Boolean) {
        val seconds: Double get() = if (rate > 0) frames.toDouble() / rate else 0.0
        /** "FLAC-ready" label: "96 kHz · 24-bit · stereo" */
        fun label(): String = (if (rate % 1000 == 0) "${rate / 1000} kHz" else String.format("%.1f kHz", rate / 1000.0)) + " · $bits-bit" + (if (isFloat) " float" else "") + " · " + (if (channels == 1) "mono" else if (channels == 2) "stereo" else "$channels ch")
    }
    /** One file being written. [write] takes interleaved float frames; [close] finishes the header. */
    class Writer internal constructor(private var handle: Long, val channels: Int) : AutoCloseable {
        fun write(interleaved: FloatArray, frames: Int): Boolean = handle != 0L && writerWrite(handle, interleaved, frames, channels) == 0
        override fun close() { finish() }
        fun finish(): Boolean { val h = handle; handle = 0L; return h != 0L && writerClose(h) }
    }
    /** Opens a file for writing, or null (see [lastError]). */
    fun open(path: String, channels: Int, rate: Int, format: String, dither: String = "tpdf"): Writer? { val h = writerOpen(path, channels, rate.toDouble(), format, dither); return if (h != 0L) Writer(h, channels) else null }
    fun info(path: String): Info? { val v = DoubleArray(5); return if (probe(path, v)) Info(v[0].toInt(), v[1].toInt(), v[2].toLong(), v[3].toInt(), v[4] != 0.0) else null }
    /** {"libraries":[...],"resamplers":[...],"read":[...],"write":[...],"dither":[...]} */
    @JvmStatic external fun infoJson(): String
    @JvmStatic external fun lastError(): String
    @JvmStatic private external fun writerOpen(path: String, channels: Int, rate: Double, format: String, dither: String): Long
    @JvmStatic private external fun writerWrite(handle: Long, data: FloatArray, frames: Int, channels: Int): Int
    @JvmStatic private external fun writerClose(handle: Long): Boolean
    @JvmStatic private external fun probe(path: String, out: DoubleArray): Boolean
}
