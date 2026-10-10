package com.audiomixer.app;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Runs MiniBridge on a plain JVM with a fake audio backend (a sine wave as the microphone, a byte counter as the speaker): used by bridge/test.js. Prints "PORT n". */
public final class BridgeHarness {
    static final class Fake implements AudioBackend {
        final List<Interfaces.Dev> devs = new ArrayList<Interfaces.Dev>();
        Fake() {
            devs.add(new Interfaces.Dev(1, "Pixel mic", Interfaces.BUILTIN_MIC, true, false, 1, 48000));
            devs.add(new Interfaces.Dev(2, "Pixel speaker", Interfaces.BUILTIN_SPEAKER, false, true, 2, 48000));
            devs.add(new Interfaces.Dev(7, "Scarlett 2i2 USB", Interfaces.USB_DEVICE, true, false, 2, 48000));
            devs.add(new Interfaces.Dev(8, "Scarlett 2i2 USB", Interfaces.USB_DEVICE, false, true, 2, 48000));
            devs.add(new Interfaces.Dev(11, "Galaxy Buds2", Interfaces.BLUETOOTH_SCO, true, false, 1, 16000));
            devs.add(new Interfaces.Dev(12, "Galaxy Buds2", Interfaces.BLUETOOTH_A2DP, false, true, 2, 48000));
        }
        public String interfacesJson() { return Interfaces.toJson(devs); }
        public String devicesJson() { return Interfaces.devicesJson(devs); }
        public String nativeJson() { return Interfaces.nativeJson(48000, 192, true, false, true); }
        public Input openInput(Map<String, Object> o, final Listener l) throws Exception {
            final int id = Json.intOf(o, "deviceId", -1), ch = Math.max(1, Math.min(2, Json.intOf(o, "channels", 2))), rate = Json.intOf(o, "sampleRate", 48000);
            if (id == 99) throw new Exception("microphone permission is missing");
            final boolean[] on = { true };
            Thread t = new Thread(new Runnable() { public void run() {
                int n = 0; while (on[0]) { byte[] b = new byte[480 * ch * 2]; for (int i = 0; i < 480; i++) { short v = (short) (Math.sin(2 * Math.PI * 440 * (n++) / 48000.0) * 12000); for (int c = 0; c < ch; c++) { b[(i * ch + c) * 2] = (byte) v; b[(i * ch + c) * 2 + 1] = (byte) (v >> 8); } } l.onData(b, b.length); try { Thread.sleep(10); } catch (InterruptedException e) { return; } } } });
            t.setDaemon(true); t.start();
            final Map<String, Object> info = info("Fake input " + id, rate, ch);
            return new Input() { public Map<String, Object> info() { return info; } public void close() { on[0] = false; } };
        }
        public Output openOutput(Map<String, Object> o) throws Exception {
            final Map<String, Object> info = info("Fake output " + Json.intOf(o, "deviceId", -1), Json.intOf(o, "sampleRate", 48000), Json.intOf(o, "channels", 2));
            return new Output() { public Map<String, Object> info() { return info; } public void close() { } public void write(byte[] p, int off, int len) { } };
        }
        static Map<String, Object> info(String dev, int rate, int ch) { Map<String, Object> m = new HashMap<String, Object>(); m.put("device", dev); m.put("hostApi", "Android"); m.put("sampleRate", rate); m.put("channels", ch); m.put("frameSize", 480); m.put("latencyMs", 10.0); return m; }
    }

    public static void main(String[] args) throws Exception {
        final java.io.File www = args.length > 1 ? new java.io.File(args[1]) : null;
        Assets assets = www == null ? null : new Assets() { public byte[] read(String p) throws java.io.IOException { java.io.File f = new java.io.File(www, p.substring(1)); return f.isFile() ? java.nio.file.Files.readAllBytes(f.toPath()) : null; } };
        MiniBridge b = new MiniBridge(new Fake(), assets, "test");
        int port = b.start(args.length > 0 ? Integer.parseInt(args[0]) : 0, 1);
        System.out.println("PORT " + port); System.out.flush();
        System.in.read();                                  // lives until the test closes stdin
        b.stop();
    }
}
