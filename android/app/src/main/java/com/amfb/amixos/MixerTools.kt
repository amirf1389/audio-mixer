package com.amfb.amixos

/** TOOLS of the phone's own mixer. The engine has no tone generator of its own, so the TONE plays through what it does have: a deck's
 *  input (deckPush / deckRoute). It takes DECK D's channel while that deck is not playing, so the tone goes through that channel's fader,
 *  mute, group and rack, then the master, like any source on the desk. The LEVEL meter reads the engine's own peak meters. */
object MixerTools {
    /** the deck the tone borrows */
    const val TONE_DECK = LocalMixerActivity.DECKS - 1
    val kinds = listOf("sine", "pink")
    /** dBFS of a linear peak (-120 for silence) */
    fun dbfs(peak: Float): Double = if (peak > 1e-6f) 20 * Math.log10(peak.toDouble()) else -120.0
    /** an estimate of dB SPL at the phone's microphone: the input's dBFS plus the calibration of TOOLS → SOUND LEVEL (0 dBFS = splCal) */
    fun spl(inputPeak: Float, cal: Int): Double? = dbfs(inputPeak).takeIf { it > -119 }?.let { it + cal }
    fun fmt(db: Double?, unit: String): String = if (db == null || db <= -119) "—" else String.format(java.util.Locale.US, "%.1f %s", db, unit)

    /** feeds the generator's signal (stereo, the same on both sides) into a deck's input of the engine while it is on */
    class ToneFeeder(private val engine: Engine, private val deck: Int, private val strip: Int, private val rate: () -> Double) {
        val gen = SignalMath.Generator(48000)
        @Volatile var on = false; private set
        private var worker: Thread? = null
        fun start() {
            if (on) return; on = true; engine.deckFlush(deck); engine.deckRoute(deck, strip)
            worker = Thread({
                val n = 1024; val mono = FloatArray(n); val st = FloatArray(2 * n)
                while (on) {
                    val r = rate(); if (engine.deckPush(deck, null, 0, 48000.0, r) > r * 0.08) { Thread.sleep(5); continue }   // about 80 ms ahead, no more
                    synchronized(gen) { gen.fill(mono) }; for (i in 0 until n) { st[2 * i] = mono[i]; st[2 * i + 1] = mono[i] }
                    engine.deckPush(deck, st, n, 48000.0, r)   // made at 48 kHz, the engine resamples to the mix rate
                }
            }, "amix-mixer-tone").apply { isDaemon = true; start() }
        }
        fun stop() { if (!on) return; on = false; worker?.join(500); worker = null; engine.deckRoute(deck, -1); engine.deckFlush(deck) }
    }
}
