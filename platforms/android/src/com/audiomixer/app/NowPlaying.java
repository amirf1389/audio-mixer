package com.audiomixer.app;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;

/** "What is playing?": the music service of a media session and the body of GET /api/nowplaying (same shape as the PC bridge). No android.* classes: runs on a plain JVM. */
final class NowPlaying {
    private NowPlaying() {}

    static final class Session {
        final String app, status, title, artist, album;
        Session(String app, String status, String title, String artist, String album) { this.app = app; this.status = status; this.title = title; this.artist = artist; this.album = album; }
    }

    // Order matters: the first match wins (YouTube Music before YouTube). Same list as bridge/nowplaying.js.
    private static final String[][] SERVICES = {
        { "youtube-music", "YouTube Music", "music\\.youtube\\.com|youtube music|youtubemusic|youtube[-.]music" },
        { "youtube", "YouTube", "youtube\\.com|youtu\\.be|\\byoutube\\b|com\\.google\\.android\\.youtube" },
        { "spotify", "Spotify", "spotify" },
        { "tidal", "TIDAL", "tidal" },
        { "apple-music", "Apple Music", "apple ?music|applemusic|music\\.apple\\.com|com\\.apple\\.android\\.music|itunes" },
        { "amazon-music", "Amazon Music", "amazon ?music|music\\.amazon|com\\.amazon\\.mp3" },
        { "deezer", "Deezer", "deezer" },
        { "soundcloud", "SoundCloud", "soundcloud" },
        { "qobuz", "Qobuz", "qobuz" },
        { "pandora", "Pandora", "pandora" },
        { "vlc", "VLC", "\\bvlc\\b|org\\.videolan" },
    };
    private static final Pattern[] TESTS = new Pattern[SERVICES.length];
    static { for (int i = 0; i < SERVICES.length; i++) TESTS[i] = Pattern.compile(SERVICES[i][2], Pattern.CASE_INSENSITIVE); }

    /** {id, name} of the service the texts (package name, title) point at, or {"other", null}. */
    static String[] classify(String... parts) {
        StringBuilder b = new StringBuilder();
        for (String p : parts) if (p != null) b.append(p).append(' ');
        for (int i = 0; i < SERVICES.length; i++) if (TESTS[i].matcher(b).find()) return new String[] { SERVICES[i][0], SERVICES[i][1] };
        return new String[] { "other", null };
    }

    /** PlaybackState.STATE_* of a media session as the page's status words. */
    static String status(int state) {
        switch (state) {
            case 3: case 6: case 8: case 4: case 5: return "playing";      // playing, buffering, connecting, fast forwarding, rewinding
            case 2: return "paused";
            case 0: case 1: return "stopped";
            default: return "unknown";
        }
    }

    private static String clip(String s) { s = s == null ? "" : s; return s.length() > 200 ? s.substring(0, 200) : s; }

    private static String appName(String pkg) {
        String[] last = (pkg == null ? "" : pkg).split("\\.");
        String n = last.length > 0 && last[last.length - 1].length() > 0 ? last[last.length - 1] : "unknown";
        return n.length() > 40 ? n.substring(0, 40) : n;
    }

    private static String one(Session s) {
        String[] svc = classify(s.app, s.title);
        String name = svc[1] != null ? svc[1] : appName(s.app);
        return "{\"service\":" + Json.str(svc[0]) + ",\"serviceName\":" + Json.str(name) + ",\"app\":" + Json.str(s.app) + ",\"status\":" + Json.str(s.status)
            + ",\"title\":" + Json.str(clip(s.title)) + ",\"artist\":" + Json.str(clip(s.artist)) + ",\"album\":" + Json.str(clip(s.album)) + "}";
    }

    private static int rank(Session s) { return "playing".equals(s.status) ? 0 : "paused".equals(s.status) ? 1 : 2; }

    /** Body of GET /api/nowplaying: playing sessions first, `playing` is the first of them. `access` false: Android has not been told to let the app see other apps' media sessions. */
    static String toJson(List<Session> in, boolean access, String hint) {
        List<Session> list = new ArrayList<Session>();
        for (Session s : in) if (s.title.length() > 0 || !"other".equals(classify(s.app, s.title)[0])) list.add(s);
        for (int i = 1; i < list.size(); i++) { Session x = list.get(i); int j = i - 1; while (j >= 0 && rank(list.get(j)) > rank(x)) { list.set(j + 1, list.get(j)); j--; } list.set(j + 1, x); }     // stable by status
        StringBuilder b = new StringBuilder("{\"ok\":true,\"platform\":\"android\",\"method\":\"media-session\",\"access\":" + access);
        if (hint != null) b.append(",\"hint\":").append(Json.str(hint));
        b.append(",\"sessions\":[");
        Session playing = null;
        for (int i = 0; i < list.size(); i++) { if (i > 0) b.append(','); b.append(one(list.get(i))); if (playing == null && "playing".equals(list.get(i).status)) playing = list.get(i); }
        return b.append("],\"playing\":").append(playing == null ? "null" : one(playing)).append('}').toString();
    }
}
