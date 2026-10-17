package com.audiomixer.app;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Virtual audio devices on Android (the same idea and protocol as bridge/virtual.js): the Audio Mixer audio HAL (drivers/android-hal) connects to
 * WS /ws/virtual, says hello {"type":"hello","name":"Audio Mixer","channels":2,"rate":48000} and then
 *   driver -> engine  binary Int16 LE PCM = what apps PLAYED to the virtual device  -> the mixer reads it (LIVE SOURCES: READ, /ws/input with the device id)
 *   engine -> driver  binary Int16 LE PCM = what the mixer sends to the device       -> apps record it (LIVE SOURCES: WRITE, /ws/output with the device id)
 * Device ids start at 9000. Nothing is stored; frames are forwarded as they arrive. No android.* classes: runs on a plain JVM (tested there).
 */
final class VirtualHub {
    static final int BASE = 9000;
    static final int MAX_NAME = 48;

    interface Sink { void write(byte[] pcm, int off, int len); }
    interface Reader { void onFrames(byte[] pcm, int len); void onGone(); }

    static final class Device {
        final String name; final int id, channels, rate; final Sink toDriver;
        final List<Reader> readers = new ArrayList<Reader>();
        long frames;
        Device(String name, int id, int channels, int rate, Sink toDriver) { this.name = name; this.id = id; this.channels = channels; this.rate = rate; this.toDriver = toDriver; }
    }

    private final Map<String, Integer> ids = new LinkedHashMap<String, Integer>();
    private final Map<String, Device> devices = new HashMap<String, Device>();

    static boolean isVirtualId(int id) { return id >= BASE && id < BASE + 1000; }

    static String clean(String n) {
        if (n == null) return "";
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < n.length() && b.length() < MAX_NAME; i++) { char c = n.charAt(i); if (c >= 32 && c != 127 && "<>\"'`&".indexOf(c) < 0) b.append(c); }
        return b.toString().trim();
    }

    /** The driver's hello. Returns the device, or throws IllegalArgumentException with a message for the driver. */
    synchronized Device register(String rawName, int channels, int rate, Sink toDriver) {
        String name = clean(rawName);
        if (name.isEmpty()) throw new IllegalArgumentException("name is required");
        if (channels < 1 || channels > 32) throw new IllegalArgumentException("channels must be 1 to 32");
        if (rate < 8000 || rate > 384000) throw new IllegalArgumentException("rate must be 8000 to 384000");
        if (devices.containsKey(name)) throw new IllegalArgumentException("a driver named \"" + name + "\" is already connected");
        Integer id = ids.get(name);
        if (id == null) { id = BASE + ids.size(); ids.put(name, id); }
        Device d = new Device(name, id, channels, rate, toDriver);
        devices.put(name, d);
        return d;
    }

    /** PCM that apps played, from the driver. */
    void frames(Device d, byte[] pcm, int len) {
        if (len % (2 * d.channels) != 0) return;                       // a partial frame would shift every later sample
        List<Reader> copy;
        synchronized (this) { d.frames += len / (2 * d.channels); copy = new ArrayList<Reader>(d.readers); }
        for (Reader r : copy) r.onFrames(pcm, len);
    }

    void unregister(Device d) {
        List<Reader> copy;
        synchronized (this) {
            if (devices.get(d.name) != d) return;
            devices.remove(d.name);
            copy = new ArrayList<Reader>(d.readers); d.readers.clear();
        }
        for (Reader r : copy) r.onGone();
    }

    synchronized Device byId(int id) { for (Device d : devices.values()) if (d.id == id) return d; return null; }

    /** The mixer reads the device. */
    synchronized Runnable capture(final Device d, final Reader r) { d.readers.add(r); return new Runnable() { public void run() { synchronized (VirtualHub.this) { d.readers.remove(r); } } }; }

    /** Body part for GET /api/virtual. */
    synchronized String json() {
        StringBuilder b = new StringBuilder("{\"ok\":true,\"devices\":[");
        boolean first = true;
        for (Device d : devices.values()) {
            if (!first) b.append(','); first = false;
            b.append("{\"id\":").append(d.id).append(",\"name\":").append(Json.str(d.name)).append(",\"channels\":").append(d.channels).append(",\"rate\":").append(d.rate)
                .append(",\"capturing\":").append(!d.readers.isEmpty()).append(",\"frames\":").append(d.frames).append('}');
        }
        return b.append("]}").toString();
    }

    /** The virtual devices as interface groups, added to the array of GET /api/interfaces (same shape as Interfaces.toJson). */
    synchronized String interfacesJson(String json) {
        if (devices.isEmpty()) return json;
        int at = json.lastIndexOf("]");
        if (at < 0) return json;
        StringBuilder b = new StringBuilder();
        boolean empty = json.substring(0, at).trim().endsWith("[");
        for (Device d : devices.values()) {
            String api = "{\"api\":\"Audio Mixer Virtual\",\"deviceId\":" + d.id + ",\"inputs\":" + d.channels + ",\"outputs\":" + d.channels + ",\"sampleRate\":" + d.rate + "}";
            if (!(empty && b.length() == 0)) b.append(',');
            b.append("{\"key\":").append(Json.str("virtual:" + d.name)).append(",\"name\":").append(Json.str(d.name)).append(",\"inputs\":").append(d.channels).append(",\"outputs\":").append(d.channels)
                .append(",\"loopback\":false,\"asio\":false,\"usb\":false,\"bluetooth\":false,\"transport\":\"virtual\",\"apis\":[").append(api).append("],\"read\":").append(api).append(",\"write\":").append(api).append('}');
        }
        return json.substring(0, at) + b + json.substring(at);
    }
}
