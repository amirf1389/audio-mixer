package com.amfb.amixos

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder

/** 1.27 OTHER APPS' AUDIO on a player strip of the phone's mixer. It uses Android's own playback capture (AudioPlaybackCaptureConfiguration,
 *  Android 10+). The user agrees through the system MediaProjection prompt, and only apps that allow capture are heard. The sound is
 *  mixed live into the strip's player lane and is never recorded or saved. A foreground service carries the capture, as Android
 *  requires, and its notification says so. */
object AppCapture {
    @Volatile var running = false; private set
    @Volatile var paused = false
    @Volatile var levelDb = -120.0; private set
    private var projection: MediaProjection? = null
    private var thread: Thread? = null
    var engine: Engine? = null
    var deck = -1
    var mixRate: () -> Double = { 48000.0 }

    fun consentIntent(c: Context): Intent = (c.getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager).createScreenCaptureIntent()

    /** called by the service once it is in the foreground, with the user's consent */
    @SuppressLint("MissingPermission")   // RECORD_AUDIO is checked by FaderSources.gate before the consent is asked for
    internal fun begin(c: Context, resultCode: Int, data: Intent) {
        if (Build.VERSION.SDK_INT < 29 || running) return
        val mp = (c.getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager).getMediaProjection(resultCode, data) ?: return
        projection = mp
        val cfg = AudioPlaybackCaptureConfiguration.Builder(mp).addMatchingUsage(AudioAttributes.USAGE_MEDIA).addMatchingUsage(AudioAttributes.USAGE_GAME).addMatchingUsage(AudioAttributes.USAGE_UNKNOWN).build()
        val rate = 48000; val fmt = AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setSampleRate(rate).setChannelMask(AudioFormat.CHANNEL_IN_STEREO).build()
        val min = AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT).coerceAtLeast(4096)
        val rec = runCatching { AudioRecord.Builder().setAudioFormat(fmt).setBufferSizeInBytes(min * 2).setAudioPlaybackCaptureConfig(cfg).build() }.getOrNull() ?: run { stop(c); return }
        running = true
        thread = Thread({
            val buf = FloatArray(1024 * 2); rec.startRecording()
            try { while (running) {
                val n = rec.read(buf, 0, buf.size, AudioRecord.READ_BLOCKING); if (n <= 0) continue
                var pk = 0f; for (i in 0 until n) pk = maxOf(pk, kotlin.math.abs(buf[i])); levelDb = if (pk > 1e-6f) 20 * Math.log10(pk.toDouble()) else -120.0
                val e = engine; val d = deck
                if (e != null && d >= 0 && !paused) e.deckPush(d, buf.copyOf(n), n / 2, rate.toDouble(), mixRate())   // live only: nothing is written anywhere
            } } finally { runCatching { rec.stop() }; rec.release() }
        }, "app-capture").also { it.start() }
    }
    fun stop(c: Context) { running = false; thread = null; projection?.stop(); projection = null; levelDb = -120.0; c.stopService(Intent(c, AppCaptureService::class.java)) }
}

class AppCaptureService : Service() {
    override fun onBind(intent: Intent?): IBinder? = null
    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val nm = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= 26) nm.createNotificationChannel(NotificationChannel("capture", getString(R.string.lm_capture_channel), NotificationManager.IMPORTANCE_LOW))
        val n: Notification = Notification.Builder(this, "capture").setContentTitle(getString(R.string.lm_capture_title)).setContentText(getString(R.string.lm_capture_notice)).setSmallIcon(android.R.drawable.ic_btn_speak_now).setOngoing(true).build()
        if (Build.VERSION.SDK_INT >= 29) startForeground(7127, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION) else startForeground(7127, n)
        val code = intent?.getIntExtra("code", 0) ?: 0
        @Suppress("DEPRECATION") val data = intent?.getParcelableExtra<Intent>("data")
        if (data != null) AppCapture.begin(this, code, data) else stopSelf()
        return START_NOT_STICKY
    }
    override fun onDestroy() { AppCapture.stop(applicationContext); super.onDestroy() }
}
