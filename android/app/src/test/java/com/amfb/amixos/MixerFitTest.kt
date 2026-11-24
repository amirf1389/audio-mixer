package com.amfb.amixos

import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Looper
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import java.io.File
import java.io.FileOutputStream

/** FADERS FIT THE SCREEN: on a small, a tall, a tablet and a phone on its side, in both apps, every strip's fader keeps a real share of
 *  its strip and no key is cut off; on its side more strips are across. Every MUTE / SOLO / FX / EQ / fader of the phone mixer answers. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class MixerFitTest {
    @After fun reset() { Nav.standaloneForTests = null }
    private fun all(v: View, out: MutableList<View> = mutableListOf()): List<View> { out += v; if (v is ViewGroup) for (i in 0 until v.childCount) all(v.getChildAt(i), out); return out }
    private fun open(standalone: Boolean): LocalMixerActivity {
        Nav.standaloneForTests = standalone
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().visible().get()
        val dm = a.resources.displayMetrics; val root = a.window.decorView
        repeat(3) { root.measure(View.MeasureSpec.makeMeasureSpec(dm.widthPixels, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(dm.heightPixels, View.MeasureSpec.EXACTLY)); root.layout(0, 0, dm.widthPixels, dm.heightPixels); shadowOf(Looper.getMainLooper()).idle() }
        return a
    }
    private fun check(name: String, minAcross: Int) {
        for (standalone in listOf(true, false)) {
            val a = open(standalone); val root = a.window.decorView; val faders = all(root).filterIsInstance<FaderView>().filter { it.isShown }
            assertTrue("$name: faders on screen", faders.size >= 3)
            for (f in faders) { val strip = f.parent as LinearLayout
                assertTrue("$name: fader ${f.height} px of a ${strip.height} px strip", f.height >= strip.height * 0.30)
                for (i in 0 until strip.childCount) { val c = strip.getChildAt(i); if (c.visibility != View.GONE) assertTrue("$name: ${c.javaClass.simpleName} cut off (${c.bottom} > ${strip.height})", c.bottom <= strip.height) } }
            val desk = root.findViewWithTag<View>("localChannels") as LinearLayout; val scroller = desk.parent as View
            val across = (0 until desk.childCount).count { val c = desk.getChildAt(it); c.left < scroller.width && c.right > 0 && c.right <= scroller.width + 8 }
            assertTrue("$name: $across strips across", across >= minAcross)
            assertTrue("$name: no quick master row over the mixer (its master strip is there)", root.findViewWithTag<View>("quickMaster") == null)
            if (standalone) { val out = File(System.getProperty("amfx.shots2") ?: "build/shots2").apply { mkdirs() }
                val b = Bitmap.createBitmap(root.width, root.height, Bitmap.Config.ARGB_8888); root.draw(Canvas(b)); FileOutputStream(File(out, "mixer-$name.png")).use { b.compress(Bitmap.CompressFormat.PNG, 100, it) } }
        }
    }
    @Test @Config(qualifiers = "w360dp-h640dp-xhdpi") fun smallPhone() = check("360x640", 3)
    @Test @Config(qualifiers = "w412dp-h915dp-xxhdpi") fun tallPhone() = check("412x915", 3)
    @Test @Config(qualifiers = "w800dp-h1280dp-xhdpi") fun tablet() = check("800x1280", 4)
    @Test @Config(qualifiers = "w915dp-h412dp-land-xxhdpi") fun phoneOnItsSide() = check("915x412-land", 6)
    @Test @Config(qualifiers = "w640dp-h360dp-land-xhdpi") fun smallOnItsSide() = check("640x360-land", 5)

    /** QUICK MASTER on the other screens: − / + a dB a tap and MUTE, written where the phone mixer reads them */
    @Test @Config(qualifiers = "w360dp-h640dp-xhdpi") fun quickMasterOnEveryScreen() {
        Nav.standaloneForTests = true
        for (cls in listOf(SettingsActivity::class.java, ToolsActivity::class.java)) {
            val a = Robolectric.buildActivity(cls).setup().get(); val root = a.window.decorView; val p = Prefs.local(a)
            p.edit().putFloat("masterDb", 0f).putBoolean("masterMute", false).commit()
            assertTrue("${cls.simpleName}: the quick master row", root.findViewWithTag<View>("quickMaster") != null)
            root.findViewWithTag<View>("qmUp").performClick(); assertEquals(1.0, p.getFloat("masterDb", 0f).toDouble(), 1e-6)
            root.findViewWithTag<View>("qmDown").performClick(); root.findViewWithTag<View>("qmDown").performClick(); assertEquals(-1.0, p.getFloat("masterDb", 0f).toDouble(), 1e-6)
            root.findViewWithTag<View>("qmMute").performClick(); assertTrue(QuickMaster.muted(a)); assertEquals(-120.0, LocalMixerActivity.masterOut(p, 0.0), 0.0)
            root.findViewWithTag<View>("qmMute").performClick(); assertTrue(!QuickMaster.muted(a))
            assertTrue((root.findViewWithTag<View>("qmLevel") as android.widget.TextView).text.contains("-1.0"))
        }
    }

    @Test fun keySizing() {
        assertEquals((22 * 2f).toInt(), MixerFit.keyPx(300, 4, 100, 2f))        // a short strip: the smallest key, the fader keeps its share
        assertEquals((40 * 2f).toInt(), MixerFit.keyPx(3000, 4, 100, 2f))       // a tall one: keys no bigger than 40 dp
        assertEquals(6, MixerFit.across(2745, 3f, false, 6)); assertTrue(MixerFit.across(2745, 3f, true, 6) >= 9)
    }

    /** every control of the phone mixer works: MUTE / SOLO on the inputs and decks, MASTER MUTE, FX and EQ open, faders store their level */
    @Test @Config(qualifiers = "w412dp-h915dp-xxhdpi") fun everyControlAnswers() {
        val a = open(true); val p = Prefs.local(a); val root = a.window.decorView
        fun click(tag: String) { val v = all(root).firstOrNull { it.contentDescription?.toString() == tag || it.tag == tag }; assertTrue("$tag is there and enabled", v != null && v.isEnabled && v.isClickable); v!!.performClick(); shadowOf(Looper.getMainLooper()).idle() }
        click("mute_0"); assertTrue(p.getBoolean("mute0", false)); click("mute_0"); assertTrue(!p.getBoolean("mute0", true))
        click("solo_1"); assertTrue(p.getBoolean("solo1", false)); assertTrue("solo cuts the others", LocalMixerActivity.channelLevel(p, 0).second && !LocalMixerActivity.channelLevel(p, 1).second)
        click("solo_1"); assertTrue(!LocalMixerActivity.channelLevel(p, 0).second)
        click("deckMute_0"); assertTrue(p.getBoolean("deckMute0", false)); click("deckMute_0")
        click("solo_100"); assertTrue(p.getBoolean("deckSolo0", false)); click("solo_100")
        click("masterMute"); assertTrue(p.getBoolean("masterMute", false)); assertEquals(-120.0, LocalMixerActivity.masterOut(p, 0.0), 0.0); click("masterMute")
        click("eq_0")
        val faders = all(root).filterIsInstance<FaderView>(); assertTrue(faders.isNotEmpty())
        for (f in faders) assertTrue("a fader takes a move", f.onMove != null && f.isEnabled)
        faders.first().onMove!!(500); assertTrue("the first fader stored its level", p.getFloat("db0", 99f) != 99f)
    }
}
