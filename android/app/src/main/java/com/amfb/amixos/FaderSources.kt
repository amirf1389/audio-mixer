package com.amfb.amixos

import android.content.SharedPreferences
import android.media.AudioDeviceInfo

/** 1.27 FADER SOURCES of the phone's own mixer. Every strip has a SOURCE:
 *  - an input strip (CH 1-6): the phone's input device. This is the built-in microphone, a USB audio interface or microphone, or a
 *    wired or Bluetooth headset microphone. The input strips share one device and each reads its own channel of it.
 *  - a player strip (A-D): its player deck, or OTHER APPS' AUDIO. Other apps' audio comes through Android's playback capture
 *    (Android 10+, after the user agrees, and only from apps that allow it). It is mixed live and never recorded or saved.
 *  FADER-START: raising a fader from -inf starts its source (a deck plays, the app audio flows, an input un-mutes). Pulling it down to
 *  -inf stops it (the deck pauses, the app audio pauses, the input mutes). The decisions are kept here, pure, for the unit tests. */
object FaderSources {
    enum class Kind { NONE, DECK, MIC, USB, HEADSET, BLUETOOTH, APP }
    data class Source(val kind: Kind, val id: Int = 0, val name: String = "", val rate: Int = 0, val channels: Int = 0)
    enum class Action { NONE, START, STOP }
    /** what a permission / feature check says about binding a source */
    enum class Gate { OK, NEED_MIC, NEED_CAPTURE, UNSUPPORTED, OFF }

    const val FLOOR_DB = -60.0   // at or below this a fader is at -inf (the mixer's floor)
    fun isOff(db: Double) = !db.isFinite() || db <= FLOOR_DB

    /** a fader moved from [oldDb] to [newDb]: START when it leaves -inf, STOP when it reaches it, else nothing */
    fun action(oldDb: Double, newDb: Double): Action = when {
        isOff(oldDb) && !isOff(newDb) -> Action.START
        !isOff(oldDb) && isOff(newDb) -> Action.STOP
        else -> Action.NONE
    }

    /** the kind of an Android input device */
    fun kindOf(type: Int): Kind = when (type) {
        AudioDeviceInfo.TYPE_BUILTIN_MIC -> Kind.MIC
        AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_ACCESSORY, AudioDeviceInfo.TYPE_USB_HEADSET -> Kind.USB
        AudioDeviceInfo.TYPE_WIRED_HEADSET -> Kind.HEADSET
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO, 26 /* TYPE_BLE_HEADSET */ -> Kind.BLUETOOTH
        else -> Kind.MIC
    }

    /** may [kind] be bound now? [sdk] the Android version, [featureOn] the source's Features toggle */
    fun gate(kind: Kind, micGranted: Boolean, captureGranted: Boolean, sdk: Int, featureOn: Boolean): Gate = when {
        !featureOn -> Gate.OFF
        kind == Kind.NONE || kind == Kind.DECK -> Gate.OK
        kind == Kind.APP && sdk < 29 -> Gate.UNSUPPORTED
        !micGranted -> Gate.NEED_MIC                      // playback capture needs RECORD_AUDIO too
        kind == Kind.APP && !captureGranted -> Gate.NEED_CAPTURE
        else -> Gate.OK
    }

    /** the sources a strip may take: an input strip the input devices, a player strip its deck or the apps */
    fun choices(channel: Int, inputs: List<Source>): List<Source> =
        if (channel >= 100) listOf(Source(Kind.DECK, channel - 100, "PLAYER " + ('A' + (channel - 100))), Source(Kind.APP, 0, "OTHER APPS"))
        else listOf(Source(Kind.MIC, 0, "PHONE'S CHOICE")) + inputs

    // ---- the binding, kept per channel ("src<channel>" = "KIND|id|name|rate|channels")
    fun key(channel: Int) = "src$channel"
    fun encode(s: Source) = listOf(s.kind.name, s.id, s.name.replace("|", "/"), s.rate, s.channels).joinToString("|")
    fun decode(v: String?): Source? { val f = v?.split('|') ?: return null; if (f.size < 5) return null
        val k = runCatching { Kind.valueOf(f[0]) }.getOrNull() ?: return null
        return Source(k, f[1].toIntOrNull() ?: 0, f[2], f[3].toIntOrNull() ?: 0, f[4].toIntOrNull() ?: 0) }
    fun bind(p: SharedPreferences, channel: Int, s: Source) { p.edit().putString(key(channel), encode(s)).apply() }
    /** what [channel] is bound to: the stored source, else its default (the deck for a player strip, the phone's choice of input) */
    fun bound(p: SharedPreferences, channel: Int): Source = decode(p.getString(key(channel), null))
        ?: if (channel >= 100) Source(Kind.DECK, channel - 100, "PLAYER " + ('A' + (channel - 100))) else Source(Kind.MIC, 0, "PHONE'S CHOICE")
    /** the deck an APP source feeds: the player strip's own lane */
    fun appDeck(channel: Int) = if (channel >= 100) channel - 100 else -1

    fun icon(k: Kind) = when (k) { Kind.DECK -> "▶"; Kind.MIC -> "🎤"; Kind.USB -> "⎆"; Kind.HEADSET -> "🎧"; Kind.BLUETOOTH -> "ᛒ"; Kind.APP -> "▣"; Kind.NONE -> "—" }
    /** the INFO header over a fader: icon and name (the player's track title when a deck has one), format, live level */
    fun info(s: Source, title: String?, levelDb: Double): String {
        val name = if (s.kind == Kind.DECK && !title.isNullOrBlank()) title else s.name
        val fmt = listOfNotNull(if (s.rate > 0) String.format(java.util.Locale.ROOT, "%.1fk", s.rate / 1000.0).replace(".0k", "k") else null, if (s.channels > 0) "${s.channels}ch" else null).joinToString("/")
        val lvl = if (levelDb <= -119) "-∞" else String.format(java.util.Locale.ROOT, "%.0f", levelDb)
        return icon(s.kind) + " " + name + " · " + (if (fmt.isNotEmpty()) "$fmt · " else "") + lvl
    }

}
