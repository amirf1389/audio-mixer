package com.amfb.amixos

import android.app.Activity
import android.app.AlertDialog
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.widget.ProgressBar
import android.widget.Toast
import androidx.core.content.pm.PackageInfoCompat
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

/** AUTO UPDATES for both Android apps (AmF X1 = com.amfb.amixos, AmF X1 Mixer = com.amfb.amixos.mixer).
 *  The same signed update manifest as [PresetPack] (releases/update.json, signed with the vendor key) lists each app's APK with its
 *  package, build, version code, size and sha256 (scripts/make-update.js). An APK is offered only when the manifest's signature is the
 *  vendor's, the entry is for THIS package and its version code is above the installed one; it is downloaded over https to the app's
 *  cache (no storage permission), taken only when its sha256 is the signed one, and handed to Android's PackageInstaller, which always
 *  asks the user to confirm. Nothing is installed silently. */
object AppUpdater {
    data class Offer(val key: String, val pkg: String, val versionName: String, val build: Int, val versionCode: Long, val url: String,
                     val size: Long, val sha256: String, val notes: List<String>)
    sealed class Decision {
        data class Available(val offer: Offer) : Decision()
        data class Skipped(val offer: Offer) : Decision()
        object UpToDate : Decision()
        data class Invalid(val why: String) : Decision()
    }

    const val CHECK_EVERY_MS = 12L * 60 * 60 * 1000
    private const val MAX_APK = 300L * 1024 * 1024

    /** the manifest's key for this app's APK */
    fun keyFor(pkg: String) = if (pkg.endsWith(".mixer")) "android-mixer-apk" else "android-apk"

    /** https only; plain http only to the host of an http manifest the user configured (a local OTA server) */
    fun urlAllowed(url: String, manifestUrl: String = PresetPack.MANIFEST_URL): Boolean = runCatching {
        val u = Uri.parse(url); val m = Uri.parse(manifestUrl)
        when (u.scheme?.lowercase()) {
            "https" -> !u.host.isNullOrEmpty()
            "http" -> m.scheme.equals("http", true) && !u.host.isNullOrEmpty() && u.host.equals(m.host, true)
            else -> false
        }
    }.getOrDefault(false)

    /** what to do with a manifest envelope: its signature (the vendor's unless a test gives its own key), this app's entry, the versions */
    fun decide(envelope: String, pkg: String, installedCode: Long, skippedCode: Long = 0, manifestUrl: String = PresetPack.MANIFEST_URL,
               key: java.security.PublicKey? = null): Decision {
        val m = (if (key != null) PresetPack.verifiedManifest(envelope, key) else PresetPack.verifiedManifest(envelope)) ?: return Decision.Invalid("signature")
        val k = keyFor(pkg)
        val e = m.optJSONObject("files")?.optJSONObject(k) ?: return Decision.UpToDate
        val ePkg = e.optString("package", "")
        if (ePkg != pkg) return Decision.Invalid("package")
        val sha = e.optString("sha256", "")
        if (sha.length != 64 || !sha.all { it.isDigit() || it.lowercaseChar() in 'a'..'f' }) return Decision.Invalid("sha256")
        val url = e.optString("url", "")
        if (!urlAllowed(url, manifestUrl)) return Decision.Invalid("url")
        val code = e.optLong("versionCode", 0)
        if (code <= 0) return Decision.Invalid("version")
        val notes = m.optJSONArray("notes")?.let { a -> (0 until a.length()).map { a.optString(it) }.filter { it.isNotBlank() } } ?: emptyList()
        val o = Offer(k, ePkg, e.optString("versionName", m.optString("version")), e.optInt("build", 0), code, url, e.optLong("size", 0), sha.lowercase(), notes)
        return when {
            code <= installedCode -> Decision.UpToDate          // the same build or an older one: never a downgrade
            code == skippedCode -> Decision.Skipped(o)
            else -> Decision.Available(o)
        }
    }

    /** the downloaded file is kept only when its sha256 is the signed one; otherwise it is deleted */
    fun verifyDownload(f: File, o: Offer): Boolean {
        val ok = f.isFile && (o.size <= 0 || f.length() == o.size) && PresetPack.sha256(f.readBytes()).equals(o.sha256, true)
        if (!ok) f.delete()
        return ok
    }
    /** what the APK itself says (read by Android from the file): it must be this package and newer than the installed one */
    fun archiveAcceptable(archivePkg: String?, archiveCode: Long, pkg: String, installedCode: Long) = archivePkg == pkg && archiveCode > installedCode

    // ---- prefs (Settings: AUTO UPDATES) ----
    private fun p(c: Context) = Prefs.remote(c)
    fun autoCheck(c: Context) = p(c).getBoolean("updAuto", true)
    fun wifiOnly(c: Context) = p(c).getBoolean("updWifi", true)
    fun skipped(c: Context) = p(c).getLong("updSkip", 0)
    fun skip(c: Context, code: Long) { p(c).edit().putLong("updSkip", code).apply() }
    fun manifestUrl(c: Context) = p(c).getString("updUrl", null)?.takeIf { it.isNotBlank() } ?: PresetPack.MANIFEST_URL
    fun installedCode(c: Context): Long = runCatching { PackageInfoCompat.getLongVersionCode(c.packageManager.getPackageInfo(c.packageName, 0)) }.getOrDefault(0)
    /** at most once per 12 h on start */
    fun due(last: Long, now: Long) = last <= 0 || now - last >= CHECK_EVERY_MS || now < last

    fun onWifi(c: Context): Boolean = runCatching {
        val cm = c.getSystemService(ConnectivityManager::class.java); val n = cm.getNetworkCapabilities(cm.activeNetwork) ?: return false
        n.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) || n.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)
    }.getOrDefault(false)

    /** on app start: when AUTO-CHECK is on, at most once per 12 h, and (by default) only on Wi-Fi */
    fun checkOnStart(a: Activity) {
        if (Build.FINGERPRINT == "robolectric") return   // unit tests never go online
        if (!autoCheck(a) || !due(p(a).getLong("updLast", 0), System.currentTimeMillis())) return
        if (wifiOnly(a) && !onWifi(a)) return
        check(a, manual = false)
    }

    /** fetches and verifies the manifest off the main thread, then offers the update (or, when [manual], says why there is none) */
    fun check(a: Activity, manual: Boolean) {
        val url = manifestUrl(a); val pkg = a.packageName; val installed = installedCode(a); val skip = if (manual) 0 else skipped(a)
        Thread {
            val env = if (urlAllowed(url, url)) runCatching { get(url, 4L * 1024 * 1024) }.getOrNull() else null
            if (env != null) p(a).edit().putLong("updLast", System.currentTimeMillis()).apply()
            val d = if (env == null) Decision.Invalid("network") else decide(String(env, Charsets.UTF_8), pkg, installed, skip, url)
            a.runOnUiThread {
                if (a.isFinishing) return@runOnUiThread
                when (d) {
                    is Decision.Available -> offer(a, d.offer)
                    is Decision.Skipped -> if (manual) offer(a, d.offer)
                    Decision.UpToDate -> if (manual) toast(a, a.getString(R.string.upd_none))
                    is Decision.Invalid -> if (manual) toast(a, a.getString(if (d.why == "network") R.string.upd_offline else R.string.upd_invalid))
                }
            }
        }.start()
    }

    private fun toast(a: Activity, t: String) = Toast.makeText(a, t, Toast.LENGTH_LONG).show()
    private fun mb(b: Long) = if (b <= 0) "?" else "%.1f MB".format(b / 1048576.0)

    fun offer(a: Activity, o: Offer): AlertDialog {
        val msg = buildString {
            append(a.getString(R.string.upd_size, mb(o.size)))
            if (o.notes.isNotEmpty()) { append("\n\n"); o.notes.forEach { append("• ").append(it).append('\n') } }
        }
        return AlertDialog.Builder(a).setTitle(a.getString(R.string.upd_title, o.versionName.substringBefore(" ("), o.build)).setMessage(msg.trim())
            .setPositiveButton(R.string.upd_update) { _, _ -> download(a, o) }
            .setNegativeButton(R.string.upd_later, null)
            .setNeutralButton(R.string.upd_skip) { _, _ -> skip(a, o.versionCode) }
            .show()
    }

    private fun download(a: Activity, o: Offer) {
        if (!a.packageManager.canRequestPackageInstalls()) {   // "install unknown apps" is off for this app: explain, then open its page
            AlertDialog.Builder(a).setTitle(R.string.upd_perm_title).setMessage(R.string.upd_perm_note)
                .setPositiveButton(R.string.upd_open_settings) { _, _ -> runCatching { a.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + a.packageName))) } }
                .setNegativeButton(android.R.string.cancel, null).show()
            return
        }
        val bar = ProgressBar(a, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000; setPadding(48, 24, 48, 24) }
        val dlg = AlertDialog.Builder(a).setTitle(R.string.upd_downloading).setView(bar).setCancelable(false).show()
        Thread {
            val f = File(File(a.cacheDir, "updates").apply { mkdirs() }, "update.apk"); f.delete()
            val ok = runCatching { fetchTo(o.url, f, if (o.size > 0) o.size + 1 else MAX_APK) { n, t -> a.runOnUiThread { bar.progress = if (t > 0) (n * 1000 / t).toInt() else 0 } } }.isSuccess && verifyDownload(f, o)
            val arch = if (ok) runCatching { a.packageManager.getPackageArchiveInfo(f.path, 0) }.getOrNull() else null
            val good = ok && archiveAcceptable(arch?.packageName, arch?.let { PackageInfoCompat.getLongVersionCode(it) } ?: 0, a.packageName, installedCode(a))
            if (ok && !good) f.delete()
            a.runOnUiThread {
                runCatching { dlg.dismiss() }
                if (!good) toast(a, a.getString(if (ok) R.string.upd_wrong_apk else R.string.upd_bad_download)) else install(a, f)
            }
        }.start()
    }

    /** a PackageInstaller session; Android shows its own confirmation (InstallResult starts it) */
    private fun install(a: Activity, f: File) = runCatching {
        val pi = a.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply { setAppPackageName(a.packageName) }
        val id = pi.createSession(params)
        pi.openSession(id).use { s ->
            s.openWrite("update.apk", 0, f.length()).use { out -> f.inputStream().use { it.copyTo(out) }; s.fsync(out) }
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or (if (Build.VERSION.SDK_INT >= 31) PendingIntent.FLAG_MUTABLE else 0)
            val cb = PendingIntent.getBroadcast(a, id, Intent(a, InstallResult::class.java).setPackage(a.packageName), flags)
            s.commit(cb.intentSender)
        }
    }.onFailure { toast(a, a.getString(R.string.upd_install_failed)) }

    private fun get(u: String, cap: Long): ByteArray? { val h = URL(u).openConnection() as HttpURLConnection
        return try { h.connectTimeout = 8000; h.readTimeout = 15000; h.instanceFollowRedirects = true
            if (h.responseCode != 200) null else h.inputStream.use { it.readBytes() }.takeIf { it.size < cap } } finally { h.disconnect() } }

    private fun fetchTo(u: String, f: File, cap: Long, progress: (Long, Long) -> Unit) {
        val h = URL(u).openConnection() as HttpURLConnection
        try {
            h.connectTimeout = 10000; h.readTimeout = 30000; h.instanceFollowRedirects = true   // HttpURLConnection never follows https -> http
            if (h.responseCode != 200) error("http " + h.responseCode)
            val total = h.contentLengthLong; var n = 0L; val buf = ByteArray(64 * 1024)
            h.inputStream.use { inp -> f.outputStream().use { out -> while (true) { val r = inp.read(buf); if (r < 0) break; n += r; if (n > cap) error("too big"); out.write(buf, 0, r); progress(n, total) } } }
        } finally { h.disconnect() }
    }

    /** the installer's answer: the confirmation screen to show, or the outcome */
    class InstallResult : BroadcastReceiver() {
        override fun onReceive(c: Context, i: Intent) {
            when (i.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)) {
                PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                    @Suppress("DEPRECATION") val confirm = (if (Build.VERSION.SDK_INT >= 33) i.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java) else i.getParcelableExtra(Intent.EXTRA_INTENT)) ?: return
                    c.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                }
                PackageInstaller.STATUS_SUCCESS -> Unit
                else -> Toast.makeText(c, c.getString(R.string.upd_install_failed), Toast.LENGTH_LONG).show()
            }
        }
    }
}
