package com.amfb.amixos

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import kotlin.concurrent.thread
import kotlin.math.abs

/** A needle from -50 to +50 cents (the tuner) or a bar (the level meter). */
class NeedleView(ctx: android.content.Context) : View(ctx) {
    var cents = 0; var active = false
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    override fun onMeasure(w: Int, h: Int) { val wd = MeasureSpec.getSize(w); setMeasuredDimension(wd, (wd * 0.32).toInt()) }
    override fun onDraw(c: Canvas) {
        val w = width.toFloat(); val h = height.toFloat(); c.drawColor(0xFF0B0F15.toInt())
        p.strokeWidth = 3f
        for (k in -50..50 step 10) { val x = w / 2 + k / 50f * (w / 2 - 30); p.color = if (k == 0) 0xFF34D399.toInt() else 0xFF3A4656.toInt(); c.drawLine(x, h * 0.25f, x, h * 0.75f, p) }
        if (!active) return
        val x = w / 2 + cents.coerceIn(-50, 50) / 50f * (w / 2 - 30)
        p.color = if (abs(cents) <= 5) 0xFF34D399.toInt() else if (abs(cents) <= 15) 0xFFFBBF24.toInt() else 0xFFEF4444.toInt(); p.strokeWidth = 10f
        c.drawLine(x, h * 0.1f, x, h * 0.9f, p)
    }
}

/** TOOLS on the phone itself (no console needed): TUNER (YIN pitch from the microphone), SOUND LEVEL (the microphone's level with a
 *  calibration offset, slow / fast, max hold) and SIGNAL GENERATOR (sine, pink, white, sweep through the phone's output, for checking a PA). */
class ToolsActivity : AppCompatActivity() {
    private val ui = Handler(Looper.getMainLooper())
    private lateinit var body: LinearLayout; private val tabs = ArrayList<Button>()
    private val accent = Color.parseColor("#EA580C"); private val cyan = Color.parseColor("#00F2FF"); private val dim = Color.parseColor("#8B98A8")
    var tool = 0; private set
    @Volatile private var micOn = false; private var micThread: Thread? = null
    @Volatile private var genOn = false; private var genThread: Thread? = null
    private val gen = SignalMath.Generator(48000)
    // what the screens show (set by the microphone thread, or by a test)
    private lateinit var big: TextView; private lateinit var small: TextView; private var needle: NeedleView? = null
    private var slowDb = -120.0; private var maxDb = -120.0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        root.addView(TextView(this).apply { text = getString(R.string.tools_title); setTextColor(accent); textSize = 20f; setTypeface(null, Typeface.BOLD) })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        listOf(R.string.tools_tuner, R.string.tools_spl, R.string.tools_gen).forEachIndexed { i, r -> row.addView(Button(this).apply { text = getString(r); setOnClickListener { show(i) } }.also { tabs.add(it) }) }
        root.addView(row)
        body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, 16, 0, 0) }; root.addView(body)
        setContentView(ScrollView(this).apply { setBackgroundColor(Color.parseColor("#0F141C")); addView(root) })
        show(intent.getIntExtra("tool", 0))
    }

    fun show(i: Int) {
        stopMic(); stopGen(); tool = i; body.removeAllViews(); needle = null
        tabs.forEachIndexed { k, b -> b.setTextColor(if (k == i) accent else Color.WHITE) }
        big = TextView(this).apply { setTextColor(Color.WHITE); textSize = 64f; typeface = Typeface.DEFAULT_BOLD; gravity = Gravity.CENTER; text = "—" }
        small = TextView(this).apply { setTextColor(cyan); textSize = 16f; typeface = Typeface.MONOSPACE; gravity = Gravity.CENTER }
        when (i) {
            0 -> { body.addView(big); needle = NeedleView(this).also { body.addView(it) }; body.addView(small); body.addView(note(getString(R.string.tools_tuner_note))); startMic() }
            1 -> { body.addView(big); body.addView(small)
                val p = Prefs.local(this); val cal = TextView(this).apply { setTextColor(dim) }
                fun calText() { cal.text = getString(R.string.tools_cal, p.getInt("splCal", 120)) }; calText(); body.addView(cal)
                body.addView(SeekBar(this).apply { max = 60; progress = p.getInt("splCal", 120) - 90; setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(s: SeekBar, v: Int, u: Boolean) { if (u) { p.edit().putInt("splCal", v + 90).apply(); calText() } }
                    override fun onStartTrackingTouch(s: SeekBar) {}; override fun onStopTrackingTouch(s: SeekBar) {} }) })
                body.addView(Button(this).apply { text = getString(R.string.tools_reset_max); setOnClickListener { maxDb = -120.0 } })
                body.addView(note(getString(R.string.tools_spl_note))); startMic() }
            else -> {
                val kinds = listOf("sine", "pink", "white", "sweep"); val kr = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                val kb = ArrayList<Button>()
                kinds.forEach { k -> kr.addView(Button(this).apply { text = k.uppercase(); setOnClickListener { gen.kind = k; kb.forEach { it.setTextColor(Color.WHITE) }; setTextColor(accent); genLabel() } }.also { kb.add(it) }) }
                kb[kinds.indexOf(gen.kind)].setTextColor(accent)
                body.addView(kr); body.addView(big); body.addView(small)
                fun bar(label: String, max: Int, start: Int, f: (Int) -> Unit) { body.addView(TextView(this).apply { text = label; setTextColor(dim) })
                    body.addView(SeekBar(this).apply { this.max = max; progress = start; setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(s: SeekBar, v: Int, u: Boolean) { if (u) { f(v); genLabel() } }
                        override fun onStartTrackingTouch(s: SeekBar) {}; override fun onStopTrackingTouch(s: SeekBar) {} }) }) }
                bar(getString(R.string.tools_freq), 1000, (Math.log(gen.freq / 20) / Math.log(1000.0) * 1000).toInt()) { gen.freq = 20 * Math.pow(1000.0, it / 1000.0) }
                bar(getString(R.string.tools_level), 600, ((gen.levelDb + 60) * 10).toInt()) { gen.levelDb = it / 10.0 - 60 }
                val play = Button(this).apply { text = getString(R.string.tools_play) }
                play.setOnClickListener { if (genOn) { stopGen(); play.text = getString(R.string.tools_play) } else { startGen(); play.text = getString(R.string.tools_stop) } }
                body.addView(play); body.addView(note(getString(R.string.tools_gen_note))); genLabel() }
        }
    }
    private fun note(t: String) = TextView(this).apply { text = t; setTextColor(dim); textSize = 12f; setPadding(0, 16, 0, 0) }
    private fun genLabel() { big.textSize = 44f
        big.text = when (gen.kind) { "sine" -> if (gen.freq >= 1000) String.format("%.2f kHz", gen.freq / 1000) else String.format("%.0f Hz", gen.freq); "sweep" -> "20 Hz → 20 kHz"; else -> gen.kind.uppercase() + " NOISE" }
        small.text = String.format("%.1f dBFS", gen.levelDb) }

    /** Shows one analysed block: called by the microphone thread (and by the screenshot test with made-up values). */
    fun showReading(db: Double, hz: Double) {
        when (tool) {
            0 -> { if (hz > 0 && db > -60) { val (n, c) = AudioMath.noteOf(hz); big.text = n; small.text = String.format("%.1f Hz   %+d cents", hz, c); needle?.apply { cents = c; active = true; invalidate() } }
                   else { small.text = getString(R.string.tools_play_note); needle?.apply { active = false; invalidate() } } }
            1 -> { val spl = db + Prefs.local(this).getInt("splCal", 120)   // 0 dBFS = the calibration value (phone microphones: about 120 dB SPL)
                slowDb = if (slowDb < -100) spl else slowDb + (spl - slowDb) * 0.15; maxDb = maxOf(maxDb, spl)
                big.text = String.format("%.0f", slowDb); small.text = String.format("dB SPL (approx.)   fast %.1f   max %.1f", spl, maxDb) }
        }
    }
    private fun startMic() {
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) { requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 7); small.text = getString(R.string.tools_need_mic); return }
        if (micOn) return; micOn = true
        micThread = thread(name = "amix-tools-mic") {
            val rate = 48000; val n = 4096
            val rec = runCatching { AudioRecord(MediaRecorder.AudioSource.UNPROCESSED, rate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT, maxOf(n * 4, AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT))) }.getOrNull()
            if (rec == null || rec.state != AudioRecord.STATE_INITIALIZED) { micOn = false; ui.post { small.text = getString(R.string.tools_no_mic) }; return@thread }
            val buf = FloatArray(n); rec.startRecording()
            try { while (micOn) { var got = 0; while (got < n && micOn) { val r = rec.read(buf, got, n - got, AudioRecord.READ_BLOCKING); if (r <= 0) break; got += r }
                    val db = SignalMath.rmsDb(buf); val hz = if (tool == 0) SignalMath.pitchHz(buf, rate) else 0.0; ui.post { if (micOn) showReading(db, hz) } } }
            finally { rec.stop(); rec.release() }
        }
    }
    override fun onRequestPermissionsResult(code: Int, p: Array<out String>, r: IntArray) { super.onRequestPermissionsResult(code, p, r); if (code == 7 && r.firstOrNull() == PackageManager.PERMISSION_GRANTED && tool < 2) startMic() }
    private fun stopMic() { micOn = false; micThread?.join(500); micThread = null }
    private fun startGen() {
        if (genOn) return; genOn = true
        genThread = thread(name = "amix-tools-gen") {
            val rate = 48000; val n = 1024
            val track = AudioTrack.Builder().setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setBufferSizeInBytes(n * 4 * 4).build()
            val buf = FloatArray(n); track.play()
            try { while (genOn) { gen.fill(buf); track.write(buf, 0, n, AudioTrack.WRITE_BLOCKING) } } finally { track.stop(); track.release() }
        }
    }
    private fun stopGen() { genOn = false; genThread?.join(500); genThread = null }
    override fun onPause() { super.onPause(); stopMic(); stopGen() }
    override fun onResume() { super.onResume(); if (::body.isInitialized && tool < 2 && !micOn && body.childCount > 0) startMic() }
}
