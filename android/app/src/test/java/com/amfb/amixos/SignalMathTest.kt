package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.sin

class SignalMathTest {
    private fun sine(f: Double, n: Int = 4096, rate: Int = 48000, a: Double = 0.5) = FloatArray(n) { (a * sin(2 * PI * f * it / rate)).toFloat() }
    @Test fun level() { assertEquals(-9.03, SignalMath.rmsDb(sine(1000.0)), 0.05); assertEquals(-120.0, SignalMath.rmsDb(FloatArray(512)), 1e-9) }
    @Test fun pitch() {
        for (f in listOf(82.41, 110.0, 196.0, 440.0, 1046.5)) assertEquals("pitch of $f", f, SignalMath.pitchHz(sine(f), 48000), f * 0.003)
        assertEquals(0.0, SignalMath.pitchHz(FloatArray(4096) { (java.util.Random(it.toLong()).nextGaussian() * 0.2).toFloat() }, 48000), 0.0)   // noise: no pitch
        val (n, c) = AudioMath.noteOf(SignalMath.pitchHz(sine(442.0), 48000)); assertEquals("A4", n); assertTrue(c in 6..9)
    }
    @Test fun generator() {
        val g = SignalMath.Generator(48000).apply { kind = "sine"; freq = 1000.0; levelDb = -20.0 }; val b = FloatArray(48000); g.fill(b)
        assertEquals(-23.0, SignalMath.rmsDb(b), 0.1)                          // a sine's RMS is 3 dB under its peak
        g.kind = "pink"; g.fill(b); assertTrue(SignalMath.rmsDb(b) in -40.0..-20.0)
        assertTrue(b.all { it in -1f..1f })
    }
}
