package com.amfb.amixos

import com.amfb.amixos.remote.BusState
import com.amfb.amixos.remote.ConsoleState
import com.amfb.amixos.remote.StripState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** OFFLINE SCENES: made without a console, from its last known layout; edited channel by channel; only the channels a console has are sent. */
class OfflineScenesTest {
    private val st = ConsoleState(listOf(StripState(0, "VOCAL", -6.0, 0.0, false, false), StripState(1, "GTR", -12.0, 0.2, true, false)), listOf(BusState(0, -3.0, -60.0, false)), -2.0, true, -0.5)

    @Test fun newSceneStartsFromTheLastConsole() {
        val s = OfflineScenes.blank(OfflineScenes.stateJson(st), 32, 1000)
        assertEquals(true, s["offline"]); assertEquals(1000L, s["t"]); assertEquals(-2.0, (s["master"] as Number).toDouble(), 1e-9)
        assertEquals(listOf("VOCAL", "GTR"), (s["strips"] as List<*>).map { (it as Map<*, *>)["name"] })
    }
    @Test fun noConsoleEverSeenGivesFlatChannels() {
        val s = OfflineScenes.blank(null, 8, 5)
        assertEquals(8, (s["strips"] as List<*>).size); assertEquals("CH 8", ((s["strips"] as List<*>)[7] as Map<*, *>)["name"]); assertEquals(0.0, (s["master"] as Number).toDouble(), 1e-9)
    }
    @Test fun editsClampAndRoundTrip() {
        var s = OfflineScenes.blank(OfflineScenes.stateJson(st), 32, 1)
        s = OfflineScenes.setStrip(s, 1, 2, db = 25.0, mute = false, name = "  ACOUSTIC GUITAR LONG NAME  ")
        s = OfflineScenes.setStrip(s, 0, 3, db = -99.0); s = OfflineScenes.setMaster(s, -4.26, 4)
        val back = OfflineScenes.parse(OfflineScenes.stringify(s))!!
        val strips = back["strips"] as List<*>
        assertEquals(10.0, ((strips[1] as Map<*, *>)["db"] as Number).toDouble(), 1e-9); assertEquals(false, (strips[1] as Map<*, *>)["mute"])
        assertEquals("ACOUSTIC GUITAR ", (strips[1] as Map<*, *>)["name"].toString().padEnd(16).take(16)); assertTrue((strips[1] as Map<*, *>)["name"].toString().length <= 16)
        assertEquals(-60.0, ((strips[0] as Map<*, *>)["db"] as Number).toDouble(), 1e-9); assertEquals(-4.3, (back["master"] as Number).toDouble(), 1e-9); assertEquals(4L, (back["t"] as Number).toLong())
        assertEquals("CH 2", (OfflineScenes.setStrip(s, 1, 5, name = "   ")["strips"] as List<*>).let { (it[1] as Map<*, *>)["name"] })
    }
    @Test fun aBiggerSceneOnASmallerConsoleSendsOnlyItsChannels() {
        val s = OfflineScenes.blank(null, 32, 1)
        assertEquals(16, OfflineScenes.forConsole(s, 16).size); assertEquals(0, OfflineScenes.forConsole(s, 0).size)
    }
}
