package com.amfb.amixos

import com.amfb.amixos.remote.ConsoleState
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.Endpoint
import com.amfb.amixos.remote.Meters
import com.amfb.amixos.remote.MiniJson
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.net.ServerSocket
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

/** LIVE SYNC: the console pushes {"state":...} on the meter stream; it reaches onConsole (with its change counter) and never the meters. */
class LiveSyncTest {
    private fun frame(text: String): ByteArray { val p = text.toByteArray(); val out = java.io.ByteArrayOutputStream(); out.write(0x81)
        if (p.size < 126) out.write(p.size) else { out.write(126); out.write(p.size shr 8); out.write(p.size and 0xFF) }; out.write(p); return out.toByteArray() }

    @Test fun parsesState() {
        @Suppress("UNCHECKED_CAST") val o = MiniJson.parse("""{"strips":[{"id":0,"name":"Vocal","db":-4.5,"pan":0,"mute":true,"solo":false}],"buses":[],"master":{"db":-2,"limiter":true,"ceilingDb":-0.5},"v":42}""") as Map<String, Any?>
        val s = DaemonClient.parseState(o)
        assertEquals(42L, s.v); assertEquals("Vocal", s.strips[0].name); assertEquals(-4.5, s.strips[0].db, 1e-9); assertTrue(s.strips[0].mute); assertEquals(-2.0, s.masterDb, 1e-9)
    }

    @Test fun pushedStateGoesToOnConsole() {
        val server = ServerSocket(0)
        val serve = thread(isDaemon = true) { server.accept().use { c ->
            val inp = c.getInputStream().bufferedReader(Charsets.ISO_8859_1); while (inp.readLine().orEmpty().isNotEmpty()) {}
            val out = c.getOutputStream(); out.write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n".toByteArray())
            out.write(frame("""{"state":{"strips":[{"id":0,"name":"CH 1","db":0,"pan":0,"mute":false,"solo":false}],"buses":[],"master":{"db":0},"v":7}}"""))
            out.write(frame("""{"running":true,"strips":[{"pk":0.5,"rms":0.2,"hold":0.5,"clip":0,"gr":0,"gate":0,"in":0.5}],"master":{"l":0.5,"r":0.4,"rms":0.3,"clip":0},"buses":[],"loudness":{},"monitor":{}}"""))
            out.write(frame("""{"state":{"strips":[{"id":0,"name":"Vocal","db":-7.5,"pan":0,"mute":true,"solo":false}],"buses":[],"master":{"db":-1},"v":8}}"""))
            out.flush(); Thread.sleep(1500) } }
        val states = CopyOnWriteArrayList<ConsoleState>(); val meters = CopyOnWriteArrayList<Meters>(); val got = CountDownLatch(3)
        val h = DaemonClient(Endpoint("127.0.0.1", server.localPort, "t")).meterStream({ m -> meters.add(m); got.countDown() }, null, { s -> states.add(s); got.countDown() })
        assertTrue(got.await(5, TimeUnit.SECONDS)); h.stop(); server.close(); serve.join(2000)
        assertEquals(listOf(7L, 8L), states.map { it.v })
        assertEquals(-7.5, states[1].strips[0].db, 1e-9); assertTrue(states[1].strips[0].mute); assertEquals("Vocal", states[1].strips[0].name)
        assertEquals(1, meters.size); assertTrue(meters[0].running)
    }
}
