package com.amfb.amixos

import android.content.SharedPreferences

/** QUICK CHAINS of the phone's own mixer: one tap fills a channel's insert rack with a ready chain of AmF X1 effects (and their
 *  settings), instead of picking four effects one by one. A chain replaces what the rack held. Used on the mixer itself and from the
 *  THIS PHONE section of HOME. The lists are pure (tested without a phone); the kinds are the engine's own rack effects. */
object LocalChains {
    /** one effect of a chain: its kind and the knobs that differ from the effect's own start values (0 .. 1) */
    data class Fx(val kind: String, val params: Map<String, Double> = emptyMap())
    data class Chain(val id: String, val glyph: String, val title: String, val sub: String, val slots: List<Fx>)

    val chains: List<Chain> = listOf(
        Chain("voice", "🎤", "VOICE", "Speech that stays clear and even: noise off, level held, sharp S softened", listOf(Fx("clean"), Fx("rider"), Fx("deesser"))),
        Chain("podcast", "🎧", "PODCAST", "A close, steady voice: noise off, level held, gentle levelling, a little air", listOf(Fx("clean"), Fx("rider"), Fx("opto"), Fx("exciter"))),
        Chain("stream", "📡", "LIVE STREAM", "For a phone stream: noise off, level held, glued and loud without clipping", listOf(Fx("clean"), Fx("rider"), Fx("glue"), Fx("maximizer"))),
        Chain("karaoke", "🎶", "KARAOKE", "A singing voice with a bounce and a hall around it",
            listOf(Fx("rider"), Fx("doubler"), Fx("pingpong", mapOf("left" to 0.1351, "right" to 0.2095, "feedback" to 0.35, "tone" to 0.5, "spread" to 1.0, "lowcut" to 0.3)), Fx("hall"))),   // 220 ms left, 330 ms right
        Chain("singer", "🎙", "SINGER", "A lead vocal: S softened, levelled, a room, a little air", listOf(Fx("deesser"), Fx("opto"), Fx("room"), Fx("exciter"))),
        Chain("nopeaks", "⚡", "NO PEAKS", "Clicks, pops and hits taken out, the rest of the sound untouched; then the level held", listOf(Fx("peakremover", mapOf("sens" to 0.5, "amount" to 0.5, "release" to 0.2, "listen" to 0.0)), Fx("rider"))),
        Chain("clear", "∅", "EMPTY RACK", "Takes every effect off the channel", emptyList()))

    fun byId(id: String): Chain? = chains.firstOrNull { it.id == id }
    /** the rack as the mixer keeps it: one entry per slot, "kind" or "", joined with | ([max] slots; a longer chain is cut) */
    fun rackString(c: Chain, max: Int): String = (c.slots.map { it.kind } + List(max) { "" }).take(max).joinToString("|")
    /** a slot's knobs as the mixer keeps them: id=value;id=value */
    fun paramString(f: Fx): String = f.params.entries.joinToString(";") { it.key + "=" + String.format(java.util.Locale.US, "%.4f", it.value.coerceIn(0.0, 1.0)) }

    /** writes the chain into the saved rack of [strip] (0 = CH 1 ...), replacing what was there; the mixer loads it when it opens */
    fun save(prefs: SharedPreferences, strip: Int, c: Chain, max: Int) {
        val e = prefs.edit().putString(LocalMixerActivity.rackKey(strip), rackString(c, max))
        for (slot in 0 until max) { val f = c.slots.getOrNull(slot); if (f == null || f.params.isEmpty()) e.remove(LocalMixerActivity.paramKey(strip, slot)) else e.putString(LocalMixerActivity.paramKey(strip, slot), paramString(f)) }
        e.putString("chain$strip", c.id).apply()
    }
    /** the chain a strip was given last ("" = none, or the rack was changed by hand since) */
    fun current(prefs: SharedPreferences, strip: Int): String {
        val id = prefs.getString("chain$strip", "").orEmpty(); val c = byId(id) ?: return ""
        return if (prefs.getString(LocalMixerActivity.rackKey(strip), "").orEmpty().split('|').filter { it.isNotEmpty() } == c.slots.map { it.kind }) id else ""
    }
}
