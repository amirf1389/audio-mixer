package com.amfb.amixos

import android.content.Context
import com.amfb.amixos.remote.MiniJson

/** HOME's memory: the pages opened last (RECENT), the tiles pinned to the top (long press), and WHAT'S NEW from the release feed.
 *  Kept in SharedPreferences "home"; the functions on lists are pure, so they are tested without a phone. */
object Home {
    const val MAX_RECENT = 4
    private fun prefs(c: Context) = c.getSharedPreferences("home", Context.MODE_PRIVATE)

    // ---- RECENT: page ids (the activity's class name), newest first, no repeats
    fun addRecent(list: List<String>, id: String, max: Int = MAX_RECENT): List<String> = (listOf(id) + list.filter { it != id }).take(max)
    fun recent(c: Context): List<String> = prefs(c).getString("recent", "").orEmpty().split(',').filter { it.isNotBlank() }
    fun noteRecent(c: Context, id: String) { if (id == "HomeActivity") return; prefs(c).edit().putString("recent", addRecent(recent(c), id).joinToString(",")).apply() }

    // ---- PINNED tiles: a set of tile ids; pinned tiles come first, in their usual order
    fun pinned(c: Context): Set<String> = prefs(c).getStringSet("pinned", emptySet()).orEmpty().toSet()
    fun togglePin(c: Context, id: String): Boolean { val s = pinned(c).toMutableSet(); val on = s.add(id) || run { s.remove(id); false }; prefs(c).edit().putStringSet("pinned", s).apply(); return on }
    fun <T> pinnedFirst(tiles: List<T>, id: (T) -> String, pins: Set<String>): List<T> = tiles.filter { id(it) in pins } + tiles.filter { id(it) !in pins }

    // ---- LAST SCENE: the scene recalled last on a console (RECALL LAST SCENE on HOME)
    fun lastScene(c: Context): String = prefs(c).getString("lastScene", "").orEmpty()
    fun setLastScene(c: Context, name: String) { prefs(c).edit().putString("lastScene", name).apply() }

    // ---- WHAT'S NEW: the entry of this version in the release feed (features.json), dismissed per version
    data class News(val version: String, val title: String, val items: List<String>)
    /** the feed's entry for [version] ("1.24.0.0", a build suffix ignored), or null */
    @Suppress("UNCHECKED_CAST")
    fun newsFor(feedJson: String?, version: String): News? {
        val v = version.substringBefore(' ').trim()
        val l = runCatching { (MiniJson.parse(feedJson.orEmpty()) as Map<String, Any?>)["features"] as List<Map<String, Any?>> }.getOrNull() ?: return null
        val e = l.firstOrNull { it["version"]?.toString() == v } ?: return null
        return News(v, e["title"]?.toString().orEmpty(), (e["items"] as? List<*>).orEmpty().map { it.toString() })
    }
    fun news(c: Context, version: String): News? = runCatching { c.assets.open("features.json").bufferedReader().use { it.readText() } }.getOrNull()?.let { newsFor(it, version) }
    fun newsSeen(c: Context, version: String) = prefs(c).getString("newsSeen", "") == version.substringBefore(' ')
    fun setNewsSeen(c: Context, version: String) { prefs(c).edit().putString("newsSeen", version.substringBefore(' ')).apply() }
}
