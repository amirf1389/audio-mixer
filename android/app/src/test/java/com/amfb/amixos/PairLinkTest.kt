package com.amfb.amixos

import com.amfb.amixos.remote.PairLink
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** the console's pairing QR code: only a private address and a six-digit PIN are accepted */
class PairLinkTest {
    @Test fun parses() {
        assertEquals(PairLink("192.168.1.20", 8765, "123456"), PairLink.parse("http://192.168.1.20:8765/remote#pin=123456"))
        assertEquals(PairLink("10.0.0.5", 9000, "000042"), PairLink.parse(" http://10.0.0.5:9000/remote#pin=000042 "))
        assertEquals(PairLink("172.20.1.1", 8765, "654321"), PairLink.parse("http://172.20.1.1/remote#x=1&pin=654321"))
    }
    @Test fun refuses() {
        assertNull(PairLink.parse("http://8.8.8.8:8765/remote#pin=123456"))           // public address
        assertNull(PairLink.parse("https://evil.example/remote#pin=123456"))
        assertNull(PairLink.parse("http://192.168.1.20:8765/remote#pin=12345"))       // five digits
        assertNull(PairLink.parse("http://192.168.1.20:8765/remote?pin=123456"))      // the PIN only in the fragment
        assertNull(PairLink.parse("http://172.32.0.1:8765/remote#pin=123456"))
        assertNull(PairLink.parse("not a link")); assertNull(PairLink.parse(null))
    }
}
