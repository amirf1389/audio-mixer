package com.amfb.amixos

import com.amfb.amixos.remote.ConsoleState
import com.amfb.amixos.remote.MiniJson

/** OFFLINE SCENES: scenes made and edited on the phone without a console (on the bus, the night before), recalled when it is there.
 *  A scene is the same JSON the SCENES page saves from a console: {t, master, strips:[{id,name,db,pan,mute,solo}], buses:[{id,db,mute}]}
 *  plus "offline": true when it was made or changed away from a console. One scene may be marked to recall on connect: the phone then
 *  asks first (a live console is never changed without a tap). Pure functions here; the screens are in RemoteActivity. */
object OfflineScenes {
    /** a stored scene, or null when it is not a scene (a damaged or foreign entry no longer crashes the SCENES list) */
    @Suppress("UNCHECKED_CAST")
    fun read(stored: Any?): Map<String, Any?>? = (stored as? String)?.let { runCatching { MiniJson.parse(it) as? Map<String, Any?> }.getOrNull() }
    /** the console's last state as the phone saw it (kept so an offline scene starts from the real channel names and levels) */
    fun stateJson(s: ConsoleState): String = MiniJson.stringify(mapOf("master" to s.masterDb,
        "strips" to s.strips.map { mapOf("id" to it.id, "name" to it.name, "db" to it.db, "pan" to it.pan, "mute" to it.mute, "solo" to it.solo) },
        "buses" to s.buses.map { mapOf("id" to it.id, "db" to it.db, "mute" to it.mute) }))
    /** a new scene: from the last console state when there is one, else [strips] flat channels at 0 dB named CH 1 .. */
    @Suppress("UNCHECKED_CAST")
    fun blank(lastState: String?, strips: Int, now: Long): MutableMap<String, Any?> {
        val last = runCatching { MiniJson.parse(lastState.orEmpty()) as Map<String, Any?> }.getOrNull()
        val m = LinkedHashMap<String, Any?>(); m["t"] = now; m["offline"] = true
        if (last != null && (last["strips"] as? List<*>)?.isNotEmpty() == true) { m["master"] = last["master"]; m["strips"] = last["strips"]; m["buses"] = last["buses"] ?: emptyList<Any>() }
        else { m["master"] = 0.0; m["strips"] = (0 until strips.coerceIn(1, 64)).map { mapOf("id" to it, "name" to "CH ${it + 1}", "db" to 0.0, "pan" to 0.0, "mute" to false, "solo" to false) }; m["buses"] = emptyList<Any>() }
        return m
    }
    @Suppress("UNCHECKED_CAST") fun parse(json: String?): MutableMap<String, Any?>? = runCatching { LinkedHashMap(MiniJson.parse(json.orEmpty()) as Map<String, Any?>) }.getOrNull()
    /** one channel changed in a scene (level clamped to -60 .. +10 dB, -60 is off); the scene becomes an offline edit with a new time */
    @Suppress("UNCHECKED_CAST")
    fun setStrip(scene: MutableMap<String, Any?>, id: Int, now: Long, db: Double? = null, mute: Boolean? = null, name: String? = null): MutableMap<String, Any?> {
        scene["strips"] = (scene["strips"] as? List<Map<String, Any?>>).orEmpty().map { s -> if ((s["id"] as? Number)?.toInt() != id) s else LinkedHashMap(s).apply {
            if (db != null) put("db", Math.round(db.coerceIn(-60.0, 10.0) * 10) / 10.0); if (mute != null) put("mute", mute)
            if (name != null) put("name", name.trim().take(16).ifEmpty { "CH ${id + 1}" }) } }
        scene["t"] = now; scene["offline"] = true; return scene
    }
    fun setMaster(scene: MutableMap<String, Any?>, db: Double, now: Long): MutableMap<String, Any?> { scene["master"] = Math.round(db.coerceIn(-60.0, 10.0) * 10) / 10.0; scene["t"] = now; scene["offline"] = true; return scene }
    /** the scene as the console gets it: only the channels this console has (a scene from a bigger plan skips the extra ones) */
    @Suppress("UNCHECKED_CAST")
    fun forConsole(scene: Map<String, Any?>, consoleStrips: Int): List<Map<String, Any?>> = (scene["strips"] as? List<Map<String, Any?>>).orEmpty().filter { ((it["id"] as? Number)?.toInt() ?: Int.MAX_VALUE) < consoleStrips }
    fun stringify(scene: Map<String, Any?>): String = MiniJson.stringify(scene)
}
