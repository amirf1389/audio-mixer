package com.amfb.amixos

import android.annotation.SuppressLint
import android.content.ClipboardManager
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.view.View
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/** PLAYERS: the official embedded player of YouTube, Spotify, TIDAL or Deezer, beside the mixer. A pasted link is opened in the
 *  service's own player page; the song goes to the phone's output as the service plays it. It is never routed into the mixer,
 *  processed or recorded, and the page cannot leave the service's own addresses. */
class StreamPlayerActivity : FitActivity() {
    private var service = StreamLinks.Service.YOUTUBE
    private lateinit var chips: LinearLayout; private lateinit var field: EditText; private lateinit var status: TextView; private var web: WebView? = null
    private lateinit var holder: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        service = runCatching { StreamLinks.Service.valueOf(Prefs.local(this).getString("playerService", "") ?: "") }.getOrDefault(StreamLinks.Service.YOUTUBE)
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(36, 36, 36, 24) }
        box.addView(TextView(this).apply { text = getString(R.string.players_title); setTextColor(getColor(R.color.accent)); textSize = 14f; typeface = Typeface.DEFAULT_BOLD })
        box.addView(TextView(this).apply { text = getString(R.string.players_intro); setTextColor(getColor(R.color.dim)); textSize = 13f; setPadding(0, 6, 0, 20) })
        chips = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; contentDescription = "playerServices" }; box.addView(chips)
        field = EditText(this).apply { setSingleLine(); textSize = 14f; setTextColor(getColor(R.color.md_on_surface)); contentDescription = "playerLink"
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_URI }
        box.addView(field)
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row.addView(Button(this).apply { text = getString(R.string.players_paste); contentDescription = "playerPaste"; setOnClickListener {
            val clip = (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).primaryClip; val t = clip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(this@StreamPlayerActivity)?.toString().orEmpty()
            if (t.isNotBlank()) { field.setText(t.trim()); open(t) } } }, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        row.addView(Button(this).apply { text = getString(R.string.players_open); contentDescription = "playerOpen"; setOnClickListener { open(field.text.toString()) } }, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        box.addView(row)
        status = TextView(this).apply { setTextColor(getColor(R.color.cyan)); textSize = 12f; setPadding(0, 10, 0, 10); contentDescription = "playerStatus" }; box.addView(status)
        holder = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.card_bg); setPadding(8, 8, 8, 8) }
        box.addView(holder, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        box.addView(TextView(this).apply { text = getString(R.string.players_note); setTextColor(getColor(R.color.dim)); textSize = 11f; setPadding(0, 16, 0, 0); contentDescription = "playerNote" })
        setContentView(Nav.wrap(this, box, Nav.Tab.PLAYERS)); paintChips()
        Prefs.local(this).getString("playerLink_" + service.name, "")?.takeIf { it.isNotBlank() }?.let { field.setText(it) }
    }
    private fun paintChips() {
        chips.removeAllViews(); field.hint = getString(R.string.players_hint, service.title)
        for (s in StreamLinks.Service.values()) chips.addView(PadButton(this, s.title, getColor(R.color.accent), 12f).apply { on = s == service; tag = "player_" + s.name
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { setMargins(4, 0, 4, 12) }
            setOnClickListener { if (s != service) { service = s; Prefs.local(this@StreamPlayerActivity).edit().putString("playerService", s.name).apply(); status.text = ""
                field.setText(Prefs.local(this@StreamPlayerActivity).getString("playerLink_" + s.name, "")); closePlayer(); paintChips() } } })
    }
    companion object {
        /** THE OFFICIAL PLAYER in a WebView, for the PLAYERS tab and for the mini player above the mixer's faders. The page stays on the
         *  service's own addresses; nothing starts by itself; the mixer's PLAYERS level is applied once the player is up. Only one player
         *  plays at a time: opening one closes the other (onClose). */
        @SuppressLint("SetJavaScriptEnabled")
        fun buildPlayer(a: android.app.Activity, e: StreamLinks.Embed, onClose: () -> Unit): WebView {
            val w = WebView(a).apply { setBackgroundColor(Color.TRANSPARENT); contentDescription = "playerWeb"
                settings.javaScriptEnabled = true; settings.domStorageEnabled = true; settings.mediaPlaybackRequiresUserGesture = true
                settings.allowFileAccess = false; settings.allowContentAccess = false; settings.setSupportMultipleWindows(false); settings.javaScriptCanOpenWindowsAutomatically = false
                webViewClient = object : WebViewClient() {
                    override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean = !StreamLinks.allowed(e.service, request.url.toString()) } }
            PlayersBus.attach(w, e.service, onClose)
            for (ms in longArrayOf(1500, 4000, 9000)) w.postDelayed({ PlayersBus.apply(a) }, ms)
            // the player sits in a frame on a page of this app's own name: the services ask an embedding app to say who it is
            val html = "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"referrer\" content=\"strict-origin-when-cross-origin\">" +
                "<style>html,body{margin:0;height:100%;background:#000}iframe{border:0;width:100%;height:100%}</style></head><body>" +
                "<iframe src=\"" + frameUrl(e, a.packageName) + "\" allow=\"autoplay; encrypted-media; clipboard-write; fullscreen; picture-in-picture\" allowfullscreen></iframe></body></html>"
            runCatching { w.loadDataWithBaseURL("https://" + a.packageName + "/", html, "text/html", "utf-8", null) }
            return w
        }
        /** the link the PLAYERS tab opened last (for its chosen service), as an embed; null when there is none */
        fun lastEmbed(c: android.content.Context): StreamLinks.Embed? { val p = Prefs.local(c); val svc = p.getString("playerService", "YOUTUBE") ?: "YOUTUBE"
            return StreamLinks.parse(p.getString("playerLink_$svc", "") ?: "") }
        /** the address the frame loads: YouTube's player is asked to accept its documented volume messages from this app's page */
        fun frameUrl(e: StreamLinks.Embed, pkg: String): String = if (e.service != StreamLinks.Service.YOUTUBE) e.url
            else e.url + (if (e.url.contains("?")) "&" else "?") + "enablejsapi=1&origin=https://" + pkg
    }
    /** what OPEN does with a link; the embed address that was loaded (null: not a link of the four services) */
    fun open(link: String): String? {
        val e = StreamLinks.parse(link)
        if (e == null) { status.text = getString(R.string.players_bad, service.title); return null }
        if (e.service != service) { service = e.service; Prefs.local(this).edit().putString("playerService", service.name).apply(); paintChips(); status.text = getString(R.string.players_other, service.title) } else status.text = ""
        Prefs.local(this).edit().putString("playerLink_" + service.name, link.trim()).apply()
        runCatching { load(e) }   // a phone without a system WebView: the link is kept, the message below stays
        return e.url
    }
    private fun load(e: StreamLinks.Embed) {
        closePlayer()
        val w = buildPlayer(this, e) { closePlayer() }
        web = w; holder.addView(w, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.MATCH_PARENT))
    }
    private fun closePlayer() { web?.let { PlayersBus.detach(it) }; web?.let { holder.removeView(it); it.stopLoading(); it.loadUrl("about:blank"); it.destroy() }; web = null }
    // The player keeps playing while another tab of this app is open (so the PLAYERS strip on the mixer has something to control);
    // it is paused when the whole app leaves the screen (PlayersBus counts the app's screens).
    override fun onDestroy() { closePlayer(); super.onDestroy() }
}

/**
 * THE PLAYERS STRIP'S LINK TO THE PLAYER. The mixer's PLAYERS channel sets the volume and the mute of the official player; the
 * streamed sound itself never enters the mixer (no effects, no recording). Volume uses the service's own player API where it has
 * one (YouTube's); mute uses Android's WebView mute, for every service.
 */
object PlayersBus {
    private var web: java.lang.ref.WeakReference<WebView>? = null
    var service: StreamLinks.Service? = null; private set
    private var screens = 0
    val loaded: Boolean get() = web?.get() != null
    /** only YouTube's player takes a volume from outside; the others have their own volume control inside the player */
    fun volumeWorks(s: StreamLinks.Service? = service): Boolean = s == StreamLinks.Service.YOUTUBE
    private var closer: (() -> Unit)? = null
    /** a new player: the one that played before is closed first (only one plays at a time) */
    fun attach(w: WebView?, s: StreamLinks.Service?, onClose: (() -> Unit)? = null) {
        val prev = web?.get(); val c = closer
        web = w?.let { java.lang.ref.WeakReference(it) }; service = if (w == null) null else s; closer = if (w == null) null else onClose
        if (prev != null && prev !== w) runCatching { c?.invoke() } }
    fun clear() { web = null; service = null; closer = null; screens = 0 }   // the tests start from nothing
    fun detach(w: WebView) { if (web?.get() === w) { web = null; service = null; closer = null } }
    /** the player's volume (0..100) for a fader level in dB: 0 dB and above is full, the bottom of the fader is silent */
    fun percentFor(db: Double): Int = if (db <= -60.0) 0 else Math.round(100 * Math.pow(10.0, minOf(0.0, db) / 20.0)).toInt().coerceIn(0, 100)
    /** what is sent to YouTube's player (its documented IFrame API messages: listening, setVolume) */
    fun volumeScript(percent: Int): String = "(function(){var f=document.querySelector('iframe');if(!f||!f.contentWindow)return;" +
        "var s=function(o){f.contentWindow.postMessage(JSON.stringify(o),'*')};s({event:'listening',id:1});s({event:'command',func:'setVolume',args:[" + percent.coerceIn(0, 100) + "]})})()"
    fun apply(c: android.content.Context) {
        val w = web?.get() ?: return; val p = Prefs.local(c)
        if (volumeWorks()) runCatching { w.evaluateJavascript(volumeScript(percentFor(p.getFloat("playersDb", 0f).toDouble())), null) }
        runCatching { if (androidx.webkit.WebViewFeature.isFeatureSupported(androidx.webkit.WebViewFeature.MUTE_AUDIO)) androidx.webkit.WebViewCompat.setAudioMuted(w, p.getBoolean("playersMute", false)) }
    }
    fun muteWorks(): Boolean = runCatching { androidx.webkit.WebViewFeature.isFeatureSupported(androidx.webkit.WebViewFeature.MUTE_AUDIO) }.getOrDefault(false)
    fun screenStarted() { if (screens++ == 0) runCatching { web?.get()?.onResume() } }
    fun screenStopped() { if (--screens <= 0) { screens = 0; runCatching { web?.get()?.onPause() } } }
}
