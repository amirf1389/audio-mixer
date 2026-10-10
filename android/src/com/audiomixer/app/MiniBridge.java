package com.audiomixer.app;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * The audio engine of the Android app: the same local-server protocol as bridge/server.js, so the mixer page talks to it unchanged
 * (LIVE SOURCES reads and writes the phone's USB / Bluetooth / built-in interfaces through WS /ws/input and /ws/output).
 * It runs inside the foreground service, so audio keeps going with the screen off or the app in the background.
 * Listens on 127.0.0.1 only; checks Host and Origin like the Node bridge. No android.* classes: runs on a plain JVM (tested there).
 */
final class MiniBridge {
    static final int MAX_CONNECTIONS = 16;
    private final AudioBackend audio;
    private final Assets assets;
    private final String version;
    private final ExecutorService pool;
    private final AtomicInteger open = new AtomicInteger();
    private final List<Session> sessions = new ArrayList<Session>();
    private ServerSocket server;
    private volatile boolean running;
    private int port;

    MiniBridge(AudioBackend audio, String version) { this(audio, null, version); }

    MiniBridge(AudioBackend audio, Assets assets, String version) {
        this.audio = audio; this.assets = assets; this.version = version;
        this.pool = Executors.newCachedThreadPool(new ThreadFactory() {
            private final AtomicInteger n = new AtomicInteger();
            public Thread newThread(Runnable r) { Thread t = new Thread(r, "mixer-bridge-" + n.incrementAndGet()); t.setDaemon(true); return t; }
        });
    }

    /** Starts on the first free port from `first` (8765 is where the page looks); returns the port. */
    synchronized int start(int first, int tries) throws IOException {
        IOException last = null;
        for (int p = first; p < first + Math.max(1, tries); p++) {
            try { server = new ServerSocket(p, 16, InetAddress.getByName("127.0.0.1")); port = server.getLocalPort(); break; } catch (IOException e) { last = e; }
        }
        if (server == null) throw last != null ? last : new IOException("no free port");
        running = true;
        Thread acceptor = new Thread(new Runnable() { public void run() { acceptLoop(); } }, "mixer-bridge-accept");
        acceptor.setDaemon(true); acceptor.start();
        return port;
    }

    int port() { return port; }

    synchronized void stop() {
        running = false;
        try { if (server != null) server.close(); } catch (IOException e) { /* closed */ }
        server = null;
        List<Session> copy;
        synchronized (sessions) { copy = new ArrayList<Session>(sessions); }
        for (Session s : copy) s.close();
        pool.shutdownNow();
    }

    private void acceptLoop() {
        while (running) {
            final Socket s;
            try { s = server.accept(); } catch (IOException e) { return; }
            if (open.get() >= MAX_CONNECTIONS) { try { s.getOutputStream().write("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n".getBytes("ISO-8859-1")); s.close(); } catch (IOException e) { /* gone */ } continue; }
            open.incrementAndGet();
            pool.execute(new Runnable() { public void run() { try { handle(s); } finally { open.decrementAndGet(); try { s.close(); } catch (IOException e) { /* closed */ } } } });
        }
    }

    // ── request checks (same rules as the Node bridge) ──
    static boolean hostAllowed(String h) {
        if (h == null) return false;
        String n = h.toLowerCase(Locale.ROOT).replaceAll(":\\d+$", "");
        return n.equals("localhost") || n.equals("127.0.0.1") || n.equals("[::1]");
    }

    static boolean originAllowed(String o) {
        if (o == null || o.isEmpty() || o.equals("null")) return true;      // the page is loaded from the app's files: its origin is "null"
        String l = o.toLowerCase(Locale.ROOT);
        if (!l.startsWith("http://") && !l.startsWith("https://")) return false;
        String rest = l.substring(l.indexOf("//") + 2);
        if (rest.startsWith("[::1]")) return true;
        String host = rest.replaceAll("[:/].*$", "");
        return host.equals("localhost") || host.equals("127.0.0.1");
    }

    private void handle(Socket s) {
        try {
            s.setTcpNoDelay(true); s.setSoTimeout(15000);
            InputStream raw = new BufferedInputStream(s.getInputStream(), 8192);
            OutputStream out = s.getOutputStream();
            String head = readHead(raw);
            if (head == null) return;
            String[] lines = head.split("\r\n");
            String[] req = lines[0].split(" ");
            if (req.length < 2) { send(out, 400, "{\"ok\":false,\"error\":\"bad request\"}", null); return; }
            Map<String, String> h = new HashMap<String, String>();
            for (int i = 1; i < lines.length; i++) { int c = lines[i].indexOf(':'); if (c > 0) h.put(lines[i].substring(0, c).trim().toLowerCase(Locale.ROOT), lines[i].substring(c + 1).trim()); }
            String method = req[0], path = req[1].replaceAll("[?#].*$", "");
            if (!hostAllowed(h.get("host"))) { send(out, 403, "{\"ok\":false,\"error\":\"host not allowed\"}", null); return; }
            String origin = h.get("origin");
            if (!originAllowed(origin)) { send(out, 403, "{\"ok\":false,\"error\":\"origin not allowed\"}", null); return; }
            Map<String, String> cors = new HashMap<String, String>();
            if (origin != null) { cors.put("Access-Control-Allow-Origin", origin); cors.put("Vary", "Origin"); cors.put("Access-Control-Allow-Private-Network", "true"); }
            if (method.equals("OPTIONS")) { cors.put("Access-Control-Allow-Methods", "GET, POST"); cors.put("Access-Control-Allow-Headers", "Content-Type, X-Mixer-Action"); sendRaw(out, 204, "", "text/plain", cors); return; }
            if ("websocket".equalsIgnoreCase(h.get("upgrade"))) { websocket(s, raw, out, path, h.get("sec-websocket-key")); return; }
            if (!method.equals("GET")) { send(out, 405, "{\"ok\":false,\"error\":\"method not allowed\"}", cors); return; }
            route(out, path, cors);
        } catch (Exception e) { /* connection ended */ }
    }

    private void route(OutputStream out, String path, Map<String, String> cors) throws IOException {
        if (path.equals("/api/status")) {
            send(out, 200, "{\"ok\":true,\"name\":\"audio-mixer-bridge\",\"version\":" + Json.str(version) + ",\"engine\":\"android\",\"node\":\"android\",\"pid\":0,\"streams\":" + streamsJson() + ",\"native\":" + audio.nativeJson() + ",\"time\":" + System.currentTimeMillis() + "}", cors);
        } else if (path.equals("/api/interfaces")) {
            send(out, 200, Interfaces.withNative(audio.interfacesJson(), audio.nativeJson()), cors);
        } else if (path.equals("/api/drivers")) {
            String dev = audio.devicesJson();
            send(out, 200, "{\"ok\":true,\"platform\":\"android\",\"arch\":\"arm\",\"node\":\"android\",\"drivers\":[\"aaudio\"],\"devices\":[],\"asio\":[],\"recommended\":\"aaudio\",\"vst\":{\"vst3\":[],\"vst2\":[]},"
                + "\"portaudio\":{\"engine\":\"android\",\"hostApis\":[\"Android\"],\"devices\":" + dev + "},\"audify\":null,\"engines\":{\"naudiodon\":false,\"audify\":false,\"android\":true}}", cors);
        } else if (path.equals("/api/license")) {
            send(out, 200, "{\"ok\":false,\"error\":\"the license is checked inside the app\"}", cors);      // the page verifies keys itself when the server has no license endpoint
        } else if (assets != null && !path.startsWith("/api/") && !path.startsWith("/ws/")) {
            serveAsset(out, path, cors);
        } else {
            send(out, 404, "{\"ok\":false,\"error\":\"not available in the Android app\"}", cors);
        }
    }

    private static final String[][] TYPES = { { ".html", "text/html; charset=utf-8" }, { ".js", "text/javascript; charset=utf-8" }, { ".css", "text/css; charset=utf-8" }, { ".json", "application/json" },
        { ".png", "image/png" }, { ".svg", "image/svg+xml" }, { ".ico", "image/x-icon" }, { ".woff2", "font/woff2" }, { ".woff", "font/woff" }, { ".ttf", "font/ttf" } };

    /** Only files of the bundled page, never anything outside it. */
    private void serveAsset(OutputStream out, String rawPath, Map<String, String> cors) throws IOException {
        String path = rawPath;
        try { path = java.net.URLDecoder.decode(rawPath.replace("+", "%2B"), "UTF-8"); } catch (Exception e) { /* keep the raw path */ }
        if (path.equals("/")) path = "/index.html";
        if (!path.startsWith("/") || path.contains("..") || path.contains("\\") || path.indexOf('\0') >= 0) { send(out, 403, "{\"ok\":false,\"error\":\"forbidden\"}", cors); return; }
        byte[] body = assets.read(path);
        if (body == null) { send(out, 404, "{\"ok\":false,\"error\":\"not found\"}", cors); return; }
        String type = "application/octet-stream", l = path.toLowerCase(Locale.ROOT);
        for (String[] t : TYPES) if (l.endsWith(t[0])) type = t[1];
        sendBytes(out, 200, body, type, cors);
    }

    private String streamsJson() {
        StringBuilder b = new StringBuilder("[");
        synchronized (sessions) {
            boolean first = true;
            for (Session s : sessions) { String j = s.json(); if (j == null) continue; if (!first) b.append(','); first = false; b.append(j); }
        }
        return b.append(']').toString();
    }

    // ── HTTP helpers ──
    private static String readHead(InputStream in) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        int m = 0;
        while (b.size() < 8192) {
            int c = in.read();
            if (c < 0) return null;
            b.write(c);
            m = (c == '\r' && (m == 0 || m == 2)) ? m + 1 : (c == '\n' && (m == 1 || m == 3)) ? m + 1 : (c == '\r') ? 1 : 0;
            if (m == 4) return new String(b.toByteArray(), 0, b.size() - 4, "ISO-8859-1");
        }
        return null;
    }

    private static void send(OutputStream out, int code, String json, Map<String, String> extra) throws IOException { sendRaw(out, code, json, "application/json", extra); }

    private static void sendRaw(OutputStream out, int code, String body, String type, Map<String, String> extra) throws IOException { sendBytes(out, code, body.getBytes("UTF-8"), type, extra); }

    private static void sendBytes(OutputStream out, int code, byte[] b, String type, Map<String, String> extra) throws IOException {
        StringBuilder h = new StringBuilder("HTTP/1.1 " + code + " " + (code == 200 ? "OK" : code == 204 ? "No Content" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed" : "Error")
            + "\r\nContent-Type: " + type + "\r\nContent-Length: " + b.length + "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n");
        if (extra != null) for (Map.Entry<String, String> e : extra.entrySet()) h.append(e.getKey()).append(": ").append(e.getValue()).append("\r\n");
        h.append("\r\n");
        synchronized (out) { out.write(h.toString().getBytes("ISO-8859-1")); out.write(b); out.flush(); }
    }

    // ── WebSocket: /ws/input (capture -> page) and /ws/output (page -> speaker / interface) ──
    private void websocket(Socket s, InputStream raw, OutputStream out, String path, String key) throws Exception {
        boolean input = path.equals("/ws/input"), output = path.equals("/ws/output");
        if ((!input && !output) || key == null) { send(out, 404, "{\"ok\":false,\"error\":\"not found\"}", null); return; }
        synchronized (out) {
            out.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + Ws.accept(key) + "\r\n\r\n").getBytes("ISO-8859-1"));
            out.flush();
        }
        s.setSoTimeout(0);
        Session session = new Session(out, input);
        synchronized (sessions) { sessions.add(session); }
        DataInputStream in = new DataInputStream(raw);
        try {
            while (true) {
                Ws.Message m = Ws.read(in, out);
                if (m == null) break;
                if (m.op == Ws.OP_TEXT) session.onText(new String(m.data, "UTF-8"));
                else if (m.op == Ws.OP_BIN) session.onBinary(m.data);
            }
        } catch (IOException e) { /* protocol violation or closed */ }
        finally { session.close(); synchronized (sessions) { sessions.remove(session); } }
    }

    /** One WebSocket: at most one open input or output stream. */
    private final class Session {
        private final OutputStream out; private final boolean input;
        private AudioBackend.Input in; private AudioBackend.Output outStream; private volatile long bytes;
        private Map<String, Object> info;

        Session(OutputStream out, boolean input) { this.out = out; this.input = input; }

        void text(String json) { byte[] b; try { b = json.getBytes("UTF-8"); } catch (Exception e) { return; } write(Ws.frame(Ws.OP_TEXT, b, 0, b.length)); }

        void write(byte[] frame) { try { synchronized (out) { out.write(frame); out.flush(); } } catch (IOException e) { close(); } }

        synchronized void onText(String text) {
            Map<String, Object> m = Json.parse(text);
            Object type = m.get("type");
            if ("start".equals(type)) start(m);
            else if ("stop".equals(type)) { closeStream(); text("{\"type\":\"stopped\"}"); }
        }

        void start(Map<String, Object> opts) {
            closeStream();
            try {
                if (input) in = audio.openInput(opts, new AudioBackend.Listener() {
                    public void onData(byte[] pcm, int len) { bytes += len; for (int i = 0; i < len; i += 16384) { int n = Math.min(16384, len - i); write(Ws.frame(Ws.OP_BIN, pcm, i, n)); } }
                    public void onError(String message) { text("{\"type\":\"error\",\"message\":" + Json.str(message) + "}"); }
                });
                else outStream = audio.openOutput(opts);
                info = (input ? in.info() : outStream.info());
                text(startedJson(info));
            } catch (Exception e) {
                closeStream();
                text("{\"type\":\"error\",\"message\":" + Json.str(String.valueOf(e.getMessage() == null ? e.toString() : e.getMessage())) + "}");
            }
        }

        synchronized void onBinary(byte[] data) {
            if (outStream == null || data.length % 2 != 0) return;      // whole Int16 samples only
            bytes += data.length;
            outStream.write(data, 0, data.length);
        }

        synchronized void closeStream() {
            if (in != null) { in.close(); in = null; }
            if (outStream != null) { outStream.close(); outStream = null; }
            info = null;
        }

        void close() { synchronized (this) { closeStream(); } try { out.close(); } catch (IOException e) { /* closed */ } }

        synchronized String json() {
            if (info == null) return null;
            return "{\"direction\":\"" + (input ? "input" : "output") + "\",\"engine\":\"android\",\"device\":" + Json.str(String.valueOf(info.get("device"))) + ",\"hostApi\":" + Json.str(String.valueOf(info.get("hostApi")))
                + ",\"sampleRate\":" + info.get("sampleRate") + ",\"channels\":" + info.get("channels") + ",\"bytes\":" + bytes + "}";
        }
    }

    static String startedJson(Map<String, Object> i) {
        return "{\"type\":\"started\",\"engine\":\"android\",\"device\":" + Json.str(String.valueOf(i.get("device"))) + ",\"hostApi\":" + Json.str(String.valueOf(i.get("hostApi")))
            + ",\"sampleRate\":" + i.get("sampleRate") + ",\"channels\":" + i.get("channels") + ",\"frameSize\":" + i.get("frameSize") + ",\"latencyMs\":" + i.get("latencyMs") + "}";
    }
}
