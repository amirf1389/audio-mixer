package com.amfb.amixos

import com.amfb.amixos.remote.LatestSender
import org.junit.Assert.assertEquals
import org.junit.Test
import java.util.concurrent.Executor

/** real-time faders: a burst of moves of one fader sends only the newest; other faders keep their own newest value */
class LatestSenderTest {
    @Test fun keepsOnlyTheNewestPerKey() {
        val queued = ArrayList<Runnable>(); val ex = Executor { queued.add(it) }   // the line is "busy" until we run the queue
        val sent = ArrayList<String>(); val s = LatestSender(ex)
        for (db in listOf(-10, -9, -8, -7, -6)) s.submit("gain0") { sent.add("gain0=$db") }
        s.submit("gain1") { sent.add("gain1=-3") }
        for (db in listOf(-20, -21)) s.submit("gain0") { sent.add("gain0=$db") }
        assertEquals(2, s.waiting()); assertEquals(1, queued.size)   // one drain on the line, two keys waiting
        queued.removeAt(0).run()
        assertEquals(listOf("gain1=-3", "gain0=-21"), sent)          // the moves in between were dropped; only the last of gain0 went
        assertEquals(0, s.waiting())
        s.submit("master") { sent.add("master=0") }; queued.removeAt(0).run()
        assertEquals("master=0", sent.last())
    }
}
