package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Test

class AudioMathTest {
    @Test fun eqCurve() {
        assertEquals(6.0, AudioMath.bandDb(EqBand("peaking", 1000.0, 6.0, 1.0), 1000.0), 0.05)       // a peak is its gain at its own frequency
        assertEquals(0.0, AudioMath.bandDb(EqBand("peaking", 1000.0, 6.0, 1.0), 20.0), 0.1)          // and nothing far away
        assertEquals(-3.0, AudioMath.bandDb(EqBand("highpass", 100.0, 0.0, 0.7071), 100.0), 0.1)    // Butterworth: -3 dB at the corner
        assertEquals(4.0, AudioMath.bandDb(EqBand("lowshelf", 200.0, 4.0, 0.71), 20.0), 0.2)
        assertEquals(0.0, AudioMath.bandDb(EqBand("off", 1000.0, 9.0, 1.0), 1000.0), 1e-9)
    }
    @Test fun calculator() {
        assertEquals(500.0, AudioMath.noteTimes(120.0).first { it.first == "1/4" }.second, 1e-9)
        assertEquals(343.2, AudioMath.speedOfSound(20.0), 0.2)
        assertEquals(10.0, AudioMath.distanceToMs(3.432), 0.02)
        assertEquals(6.02, AudioMath.ratioToDb(2.0), 0.01)
        assertEquals(93.0, AudioMath.sumIncoherent(listOf(90.0, 90.0)), 0.02)
        assertEquals("A4" to 0, AudioMath.noteOf(440.0)); assertEquals("C4", AudioMath.noteOf(261.63).first)
        assertEquals(5.333, AudioMath.latencyMs(256, 48000.0), 0.001)
        assertEquals(829.44, AudioMath.pcmMegabytes(48000.0, 24, 32, 3.0), 0.01)
    }
}
