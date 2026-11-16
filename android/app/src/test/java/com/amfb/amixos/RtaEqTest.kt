package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs
import kotlin.math.log2
import kotlin.math.pow

class RtaEqTest {
    // 1/6-octave centres, 20 Hz .. 20 kHz, like the console's RTA
    private val f = generateSequence(20.0) { it * 2.0.pow(1.0 / 6) }.takeWhile { it <= 20000.0 * 1.0001 }.toList()

    @Test fun flatSpectrumNeedsNoCorrection() {
        val g = RtaEq.autoEq(RtaEq.octaves(f, f.map { -30.0 }))!!
        g.forEach { assertEquals(0.0, it, 1e-9) }
    }
    @Test fun aBumpIsCutAndTheRestStays() {
        // +6 dB around 1 kHz: the median ignores the bump, 70 % of it is cut
        val lvl = RtaEq.octaves(f, f.map { if (abs(log2(it / 1000.0)) <= 0.5) -24.0 else -30.0 })
        val g = RtaEq.autoEq(lvl)!!
        assertEquals(-4.0, g[5], 1e-9)
        for (i in g.indices) if (i != 5) assertEquals(0.0, g[i], 1e-9)
    }
    @Test fun limitsAndSteps() {
        val lvl = DoubleArray(10) { -30.0 }; lvl[3] = 0.0
        val g = RtaEq.autoEq(lvl)!!
        assertEquals(-9.0, g[3], 1e-9)
        g.forEach { assertEquals(0.0, (it * 2) % 1.0, 1e-9) }
    }
    @Test fun silentBandsStayAtZeroAndSilenceGivesNothing() {
        val lvl = DoubleArray(10) { -30.0 }; lvl[0] = -120.0; lvl[9] = -100.0
        val g = RtaEq.autoEq(lvl)!!
        assertEquals(0.0, g[0], 1e-9); assertEquals(0.0, g[9], 1e-9)
        assertNull(RtaEq.autoEq(DoubleArray(10) { -120.0 }))
    }
    @Test fun warmTargetLiftsTheBassOfAFlatSpectrum() {
        val g = RtaEq.autoEq(DoubleArray(10) { -30.0 }, RtaEq.Target.WARM)!!
        assertTrue(g[1] > g[5]); assertTrue(g[5] > g[8])
    }
    @Test fun averageIsInPower() {
        val a = RtaEq.Average(1); a.add(doubleArrayOf(-10.0)); a.add(doubleArrayOf(-120.0))
        assertEquals(-13.0103, a.result()[0], 1e-3); assertEquals(2, a.frames)
    }
}
