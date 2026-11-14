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
        }
    }
}
