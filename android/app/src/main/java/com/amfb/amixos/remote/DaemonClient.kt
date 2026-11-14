package com.amfb.amixos.remote

import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import kotlin.concurrent.thread

/** Where the daemon is: host, port, the token from ~/.amixos/daemon.token on the console machine, TLS when it sits behind deploy/nginx. */
data class Endpoint(val host: String, val port: Int, val token: String, val secure: Boolean = false) {
    val http: String get() = (if (secure) "https" else "http") + "://" + host + ":" + port
}
class DaemonException(message: String, val status: Int) : IOException(message)

data class Status(val name: String, val version: String, val backend: String, val running: Boolean, val blocks: Long, val xruns: Long, val strips: Int, val buses: Int, val uptimeS: Double, val lastError: String, val backends: List<String>, val plan: String = "")
data class Device(val backend: String, val id: String, val name: String, val inputs: Int, val outputs: Int, val defaultRate: Double, val defaultIn: Boolean, val defaultOut: Boolean)
data class StripState(val id: Int, val name: String, val db: Double, val pan: Double, val mute: Boolean, val solo: Boolean)
data class BusState(val id: Int, val db: Double, val returnDb: Double, val mute: Boolean)
data class ConsoleState(val strips: List<StripState>, val buses: List<BusState>, val masterDb: Double, val limiter: Boolean, val ceilingDb: Double)
data class StripMeter(val pk: Double, val rms: Double, val hold: Double, val clip: Boolean, val gr: Double, val gate: Double, val input: Double)
data class Meters(val running: Boolean, val strips: List<StripMeter>, val masterL: Double, val masterR: Double, val masterRms: Double, val masterClip: Boolean)

/** The daemon client: plain HTTP (HttpURLConnection) and the meter WebSocket. The token travels in X-Amix-Token (control) and ?token= (meters). */
class DaemonClient(val ep: Endpoint) {
    private fun request(method: String, path: String, body: String? = null): String {
        val c = URL(ep.http + path).openConnection() as HttpURLConnection
        c.requestMethod = method; c.connectTimeout = 4000; c.readTimeout = 6000; c.setRequestProperty("X-Amix-Token", ep.token)
        if (body != null) { c.doOutput = true; c.setRequestProperty("Content-Type", "application/json"); c.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) } }
        val code = c.responseCode
        val text = (if (code >= 400) c.errorStream else c.inputStream)?.bufferedReader()?.use { it.readText() } ?: ""
        if (code >= 400) { val err = runCatching { (MiniJson.parse(text) as? Map<*, *>)?.get("error")?.toString() }.getOrNull(); throw DaemonException(err ?: "HTTP $code", code) }
        return text
    }
    @Suppress("UNCHECKED_CAST") private fun getObj(path: String): Map<String, Any?> = MiniJson.parse(request("GET", path)) as Map<String, Any?>
    @Suppress("UNCHECKED_CAST") private fun getList(path: String): List<Map<String, Any?>> = (MiniJson.parse(request("GET", path)) as List<Any?>).map { it as Map<String, Any?> }

    fun status(): Status { val o = getObj("/api/status"); return Status(o.str("name"), o.str("version"), o.str("backend"), o.bool("running"), o.num("blocks").toLong(), o.num("xruns").toLong(), o.int("strips"), o.int("buses"), o.num("uptimeS"), o.str("lastError"), (o["backends"] as? List<*>)?.map { it.toString() } ?: emptyList(), o.str("plan")) }
    fun devices(): List<Device> = getList("/api/devices").map { Device(it.str("backend"), it.str("id"), it.str("name"), it.int("in"), it.int("out"), it.num("defaultRate", 48000.0), it.bool("defaultIn"), it.bool("defaultOut")) }
    fun state(): ConsoleState { val o = getObj("/api/state"); val m = o.obj("master")
        return ConsoleState(o.list("strips").map { StripState(it.int("id"), it.str("name"), it.num("db"), it.num("pan"), it.bool("mute"), it.bool("solo")) }, o.list("buses").map { BusState(it.int("id"), it.num("db"), it.num("returnDb", -60.0), it.bool("mute")) }, m.num("db"), m.bool("limiter", true), m.num("ceilingDb", -0.5)) }
    /** The console's plug-in catalogue, racks and extensions as saved when the remote started ({available:false} when it never was). */
    fun consoleInfo(): Map<String, Any?> = getObj("/api/console-info")
    fun meters(): Meters = parseMeters(getObj("/api/meters"))
    companion object {
        /** FIND THE CONSOLE: ask every address of this phone's /24 network for /api/status on the port (in parallel, short timeouts).
         *  Returns "address  name version" for each console that answers; nothing is sent but a GET of the public status. */
        fun discover(localIp: String, port: Int = 8765, timeoutMs: Int = 350): List<Pair<String, String>> {
            val parts = localIp.split('.'); if (parts.size != 4) return emptyList(); val base = parts.take(3).joinToString(".")
            val pool = java.util.concurrent.Executors.newFixedThreadPool(48)
            try {
                val jobs = (1..254).map { n -> val host = "$base.$n"; pool.submit<Pair<String, String>?> {
                    runCatching {
                        val c = URL("http://$host:$port/api/status").openConnection() as HttpURLConnection
                        c.connectTimeout = timeoutMs; c.readTimeout = 800
                        if (c.responseCode != 200) null else { @Suppress("UNCHECKED_CAST") val o = MiniJson.parse(c.inputStream.bufferedReader().use { it.readText() }) as Map<String, Any?>
                            if (o["daemon"] == "amixos-daemon") host to (o["name"]?.toString() ?: "") + " " + (o["version"]?.toString() ?: "") else null }
                    }.getOrNull() } }
                return jobs.mapNotNull { runCatching { it.get(3, java.util.concurrent.TimeUnit.SECONDS) }.getOrNull() }
            } finally { pool.shutdownNow() }
        }
        /** PAIRING: trade the six-digit PIN the console shows this minute (NETWORK → PHONE REMOTE) for a token of this phone's own.
         *  Needs no token; the service allows one pairing per PIN and five wrong tries a minute. Returns the token. */
        fun pair(host: String, port: Int, pin: String, name: String, secure: Boolean = false): String {
            val c = URL((if (secure) "https" else "http") + "://" + host + ":" + port + "/api/pair").openConnection() as HttpURLConnection
            c.requestMethod = "POST"; c.connectTimeout = 4000; c.readTimeout = 6000; c.doOutput = true; c.setRequestProperty("Content-Type", "application/json")
            c.outputStream.use { it.write(MiniJson.stringify(mapOf("pin" to pin, "name" to name)).toByteArray(Charsets.UTF_8)) }
            val code = c.responseCode
            val text = (if (code >= 400) c.errorStream else c.inputStream)?.bufferedReader()?.use { it.readText() } ?: ""
            @Suppress("UNCHECKED_CAST") val o = runCatching { MiniJson.parse(text) as Map<String, Any?> }.getOrNull() ?: emptyMap()
            if (code >= 400) throw DaemonException(o["error"]?.toString() ?: "HTTP $code", code)
            return o["token"]?.toString()?.takeIf { it.isNotEmpty() } ?: throw DaemonException("the console sent no token", code)
        }
        fun parseMeters(o: Map<String, Any?>): Meters { val m = o.obj("master")
            return Meters(o.bool("running"), o.list("strips").map { StripMeter(it.num("pk"), it.num("rms"), it.num("hold"), it.num("clip") > 0, it.num("gr"), it.num("gate"), it.num("in")) }, m.num("l"), m.num("r"), m.num("rms"), m.num("clip") > 0) }
        /** slider 0..1 <-> fader dB: the console's law (-inf at 0, -60 at 0.25, -20 at 0.5, 0 dB at 0.75, +10 at 1) */
        fun posToDb(p: Double): Double = when { p <= 0 -> -120.0; p >= 1 -> 10.0; p >= 0.75 -> (p - 0.75) * 40; p >= 0.5 -> -20 + (p - 0.5) * 80; p >= 0.25 -> -60 + (p - 0.25) * 160; else -> -120 + p * 240 }
        fun dbToPos(db: Double): Double = when { db <= -120 -> 0.0; db >= 10 -> 1.0; db >= 0 -> 0.75 + db / 40; db >= -20 -> 0.5 + (db + 20) / 80; db >= -60 -> 0.25 + (db + 60) / 160; else -> (db + 120) / 240 }
        fun fmtDb(db: Double): String = if (db <= -119) "-∞" else (if (db > 0) "+" else "") + String.format("%.1f", db)
        fun lin2db(v: Double): Double = if (v > 1e-6) 20 * Math.log10(v) else -120.0
    }

    /** POST /api/control: {"op": ..., ...}. Returns the daemon's reply object; throws DaemonException with its message on refusal. */
    @Suppress("UNCHECKED_CAST") fun control(cmd: Map<String, Any?>): Map<String, Any?> = MiniJson.parse(request("POST", "/api/control", MiniJson.stringify(cmd))) as Map<String, Any?>
    fun gain(strip: Int, db: Double) = control(mapOf("op" to "gain", "strip" to strip, "db" to db))
    fun pan(strip: Int, pan: Double) = control(mapOf("op" to "pan", "strip" to strip, "pan" to pan))
    fun mute(strip: Int, on: Boolean) = control(mapOf("op" to "mute", "strip" to strip, "on" to on))
    fun solo(strip: Int, on: Boolean) = control(mapOf("op" to "solo", "strip" to strip, "on" to on))
    fun name(strip: Int, name: String) = control(mapOf("op" to "name", "strip" to strip, "name" to name))
    fun master(db: Double) = control(mapOf("op" to "master", "db" to db))
    fun trim(strip: Int, db: Double) = control(mapOf("op" to "trim", "strip" to strip, "db" to db))
    fun polarity(strip: Int, on: Boolean) = control(mapOf("op" to "polarity", "strip" to strip, "on" to on))
    fun delay(strip: Int, ms: Double) = control(mapOf("op" to "delay", "strip" to strip, "ms" to ms))
    fun limiter(on: Boolean, ceilingDb: Double) = control(mapOf("op" to "limiter", "on" to on, "ceilingDb" to ceilingDb))
    fun busMute(bus: Int, on: Boolean) = control(mapOf("op" to "busMute", "bus" to bus, "on" to on))
    fun busLevel(bus: Int, db: Double) = control(mapOf("op" to "busLevel", "bus" to bus, "db" to db))
    fun send(strip: Int, bus: Int, db: Double, pre: Boolean) = control(mapOf("op" to "send", "strip" to strip, "bus" to bus, "db" to db, "pre" to pre))
    fun eq(strip: Int, band: Int, type: String, freq: Double, q: Double, gainDb: Double) = control(mapOf("op" to "eq", "strip" to strip, "band" to band, "type" to type, "freq" to freq, "q" to q, "gainDb" to gainDb))
    fun compressor(strip: Int, on: Boolean, thresholdDb: Double, ratio: Double, kneeDb: Double, attackMs: Double, releaseMs: Double, makeupDb: Double) = control(mapOf("op" to "compressor", "strip" to strip, "on" to on, "thresholdDb" to thresholdDb, "ratio" to ratio, "kneeDb" to kneeDb, "attackMs" to attackMs, "releaseMs" to releaseMs, "makeupDb" to makeupDb))
    fun gate(strip: Int, on: Boolean, thresholdDb: Double, rangeDb: Double, attackMs: Double, holdMs: Double, releaseMs: Double) = control(mapOf("op" to "gate", "strip" to strip, "on" to on, "thresholdDb" to thresholdDb, "rangeDb" to rangeDb, "attackMs" to attackMs, "holdMs" to holdMs, "releaseMs" to releaseMs))
    fun monitor(source: Int, db: Double, dim: Boolean, mono: Boolean, mute: Boolean) = control(mapOf("op" to "monitor", "source" to source, "db" to db, "dim" to dim, "mono" to mono, "mute" to mute))
    fun open(backend: String, inDevice: String, outDevice: String, rate: Double, frames: Int, inChannels: Int, outChannels: Int) = control(mapOf("op" to "open", "backend" to backend, "in" to inDevice, "out" to outDevice, "rate" to rate, "frames" to frames, "inChannels" to inChannels, "outChannels" to outChannels))
    fun stop() = control(mapOf("op" to "stop"))
    fun clearClips() = control(mapOf("op" to "clearClips"))

    /** The meter stream on a worker thread: onFrame about 20 times a second; reconnects after a drop until the returned handle is stopped. */
    fun meterStream(onFrame: (Meters) -> Unit, onState: ((Boolean, String?) -> Unit)? = null): MeterStream {
        val h = MeterStream()
        h.worker = thread(name = "amix-meters", isDaemon = true) {
            while (!h.stopped) {
                val ws = MiniWebSocket(ep.host, ep.port, "/ws/meters?token=" + URLEncoder.encode(ep.token, "UTF-8"), ep.secure); h.socket = ws
                try {
                    ws.connect(); onState?.invoke(true, null)
                    while (!h.stopped) { val text = ws.receiveText() ?: break
                        @Suppress("UNCHECKED_CAST") val o = MiniJson.parse(text) as Map<String, Any?>
                        if (o.containsKey("error")) { onState?.invoke(false, o.str("error")); h.stopped = true; break }
                        onFrame(parseMeters(o)) }
                } catch (e: Exception) { if (!h.stopped) onState?.invoke(false, e.message ?: e.javaClass.simpleName) }
                ws.close(); if (!h.stopped) { onState?.invoke(false, null); Thread.sleep(1000) }
            }
        }
        return h
    }
    class MeterStream { @Volatile var stopped = false; internal var socket: MiniWebSocket? = null; internal var worker: Thread? = null
        fun stop() { stopped = true; socket?.close(); worker?.join(1500) } }
}
