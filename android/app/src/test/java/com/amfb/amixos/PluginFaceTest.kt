package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** PLUG-IN FACES: the finish of each kind, and the presets kept per effect kind */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class PluginFaceTest {
    @Test fun eachKindHasItsFinish() {
        assertEquals("aluminium", PluginFace.styleOf("plate", "PLATE REVERB").finish)
        assertEquals("green", PluginFace.styleOf("delay", "TAPE DELAY").finish)
        assertEquals("blue", PluginFace.styleOf("chorus", "CHORUS").finish)
        assertEquals("gold", PluginFace.styleOf("drive", "ANALOG DRIVE").finish)
        assertEquals("graphite", PluginFace.styleOf("comp", "COMPRESSOR").finish)
        assertEquals("violet", PluginFace.styleOf("eq", "PARAMETRIC EQ").finish)
    }
    @Test fun presetsAreKeptPerKindAndCanBeDeleted() {
        var j = PluginFace.withPreset(null, "delay", "Slap", mapOf("time" to 0.1, "fb" to 0.0))
        j = PluginFace.withPreset(j, "delay", "Long", mapOf("time" to 0.8, "fb" to 0.5))
        j = PluginFace.withPreset(j, "comp", "Vocal", mapOf("thr" to 0.4))
        assertEquals(setOf("Slap", "Long"), PluginFace.presets(j, "delay").keys); assertEquals(0.8, PluginFace.presets(j, "delay")["Long"]!!["time"]!!, 1e-9)
        j = PluginFace.withPreset(j, "delay", "Slap", null)
        assertEquals(setOf("Long"), PluginFace.presets(j, "delay").keys); assertEquals(setOf("Vocal"), PluginFace.presets(j, "comp").keys)
        assertTrue(PluginFace.presets("not json", "delay").isEmpty())
    }
}
