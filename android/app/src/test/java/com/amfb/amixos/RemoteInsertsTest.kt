package com.amfb.amixos

import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.Endpoint
import com.amfb.amixos.remote.Inserts
import com.amfb.amixos.remote.LatestSender
import com.amfb.amixos.remote.MiniJson
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.net.ServerSocket
import kotlin.concurrent.thread
import java.util.Collections
import java.util.concurrent.Executor

/** MASTER and CHANNEL INSERTS from the phone: the commands the app sends (checked against a stand-in console), the parsing of what
 *  the console answers, invalid input refused before it leaves, and the master's latest value only (one update at a time). */
class RemoteInsertsTest {
    private val cat = """{"plugins":[{"id":"0123456789abcdef","name":"Fake Gain","vendor":"Test Labs","format":"VST3","fx":false},{"id":"fedcba9876543210","name":"Reverb","vendor":"AMFB Audio","format":"built-in","fx":true},{"id":"C:/evil.dll","name":"Bad"}],"maxSlots":10}"""
    private val racks = """{"racks":[{"strip":2,"slots":[{"slot":5,"kind":"x","name":"Fake Gain","loaded":true,"bypass":true,"vendor":"Test Labs","format":"VST3","params":[{"id":7,"title":"GAIN","value":0.25,"text":"-6 dB","steps":0}],"presets":[{"id":3,"name":"Warm"}]},{"slot":1,"name":"Gone","loaded":false,"error":"missing"}]}]}"""

    /** a stand-in console: one request per connection; POST bodies are kept */
    private fun server(sent: MutableList<Map<String, Any?>>): ServerSocket { val ss = ServerSocket(0, 50, java.net.InetAddress.getByName("127.0.0.1"))
        thread(isDaemon = true) { while (!ss.isClosed) { val so = runCatching { ss.accept() }.getOrNull() ?: break
            so.use { val inp = it.getInputStream().buffered(); fun line(): String { val b = StringBuilder(); while (true) { val c = inp.read(); if (c < 0 || c == '\n'.code) break; if (c != '\r'.code) b.append(c.toChar()) }; return b.toString() }
                val req = line().split(" "); var len = 0; while (true) { val h = line(); if (h.isEmpty()) break; if (h.lowercase().startsWith("content-length:")) len = h.substringAfter(":").trim().toInt() }
                val bytes = ByteArray(len); var got = 0; while (got < len) { val r = inp.read(bytes, got, len - got); if (r < 0) break; got += r }
                @Suppress("UNCHECKED_CAST") if (req[0] == "POST") sent.add(MiniJson.parse(String(bytes, Charsets.UTF_8)) as Map<String, Any?>)
                val path = req.getOrElse(1) { "" }; val body = when { path.startsWith("/api/inserts") -> cat; path.startsWith("/api/racks") -> racks; else -> """{"ok":true,"text":"-3 dB"}""" }.toByteArray()
                it.getOutputStream().apply { write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ${body.size}\r\nConnection: close\r\n\r\n".toByteArray()); write(body); flush() } } } }
        return ss }

    @Test fun insertsAndMasterGoOutAsTheConsoleExpects() {
        val sent = Collections.synchronizedList(ArrayList<Map<String, Any?>>()); val srv = server(sent)
        try {
            val c = DaemonClient(Endpoint("127.0.0.1", srv.localPort, "tok"))
            val cl = c.insertCatalog(); assertEquals(2, cl.size); assertTrue(cl.none { it.id.contains("/") }); assertTrue(cl[1].builtIn && cl[1].label.contains("built-in"))
            val sl = c.insertSlots(2); assertEquals(listOf(1, 5), sl.map { it.slot }); val g = sl[1]
            assertTrue(g.bypass && g.loaded && g.params.single().text == "-6 dB" && g.presets.single().id == 3); assertFalse(sl[0].loaded); assertEquals("missing", sl[0].note)
            assertEquals(0, Inserts.firstFree(sl))
            c.insertAdd(2, cl[0].id); c.insertAdd(2, cl[1].id, 9); c.insertRemove(2, 5); c.insertMove(2, 5, 4); c.pluginBypass(2, 5, false); c.pluginParam(2, 5, 7, 0.5); c.pluginPreset(2, 5, 3); c.pluginDefaults(2, 5)
            c.masterMute(true); c.masterDim(true)
            fun n(m: Map<String, Any?>, k: String) = (m[k] as Number).toInt()
            assertEquals("add", sent[0]["action"]); assertEquals("0123456789abcdef", sent[0]["id"]); assertFalse(sent[0].containsKey("slot")); assertEquals(9, n(sent[1], "slot"))
            assertEquals("remove", sent[2]["action"]); assertEquals(4, n(sent[3], "to")); assertEquals(false, sent[4]["bypass"]); assertEquals(0.5, (sent[5]["value"] as Number).toDouble(), 0.0)
            assertEquals(3, n(sent[6], "preset")); assertEquals(true, sent[7]["defaults"])
            assertEquals(mapOf("op" to "master", "mute" to true), sent[8]); assertEquals(mapOf("op" to "master", "dim" to true), sent[9])   // mute / dim never carry the fader
        } finally { srv.close() }
    }

    @Test fun invalidInsertInputIsRefusedHere() {
        for (bad in listOf<() -> Unit>({ Inserts.add(0, "C:/plugins/evil.dll") }, { Inserts.add(0, "0123456789abcdef", 10) }, { Inserts.remove(0, -1) }, { Inserts.move(0, 0, 10) }, { Inserts.move(0, 12, 1) }))
            assertTrue(runCatching { bad() }.isFailure)
        assertEquals(-1, Inserts.firstFree((0 until 10).map { Inserts.Slot(it, "x", false, true, "", emptyList(), emptyList()) }))
    }

    @Test fun masterStateRoundTripsExactly() {
        val s = DaemonClient.parseState(mapOf("strips" to emptyList<Any>(), "buses" to emptyList<Any>(), "master" to mapOf("db" to -7.3, "mute" to true, "dim" to false)))
        assertEquals(-7.3, s.masterDb, 0.0); assertTrue(s.masterMute); assertFalse(s.masterDim)
        val sent = ArrayList<Double>(); val q = ArrayList<Runnable>(); val live = LatestSender(Executor { q.add(it) })
        for (v in listOf(-10.0, -9.5, -8.0, -7.3)) live.submit("master") { sent.add(v) }
        while (q.isNotEmpty()) q.removeAt(0).run()
        assertEquals(listOf(-7.3), sent)   // only the newest goes: no stale value lands after it
    }
}
