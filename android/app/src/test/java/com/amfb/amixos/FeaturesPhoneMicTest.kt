package com.amfb.amixos

import android.Manifest
import android.view.View
import android.view.ViewGroup
import android.widget.Switch
import android.widget.TextView
import androidx.test.core.app.ApplicationProvider
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import java.io.IOException

/** FEATURES (SETTINGS chooses the parts the app shows) and the PHONE MIC (the link's states, the permission flow, the page). */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-xxhdpi")
class FeaturesPhoneMicTest {
    private val ctx get() = ApplicationProvider.getApplicationContext<android.app.Application>()
    private fun find(v: View, tag: String): View? { if (v.tag == tag || v.contentDescription == tag) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), tag)?.let { return it }; return null }
    private fun texts(v: View, out: MutableList<String> = mutableListOf()): List<String> { if (v is TextView) out += v.text.toString(); if (v is ViewGroup) for (i in 0 until v.childCount) texts(v.getChildAt(i), out); return out }
    @Before fun clean() { Prefs.remote(ctx).edit().clear().commit(); Nav.standaloneForTests = null }
    @After fun after() { Prefs.remote(ctx).edit().clear().commit(); Nav.standaloneForTests = null }

    @Test fun everyPartIsOnByDefaultAndTheMixerCannotBeSwitchedOff() {
        for (f in Features.all) assertTrue(f.id, Features.isOn(ctx, f.id))
        Features.set(ctx, "mixer", false); assertTrue(Features.isOn(ctx, "mixer"))
        Features.set(ctx, "pads", false); assertFalse(Features.isOn(ctx, "pads")); assertFalse(Features.tileOn(ctx, "SamplePadsActivity")); assertTrue(Features.tileOn(ctx, "SettingsActivity"))
        Features.resetAll(ctx); assertTrue(Features.isOn(ctx, "pads"))
        assertEquals(Features.all.map { it.id }.distinct(), Features.all.map { it.id })   // no id twice
        assertTrue(Features.forApp(true).none { it.id == "mic" || it.id == "dca" }); assertTrue(Features.forApp(true).any { it.id == "streaming" })
    }
    @Test fun homeShowsOnlyTheChosenPartsAndTheBarDropsTools() {
        Features.set(ctx, "pads", false); Features.set(ctx, "tools", false); Features.set(ctx, "mic", false)
        val a = Robolectric.buildActivity(HomeActivity::class.java).setup().get(); val t = texts(a.window.decorView)
        assertFalse(t.contains(a.getString(R.string.home_pads))); assertFalse(t.contains(a.getString(R.string.home_mic))); assertTrue(t.contains(a.getString(R.string.home_mixer)))
        val bar = find(a.window.decorView, "navBar") as ViewGroup; val labels = (0 until bar.childCount).map { bar.getChildAt(it).contentDescription.toString() }
        assertFalse(labels.contains(a.getString(R.string.nav_tools))); assertTrue(labels.contains(a.getString(R.string.nav_settings)))
        Features.resetAll(ctx); val b = Robolectric.buildActivity(HomeActivity::class.java).setup().get(); assertTrue(texts(b.window.decorView).contains(b.getString(R.string.home_mic)))
    }
    @Test fun settingsSwitchesAPartAndTheRemoteToolsFollow() {
        val s = Robolectric.buildActivity(SettingsActivity::class.java).setup().get()
        val mixer = find(s.window.decorView, "feat_mixer") as Switch; assertTrue(mixer.isChecked); assertFalse(mixer.isEnabled)
        (find(s.window.decorView, "feat_dca") as Switch).performClick(); assertFalse(Features.isOn(ctx, "dca"))
        val r = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        val tools = RemoteTools.toolsPanel(r) { }; assertNull(find(tools, "tool_dca")); assertNotNull(find(tools, "tool_mic"))
    }
    @Test fun theMixerAppListsOnlyItsOwnPages() {
        Nav.standaloneForTests = true
        val s = Robolectric.buildActivity(SettingsActivity::class.java).setup().get()
        assertNotNull(find(s.window.decorView, "feat_streaming")); assertNull(find(s.window.decorView, "feat_mic"))
        Features.set(ctx, "streaming", false)
        val a = Robolectric.buildActivity(SettingsActivity::class.java).setup().get(); val bar = find(a.window.decorView, "navBar") as ViewGroup
        assertFalse((0 until bar.childCount).any { bar.getChildAt(it).contentDescription == a.getString(R.string.nav_players) })
    }

    private class FakeLink : MicSession.Link {
        val codes = ArrayDeque<Any>(); var ons = 0; var offs = 0; val sent = mutableListOf<ByteArray>()
        override fun on(strip: Int) { ons++ }
        override fun off() { offs++ }
        override fun send(pcm: ByteArray, len: Int): Int { sent += pcm.copyOf(len); return when (val c = codes.removeFirstOrNull() ?: 200) { is IOException -> throw c; else -> c as Int } }
    }
    @Test fun theMicLinkMutesReconnectsAndTurnsItselfBackOn() {
        var t = 0L; val l = FakeLink(); val s = MicSession(l, 3) { t }
        val chunk = ByteArray(8) { if (it % 2 == 0) 0 else 0x40 }   // 0x4000 = half scale
        assertEquals(0.5, MicSession.peak(chunk, chunk.size), 1e-3)
        assertFalse(s.deliver(chunk.copyOf(), 8))   // not started: nothing goes
        s.start(); assertEquals(1, l.ons); assertTrue(s.deliver(chunk.copyOf(), 8)); assertEquals(MicSession.State.LIVE, s.state)
        s.muted = true; s.deliver(chunk.copyOf(), 8); assertTrue(l.sent.last().all { it == 0.toByte() }); s.muted = false
        l.codes += IOException("timeout"); assertFalse(s.deliver(chunk.copyOf(), 8)); assertEquals(MicSession.State.RECONNECTING, s.state)
        val n = l.sent.size; assertFalse(s.deliver(chunk.copyOf(), 8)); assertEquals(n, l.sent.size)   // waits before the retry: dropped, not sent
        t += 600; assertTrue(s.deliver(chunk.copyOf(), 8)); assertEquals(MicSession.State.LIVE, s.state)
        l.codes += 409; s.deliver(chunk.copyOf(), 8); assertEquals(2, l.ons)   // the console restarted: the mic is turned on again
        t += 600; assertTrue(s.deliver(chunk.copyOf(), 8))
        l.codes += 401; s.deliver(chunk.copyOf(), 8); assertEquals(MicSession.State.REFUSED, s.state); assertFalse(s.deliver(chunk.copyOf(), 8))
        s.stop(); assertEquals(1, l.offs); assertEquals(MicSession.State.IDLE, s.state); s.stop(); assertEquals(1, l.offs)
    }
    @Test fun permissionStepsAskThenExplain() {
        assertEquals("ok", MicSession.permissionStep(true, true, false))
        assertEquals("ask", MicSession.permissionStep(false, false, false)); assertEquals("ask", MicSession.permissionStep(false, true, true))
        assertEquals("settings", MicSession.permissionStep(false, true, false))
    }
    @Test fun thePageAsksForTheMicrophoneAndExplainsARefusal() {
        Prefs.remote(ctx).edit().putString("host", "127.0.0.1").putString("token", "t").commit()
        val ac = Robolectric.buildActivity(PhoneMicActivity::class.java).setup(); val a = ac.get()
        assertEquals(a.getString(R.string.mic_idle), (find(a.window.decorView, "micState") as TextView).text.toString())
        assertFalse(find(a.window.decorView, "micMute")!!.isEnabled)
        shadowOf(a).denyPermissions(Manifest.permission.RECORD_AUDIO)
        find(a.window.decorView, "micStart")!!.performClick()
        val req = shadowOf(a).lastRequestedPermission; assertNotNull(req); assertEquals(Manifest.permission.RECORD_AUDIO, req.requestedPermissions[0])
        a.onRequestPermissionsResult(11, arrayOf(Manifest.permission.RECORD_AUDIO), intArrayOf(android.content.pm.PackageManager.PERMISSION_DENIED))
        assertEquals(a.getString(R.string.mic_denied), (find(a.window.decorView, "micState") as TextView).text.toString())
        assertNull(a.session)
        ac.configurationChange(android.content.res.Configuration(a.resources.configuration).apply { orientation = android.content.res.Configuration.ORIENTATION_LANDSCAPE })   // turning the phone keeps the page
        ac.pause().stop().destroy()
    }
}
