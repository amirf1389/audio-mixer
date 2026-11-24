package com.amfb.amixos

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri

/** MANAGE LICENCE ONLINE: the customer licence site of the vendor's licence server (<server>/licence/), opened with this device's machine code
 *  and plan filled in, the same link the console's LICENSE page opens. Only an https server (or one on this device) is used; the machine code
 *  keeps only the characters a machine code has, the plan only a known plan name, so nothing else can get into the link. */
object LicenceLink {
    const val KEY = "licenceServer"
    /** the public licence portal the console also uses by default (native/amixos/src/Update.h) */
    const val DEFAULT_SERVER = "https://licence.amirfakhribadri.my"
    private val PLANS = setOf("basic", "pro", "studio", "master", "ultimate")

    fun server(c: Context): String = Prefs.local(c).getString(KEY, DEFAULT_SERVER)?.takeIf { it.isNotBlank() } ?: DEFAULT_SERVER

    /** the link, or null when [server] is not an https address (or http on this device) */
    fun url(server: String, machine: String?, edition: String?): String? {
        val base = server.trim().trimEnd('/')
        val m = Regex("^(https)://([A-Za-z0-9.-]+)(:\\d{1,5})?(/[A-Za-z0-9._~/-]*)?$").matchEntire(base)
            ?: Regex("^(http)://(127\\.0\\.0\\.1|localhost)(:\\d{1,5})?(/[A-Za-z0-9._~/-]*)?$").matchEntire(base) ?: return null
        if (m.groupValues[2].isEmpty()) return null
        val code = (machine ?: "").trim().uppercase().filter { it in 'A'..'Z' || it in '0'..'9' || it == '-' }.take(64).takeIf { it.length >= 8 }
        val plan = (edition ?: "").trim().lowercase().takeIf { it in PLANS }
        val q = listOfNotNull(code?.let { "machine=$it" }, plan?.let { "edition=$it" }).joinToString("&")
        return "$base/licence/" + if (q.isEmpty()) "" else "?$q"
    }

    /** the link for this device: the machine code and plan from the engine's licence (amix_license_json) */
    fun forLicence(c: Context, licenceJson: String?): String? {
        val j = runCatching { org.json.JSONObject(licenceJson ?: "") }.getOrNull()
        return url(server(c), j?.optString("machine"), j?.optString("plan"))
    }

    fun open(a: Activity, link: String) { runCatching { a.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(link)).addCategory(Intent.CATEGORY_BROWSABLE)) } }
}
