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
        HomeActivity.bootShown = true   // the boot animation has its own test
        Prefs.remote(org.robolectric.RuntimeEnvironment.getApplication()).edit().putString("host", "192.168.1.20").putString("port", "8765").putString("token", "x")
            .putString("lastName", "STUDIO-PC 1.24.0.0").putString("lastPlan", "PRO").putInt("lastStrips", 20).commit()
        val a = Robolectric.buildActivity(HomeActivity::class.java).setup().resume().get()
        shot(a.window.decorView.rootView, "home")
    }

    @Test fun bootAnimation() {   // five moments of the boot animation side by side, then a tap skips it and HOME is left
        val ctx = org.robolectric.RuntimeEnvironment.getApplication(); val w = 540; val h = 1170
        val strip = android.graphics.Bitmap.createBitmap(w * 5, h, android.graphics.Bitmap.Config.ARGB_8888); val c = android.graphics.Canvas(strip)
        val v = BootView(ctx); v.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY)); v.layout(0, 0, w, h)
        floatArrayOf(0.12f, 0.30f, 0.50f, 0.70f, 0.88f).forEachIndexed { i, t -> v.t = t; c.save(); c.clipRect(i * w, 0, (i + 1) * w, h); c.translate((i * w).toFloat(), 0f); c.drawColor(android.graphics.Color.parseColor("#0B0F15")); v.draw(c); c.restore() }
        out.mkdirs(); java.io.FileOutputStream(java.io.File(out, "boot-animation.png")).use { strip.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
        org.junit.Assert.assertEquals(1f, BootView.overshoot(1f), 1e-6f); org.junit.Assert.assertTrue((0..100).any { BootView.overshoot(it / 100f) > 1.02f })   // it lands past its place and settles back
        org.junit.Assert.assertEquals(0f, BootView.alphaAt(1f), 1e-6f); org.junit.Assert.assertEquals(1f, BootView.alphaAt(0.5f), 1e-6f)
        HomeActivity.bootShown = false; Prefs.remote(ctx).edit().putBoolean("bootAnim", true).commit()
        Robolectric.buildActivity(HomeActivity::class.java).setup().get()
        org.junit.Assert.assertTrue("the boot animation starts on a fresh app start", HomeActivity.bootShown)
        var finished = 0; val tap = BootView(ctx) { finished++ }; tap.start(60_000)
        tap.dispatchTouchEvent(android.view.MotionEvent.obtain(0, 0, android.view.MotionEvent.ACTION_DOWN, 10f, 10f, 0)); tap.dispatchTouchEvent(android.view.MotionEvent.obtain(0, 0, android.view.MotionEvent.ACTION_UP, 10f, 10f, 0))
        org.junit.Assert.assertEquals("a tap skips it, once", 1, finished); org.junit.Assert.assertEquals(1f, tap.t, 1e-6f)
        Prefs.remote(ctx).edit().putBoolean("bootAnim", false).commit(); HomeActivity.bootShown = false
        Robolectric.buildActivity(HomeActivity::class.java).setup().get(); org.junit.Assert.assertFalse("switched off in SETTINGS: not shown", HomeActivity.bootShown)
    }
    @Test fun homeLive() {   // HOME with a real console: ONLINE, the engine, the master meter; Scenes and Tools pinned; recent pages; last scene
        val port = System.getProperty("amfx.daemon").orEmpty(); if (port.isEmpty()) return
        val ctx = org.robolectric.RuntimeEnvironment.getApplication(); HomeActivity.bootShown = true
        Prefs.remote(ctx).edit().putString("host", "127.0.0.1").putString("port", port).putString("token", System.getenv("AMFX_TOKEN") ?: "").putString("lastName", "STUDIO-PC 1.24.0.0").putString("lastPlan", "PRO").putInt("lastStrips", 16).commit()
        ctx.getSharedPreferences("home", 0).edit().putStringSet("pinned", setOf("Scenes", "ToolsActivity")).putString("recent", "RemoteActivity,EqActivity,PluginsActivity").putString("lastScene", "Sunday service").putString("newsSeen", "1.24.0.0").commit()
        val a = Robolectric.buildActivity(HomeActivity::class.java).setup().resume().get(); idle(2500)
        shot(a.window.decorView.rootView, "home-live")
        val tiles = a.tiles().map { it.id }; org.junit.Assert.assertEquals(listOf("Scenes", "ToolsActivity"), Home.pinnedFirst(tiles, { it }, Home.pinned(ctx)).take(2))
    }
    @Test fun computerPlugins() {   // THE COMPUTER'S PLUG-INS from the phone (AMFX_RACKS=port,token: a service playing a rack with real test plug-ins)
        val cfg = (System.getenv("AMFX_RACKS") ?: "").split(","); if (cfg.size != 2) return
        val ctx = org.robolectric.RuntimeEnvironment.getApplication()
        Prefs.remote(ctx).edit().putString("host", "127.0.0.1").putString("port", cfg[0]).putString("token", cfg[1]).commit()
        val a = Robolectric.buildActivity(PluginsActivity::class.java).setup().get(); idle(2500)
        shot(a.window.decorView.rootView, "phone-computer-racks")
        @Suppress("UNCHECKED_CAST") val slots = ((a.liveRacks!!["racks"] as List<Map<String, Any?>>).first { (it["strip"] as Number).toInt() == 0 }["slots"] as List<Map<String, Any?>>)
        println("COMPUTER RACK CH 1: " + slots.map { it["name"].toString() + " loaded=" + it["loaded"] + " " + it["error"] })
        val vst3 = slots.first { it["format"] == "VST3" && it["loaded"] == true }; val slot = (vst3["slot"] as Number).toInt()
        val c = com.amfb.amixos.remote.DaemonClient(com.amfb.amixos.remote.Endpoint("127.0.0.1", cfg[0].toInt(), cfg[1]))
        @Suppress("UNCHECKED_CAST") val full = ((c.racks(0)["racks"] as List<Map<String, Any?>>)[0]["slots"] as List<Map<String, Any?>>).first { (it["slot"] as Number).toInt() == slot }
        shot(a.faceView(c, 0, slot, full) {}, "phone-computer-plugin-face")
        @Suppress("UNCHECKED_CAST") val p0 = (full["params"] as List<Map<String, Any?>>)[0]; val id = (p0["id"] as Number).toLong(); val want = if ((p0["value"] as Number).toDouble() < 0.5) 0.8 else 0.2
        val r = c.pluginParam(0, slot, id, want); println("COMPUTER PLUG-IN " + vst3["name"] + ": " + p0["title"] + " -> " + r["text"])
        @Suppress("UNCHECKED_CAST") val after = ((((c.racks(0)["racks"] as List<Map<String, Any?>>)[0]["slots"] as List<Map<String, Any?>>).first { (it["slot"] as Number).toInt() == slot })["params"] as List<Map<String, Any?>>)[0]
        org.junit.Assert.assertEquals("the real VST3 on the computer took the phone's value", want, (after["value"] as Number).toDouble(), 0.01)
        c.pluginBypass(0, slot, true); @Suppress("UNCHECKED_CAST") val byp = ((((c.racks(0)["racks"] as List<Map<String, Any?>>)[0]["slots"] as List<Map<String, Any?>>).first { (it["slot"] as Number).toInt() == slot })["bypass"])
        org.junit.Assert.assertEquals(true, byp); c.pluginBypass(0, slot, false)
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

    private fun findGrid(v: android.view.View): android.widget.GridLayout? = if (v is android.widget.GridLayout) v
        else (v as? android.view.ViewGroup)?.let { g -> (0 until g.childCount).firstNotNullOfOrNull { findGrid(g.getChildAt(it)) } }
    private fun liveClient(): com.amfb.amixos.remote.DaemonClient? { val port = System.getProperty("amfx.daemon").orEmpty(); if (port.isEmpty()) return null
        val ctx = org.robolectric.RuntimeEnvironment.getApplication(); val tok = System.getenv("AMFX_TOKEN") ?: ""
        Prefs.remote(ctx).edit().putString("host", "127.0.0.1").putString("port", port).putString("token", tok).commit()
        return com.amfb.amixos.remote.DaemonClient(com.amfb.amixos.remote.Endpoint("127.0.0.1", port.toInt(), tok)) }
    @Test fun metersAndScore() {   // the console's levels / loudness / phase, and its sound score with the console's own rules
        liveClient() ?: return
        val m = Robolectric.buildActivity(MetersActivity::class.java).setup().resume().get(); idle(1500)
        println("METERS:\n" + m.loud.text); org.junit.Assert.assertTrue(m.loud.text.contains("LUFS")); org.junit.Assert.assertTrue(m.levels.levels.size >= 4)
        shot(m.window.decorView.rootView, "phone-meters")
        val s = Robolectric.buildActivity(SoundScoreActivity::class.java).setup().resume().get(); idle(1500)
        println("SCORE: " + s.big.text + " valid=" + s.last?.valid); org.junit.Assert.assertNotNull("the score was computed from the console's meters", s.last)
        shot(s.window.decorView.rootView, "phone-sound-score")
    }
    @Test fun routing() {   // the console's routing on the phone: a tap reaches the service (and so the console)
        val c = liveClient() ?: return
        c.inRoute(1, 1); c.outRoute(0, 3)
        val a = Robolectric.buildActivity(RoutingActivity::class.java).setup().resume().get(); idle(1500)
        org.junit.Assert.assertEquals(1, a.routing!!.inputs[1]); org.junit.Assert.assertEquals(3, a.routing!!.outputs[0])
        shot(a.window.decorView.rootView, "phone-routing")
        c.inRoute(1, 0); org.junit.Assert.assertEquals(0, c.routing().inputs[1]); c.inRoute(1, 1)
    }
    @Test fun engineControl() {   // ENGINE CONTROL: reopen the console's sound at another buffer, on the same device
        val c = liveClient() ?: return
        val a = Robolectric.buildActivity(ConsoleStatusActivity::class.java).setup().resume().get(); idle(1500)
        org.junit.Assert.assertNotNull(a.last); val before = a.last!!.bufferFrames
        a.reopen(null, 512); repeat(30) { Thread.sleep(50); shadowOf(Looper.getMainLooper()).idle() }
        val after = c.status(); println("ENGINE: buffer $before -> ${after.bufferFrames}, running ${after.running}, backend ${after.backend}")
        org.junit.Assert.assertEquals(512, after.bufferFrames); org.junit.Assert.assertTrue(after.running)
        shot(a.window.decorView.rootView, "phone-engine-control")
        a.reopen(null, if (before > 0) before else 256); repeat(20) { Thread.sleep(50); shadowOf(Looper.getMainLooper()).idle() }
    }
    @Test fun samplePads() {   // the console's 1000 pads on the phone; a tap plays one on the console; an effect put on a pad shows there; the built-in 100
        val c = liveClient() ?: return
        val a = Robolectric.buildActivity(SamplePadsActivity::class.java).setup().resume().get(); idle(1500)
        org.junit.Assert.assertEquals(100, a.samples.size); org.junit.Assert.assertEquals(50, a.pads.size); org.junit.Assert.assertEquals(1000, a.effects.size)
        val pad = findGrid(a.window.decorView.rootView)!!.getChildAt(2)
        pad.performClick(); var voices = 0; repeat(20) { if (voices == 0) { Thread.sleep(50); voices = (c.control(mapOf("op" to "sample"))["voices"] as Number).toInt() } }
        println("PADS: pad ${a.lastPad} tapped, the console plays $voices voice(s)")
        org.junit.Assert.assertEquals(3, a.lastPad); org.junit.Assert.assertTrue("the console plays the pad", voices > 0)
        shot(a.window.decorView.rootView, "phone-sample-pads"); c.samplesStop()
        // an effect on pad 3 (as a long press does), seen by the console's file, then put back
        a.selPad = 3; a.putOnPad("sfx:260", "Laser 10"); repeat(20) { Thread.sleep(50); shadowOf(Looper.getMainLooper()).idle() }
        org.junit.Assert.assertEquals("sfx:260", c.pads(3, 3).pads[0].source); c.resetPad(3)
        a.view = "EFFECTS"; a.showTabs(); idle(300); findGrid(a.window.decorView.rootView)!!.getChildAt(0).performClick(); idle(300)
        org.junit.Assert.assertEquals(1, a.lastSfx); shot(a.window.decorView.rootView, "phone-sound-effects"); c.samplesStop()
    }
    @Test fun mastering() {   // the console's master chain on the phone: what the console has shows; a move reaches it
        val c = liveClient() ?: return
        c.mastering(mapOf("on" to true, "thr" to -20.0, "low" to 2.0, "makeup" to 3.0))
        val a = Robolectric.buildActivity(MasteringActivity::class.java).setup().resume().get(); idle(1500)
        org.junit.Assert.assertTrue(a.onBtn.isChecked); org.junit.Assert.assertEquals(500, a.bars["thr"]!!.progress)
        shot(a.window.decorView.rootView, "phone-mastering")
        a.onBtn.isChecked = false; repeat(20) { Thread.sleep(50); shadowOf(Looper.getMainLooper()).idle() }
        org.junit.Assert.assertEquals("the console took the phone's OFF", false, c.masteringNow()["on"])
        c.mastering(mapOf("on" to false, "thr" to -12.0, "low" to 0.0, "makeup" to 0.0))
    }
    @Test fun consoleStatus() {   // the console's sound, rate, load, dropouts, remotes
        liveClient() ?: return
        val a = Robolectric.buildActivity(ConsoleStatusActivity::class.java).setup().resume().get(); idle(1500)
        println("STATUS:\n" + a.body.text); org.junit.Assert.assertTrue(a.body.text.contains("Hz")); org.junit.Assert.assertTrue(a.body.text.contains("ON · NULL"))
        shot(a.window.decorView.rootView, "phone-console-status")
    }
    @Test fun eqFollowsConsole() {   // a band set on the console (or another remote) shows in the phone's EQ while it is open
        val port = System.getProperty("amfx.daemon").orEmpty(); if (port.isEmpty()) return
        val ctx = org.robolectric.RuntimeEnvironment.getApplication(); val tok = System.getenv("AMFX_TOKEN") ?: ""
        Prefs.remote(ctx).edit().putString("host", "127.0.0.1").putString("port", port).putString("token", tok).commit(); Prefs.local(ctx).edit().remove("eq3").commit()
        val c = com.amfb.amixos.remote.DaemonClient(com.amfb.amixos.remote.Endpoint("127.0.0.1", port.toInt(), tok))
        c.eq(3, 2, "peaking", 2500.0, 1.4, 6.5)
        val a = Robolectric.buildActivity(EqActivity::class.java, android.content.Intent(ctx, EqActivity::class.java).putExtra("strip", 3)).setup().resume().get(); idle(1800)
        org.junit.Assert.assertEquals("peaking", a.curve.bands[2].type); org.junit.Assert.assertEquals(6.5, a.curve.bands[2].gainDb, 0.01); org.junit.Assert.assertEquals(2500.0, a.curve.bands[2].freq, 0.5)
        c.eq(3, 2, "peaking", 1200.0, 1.0, -4.0)
        repeat(12) { shadowOf(Looper.getMainLooper()).idleFor(java.time.Duration.ofMillis(250)); Thread.sleep(120) }   // the screen's own once-a-second check (the test clock moves only when told)
        org.junit.Assert.assertEquals("followed while open", -4.0, a.curve.bands[2].gainDb, 0.01)
        shot(a.window.decorView.rootView, "eq-follows-console"); c.eq(3, 2, "off", 1200.0, 1.0, 0.0)
    }
    @Test fun rtaEq() {   // RTA EQ against a real service: the RTA and the program EQ go through it; the test device is silent, so a
        // programme-like spectrum is put on the graph for the picture, and AUTO EQ's proposal is drawn from it
        val port = System.getProperty("amfx.daemon").orEmpty(); if (port.isEmpty()) return
        val ctx = org.robolectric.RuntimeEnvironment.getApplication(); val tok = System.getenv("AMFX_TOKEN") ?: ""
        Prefs.remote(ctx).edit().putString("host", "127.0.0.1").putString("port", port).putString("token", tok).commit()
        val c = com.amfb.amixos.remote.DaemonClient(com.amfb.amixos.remote.Endpoint("127.0.0.1", port.toInt(), tok))
        c.programEq(listOf(0.0, 0.0, 0.0, -2.0, 0.0, 0.0, 1.5, 0.0, 0.0, 0.0), true)
        val a = Robolectric.buildActivity(RtaEqActivity::class.java).setup().resume().get(); idle(2500)
        val r = c.rtaNow(); println("RTA from the service: " + (r?.f?.size ?: 0) + " bands, source " + r?.src)
        org.junit.Assert.assertEquals("the phone asked for the master's RTA", -1, r?.src); org.junit.Assert.assertEquals(60, r!!.f.size)
        org.junit.Assert.assertEquals("the program EQ came from the service", -2.0, a.graph.gains[3], 1e-9)
        val db = r.f.map { f -> -38 - 4 * kotlin.math.log2(f / 1000) * (if (f > 1000) 1.6 else 0.4) + (if (kotlin.math.abs(kotlin.math.log2(f / 250)) < 0.6) 7 else 0) }
        a.graph.spectrum = com.amfb.amixos.remote.Rta(-1, r.f, db); a.graph.proposal = RtaEq.autoEq(RtaEq.octaves(r.f, db), RtaEq.Target.FLAT)
        println("AUTO EQ (FLAT): " + a.graph.proposal!!.joinToString())
        shot(a.window.decorView.rootView, "rta-eq")
        c.programEq(a.graph.proposal!!.toList(), true)
        org.junit.Assert.assertEquals(a.graph.proposal!!.toList(), c.programEqNow().gains)
        c.programEq(List(10) { 0.0 }, false); c.rta(null)
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
        // RTA: pink-ish noise with a bump at 2.5 kHz, as the microphone thread would deliver it
        a.show(3); repeat(6) { a.showSpectrum(DoubleArray(31) { i -> -42.0 - i * 0.6 + (if (i in 20..22) 14.0 else 0.0) + ((i * 7) % 5) }) }; shot(a.window.decorView.rootView, "tools-rta")
    }
    @Test fun recorder() {   // no microphone and no native library on the JVM: the list is shown as the screen fills it after two takes
        val a = Robolectric.buildActivity(RecorderActivity::class.java).setup().get()
        a.showTakes(listOf(RecorderActivity.Take(File("2026-11-16_10-42-07.flac"), "96 kHz · 24-bit · stereo · 03:25 · 41.8 MB"), RecorderActivity.Take(File("2026-11-15_19-03-55.wav"), "48 kHz · 32-bit float · mono · 00:48 · 9.2 MB")))
        shot(a.window.decorView.rootView, "recorder")
    }
    @Test @Config(qualifiers = "w891dp-h411dp-land-xxhdpi") fun phoneMixerOnItsSide() {   // landscape: six channels across, the faders over most of the height
        val ctx = org.robolectric.RuntimeEnvironment.getApplication()
        Prefs.local(ctx).edit().putInt("inChannels", 2).putFloat("db0", -3f).putString("section", "faders").putString(LocalMixerActivity.rackKey(0), "comp|plate|delay|drive").commit()
        val v = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get().window.decorView.rootView
        v.measure(View.MeasureSpec.makeMeasureSpec(2673, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(1233, View.MeasureSpec.EXACTLY)); v.layout(0, 0, 2673, 1233)
        val b = Bitmap.createBitmap(2673, 1233, Bitmap.Config.ARGB_8888); v.draw(Canvas(b)); FileOutputStream(File(out, "phone-mixer-landscape.png")).use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }
    @Test fun phoneMixer() {   // no native engine on the JVM: the screen and a rack are drawn from saved choices and made-up engine JSON
        val ctx = org.robolectric.RuntimeEnvironment.getApplication()
        Prefs.local(ctx).edit().putInt("inChannels", 2).putFloat("db0", -3f).putString(LocalMixerActivity.rackKey(0), "comp|plate|delay|drive").putString(LocalMixerActivity.rackKey(-1), "maximizer|||").commit()
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        fun tap(d: String) { fun f(v: View): View? { if (v.contentDescription == d) return v; if (v is android.view.ViewGroup) for (i in 0 until v.childCount) f(v.getChildAt(i))?.let { return it }; return null }; f(a.window.decorView)!!.performClick() }
        tap("sec_faders"); shot(a.window.decorView.rootView, "phone-mixer")                                              // FADERS: the desk has the screen
        tap("sec_media"); shot(a.window.decorView.rootView, "phone-mixer-media")                                         // MEDIA: the decks
        tap("sec_setup")                                                                                                 // SETUP: input, output, chains
        a.paintIo(true, true, true, 0.12f, 0.35f, 0, 0); shot(a.window.decorView.rootView, "phone-mixer-io-live")        // the INPUT / OUTPUT indicator while it runs
        a.paintIo(true, false, true, 0f, 0f, 0, 0); shot(a.window.decorView.rootView, "phone-mixer-io-no-input")         // no microphone: shown red
        val cat = listOf("comp" to "COMPRESSOR", "plate" to "PLATE REVERB", "delay" to "DELAY", "chorus" to "CHORUS")
        val js = mapOf(0 to "{\"kind\":\"comp\",\"name\":\"COMPRESSOR\",\"bypass\":false,\"mix\":1.0,\"params\":[{\"id\":\"thr\",\"title\":\"THRESHOLD\",\"value\":0.55,\"default\":0.5,\"text\":\"-18.0 dB\"},{\"id\":\"ratio\",\"title\":\"RATIO\",\"value\":0.3,\"default\":0.3,\"text\":\"4.0:1\"}]}",
            1 to "{\"kind\":\"plate\",\"name\":\"PLATE REVERB\",\"bypass\":true,\"mix\":0.25,\"params\":[{\"id\":\"decay\",\"title\":\"DECAY\",\"value\":0.4,\"default\":0.4,\"text\":\"1.8 s\"}]}",
            2 to "{\"kind\":\"delay\",\"name\":\"TAPE DELAY\",\"bypass\":false,\"mix\":0.3,\"params\":[{\"id\":\"time\",\"title\":\"TIME\",\"value\":0.35,\"default\":0.3,\"text\":\"375 ms\"},{\"id\":\"fb\",\"title\":\"FEEDBACK\",\"value\":0.42,\"default\":0.3,\"text\":\"42 %\"},{\"id\":\"tone\",\"title\":\"TONE\",\"value\":0.6,\"default\":0.5,\"text\":\"4.8 kHz\"}]}",
            3 to "{\"kind\":\"drive\",\"name\":\"ANALOG DRIVE\",\"bypass\":false,\"mix\":1.0,\"params\":[{\"id\":\"drive\",\"title\":\"DRIVE\",\"value\":0.7,\"default\":0.3,\"text\":\"+14 dB\"},{\"id\":\"out\",\"title\":\"OUTPUT\",\"value\":0.45,\"default\":0.5,\"text\":\"-1.5 dB\"}]}")
        shot(a.rackPanel(0, cat, { js[it] ?: "null" }, {}), "phone-mixer-rack")
    }
    @Test fun consoles() {   // the paired-consoles list, as the CONSOLES dialog shows it
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get(); val now = 1_763_300_000_000L
        val l = listOf(PairedConsoles.Console("192.168.1.20", 8765, "t1", "Studio A", now - 86_400_000L * 9, now - 120_000L), PairedConsoles.Console("192.168.0.42", 8765, "t2", "FOH stage", now - 86_400_000L * 30, now - 86_400_000L * 3))
        shot(a.consolesPanel(l, now, {}, {}), "consoles")
    }
    @Test fun muteGroups() {
        val ctx = org.robolectric.RuntimeEnvironment.getApplication()
        ctx.getSharedPreferences("muteGroups", 0).edit().putString("g0", "0,1,2,3").putBoolean("on0", true).putString("g1", "8,9").putString("g3", "4,5,6").commit()
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        shot(a.muteGroupsPanel(null), "mute-groups")
    }

    @Test fun liveSync() {   // LIVE SYNC: another remote (a browser, the console) moves strips while this phone is connected: they move here at once
        val port = System.getProperty("amfx.daemon").orEmpty(); if (port.isEmpty()) return
        val ctx = org.robolectric.RuntimeEnvironment.getApplication(); val tok = System.getenv("AMFX_TOKEN") ?: ""
        Prefs.remote(ctx).edit().putString("host", "127.0.0.1").putString("port", port).putString("token", tok).putBoolean("autoConnect", true).commit()
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get(); idle(2500)
        val other = com.amfb.amixos.remote.DaemonClient(com.amfb.amixos.remote.Endpoint("127.0.0.1", port.toInt(), tok))
        other.name(0, "VOCAL"); other.gain(0, -6.0); other.name(1, "GUITAR"); other.gain(1, -18.0); other.mute(2, true); other.name(3, "KEYS"); other.solo(3, true); other.master(-3.0)
        idle(1200); shot(a.window.decorView.rootView, "remote-live-sync")
        other.name(0, "CH 1"); other.gain(0, 0.0); other.name(1, "CH 2"); other.gain(1, 0.0); other.mute(2, false); other.name(3, "CH 4"); other.solo(3, false); other.master(0.0)
    }
    @Test fun autoDetect() {   // AUTO-DETECT: the phone paired with the console at .50; the PC now has another address: the search follows it by its consoleId
        val port = System.getProperty("amfx.daemon").orEmpty(); if (port.isEmpty()) return
        val ctx = org.robolectric.RuntimeEnvironment.getApplication(); val tok = System.getenv("AMFX_TOKEN") ?: ""
        val id = com.amfb.amixos.remote.DaemonClient.probe("127.0.0.1", port.toInt())!!.id
        Prefs.remote(ctx).edit().clear().putString("port", port).putBoolean("autoFind", false).commit()
        ctx.getSharedPreferences("consoles", 0).edit().putString("list", PairedConsoles.format(listOf(PairedConsoles.Console("127.0.0.50", port.toInt(), tok, "Studio PC", 1_763_200_000_000L, 1_763_200_000_000L, id)))).commit()
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        Prefs.remote(ctx).edit().putBoolean("autoFind", true).commit(); a.autoFind("127.0.0.9"); idle(3500)
        shot(a.window.decorView.rootView, "remote-auto-detect")
        val moved = PairedConsoles.parse(ctx.getSharedPreferences("consoles", 0).getString("list", "[]"))
        org.junit.Assert.assertEquals("the paired console follows the new address", "127.0.0.1", moved[0].host); org.junit.Assert.assertEquals(1, moved.size)
        org.junit.Assert.assertEquals("127.0.0.1", Prefs.remote(ctx).getString("host", ""))
    }
    @Test fun severalConsoles() {   // TWO REAL CONSOLES (AMFX_MULTI=port,tokenA,tokenB: A on 127.0.0.1, B on 127.0.0.2): the bar, a switch, then B disconnects the phone
        val multi = (System.getenv("AMFX_MULTI") ?: "").split(","); if (multi.size != 3) return
        val port = multi[0].toInt(); val ctx = org.robolectric.RuntimeEnvironment.getApplication()
        val idA = com.amfb.amixos.remote.DaemonClient.probe("127.0.0.1", port)!!.id; val idB = com.amfb.amixos.remote.DaemonClient.probe("127.0.0.2", port)!!.id
        org.junit.Assert.assertNotEquals("two consoles, two ids", idA, idB)
        Prefs.remote(ctx).edit().clear().putString("host", "127.0.0.1").putString("port", port.toString()).putString("token", multi[1]).putBoolean("autoConnect", true).putBoolean("autoFind", false).commit()
        ctx.getSharedPreferences("consoles", 0).edit().putString("list", PairedConsoles.format(listOf(
            PairedConsoles.Console("127.0.0.1", port, multi[1], "Studio PC", 3, 3, idA), PairedConsoles.Console("127.0.0.2", port, multi[2], "Stage PC", 2, 2, idB),
            PairedConsoles.Console("127.0.0.9", port, "amx_x", "Home PC", 1, 1, "home00000000")))).commit()
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get(); idle(2500)
        a.presenceScan("127.0.0.50"); idle(2500)
        val chips = a.barChips(); println("CONSOLE BAR: " + chips.map { it.label + " " + it.presence + " " + it.console.host })
        org.junit.Assert.assertEquals(listOf(PairedConsoles.Presence.LINKED, PairedConsoles.Presence.ONLINE, PairedConsoles.Presence.AWAY), chips.map { it.presence })
        shot(a.window.decorView.rootView, "remote-consoles")
        a.switchTo(chips[1]); idle(3000)
        org.junit.Assert.assertEquals("127.0.0.2", Prefs.remote(ctx).getString("host", ""))
        org.junit.Assert.assertEquals("Stage PC", a.barChips().first { it.presence == PairedConsoles.Presence.LINKED }.label)
        shot(a.window.decorView.rootView, "remote-console-switched")
        // the stage console's owner presses DISCONNECT on this phone
        val b = com.amfb.amixos.remote.DaemonClient(com.amfb.amixos.remote.Endpoint("127.0.0.2", port, multi[2]))
        @Suppress("UNCHECKED_CAST") val links = (com.amfb.amixos.remote.MiniJson.parse(java.net.URL("http://127.0.0.2:$port/api/links").openConnection().let { it.setRequestProperty("X-Amix-Token", multi[2]); it.getInputStream().bufferedReader().readText() }) as Map<String, Any?>)["links"] as List<Map<String, Any?>>
        println("CONNECTED PHONES on the stage console: " + links.map { it["name"].toString() + " " + it["ip"] })
        org.junit.Assert.assertEquals(1, links.size)
        val c = java.net.URL("http://127.0.0.2:$port/api/links/kick").openConnection() as java.net.HttpURLConnection
        c.requestMethod = "POST"; c.doOutput = true; c.setRequestProperty("X-Amix-Token", multi[2]); c.setRequestProperty("Content-Type", "application/json")
        c.outputStream.use { it.write("{\"id\":${(links[0]["id"] as Number).toInt()}}".toByteArray()) }; org.junit.Assert.assertEquals(200, c.responseCode)
        idle(1500)
        org.junit.Assert.assertTrue(a.barChips().none { it.presence == PairedConsoles.Presence.LINKED })
        shot(a.window.decorView.rootView, "remote-kicked")
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
