package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** AUDIO PATH: which audio API runs the mixer, from the API level, the user's choice and what failed to open. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class AudioPathTest {
    @Test fun autoPrefersAAudioThenOpenSlThenJava() {
        assertEquals("aaudio", AudioPaths.choose(26, AudioPathChoice.AUTO))
        assertEquals("aaudio", AudioPaths.choose(34, AudioPathChoice.AUTO))
        assertEquals("opensl", AudioPaths.choose(34, AudioPathChoice.AUTO, aaudioFailed = true))
        assertEquals("java", AudioPaths.choose(34, AudioPathChoice.AUTO, aaudioFailed = true, openSlFailed = true))
    }
    @Test fun autoBelowAndroid8UsesOpenSl() {
        assertEquals("opensl", AudioPaths.choose(25, AudioPathChoice.AUTO))
        assertEquals("opensl", AudioPaths.choose(21, AudioPathChoice.AUTO, aaudioFailed = true))
        assertEquals("java", AudioPaths.choose(21, AudioPathChoice.AUTO, openSlFailed = true))
        assertEquals(listOf("opensl", "java"), AudioPaths.order(25, AudioPathChoice.AUTO))
    }
    @Test fun forcedChoicesFallBackOnlyToJava() {
        assertEquals("aaudio", AudioPaths.choose(30, AudioPathChoice.AAUDIO))
        assertEquals("java", AudioPaths.choose(30, AudioPathChoice.AAUDIO, aaudioFailed = true))
        assertEquals("java", AudioPaths.choose(24, AudioPathChoice.AAUDIO))                  // no AAudio before Android 8
        assertEquals("opensl", AudioPaths.choose(34, AudioPathChoice.OPENSL, aaudioFailed = true))
        assertEquals("java", AudioPaths.choose(34, AudioPathChoice.OPENSL, openSlFailed = true))
        assertEquals("java", AudioPaths.choose(34, AudioPathChoice.JAVA))
    }
    @Test fun storedValuesParse() {
        assertEquals(AudioPathChoice.OPENSL, AudioPathChoice.parse("OPENSL"))
        assertEquals(AudioPathChoice.AAUDIO, AudioPathChoice.parse(" aaudio "))
        assertEquals(AudioPathChoice.AUTO, AudioPathChoice.parse(null))
        assertEquals(AudioPathChoice.AUTO, AudioPathChoice.parse("nonsense"))
        assertEquals("OpenSL ES (native)", AudioPaths.describe("opensl"))
        assertEquals("stopped", AudioPaths.describe(""))
    }
}
