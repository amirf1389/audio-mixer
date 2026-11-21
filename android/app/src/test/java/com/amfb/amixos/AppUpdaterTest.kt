package com.amfb.amixos

import android.widget.TextView
import androidx.test.core.app.ApplicationProvider
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import java.security.KeyPairGenerator
import java.security.Signature
import java.security.spec.ECGenParameterSpec
import java.util.Base64

/** AUTO UPDATES: only a vendor-signed manifest, only this app's entry, only newer builds, only https, only the signed sha256 */
@RunWith(RobolectricTestRunner::class)
class AppUpdaterTest {
    private val kp = KeyPairGenerator.getInstance("EC").apply { initialize(ECGenParameterSpec("secp256r1")) }.generateKeyPair()
    private val app = "com.amfb.amixos"; private val mixer = "com.amfb.amixos.mixer"
    private val shaA = "a".repeat(64); private val shaM = "b".repeat(64)

    private fun entry(pkg: String, build: Int, sha: String, url: String = "https://ota.example/AmF-X1-1.25.0.0-b$build-android.apk") =
        JSONObject().put("package", pkg).put("build", build).put("versionCode", 125000000L + build).put("versionName", "1.25.0.0 (build $build)").put("url", url).put("size", 1234).put("sha256", sha)
    private fun payload(app: JSONObject? = entry(this.app, 6, shaA), mixer: JSONObject? = entry(this.mixer, 7, shaM)) =
        JSONObject().put("product", "audio-mixer").put("version", "1.25.0.0").put("notes", JSONArray().put("faster meters"))
            .put("files", JSONObject().apply { if (app != null) put("android-apk", app); if (mixer != null) put("android-mixer-apk", mixer) }).toString()
    private fun envelope(payload: String = payload(), key: java.security.PrivateKey = kp.private): String {
        val der = Signature.getInstance("SHA256withECDSA").apply { initSign(key); update(payload.toByteArray()) }.sign()
        var i = 3; val rl = der[i].toInt(); val r = der.copyOfRange(i + 1, i + 1 + rl); i += 1 + rl + 1; val sl = der[i].toInt(); val s = der.copyOfRange(i + 1, i + 1 + sl)
        fun f(b: ByteArray) = ByteArray(32).also { val t = b.takeLast(32).toByteArray(); System.arraycopy(t, 0, it, 32 - t.size, t.size) }
        return JSONObject().put("payload", payload).put("signature", Base64.getUrlEncoder().withoutPadding().encodeToString(f(r) + f(s))).toString()
    }
    private fun decide(env: String = envelope(), pkg: String = app, installed: Long = 125000005, skipped: Long = 0) =
        AppUpdater.decide(env, pkg, installed, skipped, key = kp.public)

    @Test fun newerOlderAndSameVersion() {
        val d = decide(); assertTrue(d is AppUpdater.Decision.Available)
        val o = (d as AppUpdater.Decision.Available).offer
        assertEquals(6, o.build); assertEquals(125000006L, o.versionCode); assertEquals(shaA, o.sha256); assertEquals(listOf("faster meters"), o.notes)
        assertEquals("same build: nothing offered", AppUpdater.Decision.UpToDate, decide(installed = 125000006))
        assertEquals("older build: never a downgrade", AppUpdater.Decision.UpToDate, decide(installed = 125000009))
        assertEquals("no entry for this app", AppUpdater.Decision.UpToDate, decide(envelope(payload(app = null))))
    }

    @Test fun eachAppTakesItsOwnEntry() {
        assertEquals("android-apk", AppUpdater.keyFor(app)); assertEquals("android-mixer-apk", AppUpdater.keyFor(mixer))
        val m = (decide(pkg = mixer) as AppUpdater.Decision.Available).offer
        assertEquals(mixer, m.pkg); assertEquals(7, m.build); assertEquals(shaM, m.sha256)
        assertEquals(shaA, (decide(pkg = app) as AppUpdater.Decision.Available).offer.sha256)
    }

    @Test fun skippedVersionIsNotOfferedOnStart() {
        val ctx = ApplicationProvider.getApplicationContext<android.content.Context>()
        AppUpdater.skip(ctx, 125000006); assertEquals(125000006L, AppUpdater.skipped(ctx))
        assertTrue(decide(skipped = AppUpdater.skipped(ctx)) is AppUpdater.Decision.Skipped)
        assertTrue("a later build than the skipped one is offered again", decide(envelope(payload(app = entry(app, 8, shaA))), skipped = 125000006) is AppUpdater.Decision.Available)
        assertTrue(AppUpdater.due(0, 1000)); assertFalse(AppUpdater.due(1000, 1000 + AppUpdater.CHECK_EVERY_MS - 1)); assertTrue(AppUpdater.due(1000, 1000 + AppUpdater.CHECK_EVERY_MS))
    }

    @Test fun sha256MismatchIsRejectedAndDeleted() {
        val dir = createTempDir(); val bytes = ByteArray(1234) { it.toByte() }
        val good = AppUpdater.Offer("android-apk", app, "1.25.0.0", 6, 125000006, "https://x/a.apk", 1234, PresetPack.sha256(bytes), emptyList())
        val f = java.io.File(dir, "update.apk").apply { writeBytes(bytes) }
        assertTrue(AppUpdater.verifyDownload(f, good)); assertTrue(f.exists())
        assertFalse(AppUpdater.verifyDownload(f, good.copy(sha256 = "0".repeat(64)))); assertFalse("a mismatching download is deleted", f.exists())
        f.writeBytes(bytes + 1); assertFalse("a different size", AppUpdater.verifyDownload(f, good)); assertFalse(f.exists())
        assertTrue(decide(envelope(payload(app = entry(app, 6, "")))) is AppUpdater.Decision.Invalid)   // an entry without sha256 is never offered
        dir.deleteRecursively()
    }

    @Test fun unsignedOrWrongKeyManifestIsRejected() {
        val other = KeyPairGenerator.getInstance("EC").apply { initialize(ECGenParameterSpec("secp256r1")) }.generateKeyPair()
        assertEquals(AppUpdater.Decision.Invalid("signature"), decide(envelope(key = other.private)))
        assertEquals(AppUpdater.Decision.Invalid("signature"), decide(JSONObject().put("payload", payload()).toString()))
        assertEquals(AppUpdater.Decision.Invalid("signature"), decide(payload()))
        assertEquals("not signed by the vendor key", AppUpdater.Decision.Invalid("signature"), AppUpdater.decide(envelope(), app, 125000005))
        val tampered = JSONObject(envelope()).let { it.put("payload", it.getString("payload").replace("125000006", "125000099")) }.toString()
        assertEquals(AppUpdater.Decision.Invalid("signature"), decide(tampered))
    }

    @Test fun nonHttpsIsRefused() {
        assertEquals(AppUpdater.Decision.Invalid("url"), decide(envelope(payload(app = entry(app, 6, shaA, "http://ota.example/a.apk")))))
        assertEquals(AppUpdater.Decision.Invalid("url"), decide(envelope(payload(app = entry(app, 6, shaA, "file:///sdcard/a.apk")))))
        assertTrue(AppUpdater.urlAllowed("https://ota.example/a.apk"))
        assertTrue("http only to a configured http OTA host", AppUpdater.urlAllowed("http://192.168.1.5:8080/a.apk", "http://192.168.1.5:8080/update.json"))
        assertFalse(AppUpdater.urlAllowed("http://evil.example/a.apk", "http://192.168.1.5:8080/update.json"))
    }

    @Test fun wrongPackageIsRefused() {
        assertEquals(AppUpdater.Decision.Invalid("package"), decide(envelope(payload(app = entry(mixer, 6, shaA)))))
        assertEquals(AppUpdater.Decision.Invalid("package"), decide(envelope(payload(app = entry(app, 6, shaA).apply { remove("package") }))))
        assertTrue(AppUpdater.archiveAcceptable(app, 125000006, app, 125000005))
        assertFalse("the APK file is another app", AppUpdater.archiveAcceptable(mixer, 125000006, app, 125000005))
        assertFalse("the APK file is older", AppUpdater.archiveAcceptable(app, 125000004, app, 125000005))
    }

    @Test fun settingsShowTheUpdateSection() {
        val a = Robolectric.buildActivity(SettingsActivity::class.java).setup().get()
        fun texts(v: android.view.View): List<String> = when (v) { is TextView -> listOf(v.text.toString()); is android.view.ViewGroup -> (0 until v.childCount).flatMap { texts(v.getChildAt(it)) }; else -> emptyList() }
        val t = texts(a.window.decorView)
        assertTrue(t.contains(a.getString(R.string.set_updates))); assertTrue(t.contains(a.getString(R.string.set_upd_auto))); assertTrue(t.contains(a.getString(R.string.set_upd_wifi)))
        assertTrue(t.contains(a.getString(R.string.set_upd_check)))
        assertTrue(AppUpdater.autoCheck(a)); assertTrue(AppUpdater.wifiOnly(a))
    }
}
