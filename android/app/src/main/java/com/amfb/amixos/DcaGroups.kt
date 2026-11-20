package com.amfb.amixos

import android.content.SharedPreferences

/** GROUP FADERS (DCA) of the phone's own mixer: up to four groups. A channel belongs to at most one group. The group's fader does not
 *  move its members' faders: it adds its level (a dB offset) to each member's own level, and the group's MUTE mutes every member.
 *  Channels are named as the mixer's faders are: input s = s (0, 1), deck d = 100 + d. Kept in the phone mixer's prefs. */
object Dca {
    const val COUNT = 4
    /** at or below this the engine's channel is off (the mixer's faders send -60 dB for "fully down") */
    const val FLOOR = -60.0
    const val MAX_OFFSET = 10.0
    /** the loudest a channel gets, its fader and its group together */
    const val CEILING = 12.0
    fun groupOf(p: SharedPreferences, ch: Int): Int = p.getInt("dcaOf$ch", -1).takeIf { it in 0 until COUNT } ?: -1
    fun setGroup(p: SharedPreferences, ch: Int, g: Int) { p.edit().putInt("dcaOf$ch", if (g in 0 until COUNT) g else -1).apply() }
    /** the key under a channel in SETUP: none → 1 → 2 → 3 → 4 → none */
    fun cycle(p: SharedPreferences, ch: Int): Int { val n = groupOf(p, ch) + 1; val g = if (n >= COUNT) -1 else n; setGroup(p, ch, g); return g }
    fun members(p: SharedPreferences, g: Int, channels: List<Int>): List<Int> = channels.filter { groupOf(p, it) == g }
    fun level(p: SharedPreferences, g: Int): Double = p.getFloat("dcaDb$g", 0f).toDouble()
    fun setLevel(p: SharedPreferences, g: Int, db: Double) { p.edit().putFloat("dcaDb$g", db.coerceIn(FLOOR, MAX_OFFSET).toFloat()).apply() }
    fun muted(p: SharedPreferences, g: Int): Boolean = p.getBoolean("dcaMute$g", false)
    fun setMuted(p: SharedPreferences, g: Int, on: Boolean) { p.edit().putBoolean("dcaMute$g", on).apply() }
    /** the level the engine gets: the channel's own fader plus its group's offset; a fader or a group fully down stays off */
    fun effectiveDb(faderDb: Double, groupDb: Double?): Double {
        if (faderDb <= FLOOR || (groupDb != null && groupDb <= FLOOR)) return FLOOR
        return (faderDb + (groupDb ?: 0.0)).coerceIn(FLOOR, CEILING)
    }
    fun effectiveDb(p: SharedPreferences, ch: Int, faderDb: Double): Double { val g = groupOf(p, ch); return effectiveDb(faderDb, if (g >= 0) level(p, g) else null) }
    /** muted by its own MUTE or by its group's */
    fun effectiveMute(p: SharedPreferences, ch: Int, own: Boolean): Boolean { val g = groupOf(p, ch); return own || (g >= 0 && muted(p, g)) }
}
