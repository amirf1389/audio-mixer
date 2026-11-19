package com.amfb.amixos

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaPlayer
import android.media.MediaRecorder
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.concurrent.thread

/** HI-RES RECORDER on the phone itself: the microphone (or a USB interface, when Android routes one in) recorded unprocessed, in float,
 *  at the rate picked here, and written by the engine (HiRes: libFLAC / dr_wav) as FLAC 24-bit, WAV 24-bit or WAV 32-bit float. The files
 *  stay in the app's own folder (Android/data/…/files/recordings: no storage permission) and play back from the list. */
class RecorderActivity : FitActivity() {
    data class Take(val file: File, val label: String)
    private val ui = Handler(Looper.getMainLooper())
    private val accent = Color.parseColor("#FFB590"); private val cyan = Color.parseColor("#7FD8E8"); private val dim = Color.parseColor("#C2C7CF"); private val green = Color.parseColor("#34D399")
    private lateinit var big: TextView; private lateinit var small: TextView; private lateinit var recBtn: Button; private lateinit var list: LinearLayout
    private val fmtBtns = ArrayList<Button>(); private val rateBtns = ArrayList<Button>(); private val chBtns = ArrayList<Button>()
    @Volatile private var recording = false; private var worker: Thread? = null; private var player: MediaPlayer? = null; private var playing: File? = null
    private var askedMic = false
    companion object {
        val formats = listOf("flac24" to "FLAC 24-BIT", "wav24" to "WAV 24-BIT", "wavf32" to "WAV 32-BIT FLOAT")
        val rates = listOf(44100, 48000, 96000, 192000)
        fun extOf(format: String) = if (format.startsWith("flac")) "flac" else "wav"
        fun mmss(sec: Double): String { val s = sec.toInt(); return String.format("%02d:%02d", s / 60, s % 60) }
        fun sizeText(bytes: Long): String = if (bytes >= 1 shl 20) String.format("%.1f MB", bytes / 1048576.0) else String.format("%d kB", bytes / 1024)
        /** the highest of the wanted rates this phone's microphone path opens in float (44.1 / 48 kHz always do) */
        fun rateOpens(rate: Int, channels: Int): Boolean = AudioRecord.getMinBufferSize(rate, if (channels == 2) AudioFormat.CHANNEL_IN_STEREO else AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT) > 0
    }
    private fun prefs() = Prefs.local(this)
    private fun folder() = File(getExternalFilesDir(null) ?: filesDir, "recordings").apply { mkdirs() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        root.addView(TextView(this).apply { text = getString(R.string.rec_title); setTextColor(accent); textSize = 20f; setTypeface(null, Typeface.BOLD) })
        root.addView(TextView(this).apply { text = getString(R.string.rec_sub); setTextColor(dim); textSize = 12f; setPadding(0, 4, 0, 12) })
        fun row(label: Int, items: List<String>, into: ArrayList<Button>, pick: (Int) -> Unit) {
            root.addView(TextView(this).apply { text = getString(label); setTextColor(dim); textSize = 11f; letterSpacing = 0.15f })
            val r = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            items.forEachIndexed { i, t -> r.addView(Button(this).apply { text = t; textSize = 11f; setOnClickListener { if (!recording) { pick(i); mark() } } }.also { into.add(it) }) }
            root.addView(r)
        }
        row(R.string.rec_format, formats.map { it.second }, fmtBtns) { prefs().edit().putString("recFormat", formats[it].first).apply() }
        row(R.string.rec_rate, rates.map { if (it % 1000 == 0) "${it / 1000} kHz" else "44.1 kHz" }, rateBtns) { prefs().edit().putInt("recRate", rates[it]).apply() }
        row(R.string.rec_channels, listOf(getString(R.string.rec_mono), getString(R.string.rec_stereo)), chBtns) { prefs().edit().putInt("recChannels", it + 1).apply() }
        big = TextView(this).apply { setTextColor(Color.WHITE); textSize = 56f; typeface = Typeface.MONOSPACE; gravity = Gravity.CENTER; text = "00:00"; setPadding(0, 16, 0, 0) }
        small = TextView(this).apply { setTextColor(cyan); textSize = 13f; typeface = Typeface.MONOSPACE; gravity = Gravity.CENTER }
        recBtn = Button(this).apply { text = getString(R.string.rec_start); textSize = 16f; setOnClickListener { if (recording) stopRec() else startRec() } }
        root.addView(big); root.addView(small); root.addView(recBtn)
        root.addView(TextView(this).apply { text = getString(R.string.rec_takes); setTextColor(accent); textSize = 13f; letterSpacing = 0.15f; setTypeface(null, Typeface.BOLD); setPadding(0, 28, 0, 8) })
        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; root.addView(list)
        root.addView(TextView(this).apply { text = getString(R.string.rec_note); setTextColor(dim); textSize = 12f; setPadding(0, 16, 0, 0) })
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(Color.parseColor("#111418")); addView(root) }, null))
        mark(); refresh()
    }
    private fun format() = prefs().getString("recFormat", "flac24") ?: "flac24"
    private fun rate() = prefs().getInt("recRate", 48000)
    private fun channels() = prefs().getInt("recChannels", 1)
    private fun mark() {
        fmtBtns.forEachIndexed { i, b -> b.setTextColor(if (formats[i].first == format()) accent else Color.WHITE) }
        rateBtns.forEachIndexed { i, b -> b.setTextColor(if (rates[i] == rate()) accent else Color.WHITE) }
        chBtns.forEachIndexed { i, b -> b.setTextColor(if (i + 1 == channels()) accent else Color.WHITE) }
        if (!recording) small.text = getString(R.string.rec_ready)
    }

    /** The list of takes: newest first, each with what the engine reads from its header. Shown with made-up entries by the screenshot test. */
    fun showTakes(takes: List<Take>) {
        list.removeAllViews()
        if (takes.isEmpty()) { list.addView(TextView(this).apply { text = getString(R.string.rec_none); setTextColor(dim) }); return }
        for (t in takes) {
            val r = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, 6, 0, 6) }
            val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) }
            col.addView(TextView(this).apply { text = t.file.name; setTextColor(Color.WHITE); textSize = 13f; typeface = Typeface.MONOSPACE })
            col.addView(TextView(this).apply { text = t.label; setTextColor(dim); textSize = 11f })
            r.addView(col)
            r.addView(Button(this).apply { text = if (playing == t.file) getString(R.string.tools_stop) else getString(R.string.tools_play); textSize = 11f; setTextColor(if (playing == t.file) green else Color.WHITE); setOnClickListener { toggle(t.file) } })
            r.addView(Button(this).apply { text = "✕"; textSize = 11f; contentDescription = getString(R.string.rec_delete); setOnClickListener { confirmDelete(t.file) } })
            list.addView(r)
        }
    }
    private fun refresh() {
        val files = folder().listFiles { f -> f.isFile && (f.name.endsWith(".flac") || f.name.endsWith(".wav")) }?.sortedByDescending { it.lastModified() } ?: emptyList()
        showTakes(files.map { f -> val i = runCatching { HiRes.info(f.path) }.getOrNull(); Take(f, (if (i != null) i.label() + " · " + mmss(i.seconds) else "?") + " · " + sizeText(f.length())) })
    }
    private fun confirmDelete(f: File) { AlertDialog.Builder(this).setMessage(getString(R.string.rec_delete_ask, f.name)).setPositiveButton(R.string.rec_delete) { _, _ -> if (playing == f) stopPlay(); f.delete(); refresh() }.setNegativeButton(android.R.string.cancel, null).show() }

    private fun toggle(f: File) { if (playing == f) { stopPlay(); refresh(); return }
        stopPlay(); if (recording) return
        val p = MediaPlayer(); val ok = runCatching { p.setDataSource(f.path); p.setOnCompletionListener { stopPlay(); refresh() }; p.prepare(); p.start() }.isSuccess
        if (ok) { player = p; playing = f } else { p.release(); small.text = getString(R.string.rec_cannot_play) }
        refresh() }
    private fun stopPlay() { player?.let { runCatching { it.stop() }; it.release() }; player = null; playing = null }

    private fun startRec() {
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) { if (!askedMic) { askedMic = true; requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 9) }; small.text = getString(R.string.tools_need_mic); return }
        stopPlay(); val fmt = format(); val ch = channels(); var rate = rate()
        if (!rateOpens(rate, ch)) { rate = 48000; small.text = getString(R.string.rec_rate_fallback) }   // this phone's input does not open at that rate
        val file = File(folder(), SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(Date()) + "." + extOf(fmt))
        val mask = if (ch == 2) AudioFormat.CHANNEL_IN_STEREO else AudioFormat.CHANNEL_IN_MONO; val block = 4096
        val rec = runCatching { AudioRecord(MediaRecorder.AudioSource.UNPROCESSED, rate, mask, AudioFormat.ENCODING_PCM_FLOAT, maxOf(block * ch * 4 * 4, AudioRecord.getMinBufferSize(rate, mask, AudioFormat.ENCODING_PCM_FLOAT))) }.getOrNull()
        if (rec == null || rec.state != AudioRecord.STATE_INITIALIZED) { rec?.release(); small.text = getString(R.string.tools_no_mic); return }
        val w = runCatching { HiRes.open(file.path, ch, rate, fmt, "tpdf") }.getOrNull()
        if (w == null) { rec.release(); small.text = getString(R.string.rec_cannot_write, runCatching { HiRes.lastError() }.getOrDefault("")); return }
        recording = true; recBtn.text = getString(R.string.rec_stop); recBtn.setTextColor(Color.parseColor("#EF4444")); window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        worker = thread(name = "amix-recorder") {
            val buf = FloatArray(block * ch); var frames = 0L; var ok = true; var last = 0L; rec.startRecording()
            try { while (recording && ok) { val got = rec.read(buf, 0, buf.size, AudioRecord.READ_BLOCKING); if (got <= 0) break
                    val n = got / ch; ok = w.write(buf, n); frames += n
                    val now = System.currentTimeMillis(); if (now - last > 200) { last = now; var pk = 0f; for (i in 0 until got) pk = maxOf(pk, Math.abs(buf[i])); val sec = frames.toDouble() / rate
                        ui.post { if (recording) { big.text = mmss(sec); small.text = String.format("%s   peak %.1f dBFS%s", file.name, if (pk > 1e-6f) 20 * Math.log10(pk.toDouble()) else -120.0, if (pk >= 1f) "   CLIP" else "") } } } } }
            finally { rec.stop(); rec.release(); val closed = w.finish()
                ui.post { recording = false; recBtn.text = getString(R.string.rec_start); recBtn.setTextColor(Color.WHITE); window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    small.text = if (ok && closed) getString(R.string.rec_saved, file.name) else getString(R.string.rec_cannot_write, runCatching { HiRes.lastError() }.getOrDefault("")); refresh() } }
        }
    }
    private fun stopRec() { recording = false; worker?.join(1500); worker = null }
    override fun onRequestPermissionsResult(code: Int, p: Array<out String>, r: IntArray) { super.onRequestPermissionsResult(code, p, r); if (code == 9 && r.firstOrNull() == PackageManager.PERMISSION_GRANTED) startRec() }
    override fun onPause() { super.onPause(); stopPlay() }   // a recording carries on while the screen is on; leaving the app's foreground keeps the thread until STOP
    override fun onDestroy() { stopRec(); stopPlay(); super.onDestroy() }
}
