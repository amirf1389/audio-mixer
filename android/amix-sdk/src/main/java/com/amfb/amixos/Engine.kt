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
    /** starts the native AAudio loop (microphone on strip 1 -> mixer -> speaker / headphones); the rate in use, 0 = not available */
    fun startNative(rate: Int, mic: Boolean, outDevice: Int = 0, inDevice: Int = 0, bursts: Int = 2, inChannels: Int = 1): Int = aaudioStart(handle, rate, mic, outDevice, inDevice, bursts, inChannels)
    /** INSERTS: [Engine.MAX_INSERTS] AmF X1 rack effects per strip and on the master (strip = -1); kind "" empties the slot. 0 = ok. */
    fun insert(strip: Int, slot: Int, kind: String): Int = insert(handle, strip, slot, kind)
    /** {"kind","name","bypass","mix","params":[{"id","title","value","default","text"}]} or "null" */
    fun insertJson(strip: Int, slot: Int): String = insertJson(handle, strip, slot)
    /** a parameter 0..1 by id; "mix" (0 dry .. 1 wet) and "bypass" (0 / 1) on every effect */
    fun insertSet(strip: Int, slot: Int, id: String, value: Double): Int = insertSet(handle, strip, slot, id, value)
    /** [running, disconnected, framesPerBurst, bufferFrames, xruns, inputOpen, outDeviceId, inDeviceId, sampleRate] */
    fun nativeState(): IntArray = IntArray(9).also { aaudioState(it) }
    // DECKS (two file players on strip pairs) and the MASTER RECORDER: see amix_jni.cpp. Called from worker threads, never the UI thread.
    fun deckRoute(deck: Int, stripL: Int) = deckRoute(handle, deck, stripL)
    /** decoded interleaved stereo at [srcRate], resampled to [dstRate]; returns the frames now waiting to be played */
    fun deckPush(deck: Int, data: FloatArray?, frames: Int, srcRate: Double, dstRate: Double): Int = deckPush(handle, deck, data, frames, srcRate, dstRate)
    fun deckFlush(deck: Int) = deckFlush(handle, deck)
    fun deckPlayed(deck: Int): Long = deckPlayed(handle, deck)
    fun deckResetPlayed(deck: Int) = deckResetPlayed(handle, deck)
    fun recArm(on: Boolean) = recArm(handle, on)
    fun recPop(out: FloatArray, maxFrames: Int): Int = recPop(handle, out, maxFrames)
    fun recDropped(): Long = recDropped(handle)
    /** MULTITRACK: every strip's own input (before fader and effects) for recording, `strips` channels a frame, interleaved */
    fun multiArm(on: Boolean) = multiArm(handle, on)
    fun multiPop(out: FloatArray, strips: Int, maxFrames: Int): Int = multiPop(handle, out, strips, maxFrames)
    fun multiDropped(): Long = multiDropped(handle)
    fun peaks(): FloatArray { peaks(handle, peakBuf); return peakBuf.copyOf() }
    /** each strip's INPUT level before fader, mute and effects (linear peak, 0..1+): the input indicator */
    fun inputPeaks(): FloatArray = FloatArray(strips).also { inputPeaks(handle, it) }

    override fun close() { if (handle != 0L) { destroy(handle); handle = 0L } }

    companion object {
        const val MAX_FRAMES = 4096
        const val MAX_INSERTS = 4
        /** [{"kind","name"}]: every effect the inserts take */
        @JvmStatic external fun fxCatalogJson(): String
        /** the input channels the native loop really opened (2 = stereo on strips 1-2, 1 = mono, 0 = none) */
        @JvmStatic external fun aaudioInChannels(): Int
        @JvmStatic private external fun insert(handle: Long, strip: Int, slot: Int, kind: String): Int
        @JvmStatic private external fun insertJson(handle: Long, strip: Int, slot: Int): String
        @JvmStatic private external fun insertSet(handle: Long, strip: Int, slot: Int, id: String, value: Double): Int
        init { System.loadLibrary("amix") }
        @JvmStatic external fun version(): String
        /** Where the library keeps its files: the app's filesDir for data, cacheDir for cache / prefetch / temp (Android clears it when space is short). Call once at start. */
        @JvmStatic external fun setDirs(data: String, cache: String, temp: String)
        @JvmStatic external fun pathsJson(): String
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
        // AAUDIO: the native low-latency loop (amix_jni.cpp). aaudioStart returns the rate in use, 0 = not available (use AudioLoop's Java path)
        @JvmStatic private external fun aaudioStart(handle: Long, rate: Int, mic: Boolean, outDev: Int, inDev: Int, bursts: Int, inChannels: Int): Int
        /** AUDIO HAL PROBE (see AudioHal): what AAudio grants when asked for the best, output then input; nothing is played */
        @JvmStatic external fun aaudioProbe(out: IntArray)
        @JvmStatic external fun aaudioStop()
        @JvmStatic private external fun aaudioState(out: IntArray)
        @JvmStatic private external fun inputPeaks(handle: Long, out: FloatArray)
        @JvmStatic private external fun deckRoute(handle: Long, deck: Int, stripL: Int)
        @JvmStatic private external fun deckPush(handle: Long, deck: Int, data: FloatArray?, frames: Int, srcRate: Double, dstRate: Double): Int
        @JvmStatic private external fun deckFlush(handle: Long, deck: Int)
        @JvmStatic private external fun deckPlayed(handle: Long, deck: Int): Long
        @JvmStatic private external fun deckResetPlayed(handle: Long, deck: Int)
        @JvmStatic private external fun recArm(handle: Long, on: Boolean)
        @JvmStatic private external fun recPop(handle: Long, out: FloatArray, maxFrames: Int): Int
        @JvmStatic private external fun recDropped(handle: Long): Long
        @JvmStatic private external fun multiArm(handle: Long, on: Boolean)
        @JvmStatic private external fun multiPop(handle: Long, out: FloatArray, strips: Int, maxFrames: Int): Int
        @JvmStatic private external fun multiDropped(handle: Long): Long
    }
}
