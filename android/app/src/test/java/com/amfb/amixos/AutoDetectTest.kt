package com.amfb.amixos

import com.amfb.amixos.remote.DaemonClient
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.net.InetAddress
import java.net.ServerSocket
import kotlin.concurrent.thread

/** AUTO-DETECT: the phone finds the Windows console on the Wi-Fi by itself, follows a paired console to a new address by its consoleId,
 *  and asks for the PIN only for a console it never paired with. */
class AutoDetectTest {
    private fun found(h: String, id: String, name: String = "A mix OS") = DaemonClient.Companion.Found(h, 8765, id, name, "1.23.0.1")
    private fun paired(h: String, t: String, id: String = "", at: Long = 1000) = PairedConsoles.Console(h, 8765, t, "Studio", at, at, id)

    @Test fun pairedConsoleIsFollowedToItsNewAddress() {
        val m = PairedConsoles.match(listOf(paired("192.168.1.20", "tokA", "a1b2c3d4e5f6")), listOf(found("192.168.1.33", "a1b2c3d4e5f6")))
        assertTrue(m is PairedConsoles.Match.Known); m as PairedConsoles.Match.Known
        assertEquals("192.168.1.33", m.console.host); assertEquals("tokA", m.console.token); assertTrue(m.moved)
    }
    @Test fun sameAddressSameIdIsNotAMove() {
        val m = PairedConsoles.match(listOf(paired("192.168.1.20", "tokA", "a1b2c3d4e5f6")), listOf(found("192.168.1.20", "a1b2c3d4e5f6"))) as PairedConsoles.Match.Known
        assertEquals(false, m.moved)
    }
    @Test fun oldPairingWithoutIdMatchesByAddressAndLearnsTheId() {
        val m = PairedConsoles.match(listOf(paired("192.168.1.20", "tokA")), listOf(found("192.168.1.20", "ffffeeeedddd"))) as PairedConsoles.Match.Known
        assertEquals("ffffeeeedddd", m.console.id)
    }
    @Test fun anotherConsoleAtTheOldAddressIsNotTakenForOurs() {
        // our console had id A at .20; now a different console (id B) answers at .20: it is new, not ours with our token
        val m = PairedConsoles.match(listOf(paired("192.168.1.20", "tokA", "aaaaaaaaaaaa")), listOf(found("192.168.1.20", "bbbbbbbbbbbb")))
        assertTrue(m is PairedConsoles.Match.New)
    }
    @Test fun mostRecentlyUsedWinsAndNothingFoundIsNone() {
        val l = listOf(paired("10.0.0.5", "stage", "stage0000000"), paired("192.168.1.20", "studio", "studio000000"))
        val m = PairedConsoles.match(l, listOf(found("10.0.0.9", "studio000000"), found("10.0.0.7", "stage0000000"))) as PairedConsoles.Match.Known
        assertEquals("stage", m.console.token)
        assertEquals(PairedConsoles.Match.None, PairedConsoles.match(l, emptyList()))
        assertTrue(PairedConsoles.match(emptyList(), listOf(found("10.0.0.9", "x"))) is PairedConsoles.Match.New)
    }
    @Test fun upsertByIdReplacesTheOldAddress() {
        var l = PairedConsoles.upsert(emptyList(), paired("192.168.1.20", "tokA", "a1b2c3d4e5f6"))
        l = PairedConsoles.upsert(l, paired("192.168.1.33", "tokA", "a1b2c3d4e5f6", 2000))
        assertEquals(listOf("192.168.1.33:8765"), l.map { it.key }); assertEquals(1000L, l[0].pairedAt)
        assertEquals(l, PairedConsoles.parse(PairedConsoles.format(l)))   // the id survives a save
    }
    @Test fun searchBacksOffAndHintsComeFirst() {
        assertEquals(listOf(3000L, 6000L, 12000L, 20000L, 20000L), (1..5).map { PairedConsoles.searchDelayMs(it) })
        assertEquals(listOf("a", "b"), PairedConsoles.hints(listOf(paired("a", "1"), paired("b", "2"), paired("a", "3"))))
    }
    @Test fun timelineKeepsTheLastEventsAndFoldsRepeats() {
        val t = LinkTimeline(3)
        t.add(1, "searching"); t.add(2, "none"); t.add(3, "none"); t.add(4, "found"); t.add(5, "connected")
        assertEquals(listOf("none", "found", "connected"), t.items.map { it.text }); assertEquals(3L, t.items[0].at)
        assertEquals("3  none\n4  found\n5  connected", t.render { it.toString() })
    }
    /** a one-route HTTP server on [addr]: answers every request with [json] */
    private fun fake(addr: String, json: String): ServerSocket { val ss = ServerSocket(0, 50, InetAddress.getByName(addr))
        thread(isDaemon = true) { while (!ss.isClosed) { val s = runCatching { ss.accept() }.getOrNull() ?: break
            thread(isDaemon = true) { s.use { val r = it.getInputStream().bufferedReader(); while (r.readLine()?.isNotEmpty() == true) { }
                val b = json.toByteArray(); it.getOutputStream().write(("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ${b.size}\r\nConnection: close\r\n\r\n").toByteArray() + b); it.getOutputStream().flush() } } } }
        return ss }
    @Test fun scanFindsAConsoleAndItsIdOnTheNetwork() {
        // a console answering /api/status on 127.0.0.1 (the scan walks 127.0.0.1 .. 254 like a /24 Wi-Fi); a web server that is not a console is ignored
        val srv = fake("127.0.0.1", """{"name":"A mix OS","version":"1.23.0.1","daemon":"amixos-daemon","consoleId":"a1b2c3d4e5f6"}""")
        val other = fake("127.0.0.2", """{"name":"router"}""")
        try {
            val port = srv.localPort
            val t0 = System.currentTimeMillis(); val hits = DaemonClient.discoverConsoles("127.0.0.9", port, 200, first = listOf("127.0.0.1"))
            println("AUTO-DETECT scan of 253 addresses: ${System.currentTimeMillis() - t0} ms, found $hits")
            assertEquals(listOf("127.0.0.1"), hits.map { it.host }); assertEquals("a1b2c3d4e5f6", hits[0].id); assertEquals("A mix OS 1.23.0.1", hits[0].label)
            assertNull(DaemonClient.probe("127.0.0.2", other.localPort, 200))
            val m = PairedConsoles.match(listOf(PairedConsoles.Console("127.0.0.50", port, "tok", "Studio", 1, 1, "a1b2c3d4e5f6")), hits) as PairedConsoles.Match.Known
            assertEquals("127.0.0.1", m.console.host); assertTrue(m.moved)
        } finally { srv.close(); other.close() }
    }
    @Test fun scanFindsTheRealConsoleService() {
        // the real AmF X1 Network service (amixos-daemon), when one runs: -Damfx.daemon=PORT (AMFX_DAEMON)
        val port = System.getProperty("amfx.daemon").orEmpty().toIntOrNull() ?: return
        val hits = DaemonClient.discoverConsoles("127.0.0.9", port, 300, first = listOf("127.0.0.1"))
        println("AUTO-DETECT real console: $hits")
        assertEquals(listOf("127.0.0.1"), hits.map { it.host }); assertTrue(Regex("^[0-9a-f]{12}$").matches(hits[0].id))
        assertEquals(hits[0].id, DaemonClient.probe("127.0.0.1", port)?.id)   // the same id each time
    }
    @Test fun consoleBarShowsWhereEachConsoleIs() {
        val studio = paired("192.168.1.20", "s", "studio000000", 3000); val stage = paired("192.168.1.30", "t", "stage0000000", 2000); val home = paired("192.168.1.40", "h", "home00000000", 1000)
        val found = listOf(found("192.168.1.31", "stage0000000", "Stage PC"), found("192.168.1.20", "studio000000"), found("192.168.1.77", "fresh0000000", "Booth"))
        val chips = PairedConsoles.chips(listOf(studio, stage, home), found, "studio000000", "192.168.1.20:8765")
        assertEquals(listOf(PairedConsoles.Presence.LINKED, PairedConsoles.Presence.ONLINE, PairedConsoles.Presence.AWAY, PairedConsoles.Presence.NEW), chips.map { it.presence })
        assertEquals("192.168.1.31", chips[1].console.host)   // the stage PC moved: switching goes to where it is now, with its own pairing
        assertEquals("t", chips[1].console.token); assertEquals("", chips[3].console.token); assertEquals("Booth", chips[3].label)
        // nothing linked: online ones first, still most recently used first inside a group
        val idle = PairedConsoles.chips(listOf(studio, stage, home), found, "", "")
        assertEquals(listOf("s", "t", "h"), idle.take(3).map { it.console.token }); assertEquals(PairedConsoles.Presence.ONLINE, idle[0].presence)
        assertTrue(PairedConsoles.chips(emptyList(), emptyList(), "", "").isEmpty())
    }
}
