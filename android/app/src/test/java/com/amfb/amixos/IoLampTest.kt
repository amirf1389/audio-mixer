package com.amfb.amixos

import com.amfb.amixos.IoLamp.State
import org.junit.Assert.assertEquals
import org.junit.Test

/** the phone mixer's INPUT / OUTPUT indicator */
class IoLampTest {
    @Test fun states() {
        assertEquals(State.STOPPED, IoLamp.of(running = false, open = true, peak = 0.5f))
        assertEquals(State.NOT_OPEN, IoLamp.of(running = true, open = false, peak = 0.5f))   // e.g. the microphone permission was refused
        assertEquals(State.SILENT, IoLamp.of(running = true, open = true, peak = 0f))
        assertEquals(State.SILENT, IoLamp.of(running = true, open = true, peak = 0.0009f))  // -61 dBFS: below the floor
        assertEquals(State.SIGNAL, IoLamp.of(running = true, open = true, peak = 0.1f))     // -20 dBFS
        assertEquals(State.CLIP, IoLamp.of(running = true, open = true, peak = 1.2f))
    }
    @Test fun meter() {
        assertEquals(0, IoLamp.pos(0f)); assertEquals(1000, IoLamp.pos(1f)); assertEquals(666, IoLamp.pos(0.1f))   // -20 dBFS: two thirds of the 60 dB bar
        assertEquals(-20.0, IoLamp.db(0.1f), 1e-4)
    }
}
