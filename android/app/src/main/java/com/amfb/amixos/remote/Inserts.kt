package com.amfb.amixos.remote

/** CHANNEL INSERTS on the console, as a remote sees them: up to [MAX] slots per channel (0..MAX-1). Plug-ins are added by the opaque id
 *  of the console's own list (never a file path); every command is checked again by the console. Pure: parsing and the commands. */
object Inserts {
    const val MAX = 10
    data class Entry(val id: String, val name: String, val vendor: String, val format: String, val builtIn: Boolean) { val label get() = if (builtIn) "$name · built-in" else listOf(name, format, vendor).filter { it.isNotBlank() }.joinToString(" · ") }
    data class Param(val id: Long, val title: String, val value: Double, val text: String, val steps: Int)
    data class Preset(val id: Int, val name: String)
    data class Slot(val slot: Int, val name: String, val bypass: Boolean, val loaded: Boolean, val note: String, val params: List<Param>, val presets: List<Preset>)

    private fun Map<*, *>.s(k: String) = this[k]?.toString().orEmpty()
    private fun Map<*, *>.n(k: String) = (this[k] as? Number)?.toDouble() ?: 0.0
    fun catalog(o: Map<String, Any?>): List<Entry> = (o["plugins"] as? List<*>).orEmpty().mapNotNull { it as? Map<*, *> }
        .filter { Regex("^[0-9a-f]{16}$").matches(it.s("id")) }.map { Entry(it.s("id"), it.s("name"), it.s("vendor"), it.s("format"), it["fx"] == true) }
    fun slots(racks: Map<String, Any?>): List<Slot> = ((racks["racks"] as? List<*>)?.firstOrNull() as? Map<*, *>)?.let { r -> (r["slots"] as? List<*>).orEmpty().mapNotNull { it as? Map<*, *> }.map { s ->
        Slot(s.n("slot").toInt(), s.s("name"), s["bypass"] == true, s["loaded"] == true, if (s["loaded"] == true) listOf(s.s("vendor"), s.s("format")).filter { it.isNotBlank() }.joinToString(" ") else s.s("error"),
            (s["params"] as? List<*>).orEmpty().mapNotNull { it as? Map<*, *> }.map { p -> Param(p.n("id").toLong(), p.s("title"), p.n("value").coerceIn(0.0, 1.0), p.s("text"), p.n("steps").toInt()) },
            (s["presets"] as? List<*>).orEmpty().mapNotNull { it as? Map<*, *> }.map { p -> Preset(p.n("id").toInt(), p.s("name")) }) }.sortedBy { it.slot } }.orEmpty()
    fun firstFree(slots: List<Slot>): Int = (0 until MAX).firstOrNull { k -> slots.none { it.slot == k } } ?: -1
    fun inRange(slot: Int) = slot in 0 until MAX
    fun add(strip: Int, id: String, slot: Int? = null): Map<String, Any?> { require(Regex("^[0-9a-f]{16}$").matches(id)) { "not a plug-in id" }; slot?.let { require(inRange(it)) }
        return buildMap { put("op", "insert"); put("strip", strip); put("action", "add"); put("id", id); slot?.let { put("slot", it) } } }
    fun remove(strip: Int, slot: Int): Map<String, Any?> { require(inRange(slot)); return mapOf("op" to "insert", "strip" to strip, "action" to "remove", "slot" to slot) }
    fun move(strip: Int, slot: Int, to: Int): Map<String, Any?> { require(inRange(slot) && inRange(to)); return mapOf("op" to "insert", "strip" to strip, "action" to "move", "slot" to slot, "to" to to) }
}
