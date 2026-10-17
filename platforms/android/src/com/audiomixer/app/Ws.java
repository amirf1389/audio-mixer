package com.audiomixer.app;

import java.io.DataInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.security.MessageDigest;

/** RFC 6455 WebSocket, server side (text + binary, no extensions): the same framing rules as bridge/ws.js. No android.* classes: runs on a plain JVM. */
final class Ws {
    static final int MAX_PAYLOAD = 1 << 20;
    static final int OP_CONT = 0, OP_TEXT = 1, OP_BIN = 2, OP_CLOSE = 8, OP_PING = 9, OP_PONG = 10;
    private static final String GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    private Ws() {}

    /** Sec-WebSocket-Accept for a client key. */
    static String accept(String key) throws Exception {
        MessageDigest sha = MessageDigest.getInstance("SHA-1");
        return base64(sha.digest((key + GUID).getBytes("ISO-8859-1")));
    }

    /** java.util.Base64 needs Android 8: a small encoder keeps API 24 working. */
    static String base64(byte[] d) {
        final String A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < d.length; i += 3) {
            int n = (d[i] & 255) << 16 | (i + 1 < d.length ? (d[i + 1] & 255) << 8 : 0) | (i + 2 < d.length ? d[i + 2] & 255 : 0);
            b.append(A.charAt(n >> 18 & 63)).append(A.charAt(n >> 12 & 63)).append(i + 1 < d.length ? A.charAt(n >> 6 & 63) : '=').append(i + 2 < d.length ? A.charAt(n & 63) : '=');
        }
        return b.toString();
    }

    /** One server frame (never masked). */
    static byte[] frame(int op, byte[] payload, int off, int len) {
        int head = len < 126 ? 2 : len < 65536 ? 4 : 10;
        byte[] f = new byte[head + len];
        f[0] = (byte) (0x80 | op);
        if (len < 126) f[1] = (byte) len;
        else if (len < 65536) { f[1] = 126; f[2] = (byte) (len >> 8); f[3] = (byte) len; }
        else { f[1] = 127; for (int i = 0; i < 8; i++) f[2 + i] = (byte) (((long) len) >> (8 * (7 - i))); }
        System.arraycopy(payload, off, f, head, len);
        return f;
    }

    /** A complete message (continuation frames joined) or a control frame. */
    static final class Message { final int op; final byte[] data; Message(int op, byte[] data) { this.op = op; this.data = data; } }

    /** Reads frames until one complete message is there. Returns null when the peer closed; throws IOException on a protocol violation. */
    static Message read(DataInputStream in, OutputStream pongTo) throws IOException {
        java.io.ByteArrayOutputStream frag = null; int fragOp = 0;
        while (true) {
            int b0 = in.read();
            if (b0 < 0) return null;
            int b1 = in.read();
            if (b1 < 0) return null;
            boolean fin = (b0 & 0x80) != 0; int op = b0 & 0x0f;
            if ((b0 & 0x70) != 0) throw new IOException("RSV bits set");
            boolean masked = (b1 & 0x80) != 0; long len = b1 & 0x7f;
            if (op >= 8 && (!fin || len > 125)) throw new IOException("bad control frame");
            if (len == 126) len = (in.readUnsignedByte() << 8) | in.readUnsignedByte();
            else if (len == 127) { len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | in.readUnsignedByte(); }
            if (!masked || len < 0 || len > MAX_PAYLOAD) throw new IOException("unmasked or too large");
            byte[] mask = new byte[4]; in.readFully(mask);
            byte[] data = new byte[(int) len]; in.readFully(data);
            for (int i = 0; i < data.length; i++) data[i] ^= mask[i & 3];
            if (op == OP_CLOSE) return null;
            if (op == OP_PING) { if (pongTo != null) synchronized (pongTo) { pongTo.write(frame(OP_PONG, data, 0, data.length)); pongTo.flush(); } continue; }
            if (op == OP_PONG) continue;
            if (op == OP_TEXT || op == OP_BIN) {
                if (frag != null) throw new IOException("new message inside a fragmented one");
                if (fin) return new Message(op, data);
                frag = new java.io.ByteArrayOutputStream(); frag.write(data); fragOp = op;
            } else if (op == OP_CONT) {
                if (frag == null) throw new IOException("continuation without start");
                frag.write(data);
                if (frag.size() > MAX_PAYLOAD) throw new IOException("message too large");
                if (fin) return new Message(fragOp, frag.toByteArray());
            } else throw new IOException("unknown opcode");
        }
    }
}
