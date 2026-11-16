package com.amfb.amixos

import com.amfb.amixos.remote.MiniJson

/** PAIRED CONSOLES: every console this phone has paired with (its own token each), kept on the phone so switching between a studio and a
 *  stage console is one tap. Stored as one JSON list in SharedPreferences "consoles"; the token stays on the phone and is never shown. */
object PairedConsoles {
    data class Console(val host: String, val port: Int, val token: String, val name: String, val pairedAt: Long, val lastSeen: Long) {
        val key: String get() = "$host:$port"
    }
    fun parse(json: String?): List<Console> {
        @Suppress("UNCHECKED_CAST") val l = runCatching { MiniJson.parse(json.orEmpty().ifBlank { "[]" }) as List<Map<String, Any?>> }.getOrDefault(emptyList())
        return l.mapNotNull { m -> val h = m["host"]?.toString().orEmpty(); val t = m["token"]?.toString().orEmpty(); val p = (m["port"] as? Number)?.toInt() ?: 8765
            if (h.isBlank() || t.isBlank()) null else Console(h, p, t, m["name"]?.toString().orEmpty(), (m["pairedAt"] as? Number)?.toLong() ?: 0L, (m["lastSeen"] as? Number)?.toLong() ?: 0L) }
            .distinctBy { it.key }
    }
    fun format(list: List<Console>): String = MiniJson.stringify(list.map { mapOf("host" to it.host, "port" to it.port, "token" to it.token, "name" to it.name, "pairedAt" to it.pairedAt, "lastSeen" to it.lastSeen) })
    /** adds the console or updates the one at the same address (a new token after pairing again replaces the old one); most recent first */
    fun upsert(list: List<Console>, c: Console): List<Console> {
        val old = list.firstOrNull { it.key == c.key }
        val merged = c.copy(name = c.name.ifBlank { old?.name.orEmpty() }, pairedAt = if (old != null && old.token == c.token) old.pairedAt else c.pairedAt)
        return listOf(merged) + list.filter { it.key != c.key }
    }
    fun forget(list: List<Console>, key: String): List<Console> = list.filter { it.key != key }
    /** "2 min ago", "3 h ago", "5 days ago", "never" */
    fun ago(now: Long, t: Long): String { if (t <= 0) return "never"; val s = (now - t) / 1000
        return when { s < 60 -> "just now"; s < 3600 -> "${s / 60} min ago"; s < 86400 -> "${s / 3600} h ago"; else -> "${s / 86400} days ago" } }
    /** the delay before reconnect attempt n (1, 2, 3 ...): 1, 2, 4, 8 s, then every 15 s */
    fun retryDelayMs(attempt: Int): Long = if (attempt <= 0) 1000L else minOf(15000L, 1000L shl minOf(attempt - 1, 4)).let { if (attempt >= 5) 15000L else it }
}
