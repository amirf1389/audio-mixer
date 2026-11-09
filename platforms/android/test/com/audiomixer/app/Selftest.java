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
        System.out.println("fingerprint=" + AppInfo.fingerprint("abc".getBytes()));
        java.util.List<AppInfo.Perm> ps = new java.util.ArrayList<AppInfo.Perm>(); ps.add(new AppInfo.Perm("android.permission.RECORD_AUDIO", true)); ps.add(new AppInfo.Perm("android.permission.X_UNKNOWN", false));
        System.out.println("appinfo=" + AppInfo.toJson("com.audiomixer.app", "1.0", 10000, 29, "AA:BB", "com.android.vending", false, ps, false, true));
        System.out.println("nowplaying=" + NowPlaying.toJson(l, true, null));
        // AUDIO HAL DETECTION + BATTERY (beta 1.1)
        Hal.Info hi = new Hal.Info(); hi.sdk = 34; hi.manufacturer = "Google"; hi.model = "Pixel 8"; hi.nativeRate = 48000; hi.burstFrames = 96; hi.lowLatency = true; hi.proAudio = true; hi.usbHost = true; hi.devicesVersion = 3;
        java.util.List<Hal.Device> hd = new java.util.ArrayList<Hal.Device>();
        hd.add(new Hal.Device(1, "Pixel 8", 15, true, false, 1, new int[] { 48000 }, new int[] { 2 }));
        hd.add(new Hal.Device(2, "Pixel 8", 2, false, true, 2, new int[] { 48000 }, new int[] { 2, 4 }));
        hd.add(new Hal.Device(3, "WH-1000XM4", 8, false, true, 2, new int[] { 48000 }, new int[] { 2 }));
        hd.add(new Hal.Device(4, "UMC404HD 192k", 11, true, true, 4, new int[] { 44100, 192000 }, new int[] { 2, 21, 4 }));
        hd.add(new Hal.Device(5, "Pixel 8", 1, false, true, 1, new int[] { 48000 }, new int[] { 2 }));
        hd.add(new Hal.Device(6, "TV", 9, false, true, 8, new int[] { 48000 }, new int[] { 2, 5, 6, 7 }));
        System.out.println("hal=" + Hal.toJson(hi, hd));
        Hal.Info h0 = new Hal.Info(); System.out.println("hal.none=" + Hal.toJson(h0, new java.util.ArrayList<Hal.Device>()));
        Power.State ps1 = new Power.State(); ps1.percent = 80; ps1.charging = false; ps1.streams = 0; ps1.idleMs = 120000; System.out.println("power.idle=" + Power.toJson(ps1));
        Power.State ps2 = new Power.State(); ps2.percent = 15; ps2.streams = 1; System.out.println("power.low=" + Power.toJson(ps2));
        Power.State ps3 = new Power.State(); ps3.percent = 15; ps3.charging = true; ps3.idleMs = 30000; System.out.println("power.charging=" + Power.toJson(ps3));
        Power.State ps4 = new Power.State(); ps4.percent = 90; ps4.saver = true; ps4.idleMs = 999999; System.out.println("power.saver=" + Power.toJson(ps4));
        System.out.println("nowplaying.none=" + NowPlaying.toJson(new java.util.ArrayList<NowPlaying.Session>(), false, "Settings > Notifications > Device & app notifications > Audio Mixer"));
    }
}
