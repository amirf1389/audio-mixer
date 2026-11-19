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
class StreamPlayerActivity : AppCompatActivity() {
    private var service = StreamLinks.Service.YOUTUBE
    private lateinit var chips: LinearLayout; private lateinit var field: EditText; private lateinit var status: TextView; private var web: WebView? = null
    private lateinit var holder: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        service = runCatching { StreamLinks.Service.valueOf(Prefs.local(this).getString("playerService", "") ?: "") }.getOrDefault(StreamLinks.Service.YOUTUBE)
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(36, 36, 36, 24) }
        box.addView(TextView(this).apply { text = getString(R.string.players_title); setTextColor(getColor(R.color.accent)); textSize = 22f; typeface = Typeface.DEFAULT_BOLD })
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
    /** what OPEN does with a link; the embed address that was loaded (null: not a link of the four services) */
    fun open(link: String): String? {
        val e = StreamLinks.parse(link)
        if (e == null) { status.text = getString(R.string.players_bad, service.title); return null }
        if (e.service != service) { service = e.service; Prefs.local(this).edit().putString("playerService", service.name).apply(); paintChips(); status.text = getString(R.string.players_other, service.title) } else status.text = ""
        Prefs.local(this).edit().putString("playerLink_" + service.name, link.trim()).apply()
        runCatching { load(e) }   // a phone without a system WebView: the link is kept, the message below stays
        return e.url
    }
    @SuppressLint("SetJavaScriptEnabled")
    private fun load(e: StreamLinks.Embed) {
        closePlayer()
        val w = WebView(this).apply { setBackgroundColor(Color.TRANSPARENT)
            settings.javaScriptEnabled = true; settings.domStorageEnabled = true; settings.mediaPlaybackRequiresUserGesture = true   // the players need scripts; nothing starts by itself
            settings.allowFileAccess = false; settings.allowContentAccess = false; settings.setSupportMultipleWindows(false); settings.javaScriptCanOpenWindowsAutomatically = false
            webViewClient = object : WebViewClient() {   // the page stays on the service's own addresses
                override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean = !StreamLinks.allowed(e.service, request.url.toString()) } }
        web = w; holder.addView(w, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.MATCH_PARENT))
        // the player sits in a frame on a page of this app's own name: the services ask an embedding app to say who it is
        val html = "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"referrer\" content=\"strict-origin-when-cross-origin\">" +
            "<style>html,body{margin:0;height:100%;background:#1D2024}iframe{border:0;width:100%;height:100%}</style></head><body>" +
            "<iframe src=\"" + e.url + "\" allow=\"autoplay; encrypted-media; clipboard-write; fullscreen; picture-in-picture\" allowfullscreen></iframe></body></html>"
        w.loadDataWithBaseURL("https://" + packageName + "/", html, "text/html", "utf-8", null)
    }
    private fun closePlayer() { web?.let { holder.removeView(it); it.stopLoading(); it.loadUrl("about:blank"); it.destroy() }; web = null }
    override fun onPause() { super.onPause(); web?.onPause() }
    override fun onResume() { super.onResume(); web?.onResume() }
    override fun onDestroy() { closePlayer(); super.onDestroy() }
}
