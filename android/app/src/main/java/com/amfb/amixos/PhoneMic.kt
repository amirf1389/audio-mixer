package com.amfb.amixos

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.net.Uri
import android.provider.Settings
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import java.io.IOException
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.TimeUnit

/** PHONE MIC link logic, free of Android audio so it can be tested: chunks in, the console's answers out.
 *  [deliver] sends one chunk (muted: silence, so the console's strip stays live and nothing clicks). A refused or lost chunk moves to
 *  RECONNECTING: chunks are dropped (never queued up: talkback must stay current) and a retry goes out after a short wait that grows
 *  from 0.5 s to 4 s; a 409 (the console restarted, so its mic is off) turns the mic on again on the strip picked. 401 / 403 stop it:
 *  the pairing is no longer accepted. */
class MicSession(private val link: Link, private val strip: Int, private val now: () -> Long = { System.currentTimeMillis() }) {
    interface Link { fun on(strip: Int); fun off(); fun send(pcm: ByteArray, len: Int): Int }
    enum class State { IDLE, LIVE, RECONNECTING, REFUSED }
    @Volatile var state = State.IDLE; private set
    @Volatile var muted = false
    @Volatile var lastError = ""; private set
    @Volatile var sent = 0; private set
    private var nextTry = 0L; private var waitMs = 500L
    fun start() { link.on(strip); state = State.LIVE; waitMs = 500; nextTry = 0 }
    fun stop() { if (state == State.IDLE) return; state = State.IDLE; runCatching { link.off() } }
    /** one chunk from the capture thread (on the sender thread); returns true when the console took it */
    fun deliver(pcm: ByteArray, len: Int): Boolean {
        if (state == State.IDLE || state == State.REFUSED) return false
        if (state == State.RECONNECTING && now() < nextTry) return false
        if (muted) java.util.Arrays.fill(pcm, 0, len, 0)
        val code = try { link.send(pcm, len) } catch (e: IOException) { lastError = e.message ?: e.toString(); -1 }
        when (code) {
            200 -> { state = State.LIVE; waitMs = 500; sent++; return true }
            401, 403 -> { state = State.REFUSED; lastError = "HTTP $code"; return false }
            409 -> { runCatching { link.on(strip) }.onFailure { lastError = it.message ?: "" }; backOff() }
            else -> { if (code > 0) lastError = "HTTP $code"; backOff() }
        }
        return false
    }
    private fun backOff() { if (state == State.IDLE) return; state = State.RECONNECTING; nextTry = now() + waitMs; waitMs = (waitMs * 2).coerceAtMost(4000) }
    companion object {
        const val RATE = 48000; const val CHUNK = 1920   // 20 ms of 16-bit mono at 48 kHz, in samples: 960
        /** the peak of 16-bit little-endian samples, 0..1 */
        fun peak(pcm: ByteArray, len: Int): Double { var m = 0; var i = 0; while (i + 1 < len) { val v = (pcm[i].toInt() and 0xff) or (pcm[i + 1].toInt() shl 8); m = maxOf(m, kotlin.math.abs(v.toShort().toInt())); i += 2 }; return m / 32768.0 }
        /** what the permission answer means for the page: ask, or explain and offer the app's settings (after "don't ask again") */
        fun permissionStep(granted: Boolean, askedBefore: Boolean, rationale: Boolean): String = when { granted -> "ok"; !askedBefore || rationale -> "ask"; else -> "settings" }
    }
}

/** PHONE MIC: this phone as a microphone / talkback into the console. Pick the channel, START: the phone's microphone goes to that
 *  channel of the console (it reads the phone instead of its own input until STOP). MUTE sends silence, the meter shows the level,
 *  a lost connection comes back by itself. Needs the microphone permission (asked on START, explained when refused) and a pairing. */
class PhoneMicActivity : ConsoleSection() {
    override val titleRes = R.string.mic_title
    internal var strip = 0; internal var session: MicSession? = null
    private var rec: AudioRecord? = null; @Volatile private var running = false
    private var capture: Thread? = null; private var sender: Thread? = null
    @Volatile internal var level = 0.0
    internal lateinit var startBtn: Button; internal lateinit var muteBtn: Button; internal lateinit var chBtn: Button; internal lateinit var stateText: TextView; private lateinit var meter: MeterBar
    private var askedMic = false

    override fun build() {
        strip = Prefs.remote(this).getInt("micStrip", 0)
        root.addView(label(getString(R.string.mic_note)))
        chBtn = Button(this).apply { contentDescription = "micStrip"; isAllCaps = false; setOnClickListener { pickStrip() } }
        root.addView(chBtn)
        meter = MeterBar(this).apply { contentDescription = "micMeter"; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, ConsoleUi.dp(this@PhoneMicActivity, 28f)).apply { setMargins(0, 16, 0, 16) } }
        root.addView(meter)
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        startBtn = Button(this).apply { contentDescription = "micStart"; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f); setOnClickListener { if (session == null) tryStart() else stopMic() } }
        muteBtn = Button(this).apply { contentDescription = "micMute"; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f); isEnabled = false
            setOnClickListener { session?.let { it.muted = !it.muted }; refresh() } }
        row.addView(startBtn); row.addView(muteBtn); root.addView(row)
        stateText = label("", dim, 13f).apply { contentDescription = "micState"; setPadding(0, 16, 0, 0) }; root.addView(stateText)
        refresh()
    }
    private fun pickStrip() {
        val n = Prefs.stripCount(this).coerceIn(1, 32)
        android.app.AlertDialog.Builder(this).setTitle(R.string.mic_channel_pick).setItems((1..n).map { getString(R.string.mic_channel, it) }.toTypedArray()) { _, w ->
            val wasOn = session != null; if (wasOn) stopMic()
            strip = w; Prefs.remote(this).edit().putInt("micStrip", w).apply(); refresh(); if (wasOn) tryStart() }.show()
    }
    internal fun refresh() {
        chBtn.text = getString(R.string.mic_channel_is, strip + 1)
        val s = session
        startBtn.text = getString(if (s == null) R.string.mic_start else R.string.mic_stop)
        muteBtn.isEnabled = s != null; muteBtn.text = getString(if (s?.muted == true) R.string.mic_unmute else R.string.mic_mute)
        muteBtn.setTextColor(if (s?.muted == true) Color.parseColor("#EF4444") else Color.WHITE)
        stateText.text = when {
            client == null -> getString(R.string.eq_offline)
            s == null -> getString(R.string.mic_idle)
            s.state == MicSession.State.REFUSED -> getString(R.string.mic_refused)
            s.state == MicSession.State.RECONNECTING -> getString(R.string.mic_reconnecting, s.lastError)
            s.muted -> getString(R.string.mic_muted, strip + 1)
            else -> getString(R.string.mic_live, strip + 1) }
        meter.set(if (s?.muted == true) 0.0 else level, if (s?.muted == true) 0.0 else level)
        if (s?.state == MicSession.State.REFUSED) stopMic()
    }
    internal fun tryStart() {
        if (client == null) { refresh(); return }
        val granted = checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
        when (MicSession.permissionStep(granted, askedMic, shouldShowRequestPermissionRationale(Manifest.permission.RECORD_AUDIO))) {
            "ok" -> startMic()
            "ask" -> { askedMic = true; requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 11) }
            else -> showDenied(true) }
    }
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != 11) return
        if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) startMic() else showDenied(!shouldShowRequestPermissionRationale(Manifest.permission.RECORD_AUDIO))
    }
    private fun showDenied(forever: Boolean) {
        stateText.text = getString(R.string.mic_denied)
        val b = android.app.AlertDialog.Builder(this).setTitle(R.string.mic_title).setMessage(if (forever) R.string.mic_denied_settings else R.string.mic_denied).setNegativeButton(android.R.string.cancel, null)
        if (forever) b.setPositiveButton(R.string.mic_open_settings) { _, _ -> runCatching { startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", packageName, null))) } }
        b.show()
    }
    private fun startMic() {
        val c = client ?: return
        val min = AudioRecord.getMinBufferSize(MicSession.RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val r = try { AudioRecord(MediaRecorder.AudioSource.MIC, MicSession.RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, maxOf(min, MicSession.CHUNK * 8)) } catch (e: SecurityException) { null } catch (e: IllegalArgumentException) { null }
        if (r == null || r.state != AudioRecord.STATE_INITIALIZED) { r?.release(); stateText.text = getString(R.string.tools_no_mic); return }
        val link = object : MicSession.Link {
            override fun on(strip: Int) { c.phoneMic(strip, true) }
            override fun off() { c.phoneMic(0, false) }
            override fun send(pcm: ByteArray, len: Int) = c.micChunk(pcm, len, MicSession.RATE)
        }
        val s = MicSession(link, strip); session = s; rec = r; running = true; refresh(); stateText.text = getString(R.string.mic_starting)
        val q = ArrayBlockingQueue<ByteArray>(8)
        sender = Thread({
            val ok = runCatching { s.start() }
            if (ok.isFailure) { ui.post { if (session === s) { stopMic(); stateText.text = getString(R.string.failed, ok.exceptionOrNull()?.message ?: "") } }; return@Thread }
            ui.post { refresh() }
            var last = MicSession.State.LIVE
            while (running) { val buf = runCatching { q.poll(200, TimeUnit.MILLISECONDS) }.getOrNull() ?: continue
                s.deliver(buf, buf.size); if (s.state != last) { last = s.state; ui.post { refresh() } } }
        }, "phone-mic-send").apply { isDaemon = true; start() }
        capture = Thread({
            runCatching { r.startRecording() }
            val bytes = MicSession.CHUNK; val shorts = ShortArray(bytes / 2)
            while (running) {
                var got = 0; while (got < shorts.size && running) { val k = r.read(shorts, got, shorts.size - got); if (k <= 0) break; got += k }
                if (got <= 0) { if (running) Thread.sleep(10); continue }
                val b = ByteArray(got * 2); for (i in 0 until got) { val v = shorts[i].toInt(); b[2 * i] = (v and 0xff).toByte(); b[2 * i + 1] = ((v shr 8) and 0xff).toByte() }
                val pk = MicSession.peak(b, b.size); level = maxOf(pk, level * 0.85)
                if (!q.offer(b)) { q.poll(); q.offer(b) }   // never fall behind: the oldest chunk goes
                ui.post(meterTick)
            }
        }, "phone-mic-capture").apply { isDaemon = true; start() }
    }
    private val meterTick = Runnable { if (session != null) meter.set(if (session?.muted == true) 0.0 else level, if (session?.muted == true) 0.0 else level) }
    internal fun stopMic() {
        val s = session ?: return; session = null; running = false
        val r = rec; rec = null; val cap = capture; capture = null; sender = null
        Thread({ runCatching { cap?.join(500) }; runCatching { r?.stop() }; runCatching { r?.release() }; s.stop() }, "phone-mic-stop").apply { isDaemon = true; start() }
        level = 0.0; refresh()
    }
    // Android does not let an app keep the microphone in the background without a foreground service: leaving the page stops it
    override fun onStop() { if (session != null && !isChangingConfigurations) { stopMic() }; super.onStop() }
    override fun onDestroy() { stopMic(); super.onDestroy() }
}
