package com.amfb.amixos

/** PLAYERS: a shared link of YouTube, Spotify, TIDAL or Deezer becomes the address of that service's own embedded player.
 *  Only the services' official embed pages are used; nothing is downloaded, captured or sent through the mixer. */
object StreamLinks {
    enum class Service(val title: String, val hosts: List<String>) {
        YOUTUBE("YouTube", listOf("youtube.com", "youtu.be", "youtube-nocookie.com", "ytimg.com", "googlevideo.com", "google.com", "gstatic.com", "ggpht.com", "googleusercontent.com", "doubleclick.net")),
        SPOTIFY("Spotify", listOf("spotify.com", "scdn.co", "spotifycdn.com")),
        TIDAL("TIDAL", listOf("tidal.com")),
        DEEZER("Deezer", listOf("deezer.com", "dzcdn.net"))
    }
    data class Embed(val service: Service, val url: String)

    private val id = Regex("^[A-Za-z0-9_-]{1,64}$")
    private fun ok(s: String?) = s != null && id.matches(s)

    /** the embed address for a pasted link, or null when it is not a link of one of the four services */
    fun parse(link: String): Embed? {
        val t = link.trim(); if (t.length > 2000) return null
        val m = Regex("^(?:https?://)?([^/?#]+)([^?#]*)(?:\\?([^#]*))?(?:#.*)?$", RegexOption.IGNORE_CASE).find(t) ?: return null
        val host = m.groupValues[1].lowercase().removePrefix("www.").removePrefix("m."); if (host.contains('@') || host.contains(':')) return null
        val path = m.groupValues[2].split('/').filter { it.isNotEmpty() }
        val query = m.groupValues[3].split('&').mapNotNull { val i = it.indexOf('='); if (i > 0) it.substring(0, i) to it.substring(i + 1) else null }.toMap()
        return when (host) {
            "youtu.be" -> path.firstOrNull()?.takeIf(::ok)?.let { Embed(Service.YOUTUBE, "https://www.youtube-nocookie.com/embed/$it") }
            "youtube.com", "music.youtube.com", "youtube-nocookie.com" -> {
                val v = query["v"]; val list = query["list"]
                when {
                    ok(v) -> Embed(Service.YOUTUBE, "https://www.youtube-nocookie.com/embed/$v" + (if (ok(list)) "?list=$list" else ""))
                    path.size == 2 && path[0] in setOf("shorts", "embed", "live") && ok(path[1]) -> Embed(Service.YOUTUBE, "https://www.youtube-nocookie.com/embed/${path[1]}")
                    path.firstOrNull() == "playlist" && ok(list) -> Embed(Service.YOUTUBE, "https://www.youtube-nocookie.com/embed/videoseries?list=$list")
                    else -> null
                }
            }
            "open.spotify.com" -> {
                val p = path.filterNot { it.startsWith("intl-") || it == "embed" }
                if (p.size >= 2 && p[0] in setOf("track", "album", "playlist", "artist", "episode", "show") && ok(p[1])) Embed(Service.SPOTIFY, "https://open.spotify.com/embed/${p[0]}/${p[1]}") else null
            }
            "tidal.com", "listen.tidal.com", "embed.tidal.com" -> {
                val p = path.filterNot { it == "browse" }
                val kind = mapOf("track" to "tracks", "tracks" to "tracks", "album" to "albums", "albums" to "albums", "playlist" to "playlists", "playlists" to "playlists", "video" to "videos", "videos" to "videos")[p.getOrNull(0)]
                if (kind != null && ok(p.getOrNull(1))) Embed(Service.TIDAL, "https://embed.tidal.com/$kind/${p[1]}") else null
            }
            "deezer.com", "widget.deezer.com" -> {
                val i = path.indexOfFirst { it in setOf("track", "album", "playlist", "episode", "show") }
                if (i >= 0 && ok(path.getOrNull(i + 1))) Embed(Service.DEEZER, "https://widget.deezer.com/widget/dark/${path[i]}/${path[i + 1]}") else null
            }
            else -> null
        }
    }
    /** may the player's own page load this address? (its own hosts over https only; everything else stays out of the app) */
    fun allowed(service: Service, url: String): Boolean {
        val m = Regex("^https://([^/?#:@]+)(?:[/?#].*)?$", RegexOption.IGNORE_CASE).find(url.trim()) ?: return false
        val h = m.groupValues[1].lowercase(); return service.hosts.any { h == it || h.endsWith(".$it") }
    }
}
