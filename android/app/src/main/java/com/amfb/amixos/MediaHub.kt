package com.amfb.amixos

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.service.notification.NotificationListenerService

/** PLAY: remote control of the music apps playing on THIS phone (TIDAL, Spotify, Deezer or any player that publishes a media session),
 *  through Android's own MediaSessionManager / MediaController. Android only lists other apps' sessions to an app the user has allowed
 *  as a notification listener ([MediaListener]); the listener itself reads no notifications. Metadata and transport only: the audio is
 *  never recorded, captured or downloaded, and no player SDK is bundled. */
object MediaHub {
    /** the players with a shortcut on PLAY (text names only); the package is only used to find the app's own launch intent */
    data class Player(val name: String, val pkg: String)
    val players = listOf(Player("TIDAL", "com.aspiro.tidal"), Player("Spotify", "com.spotify.music"), Player("Deezer", "deezer.android.app"))
    fun appName(pkg: String, fallback: String? = null): String = players.firstOrNull { it.pkg == pkg }?.name ?: fallback?.takeIf { it.isNotBlank() } ?: pkg

    /** what PLAY shows of one session (built from a MediaController, or by hand in tests) */
    data class Now(val pkg: String, val title: String, val artist: String, val album: String, val playing: Boolean, val durationMs: Long, val positionMs: Long,
                   val updatedAt: Long, val speed: Float, val canSeek: Boolean)

    /** the session to show: the one playing (the first when several are), else the most recent one Android lists first */
    fun pick(list: List<Now>): Now? = list.firstOrNull { it.playing } ?: list.firstOrNull()
    /** the play position now, from the last position the player reported (it reports only on changes) */
    fun positionNow(n: Now, now: Long): Long {
        val p = if (n.playing && n.updatedAt > 0) n.positionMs + ((now - n.updatedAt) * n.speed).toLong() else n.positionMs
        return if (n.durationMs > 0) p.coerceIn(0, n.durationMs) else p.coerceAtLeast(0)
    }
    fun seekTarget(fraction: Double, durationMs: Long): Long = if (durationMs <= 0) 0 else (fraction.coerceIn(0.0, 1.0) * durationMs).toLong()
    fun fmt(ms: Long): String { if (ms < 0) return "0:00"; val s = ms / 1000; return if (s >= 3600) String.format("%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60) else String.format("%d:%02d", s / 60, s % 60) }

    fun listenerComponent(c: Context) = ComponentName(c, MediaListener::class.java)
    /** has the user allowed the listener (SETTINGS → Notification access)? */
    fun allowed(c: Context): Boolean = runCatching { androidx.core.app.NotificationManagerCompat.getEnabledListenerPackages(c).contains(c.packageName) }.getOrDefault(false)
    fun accessIntent(): Intent = Intent(android.provider.Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)

    /** the media sessions of the phone, newest first; empty without the permission (never throws) */
    fun controllers(c: Context): List<MediaController> {
        if (!allowed(c)) return emptyList()
        val m = c.getSystemService(Context.MEDIA_SESSION_SERVICE) as? MediaSessionManager ?: return emptyList()
        return runCatching { m.getActiveSessions(listenerComponent(c)) }.getOrDefault(emptyList()).filter { it.packageName != c.packageName }
    }
    fun now(mc: MediaController): Now {
        val md = mc.metadata; val st = mc.playbackState
        fun s(k: String) = md?.getString(k).orEmpty()
        return Now(mc.packageName, s(MediaMetadata.METADATA_KEY_TITLE).ifEmpty { s(MediaMetadata.METADATA_KEY_DISPLAY_TITLE) }, s(MediaMetadata.METADATA_KEY_ARTIST).ifEmpty { s(MediaMetadata.METADATA_KEY_ALBUM_ARTIST) },
            s(MediaMetadata.METADATA_KEY_ALBUM), st?.state == PlaybackState.STATE_PLAYING || st?.state == PlaybackState.STATE_BUFFERING, md?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: 0L,
            st?.position ?: 0L, st?.lastPositionUpdateTime ?: 0L, st?.playbackSpeed ?: 1f, ((st?.actions ?: 0L) and PlaybackState.ACTION_SEEK_TO) != 0L)
    }
    fun art(mc: MediaController): android.graphics.Bitmap? = mc.metadata?.let { it.getBitmap(MediaMetadata.METADATA_KEY_ART) ?: it.getBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART) ?: it.getBitmap(MediaMetadata.METADATA_KEY_DISPLAY_ICON) }
    /** the transport: every call is guarded (a player can go away between two taps) */
    fun toggle(mc: MediaController) = runCatching { if (now(mc).playing) mc.transportControls.pause() else mc.transportControls.play() }.isSuccess
    fun next(mc: MediaController) = runCatching { mc.transportControls.skipToNext() }.isSuccess
    fun previous(mc: MediaController) = runCatching { mc.transportControls.skipToPrevious() }.isSuccess
    fun seek(mc: MediaController, ms: Long) = runCatching { mc.transportControls.seekTo(ms) }.isSuccess

    /** the app's own launch intent (null when it is not installed) */
    fun launch(c: Context, pkg: String): Intent? = runCatching { c.packageManager.getLaunchIntentForPackage(pkg) }.getOrNull()

    /** THE COMPUTER: the console's STREAMING page (GET /api/streaming: {ok, console, now:{service,title,artist,album,source,playing}}) */
    data class Console(val shown: Boolean, val service: String, val title: String, val artist: String, val playing: Boolean)
    fun parseConsole(o: Map<String, Any?>): Console {
        val n = o["now"] as? Map<*, *> ?: emptyMap<String, Any?>(); fun s(k: String) = (n[k] as? String).orEmpty()
        return Console(o["console"] == true, s("service").ifEmpty { s("source") }, s("title"), s("artist"), n["playing"] == true)
    }
    /** the commands the console accepts (anything else is refused before it is sent) */
    val consoleCmds = setOf("play", "pause", "toggle", "next", "previous")
}

/** Needed only so Android lists the phone's media sessions to PLAY ([MediaHub.controllers]); it ignores every notification. */
class MediaListener : NotificationListenerService()
