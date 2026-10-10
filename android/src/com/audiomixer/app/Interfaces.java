package com.audiomixer.app;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Groups the Android audio devices into physical interfaces (one USB interface shows once with its inputs and outputs) in the shape of bridge/interfaces.js. No android.* classes. */
final class Interfaces {
    private Interfaces() {}

    /** One AudioDeviceInfo, reduced to what the grouping needs. */
    static final class Dev {
        final int id; final String name; final int type; final boolean source, sink; final int channels, sampleRate;
        Dev(int id, String name, int type, boolean source, boolean sink, int channels, int sampleRate) {
            this.id = id; this.name = name == null ? "" : name.trim(); this.type = type; this.source = source; this.sink = sink; this.channels = channels; this.sampleRate = sampleRate > 0 ? sampleRate : 48000;
        }
    }

    // AudioDeviceInfo.TYPE_* values
    static final int BUILTIN_EARPIECE = 1, BUILTIN_SPEAKER = 2, WIRED_HEADSET = 3, WIRED_HEADPHONES = 4, LINE_ANALOG = 5, LINE_DIGITAL = 6, BLUETOOTH_SCO = 7, BLUETOOTH_A2DP = 8, HDMI = 9,
        HDMI_ARC = 10, USB_DEVICE = 11, USB_ACCESSORY = 12, DOCK = 13, FM = 14, BUILTIN_MIC = 15, FM_TUNER = 16, TV_TUNER = 17, TELEPHONY = 18, AUX_LINE = 19, IP = 20, BUS = 21, USB_HEADSET = 22;

    static String transport(int t) {
        switch (t) {
            case USB_DEVICE: case USB_ACCESSORY: case USB_HEADSET: return "usb";
            case BLUETOOTH_SCO: case BLUETOOTH_A2DP: return "bluetooth";
            case WIRED_HEADSET: case WIRED_HEADPHONES: case LINE_ANALOG: case LINE_DIGITAL: case AUX_LINE: return "wired";
            case HDMI: case HDMI_ARC: return "display";
            case BUILTIN_EARPIECE: case BUILTIN_SPEAKER: case BUILTIN_MIC: return "builtin";
            default: return "other";
        }
    }

    /** Bluetooth headsets list SCO (hands-free microphone) and A2DP (stereo) separately: one device, and each side keeps its own profile. */
    private static String keyOf(Dev d) {
        String t = transport(d.type), n = d.name.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9]+", " ").trim();
        if (t.equals("builtin")) return "builtin";
        return t + ":" + (n.isEmpty() ? "device" + d.id : n);
    }

    private static final class Group {
        String key, name, transport; int inputs, outputs; final List<Dev> devs = new ArrayList<Dev>();
    }

    /** Body of GET /api/interfaces. */
    static String toJson(List<Dev> devices) {
        Map<String, Group> groups = new LinkedHashMap<String, Group>();
        for (Dev d : devices) {
            String k = keyOf(d); Group g = groups.get(k);
            if (g == null) { g = new Group(); g.key = k; g.transport = transport(d.type); g.name = d.name.isEmpty() ? "Audio device" : d.name; groups.put(k, g); }
            g.devs.add(d);
            int ch = d.channels > 0 ? d.channels : 2;
            if (d.source) g.inputs = Math.max(g.inputs, ch);
            if (d.sink) g.outputs = Math.max(g.outputs, ch);
        }
        StringBuilder b = new StringBuilder("{\"ok\":true,\"platform\":\"android\",\"portaudio\":true,\"engine\":\"android\",\"asio\":[],\"interfaces\":[");
        boolean first = true;
        for (Group g : groups.values()) {
            if (g.key.equals("builtin")) g.name = "This device (built-in microphone and speaker)";
            if (!first) b.append(','); first = false;
            Dev read = null, write = null;
            StringBuilder apis = new StringBuilder();
            for (Dev d : g.devs) {
                int in = d.source ? (d.channels > 0 ? d.channels : 2) : 0, out = d.sink ? (d.channels > 0 ? d.channels : 2) : 0;
                if (apis.length() > 0) apis.append(',');
                apis.append(api(d, in, out));
                if (read == null && d.source) read = d;
                if (write == null && d.sink) write = d;
            }
            b.append("{\"key\":").append(Json.str(g.key)).append(",\"name\":").append(Json.str(g.name)).append(",\"inputs\":").append(g.inputs).append(",\"outputs\":").append(g.outputs)
                .append(",\"loopback\":false,\"asio\":false,\"usb\":").append(g.transport.equals("usb")).append(",\"bluetooth\":").append(g.transport.equals("bluetooth")).append(",\"transport\":").append(Json.str(g.transport))
                .append(",\"apis\":[").append(apis).append("],\"read\":").append(read == null ? "null" : api(read, read.channels > 0 ? read.channels : 2, 0))
                .append(",\"write\":").append(write == null ? "null" : api(write, 0, write.channels > 0 ? write.channels : 2)).append('}');
        }
        return b.append("]}").toString();
    }

    private static String api(Dev d, int in, int out) {
        return "{\"api\":\"Android\",\"deviceId\":" + d.id + ",\"inputs\":" + in + ",\"outputs\":" + out + ",\"sampleRate\":" + d.sampleRate + "}";
    }

    /** The PortAudio-shaped device list used by GET /api/drivers. */
    static String devicesJson(List<Dev> devices) {
        StringBuilder b = new StringBuilder("[");
        boolean first = true;
        for (Dev d : devices) {
            if (!first) b.append(','); first = false;
            b.append("{\"id\":").append(d.id).append(",\"name\":").append(Json.str(d.name.isEmpty() ? "Audio device" : d.name)).append(",\"hostApi\":\"Android\",\"inputs\":").append(d.source ? (d.channels > 0 ? d.channels : 2) : 0)
                .append(",\"outputs\":").append(d.sink ? (d.channels > 0 ? d.channels : 2) : 0).append(",\"sampleRate\":").append(d.sampleRate).append('}');
        }
        return b.append(']').toString();
    }
}
