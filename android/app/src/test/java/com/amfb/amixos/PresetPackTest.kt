package com.amfb.amixos

import android.app.AlertDialog
import android.os.Looper
import androidx.test.core.app.ApplicationProvider
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.shadows.ShadowAlertDialog
import java.security.KeyPairGenerator
import java.security.Signature
import java.security.spec.ECGenParameterSpec
import java.util.Base64

/** THE PRESET PACK: the built-in pack parses with every set, the lists show it, a downloaded pack counts only with the signed sha256 */
@RunWith(RobolectricTestRunner::class)
class PresetPackTest {
    private val ctx get() = ApplicationProvider.getApplicationContext<android.content.Context>()
    private fun repoPack() = (java.io.File("../../releases/presets/presets-pack.json").takeIf { it.exists() } ?: java.io.File("../releases/presets/presets-pack.json")).readBytes()

    @Test fun builtInPackParsesWithTheOwnersPresets() {
        val p = PresetPack.builtIn(ctx); assertNotNull("the pack is bundled as an asset", p); p!!
        assertEquals(10, p.mastering.size); assertEquals(600, p.effects.size); assertTrue(p.tone.size >= 25)
        assertEquals(listOf("AMFB SIGNATURE", "TRANSPARENT", "STREAMING", "WARM", "PUNCH", "LOUD", "BROADCAST VOICE", "SHORT VIDEO / LIVE", "METAL", "HARD ROCK"), p.mastering.map { it.name })
        assertEquals(2.5, (p.mastering.first { it.name == "WARM" }.params["low"] as Number).toDouble(), 0.0)
        assertEquals(setOf("reverb", "delay", "chorus", "drive", "pingpong"), p.effects.map { it.kind }.toSet())
        assertEquals(PresetPack.sha256(repoPack()), PresetPack.sha256(ctx.assets.open("presets-pack.json").readBytes()))   // the asset is the repository's pack, byte for byte
        assertNull(PresetPack.parse("{}")); assertNull(PresetPack.parse("not json"))
        assertTrue(PresetPack.newer("2026.11.20", "2026.11.3")); assertTrue(!PresetPack.newer("2026.1.1", "2026.1.1"))
    }

    @Test fun masteringScreenListsEveryPreset() {
        val a = Robolectric.buildActivity(MasteringActivity::class.java).setup().get(); shadowOf(Looper.getMainLooper()).idle()
        a.showMasteringPresets(); var d = ShadowAlertDialog.getLatestAlertDialog() as AlertDialog
        assertEquals(10, d.listView.adapter.count); assertEquals("AMFB SIGNATURE", d.listView.adapter.getItem(0))
        a.showAllPresets(); d = ShadowAlertDialog.getLatestAlertDialog() as AlertDialog
        assertEquals(3, d.listView.adapter.count); assertTrue(d.listView.adapter.getItem(1).toString().contains("600"))
    }

    @Test fun aDownloadedPackNeedsTheSignedSha() {
        val kp = KeyPairGenerator.getInstance("EC").apply { initialize(ECGenParameterSpec("secp256r1")) }.generateKeyPair()
        val pack = repoPack()
        fun envelope(sha: String, key: java.security.PrivateKey = kp.private): String {
            val payload = JSONObject().put("product", "audio-mixer").put("version", "1.25.0.0").put("files", JSONObject()).put("presets", JSONObject().put("version", "x").put("sha256", sha)).toString()
            val der = Signature.getInstance("SHA256withECDSA").apply { initSign(key); update(payload.toByteArray()) }.sign()
            // DER -> r||s as bridge/update.js signs
            var i = 3; val rl = der[i].toInt(); val r = der.copyOfRange(i + 1, i + 1 + rl); i += 1 + rl + 1; val sl = der[i].toInt(); val s = der.copyOfRange(i + 1, i + 1 + sl)
            fun f(b: ByteArray) = ByteArray(32).also { val t = b.takeLast(32).toByteArray(); System.arraycopy(t, 0, it, 32 - t.size, t.size) }
            return JSONObject().put("payload", payload).put("signature", Base64.getUrlEncoder().withoutPadding().encodeToString(f(r) + f(s))).toString()
        }
        val good = PresetPack.sha256(pack)
        assertNotNull("signed sha256 matches: taken", PresetPack.accept(pack, envelope(good), kp.public))
        assertNull("a different sha256: refused", PresetPack.accept(pack, envelope("0".repeat(64)), kp.public))
        val tampered = pack.copyOf().also { it[it.size / 2] = (it[it.size / 2] + 1).toByte() }
        assertNull("the pack changed after signing: refused", PresetPack.accept(tampered, envelope(good), kp.public))
        val other = KeyPairGenerator.getInstance("EC").apply { initialize(ECGenParameterSpec("secp256r1")) }.generateKeyPair()
        assertNull("signed by another key: refused", PresetPack.accept(pack, envelope(good, other.private), kp.public))
        assertNull("not signed by the vendor: refused", PresetPack.accept(pack, envelope(good)))
        assertNull(PresetPack.accept(pack, "{}", kp.public))
    }
}
