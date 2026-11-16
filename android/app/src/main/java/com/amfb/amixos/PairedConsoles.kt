package com.amfb.amixos

import com.amfb.amixos.remote.MiniJson

/** PAIRED CONSOLES: every console this phone has paired with (its own token each), kept on the phone so switching between a studio and a
 *  stage console is one tap. Stored as one JSON list in SharedPreferences "consoles"; the token stays on the phone and is never shown. */
object PairedConsoles {
    data class Console(val host: String, val port: Int, val token: String, val name: String, val pairedAt: Long, val lastSeen: Long, val id: String = "") {   // id: the console's consoleId (follows it to a new address)
        val key: String get() = "$host:$port"
    }
    fun parse(json: String?): List<Console> {
        @Suppress("UNCHECKED_CAST") val l = runCatching { MiniJson.parse(json.orEmpty().ifBlank { "[]" }) as List<Map<String, Any?>> }.getOrDefault(emptyList())
        return l.mapNotNull { m -> val h = m["host"]?.toString().orEmpty(); val t = m["token"]?.toString().orEmpty(); val p = (m["port"] as? Number)?.toInt() ?: 8765
            if (h.isBlank() || t.isBlank()) null else Console(h, p, t, m["name"]?.toString().orEmpty(), (m["pairedAt"] as? Number)?.toLong() ?: 0L, (m["lastSeen"] as? Number)?.toLong() ?: 0L, m["id"]?.toString().orEmpty()) }
            .distinctBy { it.key }
    }
    fun format(list: List<Console>): String = MiniJson.stringify(list.map { mapOf("host" to it.host, "port" to it.port, "token" to it.token, "name" to it.name, "pairedAt" to it.pairedAt, "lastSeen" to it.lastSeen, "id" to it.id) })
    /** adds the console or updates the one at the same address (a new token after pairing again replaces the old one); most recent first */
    fun upsert(list: List<Console>, c: Console): List<Console> {
        // the same console: the same address, or the same consoleId at a new address (the PC got another IP from the router)
        fun same(o: Console) = o.key == c.key || (c.id.isNotEmpty() && o.id == c.id)
        val old = list.firstOrNull { it.key == c.key } ?: list.firstOrNull { same(it) }
        val merged = c.copy(name = c.name.ifBlank { old?.name.orEmpty() }, pairedAt = if (old != null && old.token == c.token) old.pairedAt else c.pairedAt, id = c.id.ifBlank { old?.id.orEmpty() })
        return listOf(merged) + list.filter { !same(it) }
    }
    /** AUTO-DETECT: what the consoles found on the Wi-Fi mean for this phone.
     *  KNOWN: a paired console is there (matched by its consoleId, so a new address is followed; by its address for a console paired
     *  before ids existed, unless that address now answers with a different id). Most recently used first. Its host is the found one.
     *  NEW: only consoles this phone never paired with: they need the PIN once. NONE: nothing answers. */
    sealed class Match { data class Known(val console: Console, val moved: Boolean) : Match(); data class New(val found: List<com.amfb.amixos.remote.DaemonClient.Companion.Found>) : Match(); object None : Match() }
    fun match(paired: List<Console>, found: List<com.amfb.amixos.remote.DaemonClient.Companion.Found>): Match {
        if (found.isEmpty()) return Match.None
        for (c in paired) {
            val byId = if (c.id.isNotEmpty()) found.firstOrNull { it.id == c.id } else null
            val hit = byId ?: found.firstOrNull { it.host == c.host && it.port == c.port && (c.id.isEmpty() || it.id.isEmpty()) } ?: continue
            return Match.Known(c.copy(host = hit.host, port = hit.port, id = hit.id.ifEmpty { c.id }), moved = hit.host != c.host || hit.port != c.port)
        }
        return Match.New(found)
    }
    /** the hosts to ask first when searching: the addresses of the paired consoles, most recent first */
    fun hints(paired: List<Console>): List<String> = paired.map { it.host }.distinct()
    /** the delay before the next search while no console answers: 3 s, 6 s, 12 s, then every 20 s (cheap: one status call per address) */
    fun searchDelayMs(round: Int): Long = when { round <= 1 -> 3000L; round == 2 -> 6000L; round == 3 -> 12000L; else -> 20000L }
    fun forget(list: List<Console>, key: String): List<Console> = list.filter { it.key != key }
    /** "2 min ago", "3 h ago", "5 days ago", "never" */
    fun ago(now: Long, t: Long): String { if (t <= 0) return "never"; val s = (now - t) / 1000
        return when { s < 60 -> "just now"; s < 3600 -> "${s / 60} min ago"; s < 86400 -> "${s / 3600} h ago"; else -> "${s / 86400} days ago" } }
    /** the delay before reconnect attempt n (1, 2, 3 ...): 1, 2, 4, 8 s, then every 15 s */
    fun retryDelayMs(attempt: Int): Long = if (attempt <= 0) 1000L else minOf(15000L, 1000L shl minOf(attempt - 1, 4)).let { if (attempt >= 5) 15000L else it }
}

/** THE LINK TIMELINE: the last few things the link did, newest last ("12:01:05  searching the Wi-Fi", "12:01:06  found AmF X1 at
 *  192.168.1.20", "12:01:06  connected · live sync"), so the phone shows how it got to the console, not only where it is now. */
class LinkTimeline(private val max: Int = 6) {
    data class Event(val at: Long, val text: String)
    private val events = ArrayList<Event>()
    val items: List<Event> get() = events.toList()
    /** adds an event; the same text twice in a row only moves its time (a search that keeps finding nothing is one line) */
    fun add(at: Long, text: String) { if (events.lastOrNull()?.text == text) events[events.size - 1] = Event(at, text) else events.add(Event(at, text)); while (events.size > max) events.removeAt(0) }
    fun render(fmt: (Long) -> String): String = events.joinToString("\n") { fmt(it.at) + "  " + it.text }
}
