package com.amfb.amixos

import com.amfb.amixos.remote.AppVolume
import com.amfb.amixos.remote.AppVolumes
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.MiniJson
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AppVolumesTest {
    @Suppress("UNCHECKED_CAST")
    private fun obj(s: String) = MiniJson.parse(s) as Map<String, Any?>

    @Test fun parsesTheConsoleList() {
        val o = obj("""{"appsAvailable":true,"apps":[{"id":"sys","name":"System sounds","pid":0,"volume":100,"mute":false,"system":true},{"id":"sp#1","name":"Spotify","pid":42,"volume":80.5,"mute":true},{"id":"","name":"bad"},{"name":"no id"},{"id":"x","volume":180}]}""")
        val a = AppVolumes.parse(o)
        assertTrue(AppVolumes.available(o)); assertEquals(3, a.size)
        assertEquals(AppVolume("sys", "System sounds", 0, 100.0, false, true), a[0])
        assertEquals(AppVolume("sp#1", "Spotify", 42, 80.5, true, false), a[1])
        assertEquals("x", a[2].name); assertEquals(100.0, a[2].volume, 0.0)   // out of range kept in range, the id as the name
        assertFalse(AppVolumes.available(obj("""{"apps":[]}"""))); assertTrue(AppVolumes.parse(obj("{}")).isEmpty())
        assertEquals(3, DaemonClient.parseState(o).apps.size); assertTrue(DaemonClient.parseState(o).appsAvailable)
    }
    @Test fun commandsAreChecked() {
        assertEquals(mapOf("op" to "appVolume", "id" to "sp#1", "volume" to 100), AppVolumes.volumeCommand("sp#1", 140.0))
        assertEquals(mapOf("op" to "appVolume", "id" to "sp#1", "volume" to 0), AppVolumes.volumeCommand("sp#1", -3.0))
        assertEquals(mapOf("op" to "appVolume", "id" to "sp#1", "volume" to 33), AppVolumes.volumeCommand("sp#1", 33.4))
        assertNull(AppVolumes.volumeCommand("", 50.0)); assertNull(AppVolumes.volumeCommand("a", Double.NaN)); assertNull(AppVolumes.muteCommand("", true))
        assertEquals(mapOf("op" to "appMute", "id" to "a", "on" to true), AppVolumes.muteCommand("a", true))
        assertEquals("Spotify  MUTED", AppVolumes.label(AppVolume("a", "Spotify", 1, 80.0, true, false)))
        assertEquals("Spotify  80%", AppVolumes.label(AppVolume("a", "Spotify", 1, 80.0, false, false)))
    }
}
