package com.amfb.amixos.remote

import java.util.concurrent.Executor

/** REAL-TIME FADERS: a fader drag makes dozens of moves a second; sent one by one over Wi-Fi they queue up and the console trails the
 *  finger. This keeps only the NEWEST pending action per key (a fader, a pan): while one request is on the way, later moves of the same
 *  fader replace each other, and the latest goes as soon as the line is free. Different keys keep their order of arrival. */
class LatestSender(private val executor: Executor) {
    private val pending = LinkedHashMap<String, () -> Unit>()
    private var draining = false

    fun submit(key: String, action: () -> Unit) {
        val start: Boolean
        synchronized(pending) { pending.remove(key); pending[key] = action; start = !draining; if (start) draining = true }
        if (start) executor.execute { drain() }
    }
    private fun drain() {
        while (true) {
            val next: () -> Unit = synchronized(pending) {
                val it = pending.entries.iterator(); if (!it.hasNext()) { draining = false; return }
                val e = it.next(); it.remove(); e.value }
            runCatching { next() }
        }
    }
    /** actions still waiting (for tests) */
    fun waiting(): Int = synchronized(pending) { pending.size }
}
