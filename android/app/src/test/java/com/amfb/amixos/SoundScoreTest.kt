package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs
import kotlin.math.log2
import kotlin.math.pow

/** The same cases as the console's engine/cpp/test/score_test.cpp: the phone and the console must agree. */
class SoundScoreTest {
    private val freqs = (0 until 31).map { 20 * 1000.0.pow(it / 30.0) }
    private fun good() = SoundScore.Input(shortTerm = -14.0, target = -14.0, truePeak = -1.5, peakDb = -1.5, rmsDb = -12.0, correlation = 0.7,
        freqs = freqs, spectrum = freqs.map { -20 - 3 * log2(it / 1000) })

    @Test fun goodMix() { val s = SoundScore.score(good()); assertTrue(s.valid); assertTrue(s.total >= 98); assertEquals("A", s.grade); assertTrue(abs(s.tilt + 3) < 0.05) }
    @Test fun silenceGivesNoScore() = assertFalse(SoundScore.score(good().copy(shortTerm = -90.0)).valid)
    @Test fun clip() { val s = SoundScore.score(good().copy(masterClip = true)); assertEquals(0.0, s.parts[1].points, 1e-9); assertTrue(s.total <= 80) }
    @Test fun squashed() = assertTrue(SoundScore.score(good().copy(rmsDb = -4.0)).parts[2].points < 8)
    @Test fun bump() { val sp = good().spectrum.toMutableList(); sp[15] += 12.0; sp[16] += 12.0
        val s = SoundScore.score(good().copy(spectrum = sp)); assertTrue(s.parts[3].points < 18); assertTrue(s.parts[3].tip.contains("bump")) }
    @Test fun outOfPhase() = assertTrue(SoundScore.score(good().copy(correlation = -0.4)).parts[4].points <= 5)
    @Test fun tooLoud() { val s = SoundScore.score(good().copy(shortTerm = -8.0)); assertTrue(s.parts[0].points <= 0.01); assertTrue(s.parts[0].tip.contains("too loud")) }
}
