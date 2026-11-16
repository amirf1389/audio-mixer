package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PairedConsolesTest {
    private fun c(h: String, t: String, name: String = "", at: Long = 1000) = PairedConsoles.Console(h, 8765, t, name, at, at)
    @Test fun roundTrip() {
        val l = listOf(c("192.168.1.20", "tokA", "Studio"), c("10.0.0.5", "tokB", "Stage"))
        val back = PairedConsoles.parse(PairedConsoles.format(l)); assertEquals(l, back)
        assertEquals(emptyList<PairedConsoles.Console>(), PairedConsoles.parse(null)); assertEquals(emptyList<PairedConsoles.Console>(), PairedConsoles.parse("not json"))
        assertEquals(1, PairedConsoles.parse("[{\"host\":\"a\",\"token\":\"t\"},{\"host\":\"\",\"token\":\"t\"},{\"host\":\"b\",\"token\":\"\"}]").size)   // no host or no token: dropped
    }
    @Test fun upsertAndForget() {
        var l = PairedConsoles.upsert(emptyList(), c("a", "t1", "Studio", 10))
        l = PairedConsoles.upsert(l, c("b", "t2", "Stage", 20)); assertEquals(listOf("b:8765", "a:8765"), l.map { it.key })
        l = PairedConsoles.upsert(l, c("a", "t3", "", 30)); assertEquals("a:8765", l[0].key); assertEquals("Studio", l[0].name); assertEquals("t3", l[0].token); assertEquals(30, l[0].pairedAt)   // paired again: new token, name kept
        l = PairedConsoles.upsert(l, c("a", "t3", "Studio 2", 40)); assertEquals(30, l[0].pairedAt); assertEquals("Studio 2", l[0].name)   // the same token: still paired at 30
        assertEquals(listOf("b:8765"), PairedConsoles.forget(l, "a:8765").map { it.key })
    }
    @Test fun agoAndRetry() {
        assertEquals("never", PairedConsoles.ago(10_000, 0)); assertEquals("just now", PairedConsoles.ago(10_000, 9_000)); assertEquals("2 min ago", PairedConsoles.ago(200_000, 80_000))
        assertEquals(listOf(1000L, 1000L, 2000L, 4000L, 8000L, 15000L, 15000L), (0..6).map { PairedConsoles.retryDelayMs(it) })
        assertTrue((1..50).all { PairedConsoles.retryDelayMs(it) <= 15000 })
    }
}
