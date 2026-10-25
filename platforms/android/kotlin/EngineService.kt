package com.audiomixer.app

import android.app.Notification
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import java.io.IOException

/**
 * Kotlin version of the background engine service (same behaviour as EngineService.java). `node scripts/build-apk.js --kotlin` builds the app with this file
 * instead of the Java one when kotlinc is installed. MiniBridge, AndroidAudio and the protocol classes stay Java.
 * Not compiled in the project's own checks: no Kotlin compiler was available when it was written.
 */
class EngineService : Service() {
    private var bridge: MiniBridge? = null
    private var wake: PowerManager.WakeLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            EngineState.running = false
            EngineState.changed(this)
            stopForeground(true)
            stopSelf()
            return START_NOT_STICKY
        }
        startForeground(NOTIFICATION_ID, notification("Starting the audio engine..."))
        EngineState.running = true                                  // the Quick Settings tile lights up
        EngineState.changed(this)
        if (bridge == null) {
            try {
                val b = MiniBridge(AndroidAudio(this), AndroidAssets(this), version())
                val port = b.start(8765, 10)                       // 8765 is where the mixer page looks for the engine
                bridge = b
                Companion.port = port
                notifier().notify(NOTIFICATION_ID, notification("Audio engine running on 127.0.0.1:$port"))
            } catch (e: IOException) {
                Log.e(TAG, "engine could not start", e)
                notifier().notify(NOTIFICATION_ID, notification("The audio engine could not start: ${e.message}"))
            }
        }
        if (wake == null) {
            val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
            wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "AudioMixer:engine").also {
                it.setReferenceCounted(false)
                it.acquire()                                       // the CPU keeps running with the screen off while audio is processed
            }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        bridge?.stop()
        bridge = null
        Companion.port = 0
        EngineState.running = false
        EngineState.changed(this)
        wake?.takeIf { it.isHeld }?.release()
        super.onDestroy()
    }

    private fun notifier() = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

    private fun version(): String = try {
        packageManager.getPackageInfo(packageName, 0).versionName
    } catch (e: Exception) {
        "0"
    }

    private fun notification(text: String): Notification {
        val builder: Notification.Builder = if (Build.VERSION.SDK_INT >= 26) {
            // notification channels are API 26; reflection keeps the build working against the API 23 android.jar of the distribution packages
            try {
                val channelClass = Class.forName("android.app.NotificationChannel")
                val channel = channelClass.getConstructor(String::class.java, CharSequence::class.java, Int::class.javaPrimitiveType).newInstance(CHANNEL, "Audio engine", 2)
                notifier().javaClass.getMethod("createNotificationChannel", channelClass).invoke(notifier(), channel)
                Notification.Builder::class.java.getConstructor(Context::class.java, String::class.java).newInstance(this, CHANNEL)
            } catch (e: Exception) {
                Notification.Builder(this)
            }
        } else {
            Notification.Builder(this)
        }
        val immutable = if (Build.VERSION.SDK_INT >= 23) PendingIntent.FLAG_IMMUTABLE else 0
        val open = Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
        val stop = Intent(this, EngineService::class.java).setAction(ACTION_STOP)
        return builder.setContentTitle("Audio Mixer engine").setContentText(text).setSmallIcon(applicationInfo.icon).setOngoing(true)
            .setContentIntent(PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_UPDATE_CURRENT or immutable))
            .addAction(0, "Stop", PendingIntent.getService(this, 1, stop, PendingIntent.FLAG_UPDATE_CURRENT or immutable))
            .build()
    }

    companion object {
        const val ACTION_STOP = "com.audiomixer.app.STOP"
        const val CHANNEL = "mixer_engine"
        const val NOTIFICATION_ID = 8765
        private const val TAG = "AudioMixerEngine"

        /** Port of the running engine (0 while it is not running): MainActivity loads the mixer page from it. */
        @Volatile
        @JvmStatic
        var port: Int = 0
    }
}
