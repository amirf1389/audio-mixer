package com.amfb.amixos.remote

import java.net.URI

/** THE PAIRING QR CODE the console shows (NETWORK → PHONE REMOTE): http://<console address>:<port>/remote#pin=<six digits>.
 *  A phone camera opens it in the browser remote; SCAN QR in this app reads the console's address, port and the minute's PIN from it,
 *  pairs (the PIN is traded for this phone's own token) and connects. Anything else is refused: only a private-network address
 *  (where the console's service answers) and a six-digit PIN are accepted, so a stray QR code cannot point the app somewhere else. */
data class PairLink(val host: String, val port: Int, val pin: String) {
    companion object {
        fun parse(raw: String?): PairLink? {
            val u = runCatching { URI(raw?.trim() ?: return null) }.getOrNull() ?: return null
            if (u.scheme?.lowercase() != "http") return null
            val host = u.host ?: return null
            if (!isPrivate(host)) return null
            val port = if (u.port in 1..65535) u.port else 8765
            val pin = Regex("(?:^|&)pin=(\\d{6})(?:&|$)").find(u.rawFragment ?: "")?.groupValues?.get(1) ?: return null
            return PairLink(host, port, pin)
        }
        /** 10.x, 172.16–31.x, 192.168.x: the addresses the console's service answers on */
        fun isPrivate(ip: String): Boolean {
            val p = ip.split('.').map { it.toIntOrNull() ?: return false }; if (p.size != 4 || p.any { it !in 0..255 }) return false
            return p[0] == 10 || (p[0] == 172 && p[1] in 16..31) || (p[0] == 192 && p[1] == 168)
        }
    }
}
