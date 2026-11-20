package com.amfb.amixos

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.math.BigInteger
import java.net.HttpURLConnection
import java.net.URL
import java.security.AlgorithmParameters
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.Signature
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec
import java.security.spec.ECParameterSpec
import java.security.spec.ECPoint
import java.security.spec.ECPublicKeySpec
import java.util.Base64

/** THE PRESET PACK: the owner's factory presets (MASTERING, the effect presets, the channel chains and EQ curves), written from the console's
 *  code by scripts/make-presets.js and bundled into the APK (assets/presets-pack.json). A newer pack can be downloaded (the licence portal's
 *  /portal/presets or the URL in the update manifest); it is taken only when its sha256 is the one in an update manifest signed with the
 *  vendor key. Anything else keeps the built-in pack. */
object PresetPack {
    data class Mastering(val name: String, val params: Map<String, Any>)
    data class Effect(val id: Int, val kind: String, val name: String, val category: String, val values: Map<String, Double>)
    /** a tone preset: its filters ([ChannelEq.Band]) and cut filters (0 = off), so it can be put on any channel of the phone mixer */
    data class Tone(val kind: String, val name: String, val category: String, val bands: List<ChannelEq.Band> = emptyList(), val hpHz: Double = 0.0, val lpHz: Double = 0.0)
    data class Pack(val version: String, val mastering: List<Mastering>, val effects: List<Effect>, val tone: List<Tone>) {
        fun effectsOf(kind: String) = effects.filter { it.kind == kind }
    }

    // the vendor's public key (bridge/license-public.json): the only key an update manifest is trusted with
    private const val VENDOR_X = "qs3G8HQmgXrE0oAJn9Vj9jNg15YaPWuclESZ01899Fw"
    private const val VENDOR_Y = "sugiBjJ1wcFHZRerQRq4VMEFSWrGDc6ts9cnD0x-_i0"
    const val MANIFEST_URL = "https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json"
    private const val CACHE = "presets-pack.json"

    fun parse(text: String): Pack? = runCatching {
        val o = JSONObject(text)
        fun list(k: String): JSONArray = o.getJSONArray(k)
        fun map(j: JSONObject): Map<String, Any> = j.keys().asSequence().associateWith { j.get(it) }
        val m = list("mastering").let { a -> (0 until a.length()).map { a.getJSONObject(it).let { x -> Mastering(x.getString("name"), map(x.getJSONObject("params"))) } } }
        val e = list("effects").let { a -> (0 until a.length()).map { a.getJSONObject(it).let { x -> val v = x.getJSONObject("values")
            Effect(x.getInt("id"), x.getString("kind"), x.getString("name"), x.optString("category"), v.keys().asSequence().associateWith { k -> v.getDouble(k) }) } } }
        val t = list("tone").let { a -> (0 until a.length()).map { a.getJSONObject(it).let { x -> toneOf(x) } } }
        if (m.isEmpty() || e.isEmpty() || t.isEmpty()) null else Pack(o.getString("version"), m, e, t)
    }.getOrNull()

    /** a tone preset in either form of the pack: a "chain" (dsp.eq of f / g / q / t, dsp.hpFreq) or an "eq" (bands of freq / gainDb / q / type,
     *  hpFreq / lpFreq, each on when its order is above 0) */
    private fun toneOf(x: JSONObject): Tone {
        val dsp = x.optJSONObject("dsp"); val bands = ArrayList<ChannelEq.Band>(); var hp = 0.0; var lp = 0.0
        dsp?.optJSONArray("eq")?.let { a -> for (i in 0 until a.length()) a.optJSONObject(i)?.let { b -> bands += ChannelEq.Band(b.optDouble("f", 0.0), b.optDouble("g", 0.0), b.optDouble("q", 1.0), b.optInt("t", 1)) } }
        if (dsp != null && dsp.optInt("hpOrder", 0) > 0) hp = dsp.optDouble("hpFreq", 0.0)
        x.optJSONArray("bands")?.let { a -> for (i in 0 until a.length()) a.optJSONObject(i)?.let { b -> bands += ChannelEq.Band(b.optDouble("freq", 0.0), b.optDouble("gainDb", 0.0), b.optDouble("q", 1.0), b.optInt("type", 1)) } }
        if (x.optInt("hpOrder", 0) > 0) hp = x.optDouble("hpFreq", 0.0); if (x.optInt("lpOrder", 0) > 0) lp = x.optDouble("lpFreq", 0.0)
        return Tone(x.getString("kind"), x.getString("name"), x.optString("category"), bands, hp, lp)
    }
    fun sha256(b: ByteArray): String = MessageDigest.getInstance("SHA-256").digest(b).joinToString("") { "%02x".format(it) }

    private fun vendorKey(x: String = VENDOR_X, y: String = VENDOR_Y): ECPublicKey {
        val ap = AlgorithmParameters.getInstance("EC"); ap.init(ECGenParameterSpec("secp256r1"))
        val d = Base64.getUrlDecoder()
        val spec = ECPublicKeySpec(ECPoint(BigInteger(1, d.decode(x)), BigInteger(1, d.decode(y))), ap.getParameterSpec(ECParameterSpec::class.java))
        return KeyFactory.getInstance("EC").generatePublic(spec) as ECPublicKey
    }
    /** the signature is r||s (64 bytes, as bridge/update.js signs); Java wants DER */
    private fun der(rs: ByteArray): ByteArray {
        fun int(b: ByteArray): ByteArray { var v = b.dropWhile { it == 0.toByte() }.toByteArray(); if (v.isEmpty()) v = byteArrayOf(0); if (v[0] < 0) v = byteArrayOf(0) + v; return byteArrayOf(2, v.size.toByte()) + v }
        val body = int(rs.copyOfRange(0, 32)) + int(rs.copyOfRange(32, 64)); return byteArrayOf(0x30, body.size.toByte()) + body
    }
    /** the manifest's payload when the envelope {payload, signature} is signed by [key] (the vendor's unless a test gives its own) */
    fun verifiedManifest(envelope: String, key: java.security.PublicKey = vendorKey()): JSONObject? = runCatching {
        val e = JSONObject(envelope); val payload = e.getString("payload"); val sig = Base64.getUrlDecoder().decode(e.getString("signature"))
        if (sig.size != 64) return null
        val v = Signature.getInstance("SHA256withECDSA"); v.initVerify(key); v.update(payload.toByteArray(Charsets.UTF_8))
        if (!v.verify(der(sig))) return null
        JSONObject(payload).takeIf { it.optString("product") == "audio-mixer" }
    }.getOrNull()

    /** a downloaded pack is taken only when its sha256 is the one the signed manifest names; null = keep the built-in pack */
    fun accept(packBytes: ByteArray, envelope: String, key: java.security.PublicKey = vendorKey()): Pack? {
        val want = verifiedManifest(envelope, key)?.optJSONObject("presets")?.optString("sha256") ?: return null
        if (want.length != 64 || !sha256(packBytes).equals(want, ignoreCase = true)) return null
        return parse(String(packBytes, Charsets.UTF_8))
    }

    /** 2026.11.20 is newer than 2026.11.3 */
    fun newer(a: String, b: String): Boolean { val x = a.split('.', '-'); val y = b.split('.', '-')
        for (i in 0 until maxOf(x.size, y.size)) { val p = x.getOrNull(i)?.toIntOrNull() ?: 0; val q = y.getOrNull(i)?.toIntOrNull() ?: 0; if (p != q) return p > q }; return false }
    fun builtIn(c: Context): Pack? = runCatching { c.assets.open("presets-pack.json").bufferedReader().use { it.readText() } }.getOrNull()?.let(::parse)
    @Volatile private var cached: Pack? = null
    /** the pack to show: a verified download (kept in the app's files) when there is one and it is newer, else the built-in one */
    fun current(c: Context): Pack? {
        cached?.let { return it }
        val b = builtIn(c)
        val d = runCatching { java.io.File(c.filesDir, CACHE).readText() }.getOrNull()?.let(::parse)
        return (if (d != null && (b == null || newer(d.version, b.version))) d else b).also { cached = it }
    }

    /** checks online (run off the main thread): the signed manifest, then the pack from [portal] (the licence portal) or the manifest's URL */
    fun refresh(c: Context, portal: String? = null): Boolean = runCatching {
        val env = get(MANIFEST_URL) ?: return false
        val m = verifiedManifest(String(env, Charsets.UTF_8))?.optJSONObject("presets") ?: return false
        for (u in listOfNotNull(portal?.trimEnd('/')?.plus("/portal/presets"), m.optString("url").takeIf { it.startsWith("https://") })) {
            val bytes = get(u) ?: continue
            val p = accept(bytes, String(env, Charsets.UTF_8)) ?: continue
            java.io.File(c.filesDir, CACHE).writeBytes(bytes); cached = p; return true
        }
        false
    }.getOrDefault(false)

    private fun get(u: String): ByteArray? { val h = URL(u).openConnection() as HttpURLConnection
        return try { h.connectTimeout = 8000; h.readTimeout = 15000; if (h.responseCode != 200) null else h.inputStream.use { it.readBytes() }.takeIf { it.size < 8 * 1024 * 1024 } } finally { h.disconnect() } }
}
