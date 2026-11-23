package com.amfb.amixos.remote

/** PER-APP VOLUME of the console's computer: one program as the Windows volume mixer lists it (volume 0..100). */
data class AppVolume(val id: String, val name: String, val pid: Long, val volume: Double, val mute: Boolean, val system: Boolean)

/** the programs from the console's state ("apps"), and the commands that move them; checked here before they go */
object AppVolumes {
    fun parse(o: Map<String, Any?>): List<AppVolume> = o.list("apps").mapNotNull {
        val id = it["id"] as? String ?: return@mapNotNull null
        if (id.isEmpty()) null else AppVolume(id, it.str("name").ifEmpty { id }.take(80), it.num("pid").toLong(), it.num("volume", 100.0).coerceIn(0.0, 100.0), it["mute"] == true, it["system"] == true)
    }
    fun available(o: Map<String, Any?>): Boolean = o["appsAvailable"] == true
    /** {"op":"appVolume","id":..,"volume":0..100} (whole percent); null for an empty id or no number */
    fun volumeCommand(id: String, volume: Double): Map<String, Any?>? = if (id.isEmpty() || volume.isNaN()) null else mapOf("op" to "appVolume", "id" to id, "volume" to Math.round(volume.coerceIn(0.0, 100.0)).toInt())
    fun muteCommand(id: String, on: Boolean): Map<String, Any?>? = if (id.isEmpty()) null else mapOf("op" to "appMute", "id" to id, "on" to on)
    /** the row text: "Spotify  80%" / "Spotify  MUTED" */
    fun label(a: AppVolume): String = a.name + "  " + if (a.mute) "MUTED" else "${Math.round(a.volume)}%"
}
