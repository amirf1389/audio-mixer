package com.audiomixer.app;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

import java.io.IOException;

/**
 * The background engine: a foreground service that runs MiniBridge (the audio engine) so audio keeps running with the screen off or the app in the background.
 * Its notification shows the engine and has a Stop button. The Kotlin twin in android/kotlin/EngineService.kt does the same (build-apk.js --kotlin).
 */
public class EngineService extends Service {
    static final String ACTION_STOP = "com.audiomixer.app.STOP";
    static final String CHANNEL = "mixer_engine";
    static final int NOTIFICATION_ID = 8765;
    private static final String TAG = "AudioMixerEngine";
    /** Port of the running engine (0 while it is not running): MainActivity loads the mixer page from it. */
    static volatile int port;
    private MiniBridge bridge;
    private PowerManager.WakeLock wake;

    @Override public IBinder onBind(Intent intent) { return null; }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) { EngineState.running = false; EngineState.changed(this); stopForegroundCompat(); stopSelf(); return START_NOT_STICKY; }
        startForeground(NOTIFICATION_ID, notification("Starting the audio engine..."));
        EngineState.running = true; EngineState.changed(this);                  // the Quick Settings tile lights up
        if (bridge == null) {
            try {
                MiniBridge b = new MiniBridge(new AndroidAudio(this), new AndroidAssets(this), version());
                int port = b.start(8765, 10);                                    // 8765 is where the mixer page looks for the engine
                bridge = b; EngineService.port = port;
                ((android.app.NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE)).notify(NOTIFICATION_ID, notification("Audio engine running on 127.0.0.1:" + port));
            } catch (IOException e) {
                Log.e(TAG, "engine could not start", e);
                ((android.app.NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE)).notify(NOTIFICATION_ID, notification("The audio engine could not start: " + e.getMessage()));
            }
        }
        if (wake == null) {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "AudioMixer:engine");
            wake.setReferenceCounted(false);
            wake.acquire();                                                      // the CPU keeps running with the screen off while audio is processed
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (bridge != null) { bridge.stop(); bridge = null; }
        port = 0;
        EngineState.running = false; EngineState.changed(this);
        if (wake != null && wake.isHeld()) wake.release();
        super.onDestroy();
    }

    private void stopForegroundCompat() { stopForeground(true); }

    private String version() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; } catch (Exception e) { return "0"; }
    }

    private Notification notification(String text) {
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            // notification channels are API 26: reached by reflection because the build compiles against the API 23 android.jar of the distribution packages
            try {
                Class<?> ch = Class.forName("android.app.NotificationChannel");
                Object channel = ch.getConstructor(String.class, CharSequence.class, int.class).newInstance(CHANNEL, "Audio engine", 2);       // IMPORTANCE_LOW: no sound
                android.app.NotificationManager nm = (android.app.NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
                nm.getClass().getMethod("createNotificationChannel", ch).invoke(nm, channel);
                b = Notification.Builder.class.getConstructor(Context.class, String.class).newInstance(this, CHANNEL);
            } catch (Exception e) { b = new Notification.Builder(this); }
        } else b = new Notification.Builder(this);
        Intent open = new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        Intent stop = new Intent(this, EngineService.class).setAction(ACTION_STOP);
        int imm = Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0;
        b.setContentTitle("Audio Mixer engine").setContentText(text).setSmallIcon(getApplicationInfo().icon).setOngoing(true)
            .setContentIntent(PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_UPDATE_CURRENT | imm))
            .addAction(0, "Stop", PendingIntent.getService(this, 1, stop, PendingIntent.FLAG_UPDATE_CURRENT | imm));
        return b.build();
    }
}
