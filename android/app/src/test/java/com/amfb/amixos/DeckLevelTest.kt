package com.amfb.amixos

import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** THE DECKS ARE HEARD: after a fresh install and after the move from an old layout-1 save, every deck's level and mute as the engine
 *  gets them ([LocalMixerActivity.channelLevel], what applyLevel sends to both deck strips) are unity and unmuted. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class DeckLevelTest {
    private val ctx get() = ApplicationProvider.getApplicationContext<android.content.Context>()
    private fun assertAudible(p: android.content.SharedPreferences) {
        for (d in 0 until LocalMixerActivity.DECKS) { val (db, mute) = LocalMixerActivity.channelLevel(p, 100 + d)
            assertEquals("deck $d level", 0.0, db, 1e-9); assertFalse("deck $d muted", mute)
            assertEquals(listOf(LocalMixerActivity.deckStrip(d), LocalMixerActivity.deckStrip(d) + 1), LocalMixerActivity.chStrips(100 + d)) }
    }
    @Test fun freshInstall() { val p = Prefs.local(ctx); p.edit().clear().commit(); LocalMixerActivity.migrate(p); assertAudible(p) }
    @Test fun afterLayout1Migration() {
        val p = Prefs.local(ctx); p.edit().clear().putInt("inChannels", 1).putFloat("db0", 0f).putFloat("db1", -60f).putBoolean("mute1", true)
            .putString("rack2", "hall|||").putString("rack6", "delay|||").putString("chain8", "voice")
            .putFloat("deckDb0", 0f).putBoolean("deckMute0", false).putFloat("deckDb3", 0f).commit()
        LocalMixerActivity.migrate(p); assertEquals(2, p.getInt("layout", 0)); assertAudible(p)
    }
}
