package com.amfb.amixos

/** The A mix OS engine (libamix via JNI): an offline mixer fed from Kotlin. One instance per mixer; call [close] when done. */
class Engine(strips: Int, val sampleRate: Double) : AutoCloseable {
    private var handle: Long = create(strips, sampleRate)
    val strips: Int = strips
    private val interleaved = FloatArray(2 * MAX_FRAMES)
    private val peakBuf = FloatArray(strips + 2)

    fun setGain(strip: Int, db: Double) = setGain(handle, strip, db)
    fun setPan(strip: Int, pan: Double) = setPan(handle, strip, pan)
    fun setMute(strip: Int, on: Boolean) = setMute(handle, strip, on)
    fun setSolo(strip: Int, on: Boolean) = setSolo(handle, strip, on)
    fun setMaster(db: Double) = setMaster(handle, db)
    fun setLimiter(on: Boolean, ceilingDb: Double = -0.5, releaseMs: Double = 100.0) = setLimiter(handle, on, ceilingDb, releaseMs)

    /** Mixes one block: inputs[s] is strip s (null = silence). Returns interleaved stereo of 2 * frames samples. */
    fun process(inputs: Array<FloatArray?>, frames: Int): FloatArray {
        val n = frames.coerceIn(1, MAX_FRAMES)
        val done = process(handle, inputs, n, interleaved)
        return interleaved.copyOf(2 * done)
    }
    /** Peak meters: strips 0..strips-1, then master L and R, linear 0..1+. */
    fun peaks(): FloatArray { peaks(handle, peakBuf); return peakBuf.copyOf() }

    override fun close() { if (handle != 0L) { destroy(handle); handle = 0L } }

    companion object {
        const val MAX_FRAMES = 4096
        init { System.loadLibrary("amix") }
        @JvmStatic external fun version(): String
        @JvmStatic external fun devicesJson(): String
        @JvmStatic external fun licenseJson(): String
        @JvmStatic private external fun create(strips: Int, rate: Double): Long
        @JvmStatic private external fun destroy(handle: Long)
        @JvmStatic private external fun setGain(handle: Long, strip: Int, db: Double)
        @JvmStatic private external fun setPan(handle: Long, strip: Int, pan: Double)
        @JvmStatic private external fun setMute(handle: Long, strip: Int, on: Boolean)
        @JvmStatic private external fun setSolo(handle: Long, strip: Int, on: Boolean)
        @JvmStatic private external fun setMaster(handle: Long, db: Double)
        @JvmStatic private external fun setLimiter(handle: Long, on: Boolean, ceilingDb: Double, releaseMs: Double)
        @JvmStatic private external fun process(handle: Long, inputs: Array<FloatArray?>, frames: Int, out: FloatArray): Int
        @JvmStatic private external fun peaks(handle: Long, out: FloatArray)
    }
}
