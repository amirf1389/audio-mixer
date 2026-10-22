package com.audiomixer.app;

import java.security.MessageDigest;
import java.util.List;

/** What the app tells about itself (GET /api/app): package, version, the SHA-256 of its signing certificate, every permission with the reason for it and whether it is granted.
 *  Shown on the LICENSE page so the person installing it can compare the fingerprint with the published one and see what the app does and does not ask for
 *  (the answer to "Google Play Protect warns about this app"). No android.* classes: runs on a plain JVM. */
final class AppInfo {
    private AppInfo() {}

    static final class Perm {
        final String name; final boolean granted;
        Perm(String name, boolean granted) { this.name = name; this.granted = granted; }
    }

    /** Why each permission is there. A permission that is not in this table is reported as "not explained": the test of the manifest allows only the ones listed here. */
    static String why(String permission) {
        if ("android.permission.INTERNET".equals(permission)) return "The mixer page talks to the engine inside this app on 127.0.0.1 (nothing leaves the phone unless you open a PC, update or license address yourself).";
        if ("android.permission.RECORD_AUDIO".equals(permission)) return "Microphone and USB / Bluetooth audio interface inputs for the mixer channels. Only used while the mixer runs; asked when you first use an input.";
        if ("android.permission.WAKE_LOCK".equals(permission)) return "Keeps the audio engine running with the screen off (the foreground service shows its notification).";
        if ("android.permission.FOREGROUND_SERVICE".equals(permission)) return "The background audio engine shows a permanent notification with a Stop button while it runs.";
        if ("android.permission.FOREGROUND_SERVICE_MICROPHONE".equals(permission)) return "Android 14+ requires the audio engine's foreground service to declare that it uses the microphone / audio inputs.";
        if ("android.permission.POST_NOTIFICATIONS".equals(permission)) return "Android 13+ asks before an app may show the engine's notification (with its Stop button). Declining only hides the notification.";
        return "not explained";
    }

    /** "AB:CD:..." (uppercase hex, colon separated) of a signing certificate's DER bytes, the form `apksigner verify --print-certs` and the Play Console show. */
    static String fingerprint(byte[] cert) {
        try {
            byte[] d = MessageDigest.getInstance("SHA-256").digest(cert);
            StringBuilder b = new StringBuilder();
            for (int i = 0; i < d.length; i++) { if (i > 0) b.append(':'); b.append(String.format("%02X", d[i] & 0xff)); }
            return b.toString();
        } catch (Exception e) { return ""; }
    }

    static String toJson(String pkg, String versionName, int versionCode, int targetSdk, String certSha256, String installer, boolean debuggable,
                         List<Perm> perms, boolean listenerEnabled, boolean listenerAccess) {
        StringBuilder b = new StringBuilder("{\"ok\":true,\"package\":" + Json.str(pkg) + ",\"versionName\":" + Json.str(versionName) + ",\"versionCode\":" + versionCode
            + ",\"targetSdk\":" + targetSdk + ",\"signer\":{\"sha256\":" + Json.str(certSha256) + "},\"installer\":" + (installer == null ? "null" : Json.str(installer))
            + ",\"debuggable\":" + debuggable + ",\"permissions\":[");
        for (int i = 0; i < perms.size(); i++) {
            Perm p = perms.get(i);
            if (i > 0) b.append(',');
            b.append("{\"name\":").append(Json.str(p.name)).append(",\"granted\":").append(p.granted).append(",\"why\":").append(Json.str(why(p.name))).append('}');
        }
        return b.append("],\"nowPlaying\":{\"componentEnabled\":").append(listenerEnabled).append(",\"accessGranted\":").append(listenerAccess).append("}}").toString();
    }
}
