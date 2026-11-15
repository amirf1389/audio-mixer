package com.amfb.amixos

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Button
import android.widget.ProgressBar
import android.widget.SeekBar
import android.widget.TextView
import android.widget.ToggleButton
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

/** THIS PHONE: a one-strip mixer on the phone itself: microphone in, fader, pan, mute, master limiter, peak meters. The engine is the same C++ as the desktop console. */
class LocalMixerActivity : AppCompatActivity() {
    private val engine by lazy { Engine(strips = 2, sampleRate = Prefs.localRate(this).toDouble()) }
    private var loop: AudioLoop? = null
    private val ui = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Engine.setDirs(filesDir.absolutePath, cacheDir.absolutePath, cacheDir.absolutePath)   // data in the app's files, everything else in its cache (cleared by Android when space is short)
        setContentView(Nav.wrap(this, layoutInflater.inflate(R.layout.activity_main, null), null))
        findViewById<TextView>(R.id.version).text = getString(R.string.engine_version, Engine.version())
        engine.setLimiter(true, -0.5, 100.0)
        findViewById<SeekBar>(R.id.fader).apply { max = 700; progress = 600   // -60 .. +10 dB in 0.1 dB
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar, p: Int, fromUser: Boolean) { val db = -60.0 + p / 10.0; engine.setGain(0, db); findViewById<TextView>(R.id.faderDb).text = if (db <= -60) "-inf" else String.format("%+.1f dB", db) }
                override fun onStartTrackingTouch(s: SeekBar) {}
                override fun onStopTrackingTouch(s: SeekBar) {}
            }) }
        findViewById<SeekBar>(R.id.pan).apply { max = 200; progress = 100
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar, p: Int, fromUser: Boolean) { engine.setPan(0, (p - 100) / 100.0) }
                override fun onStartTrackingTouch(s: SeekBar) {}
                override fun onStopTrackingTouch(s: SeekBar) {}
            }) }
        findViewById<ToggleButton>(R.id.mute).setOnCheckedChangeListener { _, on -> engine.setMute(0, on) }
        findViewById<Button>(R.id.start).setOnClickListener { if (loop == null) startAudio() else stopAudio() }
        ui.post(object : Runnable { override fun run() { showMeters(); ui.postDelayed(this, 50) } })
    }

    private fun startAudio() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) { ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.RECORD_AUDIO), 1); return }
        loop = AudioLoop(engine, Prefs.localRate(this), Prefs.localFrames(this), Prefs.localOutDevice(this), Prefs.localInDevice(this)).also { it.start() }
        findViewById<Button>(R.id.start).text = getString(R.string.stop)
    }
    private fun stopAudio() { loop?.stop(); loop = null; findViewById<Button>(R.id.start).text = getString(R.string.start) }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 1 && grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) startAudio()
    }

    private fun showMeters() {
        val p = loop?.lastPeaks ?: return
        fun db(v: Float) = if (v > 1e-6f) 20 * Math.log10(v.toDouble()) else -120.0
        findViewById<ProgressBar>(R.id.meterIn).progress = ((db(p[0]) + 60) / 60 * 1000).toInt().coerceIn(0, 1000)
        findViewById<ProgressBar>(R.id.meterL).progress = ((db(p[engine.strips]) + 60) / 60 * 1000).toInt().coerceIn(0, 1000)
        findViewById<ProgressBar>(R.id.meterR).progress = ((db(p[engine.strips + 1]) + 60) / 60 * 1000).toInt().coerceIn(0, 1000)
    }

    override fun onDestroy() { stopAudio(); engine.close(); super.onDestroy() }
}
