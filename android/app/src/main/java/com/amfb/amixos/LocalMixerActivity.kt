package com.amfb.amixos

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.amfb.amixos.remote.MiniJson

/** THIS PHONE: the AmF X1 mixer running on the phone itself.
 *  INPUT: any input Android offers (built-in mic, headset, USB interface ...), MONO on CH 1 or STEREO on CH 1-2 (inputs 1 and 2 of a USB
 *  interface, or a stereo mic). OUTPUT: speaker, headphones, USB, Bluetooth ... Each channel: fader, pan, mute and an INSERT RACK of
 *  AmF X1 effects (the console's rack effects; desktop VST / VST3 plug-ins are Windows / macOS / Linux binaries and cannot load on Android);
 *  MASTER: fader, limiter and its own rack. What is chosen is kept for next time. Rate and buffer come from the phone's audio HAL. */
class LocalMixerActivity : FitActivity() {
    companion object {
        /** A 10-CHANNEL DIGITAL MIXER: CH 1 .. CH 6 (the inputs: mic, stereo mic or the first six inputs of an interface) and DECK A .. D.
         *  The engine's strips are 0-5 the inputs, then the decks as stereo pairs (6-7, 8-9, 10-11, 12-13): 14 strips (the engine takes 32,
         *  the multitrack ring 16). Every channel has a fader, MUTE, a 3-band EQ ([ChannelEq]) and its own FX rack; the master has the
         *  limiter and a rack. (Before layout 2 there were 2 inputs and the decks sat on strips 2-9: [migrate] moves old saves.) */
        const val INPUTS = 6
        const val DECKS = 4
        const val STRIPS = INPUTS + 2 * DECKS
        const val CHANNELS = INPUTS + DECKS
        /** what the input feeds: CH 1 (mono), CH 1-2 (stereo) or CH 1-6 (a multi-channel interface) */
        val INPUT_MODES = listOf(1, 2, INPUTS)
        const val LAYOUT = 2
        /** OLD SAVES: the layout-1 decks were on strips 2 + 2d; their racks, knob values and chains move to the new deck strips. Once. */
        fun migrate(p: android.content.SharedPreferences) {
            if (p.getInt("layout", 1) >= LAYOUT) return
            val all = p.all; val e = p.edit(); val moved = ArrayList<Pair<String, String>>()
            for (d in 0 until DECKS) { val old = 2 + 2 * d; val now = deckStrip(d)
                for ((o, n) in listOf(rackKey(old) to rackKey(now), "chain$old" to "chain$now") + (0 until Engine.MAX_INSERTS).map { paramKey(old, it) to paramKey(now, it) })
                    (all[o] as? String)?.let { v -> e.remove(o); moved += n to v } }
            for ((n, v) in moved) e.putString(n, v)   // after every remove: a moved key may land where an old one was
            e.putInt("layout", LAYOUT).commit()
        }
        /** the engine strips of a channel (input s = s, deck d = 100 + d) */
        /** what the engine gets for a channel (input s = s, deck d = 100 + d): its fader with its group's offset (dB) and MUTE (own or group) */
        fun channelLevel(p: android.content.SharedPreferences, ch: Int): Pair<Double, Boolean> =
            if (ch >= 100) Dca.effectiveDb(p, ch, p.getFloat("deckDb${ch - 100}", 0f).toDouble()) to (Dca.effectiveMute(p, ch, p.getBoolean("deckMute${ch - 100}", false)) || soloCut(p, ch))
            else Dca.effectiveDb(p, ch, p.getFloat("db$ch", 0f).toDouble()) to (Dca.effectiveMute(p, ch, p.getBoolean("mute$ch", false)) || soloCut(p, ch))
        /** SOLO: while any channel is soloed, the channels that are not are cut from the master */
        fun soloKey(ch: Int) = if (ch >= 100) "deckSolo${ch - 100}" else "solo$ch"
        fun allChannels(): List<Int> = (0 until INPUTS).toList() + (0 until DECKS).map { 100 + it }
        fun anySolo(p: android.content.SharedPreferences) = allChannels().any { p.getBoolean(soloKey(it), false) }
        fun soloCut(p: android.content.SharedPreferences, ch: Int) = anySolo(p) && !p.getBoolean(soloKey(ch), false)
        /** the master the engine gets: the fader (+ the loudness rider), nothing while MASTER MUTE is on */
        fun masterOut(p: android.content.SharedPreferences, riderDb: Double): Double { val user = p.getFloat("masterDb", 0f).toDouble(); return if (p.getBoolean("masterMute", false)) -120.0 else if (user <= -60) user else user + riderDb }
        fun chStrips(ch: Int): List<Int> = if (ch >= 100) deckStrip(ch - 100).let { listOf(it, it + 1) } else listOf(ch)
        fun deckName(d: Int) = ('A' + d).toString()
        /** the engine strips one rack works on: a deck's rack is put on its left and its right strip alike */
        fun mates(s: Int): List<Int> = if (s >= INPUTS && s < STRIPS && (s - INPUTS) % 2 == 0) listOf(s, s + 1) else listOf(s)
        fun deckStrip(d: Int) = INPUTS + 2 * d
        /** starting the sound: (ask for the microphone, open the microphone). The output always starts, so the decks play without a microphone */
        fun audioPlan(inputWanted: Boolean, micGranted: Boolean): Pair<Boolean, Boolean> = Pair(inputWanted && !micGranted, inputWanted && micGranted)
        /** the tracks of a multitrack recording: (the strip numbers in it, the file name part); inputs mono, decks stereo */
        fun trackPlan(inChannels: Int): List<Pair<IntArray, String>> =
            (0 until inChannels).map { intArrayOf(it) to "ch${it + 1}" } + (0 until DECKS).map { d -> intArrayOf(deckStrip(d), deckStrip(d) + 1) to "deck${deckName(d)}" }
        /** the rack of a strip (-1 = master) as kept in prefs: one entry per slot, "kind" or "" */
        fun rackKey(strip: Int) = if (strip < 0) "rackMaster" else "rack$strip"
        fun paramKey(strip: Int, slot: Int) = "fx_${if (strip < 0) "m" else strip.toString()}_$slot"
    }
    private val hal by lazy { AudioHal.read(this) }
    private val rate by lazy { hal.nativeRate.takeIf { it in 8000..192000 } ?: 48000 }
    private val frames by lazy { hal.nativeFrames.takeIf { it in 32..2048 } ?: 256 }
    private val bursts by lazy { BrandProfile.of(hal).bursts }
    /** the native engine; null when it cannot load (the JVM tests): the screen is still drawn */
    private val engine: Engine? by lazy { runCatching { Engine(STRIPS, rate.toDouble()) }.getOrNull()?.also { applyAll(it) } }
    private var loop: AudioLoop? = null
    /** the mixer's real rate: the native stream may run at the device's own rate */
    private fun mixRate(): Double { val st = if (loop?.path == "aaudio") runCatching { engine?.nativeState() }.getOrNull() else null; return (st?.getOrNull(8)?.takeIf { it > 0 } ?: rate).toDouble() }
    /** THE FOUR DECKS: a song file each, on a stereo pair of strips ([deckStrip]) */
    private val decks: List<DeckPlayer> by lazy { engine?.let { e -> (0 until DECKS).map { d -> DeckPlayer(this, e, d, deckStrip(d), ::mixRate).also { it.listener = object : DeckPlayer.Listener { override fun changed(deck: DeckPlayer) { ui.post { showDecks() } } } } } } ?: emptyList() }
    private var pickFor = 0
    private val pickFile = registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.OpenDocument()) { u -> if (u != null) loadDeck(pickFor, u, true) }
    private val deckViews = HashMap<Int, DeckViews>()
    private class DeckViews(val title: TextView, val time: TextView, val seek: SeekBar, val play: PadButton, val loop: PadButton, val video: PadButton)
    // THE PICTURE of a video on a deck: one pane above the desk, for the deck whose VIDEO button is on (-1: hidden)
    private var videoDeck = -1; private var videoPane: android.widget.FrameLayout? = null; private var deckVideo: DeckVideo? = null
    private fun showVideo(d: Int) {
        val pane = videoPane ?: return; val k = decks.getOrNull(d); val u = k?.source
        if (d < 0 || k == null || u == null || !k.hasVideo) { videoDeck = -1; deckVideo?.close(); pane.visibility = View.GONE; showDecks(); return }
        videoDeck = d; pane.visibility = View.VISIBLE
        val w = resources.displayMetrics.widthPixels - 64; pane.layoutParams = pane.layoutParams.apply { height = minOf(DeckVideo.heightFor(w, 0, 0), resources.displayMetrics.heightPixels / 3) }
        deckVideo?.apply { onSize = { vw, vh -> pane.layoutParams = pane.layoutParams.apply { height = minOf(DeckVideo.heightFor(w, vw, vh), resources.displayMetrics.heightPixels / 3) }; pane.requestLayout() }; show(u) }
        showDecks()
    }
    // MASTER RECORDER: the stereo master to a file (FLAC 24-bit, the recorder's folder)
    // MULTITRACK: with it on, REC also writes every channel to its own file (inputs mono, decks stereo), beside the master
    @Volatile private var multiWriters: List<Pair<IntArray, HiRes.Writer>> = emptyList(); private var multiThread: Thread? = null
        @Volatile private var recWriter: HiRes.Writer? = null; private var recThread: Thread? = null; private var recFile: java.io.File? = null; private lateinit var recBtn: PadButton
    private val ui = Handler(Looper.getMainLooper())
    private val accent by lazy { getColor(R.color.accent) }; private val cyan by lazy { getColor(R.color.cyan) }; private val dim by lazy { getColor(R.color.dim) }
    private lateinit var body: LinearLayout; private lateinit var status: TextView; private lateinit var startBtn: PadButton
    private val meters = HashMap<String, ProgressBar>(); private val fxBtns = HashMap<Int, PadButton>()
    private class IoViews(val lamp: View, val name: TextView, val state: TextView, val meter: ProgressBar)
    private val io = HashMap<Boolean, IoViews>()   // false = INPUT, true = OUTPUT
    private fun prefs() = Prefs.local(this)
    // STRIP ZOOM: the − / + keys beside the sections show more, narrower channels or fewer, wider ones (3 to 8 across)
    // COMPACT (the AmF X1 app, not the AmF X1 Mixer app): five channels across upright, eight on its side, 3 to 10 with the zoom keys,
    // and the strip keys 40 dp tall with text that shrinks to fit
    private val compact by lazy { !Nav.standalone(this) }
    private fun baseAcross(land: Boolean) = if (compact) RemoteActivity.compactAcrossFor(land) else RemoteActivity.acrossFor(land)
    private fun maxAcross() = if (compact) 10 else 8
    private fun across(land: Boolean) = (MixerFit.across(resources.displayMetrics.widthPixels, resources.displayMetrics.density, land, baseAcross(land)) + prefs().getInt("stripZoom", 0)).coerceIn(3, if (land) 12 else maxAcross())
    private fun stripZoom(d: Int) { val l = resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE
        val now = across(l); val next = (now + d).coerceIn(3, maxAcross()); if (next == now) return
        prefs().edit().putInt("stripZoom", next - baseAcross(l)).apply(); render() }
    /** the master's width: as before on a big screen, never more than about a fifth of a small one */
    private fun masterW(land: Boolean) = minOf(if (land) 340 else 252, (resources.displayMetrics.widthPixels * 0.22).toInt())
    private fun stripWidth(screenPx: Int, n: Int) = if (compact) RemoteActivity.compactStripWidthFor(screenPx, n, resources.displayMetrics.density)
        else maxOf((60 * resources.displayMetrics.density).toInt(), (screenPx - 40 - (masterW(land) + 8)) / maxOf(1, n) - 8)   // every channel finger-wide, as many as asked across
    /** a strip key's height and look: compact in the AmF X1 app, as before in the AmF X1 Mixer app */
    private fun keyH(h: Int = 84) = if (compact) ConsoleUi.dp(this, ConsoleUi.KEY_DP) else h
    private fun <T : android.widget.TextView> key(v: T): T = if (compact) ConsoleUi.compact(v, ConsoleUi.KEY_DP, 11).apply { setPadding(4, 0, 4, 0) } else v
    // LANDSCAPE: the screen is short, so everything above a fader is made small: no sub-title, no pan knob, FX and MUTE side by side
    private var land = false
    private fun pairKeys(col: LinearLayout) { if (!land || col.childCount < 2) return
        // the keys at the bottom of the strip (FX, EQ, MUTE: up to three) side by side in one row
        val ks = ArrayList<View>(); while (ks.size < 3 && col.childCount > 0 && col.getChildAt(col.childCount - 1) is PadButton) { ks.add(0, col.getChildAt(col.childCount - 1)); col.removeViewAt(col.childCount - 1) }
        if (ks.isEmpty()) return
        col.addView(LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL
            for (k in ks) { if (ks.size > 2 && k is android.widget.TextView) k.setPadding(2, 0, 2, 0); addView(k, LinearLayout.LayoutParams(0, keyH(76), 1f).apply { setMargins(2, 4, 2, 4) }) } }) }
    private fun inChannels() = prefs().getInt("inChannels", 1).takeIf { it in INPUT_MODES } ?: 1

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (savedInstanceState == null) AppUpdater.checkOnStart(this)   // AUTO UPDATES (at most once per 12 h)
        migrate(prefs())   // old saves (2 inputs, the decks on strips 2-9) before anything reads them
        runCatching { Engine.setDirs(filesDir.absolutePath, cacheDir.absolutePath, cacheDir.absolutePath) }
        body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(20, 20, 20, 16); setBackgroundColor(Color.parseColor("#0B0F15")) }
        setContentView(Nav.wrap(this, body, Nav.Tab.MIXER))   // the MIXER tab, THIS PHONE mode
        render(); QuickMaster.listener = quickHook
        ui.post(object : Runnable { override fun run() { showMeters(); loudTick(0.05); ui.postDelayed(this, 50) } })
    }

    /** builds the whole screen from the saved choices (again after a change of input channels or devices): a header with the INPUT and
     *  OUTPUT keys, then the desk: one strip per channel and the MASTER strip on the right, like the console's own mixer */
    fun render() {
        miniWeb?.let { (it.parent as? android.view.ViewGroup)?.removeView(it) }; miniPane = null   // the player keeps playing through a re-layout (a turn of the phone)
        body.removeAllViews(); meters.clear(); fxBtns.clear(); faders.clear()
        val modeBar = Nav.modeBar(this, true)   // DUAL MODE: THIS PHONE | CONSOLE (upright: on top; on its side: in SETUP, so the faders get the height)
        val head = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        head.addView(TextView(this).apply { text = getString(R.string.home_phone); setTextColor(accent); textSize = 14f; setTypeface(null, Typeface.BOLD); layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) })
        startBtn = PadButton(this, getString(if (loop == null) R.string.start else R.string.stop), ConsoleUi.GREEN, 14f).apply { on = loop != null; layoutParams = LinearLayout.LayoutParams(300, 96); setOnClickListener { if (loop == null) startAudio() else stopAudio() } }
        recBtn = PadButton(this, getString(if (recWriter != null) R.string.lm_rec_stop else R.string.lm_rec), ConsoleUi.RED, 14f).apply { on = recWriter != null; layoutParams = LinearLayout.LayoutParams(220, 96).apply { setMargins(0, 0, 12, 0) }; setOnClickListener { toggleRec() } }
        // THREE SECTIONS, one at a time, so the faders get the whole screen: FADERS (the desk), MEDIA (the decks and the picture of a video)
        // and SETUP (input, output, channels, quick chains). In landscape the section keys share the top row with REC and START.
        land = resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE
        val secKeys = LinkedHashMap<String, PadButton>()
        val secBar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; tag = "sectionBar" }
        for ((id, label) in listOf("faders" to R.string.lm_sec_faders, "media" to R.string.lm_sec_media, "tools" to R.string.lm_sec_tools, "setup" to R.string.lm_sec_setup))
            secKeys[id] = PadButton(this, getString(label), cyan, 12f).apply { contentDescription = "sec_$id"; layoutParams = LinearLayout.LayoutParams(0, if (land) 96 else 84, 1f).apply { setMargins(0, 0, 8, 0) } }.also { secBar.addView(it) }
        for ((tag, glyph, d) in listOf(Triple("zoomOut", "−", 1), Triple("zoomIn", "+", -1)))   // − : more channels (smaller), + : fewer (bigger)
            secBar.addView(PadButton(this, glyph, cyan, 16f).apply { contentDescription = tag; layoutParams = LinearLayout.LayoutParams(if (land) 96 else 84, if (land) 96 else 84).apply { setMargins(0, 0, 8, 0) }; setOnClickListener { stripZoom(d) } })
        val setupBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; val mediaBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        status = TextView(this).apply { setTextColor(dim); textSize = 11f; typeface = Typeface.MONOSPACE; setPadding(0, 2, 0, 10) }
        if (land) { setupBox.addView(modeBar); body.setPadding(16, 8, 16, 6); head.removeViewAt(0); head.addView(secBar.apply { layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) }, 0); head.addView(recBtn); head.addView(startBtn); head.setPadding(0, 0, 0, 8); body.addView(head); setupBox.addView(status) }
        else { body.addView(modeBar); body.setPadding(20, 20, 20, 16); head.addView(recBtn); head.addView(startBtn); body.addView(head); body.addView(status); body.addView(secBar.apply { setPadding(0, 0, 0, 10) }) }
        showStatus()
        // ---- INPUT / OUTPUT: two keys that open the device lists, and MONO / STEREO
        // each row: a LAMP (stopped / not open / no signal / live / clipping), the device, what it is doing and a level bar under it
        fun ioRow(tag: String, name: String, color: Int, out: Boolean, click: () -> Unit): LinearLayout {
            val lamp = View(this).apply { background = android.graphics.drawable.GradientDrawable().apply { shape = android.graphics.drawable.GradientDrawable.OVAL; setColor(IoLamp.color(IoLamp.State.STOPPED)) }
                layoutParams = LinearLayout.LayoutParams(22, 22).apply { setMargins(0, 0, 16, 0) } }
            val nameV = TextView(this).apply { text = name; setTextColor(Color.WHITE); textSize = 13f; maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END }
            val stateV = TextView(this).apply { setTextColor(dim); textSize = 10f; typeface = Typeface.MONOSPACE; maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END }
            val meter = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000; progress = 0; progressTintList = android.content.res.ColorStateList.valueOf(color)
                progressBackgroundTintList = android.content.res.ColorStateList.valueOf(0xFF0B0F15.toInt()); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 10).apply { setMargins(0, 10, 0, 0) } }
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
                addView(lamp)
                addView(TextView(this@LocalMixerActivity).apply { text = tag; setTextColor(color); textSize = 10f; letterSpacing = 0.15f; typeface = Typeface.DEFAULT_BOLD; layoutParams = LinearLayout.LayoutParams(170, LinearLayout.LayoutParams.WRAP_CONTENT) })
                addView(LinearLayout(this@LocalMixerActivity).apply { orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f); addView(nameV); addView(stateV) })
                addView(TextView(this@LocalMixerActivity).apply { text = "▾"; setTextColor(dim); textSize = 14f }) }
            io[out] = IoViews(lamp, nameV, stateV, meter)
            return LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; isClickable = true; setOnClickListener { click() }
                background = android.graphics.drawable.GradientDrawable().apply { setColor(0xFF1D2024.toInt()); cornerRadius = 16f; setStroke(2, 0xFF42474E.toInt()) }; setPadding(22, 14, 22, 14)
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 0, 0, 8) }
                addView(row); addView(meter); contentDescription = tag }
        }
        setupBox.addView(ioRow(getString(R.string.lm_input), deviceName(Prefs.localInDevice(this), false), cyan, false) { pickDevice(false) })
        setupBox.addView(ioRow(getString(R.string.lm_output), deviceName(Prefs.localOutDevice(this), true), accent, true) { pickDevice(true) })
        showIo()
        val chRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 10) }
        listOf(1 to getString(R.string.lm_mono), 2 to getString(R.string.lm_stereo), INPUTS to getString(R.string.lm_multi_in)).forEach { (n, t) ->
            chRow.addView(key(PadButton(this, t, cyan, 11f)).apply { on = inChannels() == n; contentDescription = "inMode_$n"; layoutParams = LinearLayout.LayoutParams(0, keyH(80), 1f).apply { setMargins(0, 0, if (n != INPUTS) 8 else 0, 0) }; setOnClickListener { setInChannels(n) } }) }
        // INPUT ON / OFF: OFF opens no microphone at all (the phone records nothing); the lamp then says OFF, not "open"
        val inRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 10) }
        listOf(true to getString(R.string.lm_input_on), false to getString(R.string.lm_input_off)).forEach { (v, t) ->
            inRow.addView(PadButton(this, t, if (v) cyan else ConsoleUi.RED, 11f).apply { on = Prefs.localInputOn(this@LocalMixerActivity) == v; layoutParams = LinearLayout.LayoutParams(0, 80, 1f).apply { setMargins(0, 0, if (v) 8 else 0, 0) }
                setOnClickListener { if (Prefs.localInputOn(this@LocalMixerActivity) != v) { Prefs.setLocalInputOn(this@LocalMixerActivity, v); restartIfRunning(); render() } } }) }
        setupBox.addView(chRow); setupBox.addView(inRow)
        val mt = PadButton(this, "", ConsoleUi.RED, 12f).apply { contentDescription = "multitrack"; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 80).apply { setMargins(0, 0, 0, 10) } }
        fun mtLabel() { val on = prefs().getBoolean("multitrack", false); mt.on = on; mt.text = getString(if (on) R.string.lm_multi_on else R.string.lm_multi_off) }
        mt.setOnClickListener { if (recWriter != null) return@setOnClickListener; prefs().edit().putBoolean("multitrack", !prefs().getBoolean("multitrack", false)).apply(); mtLabel() }   // not while recording
        mtLabel(); setupBox.addView(mt)
        // ---- QUICK CHAINS: one tap fills the channel's rack (both channels in STEREO) with a ready chain
        setupBox.addView(TextView(this).apply { text = getString(R.string.lm_chains); setTextColor(dim); textSize = 10f; letterSpacing = 0.15f; setPadding(0, 0, 0, 6) })
        val chainRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; tag = "chainRow" }; val nowChain = LocalChains.current(prefs(), 0)
        for (c in LocalChains.chains) chainRow.addView(PadButton(this, c.glyph + " " + c.title, cyan, 11f).apply { on = nowChain == c.id; contentDescription = c.sub; setPadding(22, 0, 22, 0)
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, 80).apply { setMargins(0, 0, 8, 0) }; setOnClickListener { applyChain(c) } })
        setupBox.addView(android.widget.HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; setPadding(0, 0, 0, 10); addView(chainRow) })
        dcaSetup(setupBox); chromeSetup(setupBox)
        // ---- THE PICTURE of a video deck (hidden until a deck's VIDEO button is on); its sound is the deck's channel on the desk
        val tv = android.view.TextureView(this); deckVideo = DeckVideo(this, tv)
        videoPane = android.widget.FrameLayout(this).apply { tag = "localVideo"; setBackgroundColor(Color.BLACK); visibility = View.GONE; addView(tv, android.widget.FrameLayout.LayoutParams(android.widget.FrameLayout.LayoutParams.MATCH_PARENT, android.widget.FrameLayout.LayoutParams.MATCH_PARENT, android.view.Gravity.CENTER))
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0).apply { setMargins(0, 0, 0, 10) } }
        mediaBox.addView(videoPane)
        // ---- THE DESK
        val desk = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f) }
        // ---- DECKS: two song players (a card each: the file, time, seek, LOAD / PLAY / STOP / LOOP); their channels are on the desk
        val deckBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; tag = "deckBox" }; mediaBox.addView(deckBox)
        for (r in 0 until DECKS / 2) { val deckRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 8) }
            for (d in 2 * r..2 * r + 1) deckRow.addView(deckCard(d).apply { layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { setMargins(if (d % 2 == 0) 0 else 6, 0, if (d % 2 == 0) 6 else 0, 0) } })
            deckBox.addView(deckRow) }
        // the channels side by side, four across in portrait, six in landscape (swipe for the rest), the master fixed on the right: every fader stays finger-wide
        val chans = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; tag = "localChannels" }; val w = stripWidth(resources.displayMetrics.widthPixels - (if (compact && land) 88 else 0), across(land))   // on its side the master is 88 px wider (its two keys stand side by side)
        fun fixed(v: View) = v.apply { layoutParams = LinearLayout.LayoutParams(w, LinearLayout.LayoutParams.MATCH_PARENT).apply { setMargins(4, 0, 4, 0) } }
        for (s in 0 until INPUTS) chans.addView(fixed(stripView(s)))   // CH 1 .. CH 6, then the decks: ten channels
        if (Nav.standalone(this) && Nav.Tab.PLAY in Nav.tabs(true)) chans.addView(fixed(playersStripView()))   // beside the inputs: on screen without a swipe
        for (d in 0 until DECKS) chans.addView(fixed(deckStripView(d)))
        for (g in 0 until Dca.COUNT) { val n = Dca.members(prefs(), g, dcaChannels()).size; if (n > 0) chans.addView(fixed(dcaStripView(g, n))) }   // GROUP FADERS: one per group in use
        desk.addView(android.widget.HorizontalScrollView(this).apply { isFillViewport = true; addView(chans, android.view.ViewGroup.LayoutParams(android.view.ViewGroup.LayoutParams.WRAP_CONTENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT)); layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f) })
        desk.addView(stripView(-1).apply { layoutParams = LinearLayout.LayoutParams(masterW(land), LinearLayout.LayoutParams.MATCH_PARENT).apply { setMargins(4, 0, 4, 0) } }); body.addView(desk); showDecks()
        desk.tag = "secFaders"
        // FADERS FIT THE SCREEN: once the desk has its real height the strip keys are sized for it (MixerFit), on any phone and either way up
        desk.addOnLayoutChangeListener { v, _, t, _, b, _, ot, _, ob -> if (b - t != ob - ot && b > t) v.post { MixerFit.fit(v, resources.displayMetrics.density, land) } }
        // THE MINI PLAYER: the PLAYERS link plays in a small video pane right above the faders (beside them when the phone lies on its
        // side), so the video and the faders are on one screen. It shows only in FADERS, and only when it is switched on and a link exists.
        if (Nav.standalone(this) && Nav.Tab.PLAY in Nav.tabs(true)) {
            val idx = body.indexOfChild(desk); body.removeView(desk)
            val fadersView = LinearLayout(this).apply { orientation = if (land) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL; tag = "secFaders"; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f) }
            desk.tag = "desk"; miniPane = android.widget.FrameLayout(this).apply { tag = "miniPlayer"; setBackgroundColor(Color.BLACK); visibility = View.GONE
                layoutParams = if (land) LinearLayout.LayoutParams((resources.displayMetrics.widthPixels * 0.36).toInt(), LinearLayout.LayoutParams.MATCH_PARENT).apply { setMargins(0, 0, 8, 0) }
                    else LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, miniHeight()).apply { setMargins(0, 0, 0, 8) } }
            fadersView.addView(miniPane); desk.layoutParams = if (land) LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f) else LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
            fadersView.addView(desk); body.addView(fadersView, idx); faderSection = fadersView
            showMini()
        } else faderSection = desk
        fun scroller(t: String, v: View) = android.widget.ScrollView(this).apply { tag = t; isFillViewport = true; addView(v); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f) }
        val toolsBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; toolsSection(toolsBox)
        val mediaView = scroller("secMedia", mediaBox); val setupView = scroller("secSetup", setupBox); val toolsView = scroller("secTools", toolsBox); body.addView(mediaView); body.addView(toolsView); body.addView(setupView)
        fun show(sec: String) { faderSection?.visibility = if (sec == "faders") View.VISIBLE else View.GONE; mediaView.visibility = if (sec == "media") View.VISIBLE else View.GONE; toolsView.visibility = if (sec == "tools") View.VISIBLE else View.GONE
            setupView.visibility = if (sec == "setup") View.VISIBLE else View.GONE; for ((id, k) in secKeys) k.on = id == sec; prefs().edit().putString("section", sec).apply() }
        for ((id, k) in secKeys) k.setOnClickListener { show(id) }
        show(prefs().getString("section", "faders").takeIf { it in secKeys } ?: "faders")
        if (videoDeck >= 0) showVideo(videoDeck)
    }
    /** the phone was turned: the screen is laid out again for the new shape (the sound keeps running, nothing is restarted) */
    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) { super.onConfigurationChanged(newConfig); Nav.fit(this); render() }
    private val faders = HashMap<Int, FaderView>()

    /** one channel strip (0, 1) or the MASTER (-1), top to bottom: name, FX (the insert rack), pan knob or LIMIT, MUTE, the fader with
     *  its LED meter, the level in dB */
    private fun stripView(s: Int): View {
        val p = prefs(); val master = s < 0; val two = inChannels() == 2 && s in 0..1
        val color = if (master) accent else ConsoleUi.palette[(s + 1) % ConsoleUi.palette.size]
        val col = ConsoleUi.strip(this, color, 0, master).apply { layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, if (master) 1.15f else 1f).apply { setMargins(4, 0, 4, 0) } }
        col.addView(TextView(this).apply { text = if (master) getString(R.string.lm_master) else getString(R.string.lm_channel, s + 1, if (two) (if (s == 0) "L" else "R") else "").trim(); setTextColor(if (master) accent else Color.WHITE); textSize = 14f; setTypeface(null, Typeface.BOLD); gravity = Gravity.CENTER })
        if (!land) col.addView(TextView(this).apply { text = if (master) getString(R.string.lm_sub_master) else getString(if (two) R.string.lm_sub_in_n else R.string.lm_sub_in, s + 1); setTextColor(dim); textSize = 9f; letterSpacing = 0.1f; gravity = Gravity.CENTER; setPadding(0, 0, 0, 8) })
        fun full(v: View, h: Int = keyH()) = v.apply { if (v is android.widget.TextView) key(v); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, h).apply { setMargins(6, 4, 6, 4) } }
        col.addView(full(PadButton(this, fxLabel(s), ConsoleUi.VIOLET, 12f).apply { on = rack(s).any { it.isNotEmpty() }; setOnClickListener { showRack(s) } }.also { fxBtns[s] = it }))
        if (!master) col.addView(full(eqKey(s)))
        if (master) {
            col.addView(full(PadButton(this, getString(R.string.lm_limit), ConsoleUi.GREEN, 12f).apply { on = p.getBoolean("limiter", true)
                setOnClickListener { val v = !p.getBoolean("limiter", true); p.edit().putBoolean("limiter", v).apply(); engine?.setLimiter(v, -0.5, 100.0); on = v } }))
            if (!land) col.addView(TextView(this).apply { text = getString(R.string.lm_limit_at); setTextColor(dim); textSize = 9f; gravity = Gravity.CENTER; setPadding(0, 0, 0, 6) })
            col.addView(full(PadButton(this, getString(R.string.lm_mute), ConsoleUi.RED, 12f).apply { contentDescription = "masterMute"; on = p.getBoolean("masterMute", false)
                setOnClickListener { setMasterMute(!p.getBoolean("masterMute", false)) } }.also { masterMuteKey = it }))
        } else {
            val defPan = if (two) (if (s == 0) -1f else 1f) else 0f
            if (!land) col.addView(KnobView(this, color).apply { value = p.getFloat("pan$s", defPan); layoutParams = LinearLayout.LayoutParams(if (compact) ConsoleUi.dp(this@LocalMixerActivity, RemoteActivity.PAN_DP) else 150, LinearLayout.LayoutParams.WRAP_CONTENT)
                onChange = { v -> p.edit().putFloat("pan$s", v).apply(); engine?.setPan(s, v.toDouble()) } })
            col.addView(full(PadButton(this, getString(R.string.lm_mute), ConsoleUi.RED, 12f).apply { on = p.getBoolean("mute$s", false)
                contentDescription = "mute_$s"; setOnClickListener { val v = !p.getBoolean("mute$s", false); p.edit().putBoolean("mute$s", v).apply(); applyLevel(engine, s); on = v } }))
            col.addView(full(soloKeyView(s)))
        }
        pairKeys(col)
        val key = if (master) "masterDb" else "db$s"; val readout = ConsoleUi.dbReadout(this)
        fun show(db: Double) { readout.text = com.amfb.amixos.remote.DaemonClient.fmtDb(db) }
        val db0 = p.getFloat(key, 0f).toDouble(); show(if (db0 <= -60) -120.0 else db0)
        col.addView(FaderView(this, color).apply { progress = (com.amfb.amixos.remote.DaemonClient.dbToPos(if (db0 <= -60) -120.0 else db0) * 1000).toInt()
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
            onMove = { pos -> val db = com.amfb.amixos.remote.DaemonClient.posToDb(pos / 1000.0); val set = if (db <= -60) -60.0 else db; p.edit().putFloat(key, set.toFloat()).apply(); show(db); if (master) engine?.setMaster(masterOut(p, masterRider.gainDb)) else { lmTouch(s); applyLevel(engine, s) } } }.also { faders[s] = it })
        col.addView(readout)
        return col
    }
    private fun card() = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.card_bg); setPadding(28, 22, 28, 22)
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 8, 0, 8) } }

    // ---- THE 3-BAND EQ of a channel (input s = s, deck d = 100 + d) and the TONE presets of the preset pack on it, see [ChannelEq]
    private val eqKeys = HashMap<Int, PadButton>()
    private val eqLoaded = HashSet<Int>()   // the strips whose EQ slot holds the ISO EQ now
    private fun eqLabel(ch: Int) = getString(if (ChannelEq.flat(ChannelEq.of(prefs(), ch))) R.string.lm_eq else R.string.lm_eq_on)
    private fun eqKey(ch: Int) = PadButton(this, eqLabel(ch), ConsoleUi.GREEN, 12f).apply { on = !ChannelEq.flat(ChannelEq.of(prefs(), ch)); contentDescription = "eq_$ch"; setOnClickListener { showEq(ch) } }.also { eqKeys[ch] = it }
    /** the channel's EQ into the engine: nothing loaded while it is flat */
    internal fun applyEq(e: Engine?, ch: Int) { e ?: return; val b = ChannelEq.of(prefs(), ch)
        for (t in chStrips(ch)) {
            if (ChannelEq.flat(b)) { if (eqLoaded.remove(t)) e.insert(t, Engine.EQ_SLOT, ""); continue }
            if (t !in eqLoaded) { if (e.insert(t, Engine.EQ_SLOT, "isoeq") != 0) continue; eqLoaded += t; e.insertSet(t, Engine.EQ_SLOT, "q", 0.0); e.insertSet(t, Engine.EQ_SLOT, "colour", 0.0); e.insertSet(t, Engine.EQ_SLOT, "output", 0.5) }
            for (i in 0 until 10) e.insertSet(t, Engine.EQ_SLOT, ChannelEq.IDS[i], ChannelEq.knob(b[i])) } }
    private fun eqChanged(ch: Int) { applyEq(engine, ch); eqKeys[ch]?.apply { text = eqLabel(ch); on = !ChannelEq.flat(ChannelEq.of(prefs(), ch)) } }
    /** LOW / MID / HIGH of a channel (dB) */
    internal fun setEq(ch: Int, low: Double, mid: Double, high: Double) { ChannelEq.set(prefs(), ch, low, mid, high); eqChanged(ch) }
    /** a TONE preset of the pack onto one channel (null takes it off) */
    internal fun applyTone(ch: Int, t: PresetPack.Tone?) { ChannelEq.setTone(prefs(), ch, t); eqChanged(ch) }
    /** an EFFECT preset of the pack into a channel's rack (strip of the channel: an input or a deck's left strip): the first free slot,
     *  else the last one; its values kept as the knobs are. The slot used. */
    internal fun applyEffectPreset(s: Int, fx: PresetPack.Effect): Int {
        val l = rack(s); val slot = l.indexOfFirst { it.isEmpty() }.takeIf { it >= 0 } ?: (Engine.MAX_INSERTS - 1)
        for (t in mates(s)) engine?.insert(t, slot, fx.kind)
        l[slot] = fx.kind; saveRack(s, l); prefs().edit().remove(paramKey(s, slot)).apply()
        fx.values.forEach { (id, v) -> for (t in mates(s)) engine?.insertSet(t, slot, id, v); setParam(s, slot, id, v) }
        fxBtns[s]?.apply { text = fxLabel(s); on = true }; return slot
    }
    private fun showEq(ch: Int) {
        val p = prefs(); val name = chName(ch)
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(40, 20, 40, 10); tag = "eqPanel" }
        val vals = doubleArrayOf(ChannelEq.low(p, ch), ChannelEq.mid(p, ch), ChannelEq.high(p, ch))
        val labels = listOf(R.string.lm_eq_low, R.string.lm_eq_mid, R.string.lm_eq_high); val bars = ArrayList<SeekBar>(); val texts = ArrayList<TextView>()
        fun fmt(v: Double) = String.format(java.util.Locale.US, "%+.1f", v)
        for (i in 0..2) { val t = TextView(this).apply { text = getString(labels[i], fmt(vals[i])); setTextColor(Color.WHITE); textSize = 13f }; texts += t; box.addView(t)
            box.addView(SeekBar(this).apply { contentDescription = "eqBand$i"; max = 48; progress = ((vals[i] + 12) * 2).toInt(); setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar, v: Int, user: Boolean) { if (!user) return; vals[i] = v / 2.0 - 12; t.text = getString(labels[i], fmt(vals[i])); setEq(ch, vals[0], vals[1], vals[2]) }
                override fun onStartTrackingTouch(sb: SeekBar) {}; override fun onStopTrackingTouch(sb: SeekBar) {} }) }.also { bars += it }) }
        val tones = PresetPack.current(this)?.tone.orEmpty()
        val toneBtn = Button(this).apply { contentDescription = "eqTone"; text = getString(R.string.lm_eq_tone, ChannelEq.toneName(p, ch).ifEmpty { getString(R.string.lm_eq_tone_none) }) }
        toneBtn.setOnClickListener { if (tones.isEmpty()) return@setOnClickListener
            AlertDialog.Builder(this).setTitle(R.string.fx_presets).setItems((listOf("— " + getString(R.string.lm_eq_tone_none)) + tones.map { "${it.category} · ${it.name}" }).toTypedArray()) { _, w ->
                applyTone(ch, if (w == 0) null else tones[w - 1]); toneBtn.text = getString(R.string.lm_eq_tone, ChannelEq.toneName(p, ch).ifEmpty { getString(R.string.lm_eq_tone_none) }) }.show() }
        box.addView(toneBtn)
        box.addView(Button(this).apply { text = getString(R.string.lm_eq_flat); contentDescription = "eqFlat"; setOnClickListener { ChannelEq.reset(p, ch); eqChanged(ch)
            for (i in 0..2) { vals[i] = 0.0; bars[i].progress = 24; texts[i].text = getString(labels[i], fmt(0.0)) }; toneBtn.text = getString(R.string.lm_eq_tone, getString(R.string.lm_eq_tone_none)) } })
        AlertDialog.Builder(this).setTitle(getString(R.string.lm_eq_title, name)).setView(ScrollView(this).apply { addView(box) }).setPositiveButton(android.R.string.ok, null).show()
    }

    // ---- INSERT RACK
    private fun rack(s: Int): MutableList<String> { val l = prefs().getString(rackKey(s), "").orEmpty().split('|').toMutableList(); while (l.size < Engine.MAX_INSERTS) l.add(""); return l.take(Engine.MAX_INSERTS).toMutableList() }
    private fun saveRack(s: Int, l: List<String>) = prefs().edit().putString(rackKey(s), l.joinToString("|")).apply()
    private fun fxLabel(s: Int): String { val n = rack(s).count { it.isNotEmpty() }; return if (n == 0) getString(R.string.lm_fx_add) else getString(R.string.lm_fx_n, n) }
    private val catalog: List<Pair<String, String>> by lazy {
        @Suppress("UNCHECKED_CAST") val l = runCatching { MiniJson.parse(Engine.fxCatalogJson()) as List<Map<String, Any?>> }.getOrDefault(emptyList())
        l.map { it["kind"].toString() to it["name"].toString() } }
    private fun showRack(s: Int) {
        val dlg = AlertDialog.Builder(this).setTitle(if (s < 0) getString(R.string.lm_rack_master) else if (s >= INPUTS) getString(R.string.lm_deck, deckName((s - INPUTS) / 2)) + " · FX" else getString(R.string.lm_rack_ch, s + 1)).setPositiveButton(android.R.string.ok, null).create()
        val holder = ScrollView(this)
        fun refresh() { holder.removeAllViews(); holder.addView(rackPanel(s, catalog, { slot -> engine?.insertJson(s, slot) ?: "null" }, { refresh() })); fxBtns[s]?.apply { text = fxLabel(s); on = rack(s).any { it.isNotEmpty() } } }
        refresh(); dlg.setView(holder); dlg.setOnDismissListener { fxBtns[s]?.apply { text = fxLabel(s); on = rack(s).any { it.isNotEmpty() } } }; dlg.show()
    }
    /** the rack of one strip: four slots, each empty (+ ADD) or an effect with BYPASS, REMOVE, MIX and its knobs. [slotJson] gives what the
     *  engine reports for a slot (a test passes made-up JSON); [changed] redraws. */
    fun rackPanel(s: Int, cat: List<Pair<String, String>>, slotJson: (Int) -> String, changed: () -> Unit): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 12, 24, 12); setBackgroundColor(Color.parseColor("#1D2024")) }
        val names = rack(s)
        // THE PRESET PACK on this channel: any factory effect preset into the rack's first free slot (the last slot when it is full)
        val pack = PresetPack.current(this)?.effects.orEmpty()
        if (pack.isNotEmpty()) box.addView(Button(this).apply { text = getString(R.string.lm_fx_pack_s); contentDescription = "fxPack"; setOnClickListener {
            AlertDialog.Builder(this@LocalMixerActivity).setTitle(R.string.fx_presets).setItems(pack.map { "${it.category} · ${it.name}" }.toTypedArray()) { _, w -> applyEffectPreset(s, pack[w]); changed() }.show() } })
        for (slot in 0 until Engine.MAX_INSERTS) {
            val c = card(); box.addView(c)
            val kind = names[slot]
            if (kind.isEmpty()) {
                c.addView(Button(this).apply { text = getString(R.string.lm_slot_add_s, slot + 1); setOnClickListener {
                    if (cat.isEmpty()) return@setOnClickListener
                    AlertDialog.Builder(this@LocalMixerActivity).setTitle(R.string.lm_pick_fx).setItems(cat.map { it.second }.toTypedArray()) { _, w ->
                        val k = cat[w].first; if (engine == null || mates(s).map { engine?.insert(it, slot, k) }.all { it == 0 }) { val l = rack(s); l[slot] = k; saveRack(s, l); prefs().edit().remove(paramKey(s, slot)).apply() }; changed() }.show() } })
                continue
            }
            @Suppress("UNCHECKED_CAST") val st = runCatching { MiniJson.parse(slotJson(slot)) as? Map<String, Any?> }.getOrNull()
                ?: mapOf("kind" to kind, "name" to (cat.firstOrNull { it.first == kind }?.second ?: kind), "bypass" to false, "mix" to 1.0, "params" to emptyList<Any>())
            // the effect as a plug-in face: power (bypass), PRESETS, a knob per parameter
            c.addView(PluginFace.build(this, st, slot, object : PluginFace.Host {
                override fun set(id: String, value: Double) { for (t in mates(s)) engine?.insertSet(t, slot, id, value); setParam(s, slot, id, value) }
                override fun remove() { for (t in mates(s)) engine?.insert(t, slot, ""); val l = rack(s); l[slot] = ""; saveRack(s, l); prefs().edit().remove(paramKey(s, slot)).apply() }
                override fun changed() = changed()
                override fun text(id: String) = liveText(s, slot, id)
            }))
        }
        return box
    }
    private fun liveText(s: Int, slot: Int, id: String): String? { @Suppress("UNCHECKED_CAST") val st = runCatching { MiniJson.parse(engine?.insertJson(s, slot) ?: "null") as? Map<String, Any?> }.getOrNull() ?: return null
        @Suppress("UNCHECKED_CAST") return (st["params"] as? List<Map<String, Any?>>)?.firstOrNull { it["id"] == id }?.get("text")?.toString() }
    /** a slot's knob values kept as "id=value;id=value" so the rack comes back as it was */
    private fun setParam(s: Int, slot: Int, id: String, v: Double) {
        val m = LinkedHashMap<String, String>(); prefs().getString(paramKey(s, slot), "").orEmpty().split(';').filter { '=' in it }.forEach { m[it.substringBefore('=')] = it.substringAfter('=') }
        m[id] = String.format(java.util.Locale.US, "%.4f", v); prefs().edit().putString(paramKey(s, slot), m.entries.joinToString(";") { it.key + "=" + it.value }).apply()
    }
    /** puts the saved levels, pans, mutes, limiter and racks onto a fresh engine */
    /** a QUICK CHAIN onto the input channel(s): saved, and put into the running engine slot by slot (an emptied slot is taken out) */
    internal fun applyChain(c: LocalChains.Chain) {
        for (s in 0 until inChannels()) { LocalChains.save(prefs(), s, c, Engine.MAX_INSERTS)
            engine?.let { e -> for (slot in 0 until Engine.MAX_INSERTS) { val f = c.slots.getOrNull(slot); e.insert(s, slot, f?.kind ?: ""); f?.params?.forEach { (id, v) -> e.insertSet(s, slot, id, v) } } } }
        render()
    }
    private fun applyAll(e: Engine) {
        val p = prefs(); val two = inChannels() == 2
        for (s in 0 until INPUTS) { e.setGain(s, p.getFloat("db$s", 0f).toDouble()); e.setPan(s, p.getFloat("pan$s", if (two && s < 2) (if (s == 0) -1f else 1f) else 0f).toDouble()); e.setMute(s, p.getBoolean("mute$s", false)) }
        for (d in 0 until DECKS) { val l = deckStrip(d); val db = p.getFloat("deckDb$d", 0f).toDouble(); val mu = p.getBoolean("deckMute$d", false)   // a deck: its left and right on their own strips
            e.setGain(l, db); e.setGain(l + 1, db); e.setPan(l, -1.0); e.setPan(l + 1, 1.0); e.setMute(l, mu); e.setMute(l + 1, mu) }
        for (ch in (0 until INPUTS) + (0 until DECKS).map { 100 + it }) { applyLevel(e, ch); applyEq(e, ch) }   // each channel with its group fader and group mute, and its EQ
        PhoneDucker.apply(e, PhoneDucker.load(p), duckRoles())   // DUCKER: CH (voice) over the DECKS (music)
        e.setMaster(masterOut(p, 0.0)); e.setLimiter(p.getBoolean("limiter", true), -0.5, 100.0)
        for (s in listOf(-1) + (0 until INPUTS) + (0 until DECKS).map { deckStrip(it) }) rack(s).forEachIndexed { slot, k -> if (k.isNotEmpty()) for (t in mates(s)) if (e.insert(t, slot, k) == 0)
            p.getString(paramKey(s, slot), "").orEmpty().split(';').filter { '=' in it }.forEach { e.insertSet(t, slot, it.substringBefore('='), it.substringAfter('=').toDoubleOrNull() ?: 0.0) } }
    }

    // ---- DEVICES
    private fun deviceName(id: Int, out: Boolean): String {
        if (id == 0) return getString(if (out) R.string.lm_out_system else R.string.lm_in_system)
        val am = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val d = am.getDevices(if (out) AudioManager.GET_DEVICES_OUTPUTS else AudioManager.GET_DEVICES_INPUTS).firstOrNull { it.id == id } ?: return getString(R.string.lm_gone)
        return (d.productName?.toString()?.takeIf { it.isNotBlank() } ?: "") + "  ·  " + DevicesActivity.typeName(d.type)
    }
    private fun pickDevice(out: Boolean) {
        val am = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val list = am.getDevices(if (out) AudioManager.GET_DEVICES_OUTPUTS else AudioManager.GET_DEVICES_INPUTS).filter { !(out && it.type == AudioDeviceInfo.TYPE_TELEPHONY) }
        val labels = listOf(getString(if (out) R.string.lm_out_system else R.string.lm_in_system)) + list.map { d ->
            val ch = d.channelCounts.distinct().sorted(); (d.productName?.toString()?.takeIf { it.isNotBlank() } ?: "") + "  ·  " + DevicesActivity.typeName(d.type) + (if (ch.isNotEmpty()) "  ·  " + ch.joinToString("/") + " ch" else "") }
        AlertDialog.Builder(this).setTitle(if (out) R.string.lm_output else R.string.lm_input).setItems(labels.toTypedArray()) { _, w ->
            prefs().edit().putInt(if (out) "outDevice" else "inDevice", if (w == 0) 0 else list[w - 1].id).apply(); restartIfRunning(); render() }.show()
    }
    private fun setInChannels(n: Int) {
        val p = prefs(); if (inChannels() == n) return
        p.edit().putInt("inChannels", n).remove("pan0").remove("pan1").apply()
        engine?.let { e -> for (s in 0..1) e.setPan(s, if (n == 2) (if (s == 0) -1.0 else 1.0) else 0.0) }   // only CH 1-2 pair up; the decks keep their own pans
        restartIfRunning(); render()
    }
    private fun restartIfRunning() { if (loop != null) { stopAudio(); startAudio() } }

    // ---- AUDIO
    private fun startAudio() {
        // INPUT OFF: no microphone is opened and none is asked for. Input ON but no permission yet: the output still starts (the decks,
        // pads and tone play) and the permission is asked for; once granted the loop restarts with the microphone
        val (ask, inOn) = audioPlan(Prefs.localInputOn(this), checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED)
        if (ask) requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 1)
        val e = engine ?: run { status.text = getString(R.string.lm_no_engine); return }
        loop = AudioLoop(e, rate, frames, Prefs.localOutDevice(this), Prefs.localInDevice(this), bursts, inChannels(), inOn).also { it.start() }
        if (::startBtn.isInitialized) { startBtn.text = getString(R.string.stop); startBtn.on = true }
        ui.postDelayed({ showStatus() }, 400)
    }
    private fun stopAudio() { stopTone(); decks.forEach { it.pause() }; loop?.stop(); loop = null; if (::startBtn.isInitialized) { startBtn.text = getString(R.string.start); startBtn.on = false }; showStatus() }
    private fun showStatus() {
        if (!::status.isInitialized) return
        val l = loop; val ch = runCatching { Engine.aaudioInChannels() }.getOrDefault(0)
        status.text = if (l == null) getString(R.string.lm_stopped, rate, frames) else getString(R.string.lm_running, l.path.uppercase(), rate, frames, if (l.path == "aaudio") (if (ch == 2) "STEREO" else if (ch == 1) "MONO" else "—") else "MONO")
    }
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 1 && grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) { if (loop != null) restartIfRunning() else startAudio() }
    }
    /** THE INPUT / OUTPUT INDICATOR: is each side open, which device it really is, and the level going in and out. A side that did not
     *  open (no microphone permission, a refused device) shows red; open but silent shows amber; sound shows green with its level. */
    private fun showIo() {
        val l = loop; val running = l != null && l.path.isNotEmpty()
        val st = if (l?.path == "aaudio") runCatching { engine?.nativeState() }.getOrNull() else null
        val inOpen = Prefs.localInputOn(this) && when (l?.path) { "aaudio" -> st?.get(5) == 1; "java" -> true; else -> false }   // INPUT OFF never reads as open
        val outOpen = when (l?.path) { "aaudio" -> st?.get(0) == 1 && st[1] == 0; "java" -> true; else -> false }
        val inPk = l?.lastInPeaks?.let { a -> (0 until minOf(inChannels(), a.size)).maxOfOrNull { a[it] } } ?: 0f
        val outPk = l?.lastPeaks?.let { p -> maxOf(p.getOrElse(STRIPS) { 0f }, p.getOrElse(STRIPS + 1) { 0f }) } ?: 0f
        paintIo(running, inOpen, outOpen, inPk, outPk, st?.get(7) ?: 0, st?.get(6) ?: 0)
    }
    /** draws both indicators; [inReal] / [outReal] are the device ids AAudio really opened (0 = not known) */
    internal fun paintIo(running: Boolean, inOpen: Boolean, outOpen: Boolean, inPk: Float, outPk: Float, inReal: Int, outReal: Int) {
        for ((out, open, pk) in listOf(Triple(false, inOpen, inPk), Triple(true, outOpen, outPk))) {
            val v = io[out] ?: continue; val s = IoLamp.of(running, open, pk)
            ((v.lamp.background) as? android.graphics.drawable.GradientDrawable)?.setColor(IoLamp.color(s))
            v.meter.progress = if (s == IoLamp.State.SIGNAL || s == IoLamp.State.CLIP || s == IoLamp.State.SILENT) IoLamp.pos(pk) else 0
            // "the phone's choice": name the device it really took (AAudio says which)
            val chosen = if (out) Prefs.localOutDevice(this) else Prefs.localInDevice(this); val real = if (out) outReal else inReal
            val using = if (chosen == 0 && real > 0) getString(R.string.io_using, deviceName(real, out)) else ""
            v.state.text = when (s) {
                IoLamp.State.STOPPED -> getString(R.string.io_stopped)
                IoLamp.State.NOT_OPEN -> getString(if (out) R.string.io_out_not_open else if (!Prefs.localInputOn(this)) R.string.io_in_off else R.string.io_in_not_open)
                IoLamp.State.SILENT -> getString(R.string.io_silent) + using
                IoLamp.State.SIGNAL -> getString(R.string.io_signal, String.format(java.util.Locale.ROOT, "%.0f", IoLamp.db(pk))) + using
                IoLamp.State.CLIP -> getString(R.string.io_clip) + using }
            v.state.setTextColor(if (s == IoLamp.State.STOPPED) dim else IoLamp.color(s))
        }
    }
    private fun showMeters() {
        showIo()
        showDeckTimes(); showLevels()
        val p = loop?.lastPeaks ?: return
        fun lvl(v: Float) = (((if (v > 1e-6f) 20 * Math.log10(v.toDouble()) else -120.0) + 60) / 60 * 1000).toInt().coerceIn(0, 1000)
        for (s in 0 until INPUTS) faders[s]?.apply { level = lvl(p.getOrElse(s) { 0f }); clip = p.getOrElse(s) { 0f } >= 1f }
        for (d in 0 until DECKS) faders[100 + d]?.apply { val l = deckStrip(d); val m = maxOf(p.getOrElse(l) { 0f }, p.getOrElse(l + 1) { 0f }); level = lvl(m); clip = m >= 1f }
        faders[-1]?.apply { val m = maxOf(p.getOrElse(STRIPS) { 0f }, p.getOrElse(STRIPS + 1) { 0f }); level = lvl(m); clip = m >= 1f }
    }
    override fun onResume() { super.onResume(); val e = StreamPlayerActivity.lastEmbed(this); if (e != null && miniWeb != null && miniUrl != e.url) closeMini(); miniUrl = e?.url; showMini() }
    private var miniUrl: String? = null
    override fun onDestroy() { if (QuickMaster.listener === quickHook) QuickMaster.listener = null; stopTone(); closeMini(); deckVideo?.close(); stopRec(); decks.forEach { it.release() }; stopAudio(); runCatching { engine?.close() }; super.onDestroy() }

    // ---- DECKS
    private fun loadDeck(d: Int, u: android.net.Uri, fresh: Boolean) {
        if (fresh) runCatching { contentResolver.takePersistableUriPermission(u, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION) }   // the deck comes back next time
        val name = runCatching { contentResolver.query(u, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c -> if (c.moveToFirst()) c.getString(0) else null } }.getOrNull() ?: u.lastPathSegment.orEmpty()
        prefs().edit().putString("deckUri$d", u.toString()).putString("deckTitle$d", name).apply()
        decks.getOrNull(d)?.load(u, name); showDecks()
    }
    private fun deckCard(d: Int): View {
        val c = card(); val letter = deckName(d)
        c.addView(TextView(this).apply { text = getString(R.string.lm_deck, letter); setTextColor(cyan); textSize = 11f; letterSpacing = 0.15f; typeface = Typeface.DEFAULT_BOLD })
        val title = TextView(this).apply { setTextColor(Color.WHITE); textSize = 12f; maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END }; c.addView(title)
        val time = TextView(this).apply { setTextColor(dim); textSize = 10f; typeface = Typeface.MONOSPACE }; c.addView(time)
        val seek = SeekBar(this).apply { max = 1000; setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar, v: Int, user: Boolean) {}
            override fun onStartTrackingTouch(sb: SeekBar) {}
            override fun onStopTrackingTouch(sb: SeekBar) { decks.getOrNull(d)?.let { k -> if (k.durationUs > 0) k.seek(k.durationUs * sb.progress / 1000) } } }) }
        c.addView(seek)
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        fun btn(t: String, col: Int, f: () -> Unit) = PadButton(this, t, col, 11f).apply { layoutParams = LinearLayout.LayoutParams(0, 76, 1f).apply { setMargins(2, 4, 2, 0) }; setOnClickListener { f() } }.also { row.addView(it) }
        btn(getString(R.string.lm_deck_load), cyan) { pickFor = d; runCatching { pickFile.launch(arrayOf("audio/*", "video/*")) } }
        val play = btn("▶", ConsoleUi.GREEN) { val k = decks.getOrNull(d) ?: return@btn; if (k.playing) k.pause() else { if (loop == null) startAudio(); k.play() } }
        btn("■", ConsoleUi.RED) { decks.getOrNull(d)?.stop() }
        val lp = btn(getString(R.string.lm_deck_loop), ConsoleUi.AMBER) { decks.getOrNull(d)?.let { k -> k.loop = !k.loop; prefs().edit().putBoolean("deckLoop$d", k.loop).apply(); showDecks() } }
        val vid = btn(getString(R.string.lm_deck_video), ConsoleUi.AMBER) { showVideo(if (videoDeck == d) -1 else d) }.apply { contentDescription = "deckVideo$d" }
        c.addView(row)
        deckViews[d] = DeckViews(title, time, seek, play, lp, vid)
        // the deck chosen last time comes back (if Android still lets the app read it)
        val saved = prefs().getString("deckUri$d", null)
        if (saved != null && decks.getOrNull(d)?.loaded == false) { decks[d].loop = prefs().getBoolean("deckLoop$d", false); runCatching { loadDeck(d, android.net.Uri.parse(saved), false) } }
        return c
    }
    private fun showDecks() { for (d in 0 until DECKS) { val v = deckViews[d] ?: continue; val k = decks.getOrNull(d)
        v.title.text = when { k == null -> getString(R.string.lm_no_engine); k.error.isNotEmpty() -> getString(R.string.lm_deck_error, k.error); k.loaded -> k.title; else -> getString(R.string.lm_deck_empty) }
        v.play.text = if (k?.playing == true) "❚❚" else "▶"; v.play.on = k?.playing == true; v.loop.on = k?.loop == true; v.video.on = videoDeck == d; v.video.visibility = if (k?.hasVideo == true) View.VISIBLE else View.GONE }
        if (videoDeck >= 0 && decks.getOrNull(videoDeck)?.hasVideo != true) showVideo(-1)
        showDeckTimes() }
    private fun showDeckTimes() { if (videoDeck >= 0) decks.getOrNull(videoDeck)?.let { k -> deckVideo?.follow(k.playing, k.positionUs / 1000) }
        for (d in 0 until DECKS) { val v = deckViews[d] ?: continue; val k = decks.getOrNull(d) ?: continue
        val pos = k.positionUs; v.time.text = DeckPlayer.clock(pos) + " / " + DeckPlayer.clock(k.durationUs)
        if (!v.seek.isPressed && k.durationUs > 0) v.seek.progress = (pos * 1000 / k.durationUs).toInt() } }
    /** a deck's channel on the desk: one stereo strip (its left and right strips together): name, MUTE, fader with meter, level */
    /** THE PLAYERS CHANNEL: the level and the mute of the official player on the PLAYERS tab. A link, not an audio path: the streamed
     *  song is not passed through this mixer (no effects, no recording); the strip tells the player how loud to play. */
    private fun playersStripView(): View {
        val p = prefs(); val color = accent
        val col = ConsoleUi.strip(this, color, 0, false).apply { tag = "playersStrip"; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f).apply { setMargins(4, 0, 4, 0) } }
        col.addView(TextView(this).apply { text = getString(R.string.lm_players); setTextColor(Color.WHITE); textSize = 14f; setTypeface(null, Typeface.BOLD); gravity = Gravity.CENTER })
        if (!land) col.addView(TextView(this).apply { text = getString(R.string.lm_sub_players); setTextColor(dim); textSize = 9f; letterSpacing = 0.1f; gravity = Gravity.CENTER; setPadding(0, 0, 0, 8) })
        col.addView(PadButton(this, getString(R.string.lm_players_video), cyan, 12f).apply { contentDescription = "miniToggle"; on = p.getBoolean("miniPlayer", true) && StreamPlayerActivity.lastEmbed(this@LocalMixerActivity) != null
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 84).apply { setMargins(6, 4, 6, 4) }
            setOnClickListener { if (StreamPlayerActivity.lastEmbed(this@LocalMixerActivity) == null) { Nav.go(this@LocalMixerActivity, StreamPlayerActivity::class.java); return@setOnClickListener }   // no link yet: pick one first
                val v = !p.getBoolean("miniPlayer", true); p.edit().putBoolean("miniPlayer", v).apply(); on = v; showMini() }
            setOnLongClickListener { Nav.go(this@LocalMixerActivity, StreamPlayerActivity::class.java); true } })
        col.addView(PadButton(this, getString(R.string.lm_mute), ConsoleUi.RED, 12f).apply { contentDescription = "playersMute"; on = p.getBoolean("playersMute", false); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 84).apply { setMargins(6, 4, 6, 4) }
            setOnClickListener { val v = !p.getBoolean("playersMute", false); p.edit().putBoolean("playersMute", v).apply(); on = v; PlayersBus.apply(this@LocalMixerActivity) } })
        pairKeys(col)
        val note = TextView(this).apply { contentDescription = "playersNote"; setTextColor(dim); textSize = 9f; gravity = Gravity.CENTER; setPadding(4, 2, 4, 2)
            text = getString(when { !PlayersBus.loaded -> R.string.lm_players_none; PlayersBus.volumeWorks() -> R.string.lm_players_volume; else -> R.string.lm_players_mute_only }) }
        if (!land) col.addView(note)
        val readout = ConsoleUi.dbReadout(this); fun show(db: Double) { readout.text = com.amfb.amixos.remote.DaemonClient.fmtDb(db) }
        val db0 = p.getFloat("playersDb", 0f).toDouble(); show(if (db0 <= -60) -120.0 else db0)
        col.addView(FaderView(this, color).apply { contentDescription = "playersFader"; progress = (com.amfb.amixos.remote.DaemonClient.dbToPos(if (db0 <= -60) -120.0 else minOf(0.0, db0)) * 1000).toInt()
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
            onMove = { pos -> val db = minOf(0.0, com.amfb.amixos.remote.DaemonClient.posToDb(pos / 1000.0)); val set = if (db <= -60) -60.0 else db   // a player has no gain above its own full level
                p.edit().putFloat("playersDb", set.toFloat()).apply(); show(if (set <= -60) -120.0 else set); PlayersBus.apply(this@LocalMixerActivity) } })
        col.addView(readout)
        return col
    }
    private var miniPane: android.widget.FrameLayout? = null; private var miniWeb: android.webkit.WebView? = null; private var faderSection: View? = null
    /** the mini player's height upright: 16:9 of the width, never more than a quarter of the screen (the faders keep the rest) */
    private fun miniHeight() = minOf(resources.displayMetrics.widthPixels * 9 / 16, resources.displayMetrics.heightPixels / 4)
    /** shows or hides the mini player above the faders: on when switched on and the PLAYERS tab has a link */
    private fun showMini() {
        val pane = miniPane ?: return; val e = StreamPlayerActivity.lastEmbed(this)
        if (!prefs().getBoolean("miniPlayer", true) || e == null) { closeMini(); pane.visibility = View.GONE; return }
        pane.visibility = View.VISIBLE
        miniWeb?.let { if (it.parent == null) pane.addView(it, android.widget.FrameLayout.LayoutParams(android.widget.FrameLayout.LayoutParams.MATCH_PARENT, android.widget.FrameLayout.LayoutParams.MATCH_PARENT)) }
        if (miniWeb == null) { val w = StreamPlayerActivity.buildPlayer(this, e) { closeMini() }; miniWeb = w; pane.removeAllViews(); pane.addView(w, android.widget.FrameLayout.LayoutParams(android.widget.FrameLayout.LayoutParams.MATCH_PARENT, android.widget.FrameLayout.LayoutParams.MATCH_PARENT)) }
    }
    private fun closeMini() { val w = miniWeb ?: return; miniWeb = null; PlayersBus.detach(w); (w.parent as? android.view.ViewGroup)?.removeView(w); runCatching { w.stopLoading(); w.loadUrl("about:blank"); w.destroy() } }
    // ---- GROUP FADERS (DCA): see [Dca]. Channels: inputs 0 / 1, decks 100 + d (as the faders are keyed)
    internal fun dcaChannels(): List<Int> = (0 until INPUTS).toList() + (0 until DECKS).map { 100 + it }   // all ten channels
    private fun chName(ch: Int) = if (ch >= 100) getString(R.string.lm_deck, deckName(ch - 100)) else getString(R.string.lm_channel, ch + 1, "").trim()
    /** puts one channel's level and mute into the engine: its own fader and MUTE with its group's offset and MUTE */
    /** QUICK MASTER (the row over the bottom bar on every screen): the engine and the master fader follow it */
    private val quickHook: () -> Unit = { ui.post { val p = prefs(); engine?.setMaster(masterOut(p, masterRider.gainDb)); masterMuteKey?.on = p.getBoolean("masterMute", false); faders[-1]?.progress = (com.amfb.amixos.remote.DaemonClient.dbToPos(p.getFloat("masterDb", 0f).toDouble().let { if (it <= -60) -120.0 else it }) * 1000).toInt() } }
    private val soloKeys = HashMap<Int, PadButton>(); private var masterMuteKey: PadButton? = null
    private fun soloKeyView(ch: Int) = PadButton(this, getString(R.string.lm_solo), ConsoleUi.AMBER, 12f).apply { contentDescription = "solo_$ch"; on = prefs().getBoolean(soloKey(ch), false); setOnClickListener { toggleSolo(ch) } }.also { soloKeys[ch] = it }
    /** CHROMEBOOK MODE: the arrow keys move the master as on the console (1 dB, with SHIFT 0.1 dB) */
    override fun dispatchKeyEvent(e: android.view.KeyEvent): Boolean {
        val step = ChromeFrame.masterStep(e, currentFocus is android.widget.EditText)
        if (step != null && ChromeFrame.active(this)) { if (e.action == android.view.KeyEvent.ACTION_DOWN) { val p = prefs(); p.edit().putFloat("masterDb", ChromeFrame.stepMaster(p.getFloat("masterDb", 0f).toDouble(), step).toFloat()).commit(); quickHook(); QuickMaster.changed(this) }; return true }
        return super.dispatchKeyEvent(e)
    }
    /** SOLO: one channel (or several) alone on the master; every channel's level is sent again */
    internal fun toggleSolo(ch: Int) { val p = prefs(); val v = !p.getBoolean(soloKey(ch), false); p.edit().putBoolean(soloKey(ch), v).commit()
        for (c in allChannels()) { applyLevel(engine, c); soloKeys[c]?.on = p.getBoolean(soloKey(c), false) } }
    /** MASTER MUTE (also the quick bar's MUTE on every screen) */
    internal fun setMasterMute(on: Boolean) { val p = prefs(); p.edit().putBoolean("masterMute", on).commit(); engine?.setMaster(masterOut(p, masterRider.gainDb)); masterMuteKey?.on = on; QuickMaster.changed(this) }
    internal fun applyLevel(e: Engine?, ch: Int) { e ?: return; val p = prefs()
        val (db, mu) = channelLevel(p, ch); val off = lmOffset[ch] ?: 0.0
        val g = if (off <= PhoneLoudness.FLOOR + 0.1 || db <= -60) -120.0 else minOf(PhoneLoudness.MAX_DB, db + off)   // LOUDNESS MANAGER: fader + DCA, then the ride and auto on/off
        for (t in chStrips(ch)) { e.setGain(t, g); e.setMute(t, mu) }
    }
    private fun applyGroup(g: Int) { for (ch in dcaChannels()) if (Dca.groupOf(prefs(), ch) == g) applyLevel(engine, ch) }
    private fun dcaStripView(g: Int, n: Int): View {
        val p = prefs(); val color = ConsoleUi.AMBER
        val col = ConsoleUi.strip(this, color, 0, false).apply { tag = "dca$g"; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f).apply { setMargins(4, 0, 4, 0) } }
        col.addView(TextView(this).apply { text = getString(R.string.lm_dca, g + 1); setTextColor(color); textSize = 14f; setTypeface(null, Typeface.BOLD); gravity = Gravity.CENTER })
        if (!land) col.addView(TextView(this).apply { text = getString(R.string.lm_dca_sub, n); setTextColor(dim); textSize = 9f; letterSpacing = 0.1f; gravity = Gravity.CENTER; setPadding(0, 0, 0, 8) })
        col.addView(key(PadButton(this, getString(R.string.lm_mute), ConsoleUi.RED, 12f)).apply { on = Dca.muted(p, g); contentDescription = "dcaMute$g"; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, keyH()).apply { setMargins(6, 4, 6, 4) }
            setOnClickListener { val v = !Dca.muted(p, g); Dca.setMuted(p, g, v); applyGroup(g); on = v } })
        pairKeys(col)
        val readout = ConsoleUi.dbReadout(this); fun show(db: Double) { readout.text = com.amfb.amixos.remote.DaemonClient.fmtDb(db) }
        val db0 = Dca.level(p, g); show(if (db0 <= Dca.FLOOR) -120.0 else db0)
        col.addView(FaderView(this, color).apply { contentDescription = "dcaFader$g"; progress = (com.amfb.amixos.remote.DaemonClient.dbToPos(if (db0 <= Dca.FLOOR) -120.0 else db0) * 1000).toInt()
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
            onMove = { pos -> val db = com.amfb.amixos.remote.DaemonClient.posToDb(pos / 1000.0); Dca.setLevel(p, g, if (db <= Dca.FLOOR) Dca.FLOOR else db); show(db); applyGroup(g) } }.also { faders[200 + g] = it })
        col.addView(readout); return col
    }
    private fun sectionTitle(box: LinearLayout, t: String) = box.addView(TextView(this).apply { text = t; setTextColor(dim); textSize = 10f; letterSpacing = 0.15f; setPadding(0, 10, 0, 6) })
    private fun dcaSetup(box: LinearLayout) {
        sectionTitle(box, getString(R.string.lm_dca_title))
        box.addView(ConsoleUi.moreLine(TextView(this).apply { text = getString(R.string.lm_dca_help); setTextColor(dim); textSize = 11f; setPadding(0, 0, 0, 6) }))
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; tag = "dcaRow" }
        for (ch in dcaChannels()) { val g = Dca.groupOf(prefs(), ch)
            row.addView(PadButton(this, chName(ch) + "  ·  " + (if (g < 0) getString(R.string.lm_dca_none) else getString(R.string.lm_dca, g + 1)), ConsoleUi.AMBER, 11f).apply { on = g >= 0; contentDescription = "dcaAssign_$ch"; setPadding(22, 0, 22, 0)
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, 80).apply { setMargins(0, 0, 8, 0) }; setOnClickListener { Dca.cycle(prefs(), ch); applyLevel(engine, ch); render() } }) }
        box.addView(android.widget.HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; setPadding(0, 0, 0, 10); addView(row) })
    }
    private fun chromeSetup(box: LinearLayout) {
        sectionTitle(box, getString(R.string.cb_mode_title))
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 10) }; val now = ChromeFrame.mode(this)
        for ((m, t) in listOf("auto" to R.string.cb_mode_auto, "on" to R.string.cb_mode_on, "off" to R.string.cb_mode_off))
            row.addView(PadButton(this, getString(t), cyan, 11f).apply { on = now == m; contentDescription = "cbMode_$m"; layoutParams = LinearLayout.LayoutParams(0, 80, 1f).apply { setMargins(0, 0, 8, 0) }
                setOnClickListener { if (ChromeFrame.mode(this@LocalMixerActivity) != m) { ChromeFrame.setMode(this@LocalMixerActivity, m); recreate() } } })
        box.addView(row)
    }
    // ---- TOOLS: the TONE (through deck D's channel, see [MixerTools]) and a LEVEL meter from the engine's meters
    private var tone: MixerTools.ToneFeeder? = null
    private var toneKind = "sine"; private var toneDb = -20.0
    private var lvlViews: List<TextView> = emptyList()
    private fun toolsSection(box: LinearLayout) {
        val p = prefs(); toneKind = p.getString("toneKind", "sine").takeIf { it in MixerTools.kinds } ?: "sine"; toneDb = p.getInt("toneDb", -20).toDouble()
        sectionTitle(box, getString(R.string.lm_tone_title))
        box.addView(TextView(this).apply { text = getString(R.string.lm_tone_help, deckName(MixerTools.TONE_DECK)); setTextColor(dim); textSize = 11f; setPadding(0, 0, 0, 6) })
        val kinds = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 8) }; val kindKeys = HashMap<String, PadButton>()
        for ((k, t) in listOf("sine" to R.string.lm_tone_sine, "pink" to R.string.lm_tone_pink))
            kinds.addView(PadButton(this, getString(t), cyan, 11f).apply { on = toneKind == k; contentDescription = "toneKind_$k"; layoutParams = LinearLayout.LayoutParams(0, 80, 1f).apply { setMargins(0, 0, 8, 0) }
                setOnClickListener { toneKind = k; p.edit().putString("toneKind", k).apply(); for ((kk, b) in kindKeys) b.on = kk == k; tuneTone() } }.also { kindKeys[k] = it })
        box.addView(kinds)
        val lvl = TextView(this).apply { text = getString(R.string.lm_tone_level, toneDb.toInt()); setTextColor(Color.WHITE); textSize = 12f }
        box.addView(lvl)
        box.addView(SeekBar(this).apply { contentDescription = "toneLevel"; max = 60; progress = toneDb.toInt() + 60; setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar, v: Int, user: Boolean) { toneDb = (v - 60).toDouble(); p.edit().putInt("toneDb", v - 60).apply(); lvl.text = getString(R.string.lm_tone_level, v - 60); tuneTone() }
            override fun onStartTrackingTouch(sb: SeekBar) {}; override fun onStopTrackingTouch(sb: SeekBar) {} }) })
        val st = TextView(this).apply { tag = "toneStatus"; setTextColor(dim); textSize = 11f; setPadding(0, 6, 0, 6) }
        box.addView(PadButton(this, getString(if (tone?.on == true) R.string.lm_tone_on else R.string.lm_tone_off), ConsoleUi.GREEN, 12f).apply { on = tone?.on == true; contentDescription = "toneToggle"
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 84)
            setOnClickListener { val now = toggleTone(st); on = now; text = getString(if (now) R.string.lm_tone_on else R.string.lm_tone_off) } })
        box.addView(st)
        sectionTitle(box, getString(R.string.lm_level_title))
        box.addView(TextView(this).apply { text = getString(R.string.lm_level_help, p.getInt("splCal", 120)); setTextColor(dim); textSize = 11f; setPadding(0, 0, 0, 6) })
        lvlViews = listOf("lvlMaster", "lvlInput", "lvlSpl").map { t -> TextView(this).apply { tag = t; setTextColor(Color.WHITE); textSize = 18f; typeface = Typeface.MONOSPACE; setPadding(0, 4, 0, 4) }.also { box.addView(it) } }
        duckSection(box)
        loudSection(box)
        showLevels()
    }
    // ---- DUCKER · VOICE OVER MUSIC (see [PhoneDucker]): the CH inputs key, the decks go down
    internal fun duckRoles(): IntArray = PhoneDucker.roles(INPUTS, (0 until DECKS).map { deckStrip(it) }, STRIPS)
    private var duckNow: TextView? = null
    private fun duckSection(box: LinearLayout) {
        val p = prefs(); sectionTitle(box, getString(R.string.lm_duck_title))
        box.addView(ConsoleUi.moreLine(TextView(this).apply { text = getString(R.string.lm_duck_help); setTextColor(dim); textSize = 11f; setPadding(0, 0, 0, 6) }))
        box.addView(PadButton(this, getString(if (PhoneDucker.load(p).on) R.string.lm_duck_on else R.string.lm_duck_off), ConsoleUi.GREEN, 12f).apply { on = PhoneDucker.load(p).on; contentDescription = "duckToggle"
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 84)
            setOnClickListener { val v = PhoneDucker.load(p).let { it.copy(on = !it.on) }; PhoneDucker.save(p, v); PhoneDucker.apply(engine, v, duckRoles()); on = v.on; text = getString(if (v.on) R.string.lm_duck_on else R.string.lm_duck_off) } })
        for ((k, from, to) in PhoneDucker.controls) {
            val label = TextView(this).apply { setTextColor(Color.WHITE); textSize = 12f }
            fun show(x: Double) { label.text = getString(when (k) { "thresholdDb" -> R.string.lm_duck_thresholdDb; "depthDb" -> R.string.lm_duck_depthDb; "attackMs" -> R.string.lm_duck_attackMs; "holdMs" -> R.string.lm_duck_holdMs; "releaseMs" -> R.string.lm_duck_releaseMs; else -> R.string.lm_duck_liftDb }, x) }
            val now = PhoneDucker.get(PhoneDucker.load(p), k); show(now); box.addView(label)
            box.addView(SeekBar(this).apply { contentDescription = "duck_$k"; max = 1000; progress = ((now - from) / (to - from) * 1000).toInt()
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(sb: SeekBar, v: Int, user: Boolean) { if (!user) return; val x = from + (to - from) * v / 1000.0; val nv = PhoneDucker.with(PhoneDucker.load(p), k, x); PhoneDucker.save(p, nv); PhoneDucker.apply(engine, nv, duckRoles()); show(PhoneDucker.get(nv, k)) }
                    override fun onStartTrackingTouch(sb: SeekBar) {}; override fun onStopTrackingTouch(sb: SeekBar) {} }) })
        }
        duckNow = TextView(this).apply { tag = "duckNow"; setTextColor(cyan); textSize = 14f; typeface = Typeface.MONOSPACE; setPadding(0, 4, 0, 8) }.also { box.addView(it) }
    }
    // ---- LOUDNESS MANAGER (see [PhoneLoudness]): AUTO FADER and AUTO ON/OFF per channel, the master target
    internal val riders = HashMap<Int, PhoneLoudness.Rider>()
    internal val lmOffset = HashMap<Int, Double>()
    internal val masterRider = PhoneLoudness.MasterRider()
    private var loudNow: TextView? = null
    private val loudKeys = HashMap<String, PadButton>()
    private fun lmTouch(ch: Int) { val r = riders[ch] ?: return; if (!PhoneLoudness.rideOn(prefs(), ch)) return; r.touch(); lmOffset[ch] = r.gateDb }
    /** one meter tick: every managed channel's ride / on-off from its input peak (before the fader), the master target from the master's peak */
    internal fun loudTick(dt: Double, inPeaks: FloatArray? = runCatching { engine?.inputPeaks() }.getOrNull(), masterPeak: Double? = null) {
        val p = prefs(); val v = PhoneLoudness.load(p)
        for (ch in dcaChannels()) {
            val ride = PhoneLoudness.rideOn(p, ch); val gate = PhoneLoudness.gateOn(p, ch)
            if (!ride && !gate && (lmOffset[ch] ?: 0.0) == 0.0) { riders.remove(ch); continue }
            val r = riders.getOrPut(ch) { PhoneLoudness.Rider() }
            val pk = inPeaks?.let { a -> chStrips(ch).maxOf { a.getOrElse(it) { 0f } }.toDouble() } ?: 0.0
            r.feed(pk, dt); val (db, mu) = channelLevel(p, ch)
            val off = r.step(dt, db, mu, ride, gate, v)
            if (lmOffset[ch] == null || kotlin.math.abs(off - (lmOffset[ch] ?: 0.0)) > 0.01) {
                lmOffset[ch] = off; applyLevel(engine, ch)
                val user = if (ch >= 100) p.getFloat("deckDb${ch - 100}", 0f).toDouble() else p.getFloat("db$ch", 0f).toDouble()
                faders[ch]?.progress = (com.amfb.amixos.remote.DaemonClient.dbToPos(if (user <= -60) -120.0 else minOf(PhoneLoudness.MAX_DB, user + r.rideDb)) * 1000).toInt()   // the fader moves with the ride
            }
        }
        val mp = masterPeak ?: loop?.lastPeaks?.let { maxOf(it.getOrElse(STRIPS) { 0f }, it.getOrElse(STRIPS + 1) { 0f }).toDouble() } ?: 0.0
        val before = masterRider.gainDb; val g = masterRider.step(mp, dt, v)
        if (kotlin.math.abs(g - before) > 0.005 || (!v.masterOn && before != 0.0)) engine?.setMaster(masterOut(p, g))
    }
    private fun loudSection(box: LinearLayout) {
        val p = prefs(); sectionTitle(box, getString(R.string.lm_loud_title)); loudKeys.clear()
        box.addView(ConsoleUi.moreLine(TextView(this).apply { text = getString(R.string.lm_loud_help); setTextColor(dim); textSize = 11f; setPadding(0, 0, 0, 6) }))
        box.addView(PadButton(this, getString(if (PhoneLoudness.load(p).masterOn) R.string.lm_loud_master_on else R.string.lm_loud_master_off), ConsoleUi.GREEN, 12f).apply { on = PhoneLoudness.load(p).masterOn; contentDescription = "loudMaster"
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 84)
            setOnClickListener { val nv = PhoneLoudness.load(p).let { it.copy(masterOn = !it.masterOn) }; PhoneLoudness.save(p, nv); if (nv.masterOn && !p.getBoolean("limiter", true)) { p.edit().putBoolean("limiter", true).apply(); engine?.setLimiter(true, -0.5, 100.0) }   // the limiter is the safety
                on = nv.masterOn; text = getString(if (nv.masterOn) R.string.lm_loud_master_on else R.string.lm_loud_master_off) } })
        val presets = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 6, 0, 6) }
        for ((name, t) in PhoneLoudness.presets) presets.addView(PadButton(this, getString(R.string.lm_loud_preset, name, t), cyan, 10f).apply { contentDescription = "loudPreset_$name"; on = PhoneLoudness.load(p).masterTarget == t
            layoutParams = LinearLayout.LayoutParams(0, 76, 1f).apply { setMargins(0, 0, 6, 0) }
            setOnClickListener { PhoneLoudness.save(p, PhoneLoudness.load(p).copy(masterTarget = t)); for ((k, b) in loudKeys) if (k.startsWith("preset_")) b.on = k == "preset_$name" } }.also { loudKeys["preset_$name"] = it })
        box.addView(presets)
        val all = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 6) }
        for ((k, t) in listOf("ride" to R.string.lm_loud_all_ride, "gate" to R.string.lm_loud_all_gate)) all.addView(PadButton(this, getString(t), if (k == "ride") cyan else ConsoleUi.AMBER, 11f).apply { contentDescription = "loudAll_$k"
            on = dcaChannels().all { if (k == "ride") PhoneLoudness.rideOn(p, it) else PhoneLoudness.gateOn(p, it) }
            layoutParams = LinearLayout.LayoutParams(0, 80, 1f).apply { setMargins(0, 0, 6, 0) }
            setOnClickListener { val nv = !on; for (ch in dcaChannels()) { if (k == "ride") PhoneLoudness.setRide(p, ch, nv) else PhoneLoudness.setGate(p, ch, nv); loudKeys["${k}_$ch"]?.on = nv }; on = nv } })
        box.addView(all)
        for ((k, from, to) in PhoneLoudness.controls) {
            val label = TextView(this).apply { setTextColor(Color.WHITE); textSize = 12f }
            fun show(x: Double) { label.text = getString(when (k) { "target" -> R.string.lm_loud_target; "rangeDb" -> R.string.lm_loud_rangeDb; "gateDb" -> R.string.lm_loud_gateDb; else -> R.string.lm_loud_holdS }, x) }
            val now = PhoneLoudness.get(PhoneLoudness.load(p), k); show(now); box.addView(label)
            box.addView(SeekBar(this).apply { contentDescription = "loud_$k"; max = 1000; progress = ((now - from) / (to - from) * 1000).toInt()
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(sb: SeekBar, v: Int, user: Boolean) { if (!user) return; val x = from + (to - from) * v / 1000.0; val nv = PhoneLoudness.with(PhoneLoudness.load(p), k, x); PhoneLoudness.save(p, nv); show(PhoneLoudness.get(nv, k)) }
                    override fun onStartTrackingTouch(sb: SeekBar) {}; override fun onStopTrackingTouch(sb: SeekBar) {} }) })
        }
        for (ch in dcaChannels()) {
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, 2, 0, 2) }
            row.addView(TextView(this).apply { text = chName(ch); setTextColor(Color.WHITE); textSize = 11f; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.2f) })
            for ((k, t) in listOf("ride" to R.string.lm_loud_ride, "gate" to R.string.lm_loud_gate)) row.addView(PadButton(this, getString(t), if (k == "ride") cyan else ConsoleUi.AMBER, 10f).apply { contentDescription = "loud_${k}_$ch"
                on = if (k == "ride") PhoneLoudness.rideOn(p, ch) else PhoneLoudness.gateOn(p, ch); layoutParams = LinearLayout.LayoutParams(0, 70, 1f).apply { setMargins(4, 0, 0, 0) }
                setOnClickListener { val nv = !on; if (k == "ride") PhoneLoudness.setRide(p, ch, nv) else PhoneLoudness.setGate(p, ch, nv); on = nv } }.also { loudKeys["${k}_$ch"] = it })
            box.addView(row)
        }
        loudNow = TextView(this).apply { tag = "loudNow"; setTextColor(cyan); textSize = 14f; typeface = Typeface.MONOSPACE; setPadding(0, 4, 0, 8) }.also { box.addView(it) }
    }
    private fun tuneTone() { tone?.gen?.let { g -> synchronized(g) { g.kind = toneKind; g.levelDb = toneDb } } }
    /** TONE ON / OFF; true = it is now on. It needs the running mixer and DECK D free. */
    internal fun toggleTone(status: TextView): Boolean {
        if (tone?.on == true) { stopTone(); status.text = ""; return false }
        val e = engine; if (e == null || loop == null) { status.text = getString(R.string.lm_tone_need_start); return false }
        if (decks.getOrNull(MixerTools.TONE_DECK)?.playing == true) { status.text = getString(R.string.lm_tone_deck_busy, deckName(MixerTools.TONE_DECK)); return false }
        val t = tone ?: MixerTools.ToneFeeder(e, MixerTools.TONE_DECK, deckStrip(MixerTools.TONE_DECK), ::mixRate).also { tone = it }
        tuneTone(); t.start(); status.text = ""; return true
    }
    private fun stopTone() { tone?.stop() }
    private fun showLevels() { if (lvlViews.size < 3) return; val l = loop; val pk = l?.lastPeaks; val inPk = l?.lastInPeaks?.getOrNull(0) ?: 0f
        val master = pk?.let { maxOf(it.getOrElse(STRIPS) { 0f }, it.getOrElse(STRIPS + 1) { 0f }) } ?: 0f
        lvlViews[0].text = getString(R.string.lm_level_master, MixerTools.fmt(MixerTools.dbfs(master), "dBFS"))
        lvlViews[1].text = getString(R.string.lm_level_input, MixerTools.fmt(MixerTools.dbfs(inPk), "dBFS"))
        lvlViews[2].text = getString(R.string.lm_level_spl, MixerTools.fmt(MixerTools.spl(inPk, prefs().getInt("splCal", 120)), "dB SPL"))
        duckNow?.text = getString(R.string.lm_duck_now, PhoneDucker.reductionDb(engine, PhoneDucker.load(prefs())))
        loudNow?.text = getString(R.string.lm_loud_now, MixerTools.fmt(masterRider.shortTerm, "").trim(), masterRider.gainDb) }

    private fun deckStripView(d: Int): View {
        val p = prefs(); val color = ConsoleUi.palette[(d + 3) % ConsoleUi.palette.size]; val l = deckStrip(d)
        val col = ConsoleUi.strip(this, color, 0, false).apply { layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f).apply { setMargins(4, 0, 4, 0) } }
        col.addView(TextView(this).apply { text = getString(R.string.lm_deck, deckName(d)); setTextColor(Color.WHITE); textSize = 14f; setTypeface(null, Typeface.BOLD); gravity = Gravity.CENTER })
        if (!land) col.addView(TextView(this).apply { text = getString(R.string.lm_sub_deck); setTextColor(dim); textSize = 9f; letterSpacing = 0.1f; gravity = Gravity.CENTER; setPadding(0, 0, 0, 8) })
        col.addView(key(PadButton(this, fxLabel(l), ConsoleUi.VIOLET, 12f)).apply { on = rack(l).any { it.isNotEmpty() }; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, keyH()).apply { setMargins(6, 4, 6, 4) }
            setOnClickListener { showRack(l) } }.also { fxBtns[l] = it })   // the deck's own rack: the same effects on its left and its right
        col.addView(key(eqKey(100 + d)).apply { layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, keyH()).apply { setMargins(6, 4, 6, 4) } })
        col.addView(key(PadButton(this, getString(R.string.lm_mute), ConsoleUi.RED, 12f)).apply { on = p.getBoolean("deckMute$d", false); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, keyH()).apply { setMargins(6, 4, 6, 4) }
            contentDescription = "deckMute_$d"; setOnClickListener { val v = !p.getBoolean("deckMute$d", false); p.edit().putBoolean("deckMute$d", v).apply(); applyLevel(engine, 100 + d); on = v } })
        col.addView(key(soloKeyView(100 + d)).apply { layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, keyH()).apply { setMargins(6, 4, 6, 4) } })
        pairKeys(col)
        val readout = ConsoleUi.dbReadout(this); fun show(db: Double) { readout.text = com.amfb.amixos.remote.DaemonClient.fmtDb(db) }
        val db0 = p.getFloat("deckDb$d", 0f).toDouble(); show(if (db0 <= -60) -120.0 else db0)
        col.addView(FaderView(this, color).apply { progress = (com.amfb.amixos.remote.DaemonClient.dbToPos(if (db0 <= -60) -120.0 else db0) * 1000).toInt()
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
            onMove = { pos -> val db = com.amfb.amixos.remote.DaemonClient.posToDb(pos / 1000.0); val set = if (db <= -60) -60.0 else db; p.edit().putFloat("deckDb$d", set.toFloat()).apply(); show(db); lmTouch(100 + d); applyLevel(engine, 100 + d) } }.also { faders[100 + d] = it })
        col.addView(readout); return col
    }

    // ---- MASTER RECORDER: the master as it is heard, to FLAC 24-bit in the recorder's folder (TOOLS → RECORDER lists it)
    private fun toggleRec() { if (recWriter != null) stopRec() else startRec() }
    private fun startRec() {
        val e = engine ?: return; if (loop == null) startAudio()
        val dir = java.io.File(getExternalFilesDir(null) ?: filesDir, "recordings").apply { mkdirs() }
        val f = java.io.File(dir, "mix_" + java.text.SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", java.util.Locale.US).format(java.util.Date()) + ".flac")
        val rateNow = mixRate().toInt(); val w = runCatching { HiRes.open(f.path, 2, rateNow, "flac24", "tpdf") }.getOrNull()
        if (w == null) { status.text = getString(R.string.rec_cannot_write, runCatching { HiRes.lastError() }.getOrDefault("")); return }
        recWriter = w; recFile = f; e.recArm(true)
        if (prefs().getBoolean("multitrack", false)) startMultitrack(e, dir, f.nameWithoutExtension, rateNow)
        recThread = kotlin.concurrent.thread(name = "amix-master-rec", isDaemon = true) { val buf = FloatArray(2 * 4096)
            while (recWriter != null) { val n = e.recPop(buf, 4096); if (n > 0) recWriter?.write(buf, n) else Thread.sleep(10) } }
        recBtn.text = getString(R.string.lm_rec_stop); recBtn.on = true; window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }
    private fun stopRec() {
        val w = recWriter ?: return; engine?.recArm(false); recWriter = null; recThread?.join(1000); recThread = null; val tracks = stopMultitrack()
        val buf = FloatArray(2 * 4096); engine?.let { e -> while (true) { val n = e.recPop(buf, 4096); if (n <= 0) break; w.write(buf, n) } }   // what is left
        val dropped = engine?.recDropped() ?: 0L; w.finish()
        if (::recBtn.isInitialized) { recBtn.text = getString(R.string.lm_rec); recBtn.on = false }; window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        status.text = getString(R.string.lm_rec_saved, recFile?.name.orEmpty()) + (if (tracks > 0) " · " + getString(R.string.lm_multi_saved, tracks) else "") + if (dropped > 0) " · " + getString(R.string.lm_rec_dropped, dropped) else ""
    }
    private fun startMultitrack(e: Engine, dir: java.io.File, base: String, rate: Int) {
        val ws = trackPlan(inChannels()).mapNotNull { (strips, name) -> runCatching { HiRes.open(java.io.File(dir, base + "_" + name + ".flac").path, strips.size, rate, "flac24", "tpdf") }.getOrNull()?.let { strips to it } }
        if (ws.isEmpty()) return
        multiWriters = ws; e.multiArm(true)
        multiThread = kotlin.concurrent.thread(name = "amix-multitrack", isDaemon = true) {
            android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_BACKGROUND)   // the tracks are written at low priority: the sound never waits for them
            val n = STRIPS; val buf = FloatArray(n * 4096); val one = FloatArray(2 * 4096)
            fun drain(): Int { val got = e.multiPop(buf, n, 4096); if (got > 0) for ((strips, w) in multiWriters) { val c = strips.size
                for (i in 0 until got) for (k in 0 until c) one[i * c + k] = buf[i * n + strips[k]]
                w.write(one, got) }; return got }
            while (multiWriters.isNotEmpty()) { if (drain() <= 0) Thread.sleep(10) }
        }
    }
    private fun stopMultitrack(): Int {
        val ws = multiWriters; if (ws.isEmpty()) return 0
        engine?.multiArm(false); val t = multiThread; multiWriters = emptyList(); t?.join(1000); multiThread = null
        engine?.let { e -> val n = STRIPS; val buf = FloatArray(n * 4096); val one = FloatArray(2 * 4096)
            while (true) { val got = e.multiPop(buf, n, 4096); if (got <= 0) break; for ((strips, w) in ws) { val c = strips.size; for (i in 0 until got) for (k in 0 until c) one[i * c + k] = buf[i * n + strips[k]]; w.write(one, got) } } }
        ws.forEach { it.second.finish() }; return ws.size
    }
}
