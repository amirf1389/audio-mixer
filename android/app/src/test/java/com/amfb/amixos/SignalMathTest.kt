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
    @Test fun fftAndThirdOctave() {
        val re = DoubleArray(8) { if (it == 0) 1.0 else 0.0 }; val im = DoubleArray(8); SignalMath.fft(re, im); assertTrue(re.all { Math.abs(it - 1.0) < 1e-12 } && im.all { Math.abs(it) < 1e-12 })   // an impulse is flat
        val b = SignalMath.thirdOctave(sine(1000.0, 16384, 48000, 1.0), 48000); val at = SignalMath.thirdOctaveCentres.indexOfFirst { it == 1000.0 }
        assertEquals(31, b.size); assertEquals("a full-scale 1 kHz sine reads 0 dB in the 1 kHz band", 0.0, b[at], 0.3)
        assertTrue("the bands away from it are far down", b[at - 4] < -60 && b[at + 4] < -60)
        val low = SignalMath.thirdOctave(sine(50.0, 16384, 48000, 0.1), 48000); assertEquals(-20.0, low[SignalMath.thirdOctaveCentres.indexOfFirst { it == 50.0 }], 0.6)
        assertTrue(SignalMath.thirdOctave(FloatArray(4096), 48000).all { it == -120.0 })
    }
    @Test fun generator() {
        val g = SignalMath.Generator(48000).apply { kind = "sine"; freq = 1000.0; levelDb = -20.0 }; val b = FloatArray(48000); g.fill(b)
        assertEquals(-23.0, SignalMath.rmsDb(b), 0.1)                          // a sine's RMS is 3 dB under its peak
        g.kind = "pink"; g.fill(b); assertTrue(SignalMath.rmsDb(b) in -40.0..-20.0)
        assertTrue(b.all { it in -1f..1f })
    }
}
