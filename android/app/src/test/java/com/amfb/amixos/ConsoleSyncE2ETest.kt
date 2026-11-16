package com.amfb.amixos

import com.amfb.amixos.remote.ConsoleState
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.Endpoint
import com.amfb.amixos.remote.LatestSender
import com.amfb.amixos.remote.MiniJson
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicReference

/** THE PHONE APP AGAINST THE REAL WINDOWS CONSOLE (end to end): the console runs with its phone remote on (AMIX_REMOTE_HOLD) and writes
 *  its own state to a file; this test is the app's own code: it pairs with the console's PIN, follows the live stream, drags a fader
 *  the way the app does (LatestSender), and checks that the console's own fader got there and that a move on the console reaches the
 *  phone. Runs only when AMFX_CONSOLE (the console's state file) is given. */
class ConsoleSyncE2ETest {
    @Suppress("UNCHECKED_CAST") private fun console(f: File): Map<String, Any?>? = runCatching { MiniJson.parse(f.readText()) as Map<String, Any?> }.getOrNull()
    private fun stripDb(c: Map<String, Any?>?, i: Int): Double? = (((c?.get("strips") as? List<*>)?.getOrNull(i) as? Map<*, *>)?.get("db") as? Number)?.toDouble()
    private fun waitFor(ms: Long, what: () -> Boolean): Long { val t0 = System.currentTimeMillis(); while (System.currentTimeMillis() - t0 < ms) { if (what()) return System.currentTimeMillis() - t0; Thread.sleep(10) }; return -1 }

    @Test fun phoneAndConsoleFollowEachOther() {
        val path = System.getProperty("amfx.console").orEmpty(); assumeTrue("AMFX_CONSOLE not set", path.isNotEmpty())
        val f = File(path); assertTrue("the console never wrote its state", waitFor(30000) { console(f)?.get("running") == true } >= 0)
        val c0 = console(f)!!; val port = (c0["port"] as Number).toInt()
        // AUTO-DETECT as the app does: search the network (127.0.0.x here, the Wi-Fi /24 on a phone); the console answers with its consoleId
        val t0 = System.currentTimeMillis(); val found = DaemonClient.discoverConsoles("127.0.0.9", port, 300)
        println("E2E auto-detect: ${found.map { it.host + " id " + it.id + " " + it.label }} in ${System.currentTimeMillis() - t0} ms")
        assertTrue("the search did not find the console once: $found", found.size == 1 && found[0].host == "127.0.0.1" && Regex("^[0-9a-f]{12}$").matches(found[0].id))
        assertTrue("a console never paired must ask for the PIN", PairedConsoles.match(emptyList(), found) is PairedConsoles.Match.New)
        // PAIR as the app does: the PIN the console shows (it changes every minute: read it fresh)
        val token = DaemonClient.pair("127.0.0.1", port, console(f)!!["pin"].toString(), "E2E phone")
        // the phone saved the console at an address it no longer has (10.9.9.9, gone from the network): the next search follows it by its id, and the saved token still works
        val m = PairedConsoles.match(listOf(PairedConsoles.Console("10.9.9.9", port, token, "Studio PC", 1, 1, found[0].id)), DaemonClient.discoverConsoles("127.0.0.9", port, 300, first = listOf("10.9.9.9")))
        assertTrue("the paired console was not followed to its address: $m", m is PairedConsoles.Match.Known && m.moved && m.console.host == "127.0.0.1")
        assertTrue("the moved console does not take the saved token", DaemonClient(Endpoint((m as PairedConsoles.Match.Known).console.host, port, m.console.token)).state().strips.isNotEmpty())
        println("E2E auto-detect: paired console followed from 10.9.9.9 to 127.0.0.1 and connected with its saved token")
        val cl = DaemonClient(Endpoint("127.0.0.1", port, token))
        val pushed = AtomicReference<ConsoleState?>(null)
        val stream = cl.meterStream({ }, null, { s -> pushed.set(s) })
        try {
            assertTrue("no state pushed on connect", waitFor(5000) { pushed.get() != null } >= 0)
            assertTrue("the console does not count this phone", waitFor(3000) { ((console(f)?.get("remotes") as? Number)?.toInt() ?: 0) >= 1 } >= 0)
            // PHONE -> CONSOLE: a drag on strip 2 (-30 .. -6 in 40 steps at ~60 Hz), only the newest value travels
            val live = LatestSender(Executors.newSingleThreadExecutor())
            for (k in 0..40) { val db = -30.0 + k * 0.6; live.submit("gain1") { cl.gain(1, db) }; Thread.sleep(16) }
            val inMs = waitFor(3000) { stripDb(console(f), 1)?.let { Math.abs(it + 6.0) < 0.06 } == true }
            println("E2E phone -> console: the console's fader at -6.0 dB ${inMs} ms after the drag ended")
            assertTrue("the console's fader did not follow the phone: ${console(f)}", inMs >= 0)
            // mute and master too
            cl.mute(2, true); cl.master(-4.0)
            assertTrue("mute / master did not reach the console", waitFor(3000) { val c = console(f); (((c?.get("strips") as? List<*>)?.getOrNull(2) as? Map<*, *>)?.get("mute") == true) && Math.abs(((c?.get("master") as? Number)?.toDouble() ?: 0.0) + 4.0) < 0.06 } >= 0)
            // CONSOLE -> PHONE: the console moves strip 4 every 4 s (-12 / -24): the phone's pushed state follows each move
            val before = stripDb(console(f), 3)
            assertTrue("the console did not move its fader", waitFor(6000) { stripDb(console(f), 3) != before } >= 0)
            val target = stripDb(console(f), 3)!!
            val outMs = waitFor(3000) { pushed.get()?.strips?.getOrNull(3)?.db?.let { Math.abs(it - target) < 0.06 } == true }
            println("E2E console -> phone: the phone saw strip 4 at $target dB ${outMs} ms after the console's dump showed it")
            assertTrue("the phone did not follow the console: ${pushed.get()?.strips?.getOrNull(3)}", outMs >= 0)
        } finally { stream.stop(); cl.mute(2, false); cl.master(0.0) }
    }
}
