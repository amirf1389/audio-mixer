package com.audiomixer.app;

import java.util.List;
import java.util.Locale;

/**
 * AUDIO HAL DETECTION (beta 1.1): what the phone's audio HAL reports, as GET /api/hal. AndroidAudio fills Hal.Device / Hal.Info from
 * AudioManager / AudioDeviceInfo / PackageManager; this class only names and rates them, so it runs (and is tested) on a plain JVM.
 * Detection only: an encoding such as AC3 or DTS is reported because the device lists it for passthrough, AmF X1 never decodes it.
 */
final class Hal {
    private Hal() {}

    /** One AudioDeviceInfo, as the HAL lists it. */
    static final class Device {
        final int id, type, channels, maxRate; final String name; final boolean source, sink; final int[] encodings, rates;
        Device(int id, String name, int type, boolean source, boolean sink, int channels, int[] rates, int[] encodings) {
            this.id = id; this.name = name; this.type = type; this.source = source; this.sink = sink; this.channels = channels;
            this.rates = rates == null ? new int[0] : rates; this.encodings = encodings == null ? new int[0] : encodings;
            int m = 0; for (int r : this.rates) m = Math.max(m, r); this.maxRate = m;
        }
    }

    /** The phone and its HAL properties. */
    static final class Info {
        int sdk; String manufacturer = "", model = "", board = "";
        int nativeRate, burstFrames; boolean lowLatency, proAudio, usbHost, unprocessedSource, offload;
        long devicesVersion;
    }

    // AudioDeviceInfo.TYPE_* (API 23..31)
    static String typeName(int t) {
        switch (t) {
            case 1: return "builtin-earpiece"; case 2: return "builtin-speaker"; case 3: return "wired-headset"; case 4: return "wired-headphones";
            case 5: return "line-analog"; case 6: return "line-digital"; case 7: return "bluetooth-sco"; case 8: return "bluetooth-a2dp";
            case 9: return "hdmi"; case 10: return "hdmi-arc"; case 11: return "usb-device"; case 12: return "usb-accessory"; case 13: return "dock";
            case 14: return "fm"; case 15: return "builtin-mic"; case 16: return "fm-tuner"; case 17: return "tv-tuner"; case 18: return "telephony";
            case 19: return "aux-line"; case 20: return "ip"; case 21: return "bus"; case 22: return "usb-headset"; case 23: return "hearing-aid";
            case 24: return "builtin-speaker-safe"; case 25: return "remote-submix"; case 26: return "ble-headset"; case 27: return "ble-speaker";
            case 29: return "hdmi-earc"; case 30: return "ble-broadcast";
            default: return "unknown";
        }
    }

    /** How the app should treat the device: usb (class-compliant interface), bluetooth (codec latency, no pro use), builtin, wired, hdmi, other. */
    static String transport(int t) {
        if (t == 11 || t == 12 || t == 22) return "usb";
        if (t == 7 || t == 8 || t == 23 || t == 26 || t == 27 || t == 30) return "bluetooth";
        if (t == 1 || t == 2 || t == 15 || t == 24) return "builtin";
        if (t == 3 || t == 4 || t == 5 || t == 19) return "wired";
        if (t == 9 || t == 10 || t == 29) return "hdmi";
        return "other";
    }

    // AudioFormat.ENCODING_*
    static String encodingName(int e) {
        switch (e) {
            case 2: return "pcm-16"; case 3: return "pcm-8"; case 4: return "pcm-float"; case 5: return "ac3"; case 6: return "e-ac3"; case 7: return "dts";
            case 8: return "dts-hd"; case 9: return "mp3"; case 10: return "aac-lc"; case 13: return "iec61937"; case 18: return "e-ac3-joc";
            case 21: return "pcm-24"; case 22: return "pcm-32"; case 1: return "default";
            default: return "enc-" + e;
        }
    }

    /** pro (the "pro audio" feature: round trip under 20 ms), low (the low-latency feature), standard. */
    static String latencyClass(Info i) { return i.proAudio ? "pro" : i.lowLatency ? "low" : "standard"; }

    /** The buffer the app should ask for: two HAL bursts (one burst underruns on most phones), about 10 ms without a reported burst. */
    static int recommendedFrames(Info i) {
        if (i.burstFrames <= 0) return Math.max(64, (i.nativeRate > 0 ? i.nativeRate : 48000) / 100);
        return Math.min(4096, i.burstFrames * 2);
    }

    /** One device worth recording with or playing to first: a USB interface, then wired, then built-in; Bluetooth last (codec delay). */
    static int rank(Device d) {
        String t = transport(d.type);
        return "usb".equals(t) ? 0 : "wired".equals(t) ? 1 : "builtin".equals(t) ? 2 : "hdmi".equals(t) ? 3 : "bluetooth".equals(t) ? 5 : 4;
    }

    static String toJson(Info i, List<Device> devs) {
        StringBuilder b = new StringBuilder("{\"ok\":true,\"platform\":\"android\"");
        b.append(",\"sdk\":").append(i.sdk).append(",\"manufacturer\":").append(Json.str(i.manufacturer)).append(",\"model\":").append(Json.str(i.model)).append(",\"board\":").append(Json.str(i.board));
        b.append(",\"hal\":{\"nativeRate\":").append(i.nativeRate).append(",\"burstFrames\":").append(i.burstFrames)
            .append(",\"burstMs\":").append(i.nativeRate > 0 && i.burstFrames > 0 ? String.format(Locale.ROOT, "%.2f", i.burstFrames * 1000.0 / i.nativeRate) : "null")
            .append(",\"lowLatency\":").append(i.lowLatency).append(",\"proAudio\":").append(i.proAudio).append(",\"usbHost\":").append(i.usbHost)
            .append(",\"unprocessedSource\":").append(i.unprocessedSource).append(",\"offload\":").append(i.offload)
            .append(",\"latencyClass\":").append(Json.str(latencyClass(i))).append(",\"recommendedFrames\":").append(recommendedFrames(i)).append('}');
        b.append(",\"devicesVersion\":").append(i.devicesVersion).append(",\"devices\":[");
        Device bestIn = null, bestOut = null;
        for (int k = 0; k < devs.size(); k++) {
            Device d = devs.get(k);
            if (k > 0) b.append(',');
            b.append("{\"id\":").append(d.id).append(",\"name\":").append(Json.str(d.name)).append(",\"type\":").append(Json.str(typeName(d.type)))
                .append(",\"transport\":").append(Json.str(transport(d.type))).append(",\"input\":").append(d.source).append(",\"output\":").append(d.sink)
                .append(",\"channels\":").append(d.channels).append(",\"maxRate\":").append(d.maxRate).append(",\"encodings\":[");
            for (int e = 0; e < d.encodings.length; e++) { if (e > 0) b.append(','); b.append(Json.str(encodingName(d.encodings[e]))); }
            b.append("]}");
            if (d.source && !"other".equals(transport(d.type)) && (bestIn == null || rank(d) < rank(bestIn))) bestIn = d;
            if (d.sink && !"other".equals(transport(d.type)) && d.type != 1 && (bestOut == null || rank(d) < rank(bestOut))) bestOut = d;
        }
        b.append("],\"suggested\":{\"input\":").append(bestIn == null ? "null" : String.valueOf(bestIn.id)).append(",\"output\":").append(bestOut == null ? "null" : String.valueOf(bestOut.id)).append("}}");
        return b.toString();
    }
}
