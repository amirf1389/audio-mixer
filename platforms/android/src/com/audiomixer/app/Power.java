package com.audiomixer.app;

/**
 * BATTERY (beta 1.1): when the engine keeps the CPU awake and when the page should run lighter, as GET /api/power. Pure logic (plain JVM,
 * tested there); EngineService and AndroidAudio feed it the battery / saver state and the bridge's open streams.
 *   - the wake lock is held only while audio streams are open, plus a short grace so a stop / start does not drop it (it used to be held
 *     for as long as the engine ran, even idle with the screen off);
 *   - low-power mode (fewer meter frames, lighter page; see the SYSTEM OPTIMIZE page) is advised when Battery Saver is on, or when the
 *     phone is not charging and the battery is at or below LOW_PERCENT.
 */
final class Power {
    static final int LOW_PERCENT = 20;
    static final long GRACE_MS = 60000;
    private Power() {}

    static final class State { boolean saver, charging, ignoringOptimizations, wakeHeld; int percent = -1; int streams; long idleMs; }

    static boolean holdWakeLock(State s) { return s.streams > 0 || s.idleMs < GRACE_MS; }

    static boolean lowPower(State s) { return s.saver || (!s.charging && s.percent >= 0 && s.percent <= LOW_PERCENT); }

    static String reason(State s) {
        if (s.saver) return "Battery Saver is on";
        if (!s.charging && s.percent >= 0 && s.percent <= LOW_PERCENT) return "battery at " + s.percent + "% and not charging";
        return "";
    }

    static String toJson(State s) {
        return "{\"ok\":true,\"platform\":\"android\",\"percent\":" + s.percent + ",\"charging\":" + s.charging + ",\"saver\":" + s.saver
            + ",\"ignoringOptimizations\":" + s.ignoringOptimizations + ",\"streams\":" + s.streams + ",\"wakeLock\":" + s.wakeHeld
            + ",\"wakeLockWanted\":" + holdWakeLock(s) + ",\"lowPower\":" + lowPower(s) + ",\"reason\":" + Json.str(reason(s)) + "}";
    }
}
