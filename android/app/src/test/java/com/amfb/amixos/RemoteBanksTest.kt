package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** 1.27: the remote's bank selector, mute-group commands and monitor / talkback commands (bounded before they leave the phone). */
class RemoteBanksTest {
    @Test fun banksFollowTheConsole() {
        assertEquals(listOf("CH 1-16", "ALL CH"), RemoteBanks.keys(16).map { it.second })
        assertEquals(listOf("CH 1-16", "CH 17-32", "CH 33-40", "ALL CH"), RemoteBanks.keys(40).map { it.second })
        assertEquals(listOf("CH 1-10", "ALL CH"), RemoteBanks.keys(10).map { it.second })
        assertTrue(RemoteBanks.inBank(1, 16)); assertFalse(RemoteBanks.inBank(1, 32)); assertTrue(RemoteBanks.inBank(2, 39)); assertFalse(RemoteBanks.inBank(2, 40))
        assertTrue(RemoteBanks.inBank(RemoteBanks.ALL, 299))
    }
    @Test fun commandsAreBounded() {
        assertEquals(mapOf("op" to "muteGroup", "group" to 8, "mute" to true), RemoteOps.muteGroup(12, true))
        assertEquals(listOf(1, 3), RemoteOps.assign(2, listOf(3, 1, 1, -4), listOf(0, 20, 15))["strips"])
        assertEquals(listOf(0, 15), RemoteOps.assign(2, listOf(3), listOf(15, 0, 20))["buses"])
        val t = RemoteOps.talkback(true, 99.0, 40); assertEquals(10.0, t["db"]); assertEquals(16, t["dest"])
        assertEquals(-10.0, RemoteOps.talkback(false, Double.NaN, 0)["db"])
        assertEquals(2, RemoteOps.monitorOut(7)["out"]); assertEquals(-60.0, RemoteOps.phones(-200.0)["phonesDb"])
        assertEquals("MAIN", RemoteOps.destLabel(0)); assertEquals("B5", RemoteOps.destLabel(5))
    }
    @Test fun stateParsesTheSharedGroups() {
        val o = com.amfb.amixos.remote.MiniJson.parse("""{"strips":[],"buses":[],"master":{},"muteGroups":[{"id":1,"name":"DRUMS","mute":true,"color":2,"strips":[0,1],"buses":[3]}],"talkback":{"on":true,"db":-6,"dest":4},"monitor":{"out":2,"phonesDb":-12}}""")
        @Suppress("UNCHECKED_CAST") val s = com.amfb.amixos.remote.DaemonClient.parseState(o as Map<String, Any?>)
        assertEquals("DRUMS", s.muteGroups[0].name); assertTrue(s.muteGroups[0].mute); assertEquals(listOf(3), s.muteGroups[0].buses)
        assertTrue(s.talkback.on); assertEquals(4, s.talkback.dest); assertEquals(2, s.monOut); assertEquals(-12.0, s.phonesDb, 0.0)
    }
}
