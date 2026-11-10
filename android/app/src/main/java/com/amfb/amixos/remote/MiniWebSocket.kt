package com.amfb.amixos.remote

import java.io.DataInputStream
import java.io.OutputStream
import java.net.Socket
import java.security.SecureRandom
import java.util.Base64
import javax.net.ssl.SSLSocketFactory

/** A minimal RFC 6455 client (text frames, client masking, ping / pong, close) for the daemon's /ws/meters stream: no library needed. */
class MiniWebSocket(private val host: String, private val port: Int, private val path: String, private val secure: Boolean = false) {
    private var socket: Socket? = null
    private var input: DataInputStream? = null
    private var output: OutputStream? = null
    private val rng = SecureRandom()
    val isOpen: Boolean get() = socket?.isClosed == false

    fun connect(timeoutMs: Int = 4000) {
        val s = if (secure) SSLSocketFactory.getDefault().createSocket(host, port) else Socket(host, port)
        s.soTimeout = timeoutMs
        val key = Base64.getEncoder().encodeToString(ByteArray(16).also { rng.nextBytes(it) })
        val req = "GET $path HTTP/1.1\r\nHost: $host:$port\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: $key\r\nSec-WebSocket-Version: 13\r\n\r\n"
        s.getOutputStream().write(req.toByteArray(Charsets.ISO_8859_1)); s.getOutputStream().flush()
        val inp = DataInputStream(s.getInputStream())
        val status = readLine(inp); if (!status.contains(" 101 ")) { s.close(); throw java.io.IOException("websocket upgrade refused: $status") }
        while (true) { val l = readLine(inp); if (l.isEmpty()) break }
        socket = s; input = inp; output = s.getOutputStream()
    }
    private fun readLine(inp: DataInputStream): String { val sb = StringBuilder(); while (true) { val c = inp.read(); if (c < 0) throw java.io.IOException("closed during handshake"); if (c == '\n'.toInt()) break; if (c != '\r'.toInt()) sb.append(c.toChar()) }; return sb.toString() }

    /** Blocks until a text message arrives; null on a clean close. Pings are answered. */
    fun receiveText(): String? {
        val inp = input ?: return null
        while (true) {
            val b0 = inp.read(); if (b0 < 0) return null
            val opcode = b0 and 0x0F; val b1 = inp.read(); if (b1 < 0) return null
            var len = (b1 and 0x7F).toLong(); if (len == 126L) len = inp.readUnsignedShort().toLong() else if (len == 127L) len = inp.readLong()
            val masked = (b1 and 0x80) != 0; val mask = ByteArray(4); if (masked) inp.readFully(mask)
            if (len > 16L * 1024 * 1024) throw java.io.IOException("frame too large")
            val data = ByteArray(len.toInt()); inp.readFully(data); if (masked) for (i in data.indices) data[i] = (data[i].toInt() xor mask[i % 4].toInt()).toByte()
            when (opcode) {
                0x1 -> return String(data, Charsets.UTF_8)
                0x9 -> send(0xA, data)
                0x8 -> { runCatching { send(0x8, data) }; close(); return null }
                else -> {}
            }
        }
    }
    fun sendText(text: String) = send(0x1, text.toByteArray(Charsets.UTF_8))
    private fun send(opcode: Int, payload: ByteArray) {
        val out = output ?: return; val mask = ByteArray(4).also { rng.nextBytes(it) }
        val head = java.io.ByteArrayOutputStream(); head.write(0x80 or opcode)
        when { payload.size < 126 -> head.write(0x80 or payload.size); payload.size < 65536 -> { head.write(0x80 or 126); head.write(payload.size shr 8); head.write(payload.size and 0xFF) }
            else -> { head.write(0x80 or 127); for (i in 7 downTo 0) head.write(((payload.size.toLong() shr (8 * i)) and 0xFF).toInt()) } }
        head.write(mask); val masked = ByteArray(payload.size) { i -> (payload[i].toInt() xor mask[i % 4].toInt()).toByte() }
        synchronized(out) { out.write(head.toByteArray()); out.write(masked); out.flush() }
    }
    fun close() { runCatching { socket?.close() }; socket = null }
}
