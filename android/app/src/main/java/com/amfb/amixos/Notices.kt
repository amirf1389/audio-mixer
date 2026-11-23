package com.amfb.amixos

/** NOTIFICATIONS: the same events the console's notification centre shows (what's new, clipping, device lost / connected, CPU overload,
 *  update ready, audio loop stopped), worked out from what the phone already reads (/api/status every few seconds and the meter stream).
 *  Each new event is a toast and goes into the history (newest first, at most [max]). Pure logic: no Android types, unit tested. */
class Notices(private val max: Int = 50) {
    enum class Kind { WHATS_NEW, CLIP, DEVICE_LOST, DEVICE_CONNECTED, CPU_OVERLOAD, UPDATE_READY, LOOP_STOPPED }
    data class Event(val atMs: Long, val kind: Kind, val text: String)
    private val list = ArrayDeque<Event>()
    val history: List<Event> get() = list.toList()
    private var running: Boolean? = null; private var device: String? = null; private var xruns = -1L; private var lastCpuMs = 0L; private var clipOn = false
    private fun add(now: Long, k: Kind, t: String): Event { val e = Event(now, k, t); list.addFirst(e); while (list.size > max) list.removeLast(); return e }
    /** the console's version against the one this phone saw last: a new one is "what's new" */
    fun versionSeen(now: Long, version: String, seenBefore: String?): Event? = if (version.isNotBlank() && seenBefore != null && seenBefore != version) add(now, Kind.WHATS_NEW, "What's new: the console is now $version") else null
    /** one /api/status reading */
    fun status(now: Long, isRunning: Boolean, outDevice: String, xrunCount: Long, loadPct: Double): List<Event> {
        val out = mutableListOf<Event>()
        val wasRunning = running; running = isRunning
        if (wasRunning == true && !isRunning) out += add(now, Kind.LOOP_STOPPED, "The audio loop stopped on the console")
        val dev = device; device = outDevice
        if (dev != null && dev.isNotBlank() && outDevice.isBlank()) out += add(now, Kind.DEVICE_LOST, "Device lost: $dev")
        else if (outDevice.isNotBlank() && dev != outDevice && dev != null) out += add(now, Kind.DEVICE_CONNECTED, "Device connected: $outDevice")
        val overload = loadPct >= 90.0 || (xruns >= 0 && xrunCount > xruns)
        if (overload && now - lastCpuMs > 30_000) { lastCpuMs = now; out += add(now, Kind.CPU_OVERLOAD, "CPU overload: dropouts on the console (load ${loadPct.toInt()} %)") }
        xruns = xrunCount
        return out
    }
    /** the meter stream: one event when a clip starts (the lamp latches; a tap on the meter clears it) */
    fun clip(now: Long, anyClip: Boolean): Event? { val e = if (anyClip && !clipOn) add(now, Kind.CLIP, "Clipping: a channel or the master went over 0 dBFS") else null; clipOn = anyClip; return e }
    fun updateReady(now: Long, version: String): Event = add(now, Kind.UPDATE_READY, "Update ready: $version")
    fun clear() = list.clear()
}
