package com.amfb.amixos

import android.content.SharedPreferences

/** MUTE GROUPS kept on the phone: eight groups, each a set of console strips muted and un-muted together with one tap. The console's
 *  remote service knows single strips only, so a group is sent as one mute command per member. Stored as "3,4,7" per group. */
object MuteGroups {
    const val COUNT = 8
    fun parse(s: String?): Set<Int> = s.orEmpty().split(',').mapNotNull { it.trim().toIntOrNull() }.filter { it >= 0 }.toSortedSet()
    fun format(members: Set<Int>): String = members.sorted().joinToString(",")
    fun members(p: SharedPreferences, group: Int): Set<Int> = parse(p.getString("g$group", ""))
    fun setMembers(p: SharedPreferences, group: Int, m: Set<Int>) = p.edit().putString("g$group", format(m)).apply()
    fun active(p: SharedPreferences, group: Int): Boolean = p.getBoolean("on$group", false)
    fun setActive(p: SharedPreferences, group: Int, on: Boolean) = p.edit().putBoolean("on$group", on).apply()
    /** What switching a group does: the strips to send and the mute state for each. Un-muting a group leaves muted any strip that
     *  another ACTIVE group still holds. */
    fun plan(groups: List<Set<Int>>, activeNow: List<Boolean>, group: Int, on: Boolean): List<Pair<Int, Boolean>> {
        val heldByOthers = groups.indices.filter { it != group && activeNow[it] }.flatMap { groups[it] }.toSet()
        return groups[group].sorted().mapNotNull { s -> if (on) s to true else if (s in heldByOthers) null else s to false }
    }
    /** "CH 4, 5, 8" (1-based, as the console shows them) or a dash for an empty group */
    fun label(members: Set<Int>): String = if (members.isEmpty()) "—" else "CH " + members.sorted().joinToString(", ") { (it + 1).toString() }
}
