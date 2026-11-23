package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NoticesTest {
    @Test fun eventsFromStatusAndMeters() {
        val n = Notices(max = 4)
        assertTrue(n.status(0, true, "Speakers", 0, 10.0).isEmpty())                     // the first reading only sets the baseline
        assertEquals(Notices.Kind.DEVICE_LOST, n.status(1000, true, "", 0, 10.0).single().kind)
        assertEquals(Notices.Kind.DEVICE_CONNECTED, n.status(2000, true, "Headphones", 0, 10.0).single().kind)
        assertEquals(Notices.Kind.LOOP_STOPPED, n.status(3000, false, "Headphones", 0, 10.0).single().kind)
        assertEquals(Notices.Kind.CPU_OVERLOAD, n.status(4000, true, "Headphones", 3, 95.0).single().kind)
        assertTrue(n.status(5000, true, "Headphones", 4, 95.0).isEmpty())                // at most one overload notice every 30 s
        assertNotNull(n.clip(6000, true)); assertNull(n.clip(6100, true)); assertNull(n.clip(6200, false)); assertNotNull(n.clip(6300, true))
        assertEquals(4, n.history.size); assertEquals(Notices.Kind.CLIP, n.history.first().kind)   // newest first, capped
    }
    @Test fun whatsNewOnlyOnAChange() {
        val n = Notices()
        assertNull(n.versionSeen(0, "1.2", null)); assertNull(n.versionSeen(0, "1.2", "1.2"))
        assertEquals(Notices.Kind.WHATS_NEW, n.versionSeen(0, "1.3", "1.2")!!.kind)
        assertEquals(Notices.Kind.UPDATE_READY, n.updateReady(1, "1.4").kind)
    }
}
