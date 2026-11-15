package com.amfb.amixos

import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Looper
import android.view.View
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File
import java.io.FileOutputStream

/** Draws the app's screens into screenshots-android/ (git-ignored) on every test run: `gradle testReleaseUnitTest`.
 *  With AMFX_DAEMON=port (a running amixos-daemon on 127.0.0.1 with token file AMFX_TOKEN), the remote is shown connected. */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-xxhdpi")
class ScreenshotTest {
    private val out = File(System.getProperty("amfx.shots") ?: "screenshots-android").apply { mkdirs() }
    private fun shot(v: View, name: String) {
        v.measure(View.MeasureSpec.makeMeasureSpec(1233, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(4000, View.MeasureSpec.AT_MOST))
        v.layout(0, 0, v.measuredWidth, maxOf(1, v.measuredHeight))
        val b = Bitmap.createBitmap(v.width, maxOf(1, v.height), Bitmap.Config.ARGB_8888); v.draw(Canvas(b))
        FileOutputStream(File(out, "$name.png")).use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }
    private fun idle(ms: Long) { val end = System.currentTimeMillis() + ms; while (System.currentTimeMillis() < end) { shadowOf(Looper.getMainLooper()).idle(); Thread.sleep(50) } }

    @Test fun home() {
        Prefs.remote(org.robolectric.RuntimeEnvironment.getApplication()).edit().putString("host", "192.168.1.20").putString("port", "8765").putString("token", "x")
            .putString("lastName", "A mix OS 1.23.0.1").putString("lastPlan", "PRO").putInt("lastStrips", 20).commit()
        val a = Robolectric.buildActivity(HomeActivity::class.java).setup().resume().get()
        shot(a.window.decorView.rootView, "home")
    }

    @Test fun mixdown() {   // the empty screen (the file picker is not there on the JVM)
        val a = Robolectric.buildActivity(MixdownActivity::class.java).setup().get(); idle(300)
        shot(a.window.decorView.rootView, "mixdown")
    }
    @Test fun devices() {   // the JVM's own audio devices and network interfaces (on a phone: the phone's)
        val a = Robolectric.buildActivity(DevicesActivity::class.java).setup().resume().get(); idle(300)
        shot(a.window.decorView.rootView, "devices")
    }
    @Test fun settings() {
        val a = Robolectric.buildActivity(SettingsActivity::class.java).setup().get()
        shot(a.window.decorView.rootView, "settings")
    }

    @Test fun eq() {
        val i = android.content.Intent(org.robolectric.RuntimeEnvironment.getApplication(), EqActivity::class.java).putExtra("strip", 0).putExtra("name", "VOCAL")
        val a = Robolectric.buildActivity(EqActivity::class.java, i).setup().get()
        a.curve.bands = EqActivity.PRESETS["VOCAL"]!!.map { it.copy() }.toMutableList(); a.curve.selected = 4
        shot(a.window.decorView.rootView, "eq")
    }
    @Test fun calculator() {
        val a = Robolectric.buildActivity(CalculatorActivity::class.java).setup().get()
        for ((k, n) in listOf(0 to "calc-delay", 1 to "calc-align", 3 to "calc-frequency")) { a.show(k); shot(a.window.decorView.rootView, n) }
    }

    @Test fun plugins() {   // from a sample console snapshot (the same JSON the console writes)
        val a = Robolectric.buildActivity(PluginsActivity::class.java).setup().get()
        val s = """{"available":true,"version":"1.23.0.1","written":1763100000000,
          "racks":[{"strip":0,"name":"VOCAL","plugins":[{"slot":0,"kind":"fx:deesser","name":"De-esser","bypass":false,"builtIn":true},{"slot":1,"kind":"vst3:x","name":"Studio Compressor","bypass":false,"builtIn":false},{"slot":2,"kind":"fx:reverb","name":"Reverb","bypass":true,"builtIn":true}]},
                   {"strip":3,"name":"GUITAR","plugins":[{"slot":0,"kind":"vst3:y","name":"Amp Sim","bypass":false,"builtIn":false,"error":"not found on this computer"}]}],
          "vst":[{"name":"Studio Compressor","vendor":"Example Audio","format":"vst3"},{"name":"Amp Sim","vendor":"Sample Labs","format":"vst3"},{"name":"Old Delay","vendor":"Legacy Co","format":"vst2"}],
          "store":[{"name":"AmF X1 Network","kind":"extension","note":"phone remote and network control","installed":true,"on":true},{"name":"AmF X1 Hybrid","kind":"extension","note":"sixteen-strip hybrid mixer","installed":false,"on":true},
                   {"name":"Reverb","kind":"effect","note":"built-in rack effect","installed":true,"on":true},{"name":"Chorus","kind":"effect","note":"built-in rack effect","installed":true,"on":false}]}"""
        @Suppress("UNCHECKED_CAST") a.show(com.amfb.amixos.remote.MiniJson.parse(s) as Map<String, Any?>)
        shot(a.window.decorView.rootView, "plugins-racks")
        a.tabTo(1); shot(a.window.decorView.rootView, "plugins-vst")
        a.tabTo(2); shot(a.window.decorView.rootView, "plugins-extensions")
    }

    @Test fun tools() {   // the microphone is not there on the JVM: the readings are put on the screen as the microphone thread would
        val a = Robolectric.buildActivity(ToolsActivity::class.java).setup().get()
        a.show(0); a.showReading(-20.0, 441.6); shot(a.window.decorView.rootView, "tools-tuner")
        a.show(1); repeat(5) { a.showReading(-38.0, 0.0) }; shot(a.window.decorView.rootView, "tools-level")
        a.show(2); shot(a.window.decorView.rootView, "tools-generator")
    }

    @Test fun remote() {
        val port = System.getProperty("amfx.daemon").orEmpty()
        val ctx = org.robolectric.RuntimeEnvironment.getApplication()
        if (port.isNotEmpty()) Prefs.remote(ctx).edit().putString("host", "127.0.0.1").putString("port", port).putString("token", System.getenv("AMFX_TOKEN") ?: "").putBoolean("autoConnect", true).commit()
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        idle(if (port.isNotEmpty()) 2500 else 200)
        shot(a.window.decorView.rootView, if (port.isNotEmpty()) "remote-connected" else "remote")
        if (port.isNotEmpty()) {   // the channel page and the master page, drawn on their own (they open as dialogs in the app)
            val c = com.amfb.amixos.remote.DaemonClient(com.amfb.amixos.remote.Endpoint("127.0.0.1", port.toInt(), System.getenv("AMFX_TOKEN") ?: ""))
            shot(a.channelPanel(c, 0), "channel"); shot(a.masterPanel(c), "master")
            ctx.getSharedPreferences("scenes", 0).edit()
                .putString("Sunday service", "{\"t\":1763090000000,\"master\":-2,\"strips\":[{\"id\":0,\"name\":\"PASTOR\",\"db\":0,\"pan\":0,\"mute\":false,\"solo\":false},{\"id\":1,\"name\":\"CHOIR\",\"db\":-6,\"pan\":0,\"mute\":true,\"solo\":false}],\"buses\":[]}")
                .putString("Band rehearsal", "{\"t\":1763100000000,\"master\":0,\"strips\":[{\"id\":0,\"name\":\"VOX\",\"db\":-3,\"pan\":0,\"mute\":false,\"solo\":false}],\"buses\":[]}").commit()
            shot(a.scenesPanel(c), "scenes")
        }
    }
}
