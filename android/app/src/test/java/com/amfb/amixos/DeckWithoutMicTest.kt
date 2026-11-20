package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Test

/** THE DECKS PLAY WITHOUT A MICROPHONE: with the input on but the microphone permission not (yet) given, the sound still starts (output
 *  only) and the permission is asked for; before this fix startAudio() returned without starting anything, so the decks were silent. */
class DeckWithoutMicTest {
    @Test fun outputStartsWithoutMicPermission() {
        assertEquals(Pair(true, false), LocalMixerActivity.audioPlan(inputWanted = true, micGranted = false))
        assertEquals(Pair(false, true), LocalMixerActivity.audioPlan(inputWanted = true, micGranted = true))
        assertEquals(Pair(false, false), LocalMixerActivity.audioPlan(inputWanted = false, micGranted = false))
    }
}
