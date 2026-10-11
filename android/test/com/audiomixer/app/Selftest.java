package com.audiomixer.app;

/** Pure-logic checks of the native audio helpers on a plain JVM (no android.* classes); used by bridge/test.js. Prints one line per case: name=value. */
public final class Selftest {
    public static void main(String[] a) {
        System.out.println("maxChannels.counts=" + Interfaces.maxChannels(new int[] { 1, 2 }, new int[0]));
        System.out.println("maxChannels.usb8=" + Interfaces.maxChannels(new int[] { 2 }, new int[] { 0xFF, 0x3 }));         // eight-channel USB interface: index mask 0xFF
        System.out.println("maxChannels.none=" + Interfaces.maxChannels(null, null));
        System.out.println("frames.requested=" + Interfaces.autoFrames(Double.valueOf(256), 48000, 48000, 192));
        System.out.println("frames.tooSmall=" + Interfaces.autoFrames(Double.valueOf(8), 48000, 48000, 192));             // out of range: treated as auto
        System.out.println("frames.auto=" + Interfaces.autoFrames("auto", 48000, 48000, 192));                             // two bursts of 192
        System.out.println("frames.autoOdd=" + Interfaces.autoFrames(null, 48000, 48000, 240));                            // 240 -> 256 per burst (multiple of 32) x 2
        System.out.println("frames.otherRate=" + Interfaces.autoFrames(null, 44100, 48000, 192));                          // the burst scaled to the stream rate
        System.out.println("frames.huge=" + Interfaces.autoFrames(null, 192000, 48000, 4096));                             // clamped
        System.out.println("frames.noNative=" + Interfaces.autoFrames(null, 48000, 0, 0));                                 // about 10 ms
        System.out.println("native=" + Interfaces.nativeJson(48000, 192, true, false, true));
        System.out.println("native.none=" + Interfaces.nativeJson(0, 0, false, false, false));
        System.out.println("with=" + Interfaces.withNative("{\"ok\":true,\"interfaces\":[]}", Interfaces.nativeJson(48000, 192, true, true, false)));
        java.util.List<NowPlaying.Session> l = new java.util.ArrayList<NowPlaying.Session>();
        l.add(new NowPlaying.Session("com.google.android.apps.youtube.music", NowPlaying.status(2), "Lofi", "Chill", ""));
        l.add(new NowPlaying.Session("com.example.player", NowPlaying.status(1), "Local file", "", ""));
        l.add(new NowPlaying.Session("com.spotify.music", NowPlaying.status(3), "Karma Police", "Radiohead", "OK Computer"));
        l.add(new NowPlaying.Session("com.example.blank", "stopped", "", "", ""));                                           // no title, unknown service: left out
        System.out.println("nowplaying=" + NowPlaying.toJson(l, true, null));
        System.out.println("nowplaying.none=" + NowPlaying.toJson(new java.util.ArrayList<NowPlaying.Session>(), false, "Settings > Notifications > Device & app notifications > Audio Mixer"));
    }
}
