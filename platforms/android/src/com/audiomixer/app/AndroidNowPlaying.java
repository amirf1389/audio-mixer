package com.audiomixer.app;

import android.content.ComponentName;
import android.content.Context;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.MediaSessionManager;
import android.media.session.PlaybackState;

import java.util.ArrayList;
import java.util.List;

/** The phone's media sessions for GET /api/nowplaying (MediaSessionManager; read-only, no notification content is touched). */
final class AndroidNowPlaying {
    private final Context ctx;

    AndroidNowPlaying(Context ctx) { this.ctx = ctx.getApplicationContext(); }

    String json() {
        List<NowPlaying.Session> out = new ArrayList<NowPlaying.Session>();
        try {
            MediaSessionManager m = (MediaSessionManager) ctx.getSystemService(Context.MEDIA_SESSION_SERVICE);
            List<MediaController> cs = m.getActiveSessions(new ComponentName(ctx, NowPlayingListener.class));
            for (MediaController c : cs) {
                MediaMetadata md = c.getMetadata();
                PlaybackState ps = c.getPlaybackState();
                if (md == null && ps == null) continue;
                out.add(new NowPlaying.Session(c.getPackageName(), NowPlaying.status(ps == null ? -1 : ps.getState()),
                    md == null ? "" : text(md.getString(MediaMetadata.METADATA_KEY_TITLE)),
                    md == null ? "" : text(md.getString(MediaMetadata.METADATA_KEY_ARTIST), md.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST)),
                    md == null ? "" : text(md.getString(MediaMetadata.METADATA_KEY_ALBUM))));
            }
            return NowPlaying.toJson(out, true, null);
        } catch (SecurityException e) {
            return NowPlaying.toJson(out, false, "Allow Audio Mixer to see what is playing: Android Settings > Notifications > Device & app notifications > Audio Mixer.");
        } catch (Exception e) {
            return NowPlaying.toJson(out, true, null);
        }
    }

    private static String text(String... v) { for (String s : v) if (s != null && s.length() > 0) return s; return ""; }
}
