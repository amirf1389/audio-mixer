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

data class Status(val name: String, val version: String, val backend: String, val running: Boolean, val blocks: Long, val xruns: Long, val strips: Int, val buses: Int, val uptimeS: Double, val lastError: String, val backends: List<String>, val plan: String = "", val consoleId: String = "", val computer: String = "",
                  val rate: Double = 0.0, val frames: Long = 0, val loadPct: Double = 0.0, val maxBlockUs: Double = 0.0, val remotes: Int = 0, val recording: Boolean = false,
                  val inDevice: String = "", val outDevice: String = "", val bufferFrames: Int = 0, val inChannels: Int = 2, val outChannels: Int = 2)   // CONSOLE STATUS (+ the device open, for ENGINE CONTROL)
data class Device(val backend: String, val id: String, val name: String, val inputs: Int, val outputs: Int, val defaultRate: Double, val defaultIn: Boolean, val defaultOut: Boolean)
/** the analog 3-band channel EQ of a strip: LOW shelf 100 Hz, MID bell [midHz] (100 Hz .. 8 kHz), HIGH shelf 10 kHz, ±15 dB each */
data class Tone(val low: Double = 0.0, val mid: Double = 0.0, val midHz: Double = 1000.0, val high: Double = 0.0, val on: Boolean = true)
data class StripState(val id: Int, val name: String, val db: Double, val pan: Double, val mute: Boolean, val solo: Boolean, val trim: Double = 0.0, val tone: Tone = Tone(), val eq: List<List<Double>> = emptyList())   // eq: bands 0..7 as [type (0 off, 1 peaking, 2 lowshelf, 3 highshelf, 4 lowpass, 5 highpass, 6 notch), freq, q, gainDb]
data class BusState(val id: Int, val db: Double, val returnDb: Double, val mute: Boolean)
/** the console's Windows output: its volume slider (-1: none here), GAIN MODE (the slider trims the master) and the trim it adds now */
data class WindowsSound(val volume: Int = -1, val gainMode: Boolean = false, val trimDb: Double = 0.0, val windowsSetsLevel: Boolean = false)
data class Rta(val src: Int, val f: List<Double>, val db: List<Double>)
data class ProgramEq(val on: Boolean = false, val gains: List<Double> = List(10) { 0.0 })
data class ConsoleState(val strips: List<StripState>, val buses: List<BusState>, val masterDb: Double, val limiter: Boolean, val ceilingDb: Double, val v: Long = 0, val windows: WindowsSound = WindowsSound())   // v: the console's change counter (LIVE SYNC)
data class StripMeter(val pk: Double, val rms: Double, val hold: Double, val clip: Boolean, val gr: Double, val gate: Double, val input: Double)
/** loudness of the master (LUFS momentary / short-term / integrated, range LU, true peak dBTP) */
data class Loudness(val m: Double = -120.0, val s: Double = -120.0, val i: Double = -120.0, val lra: Double = 0.0, val tp: Double = -120.0)
data class Meters(val running: Boolean, val strips: List<StripMeter>, val masterL: Double, val masterR: Double, val masterRms: Double, val masterClip: Boolean,
                  val loudness: Loudness = Loudness(), val correlation: Double = 1.0, val xruns: Long = 0)
/** ROUTING: per strip the device input it reads (-1 none), per output its source id; the sources by name; the open device's channels */
data class Routing(val inputs: List<Int>, val outputs: List<Int>, val sources: List<Pair<Int, String>>, val deviceIn: Int, val deviceOut: Int)

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

    fun status(): Status { val o = getObj("/api/status"); return Status(o.str("name"), o.str("version"), o.str("backend"), o.bool("running"), o.num("blocks").toLong(), o.num("xruns").toLong(), o.int("strips"), o.int("buses"), o.num("uptimeS"), o.str("lastError"), (o["backends"] as? List<*>)?.map { it.toString() } ?: emptyList(), o.str("plan"), o.str("consoleId"), o.str("computer"), o.num("rate"), o.num("frames").toLong(), o.num("loadPct"), o.num("maxBlockUs"), o.int("remotes"), o.bool("recording"), o.str("inDevice"), o.str("outDevice"), o.int("bufferFrames"), o.int("inChannels", 2), o.int("outChannels", 2)) }
    fun devices(): List<Device> = getList("/api/devices").map { Device(it.str("backend"), it.str("id"), it.str("name"), it.int("in"), it.int("out"), it.num("defaultRate", 48000.0), it.bool("defaultIn"), it.bool("defaultOut")) }
    fun state(): ConsoleState = parseState(getObj("/api/state"))
    /** LIVE SYNC check: null while the console is still at change v (a few bytes), else the new state */
    fun stateSince(v: Long): ConsoleState? { val o = getObj("/api/state?since=$v"); return if (o["same"] == true) null else parseState(o) }
    /** The console's plug-in catalogue, racks and extensions as saved when the remote started ({available:false} when it never was). */
    fun consoleInfo(): Map<String, Any?> = getObj("/api/console-info")
    fun meters(): Meters = parseMeters(getObj("/api/meters"))
    companion object {
        /** FIND THE CONSOLE: ask every address of this phone's /24 network for /api/status on the port (in parallel, short timeouts).
         *  Returns "address  name version" for each console that answers; nothing is sent but a GET of the public status. */
        /** A console found on the Wi-Fi: its address, its stable id (consoleId: the same after its address changes), name and version. */
        data class Found(val host: String, val port: Int, val id: String, val name: String, val version: String, val computer: String = "") {
            val label: String get() = "$name $version".trim()
            val title: String get() = computer.ifBlank { name }   // what a console is called on the phone: its computer's name (STAGE-PC), else the product
        }
        /** FIND: every console on this phone's /24 network that answers /api/status as amixos-daemon. [first] are asked first (the
         *  addresses this phone knew), so a console that kept its address is found at once; the whole network takes about a second. */
        fun discoverConsoles(localIp: String, port: Int = 8765, timeoutMs: Int = 350, first: List<String> = emptyList()): List<Found> {
            val parts = localIp.split('.'); if (parts.size != 4) return emptyList(); val base = parts.take(3).joinToString(".")
            val hosts = (first.filter { it.startsWith("$base.") } + (1..254).map { "$base.$it" }).distinct().filter { it != localIp }
            val pool = java.util.concurrent.Executors.newFixedThreadPool(48)
            try {
                val jobs = hosts.map { host -> pool.submit<Found?> { probe(host, port, timeoutMs) } }
                // one entry per console: a PC on two networks (Wi-Fi and cable) answers at two addresses with the same id; the first
                // (a known address, then the lowest) is kept
                return jobs.mapNotNull { runCatching { it.get(3, java.util.concurrent.TimeUnit.SECONDS) }.getOrNull() }.distinctBy { if (it.id.isEmpty()) it.host else it.id }
            } finally { pool.shutdownNow() }
        }
        /** one address: the console there, or null (nothing answers, or not an AmF X1 console) */
        fun probe(host: String, port: Int = 8765, timeoutMs: Int = 350): Found? = runCatching {
            val c = URL("http://$host:$port/api/status").openConnection() as HttpURLConnection
            c.connectTimeout = timeoutMs; c.readTimeout = 800
            if (c.responseCode != 200) null else { @Suppress("UNCHECKED_CAST") val o = MiniJson.parse(c.inputStream.bufferedReader().use { it.readText() }) as Map<String, Any?>
                if (o["daemon"] == "amixos-daemon") Found(host, port, o["consoleId"]?.toString().orEmpty(), o["name"]?.toString().orEmpty(), o["version"]?.toString().orEmpty(), o["computer"]?.toString().orEmpty()) else null }
        }.getOrNull()
        fun discover(localIp: String, port: Int = 8765, timeoutMs: Int = 350): List<Pair<String, String>> = discoverConsoles(localIp, port, timeoutMs).map { it.host to it.label }
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
        fun parseState(o: Map<String, Any?>): ConsoleState { val m = o.obj("master")
            return ConsoleState(o.list("strips").map { StripState(it.int("id"), it.str("name"), it.num("db"), it.num("pan"), it.bool("mute"), it.bool("solo"), it.num("trim"), it.obj("tone").let { t -> Tone(t.num("low"), t.num("mid"), t.num("midHz", 1000.0), t.num("high"), t["on"] != false) }, (it["eq"] as? List<*>).orEmpty().map { b -> nums(b) }) }, o.list("buses").map { BusState(it.int("id"), it.num("db"), it.num("returnDb", -60.0), it.bool("mute")) }, m.num("db"), m.bool("limiter", true), m.num("ceilingDb", -0.5), o.num("v").toLong(), o.obj("settings").let { w -> WindowsSound((w["winVolume"] as? Number)?.toInt() ?: -1, w["winGain"] == true, (w["winTrimDb"] as? Number)?.toDouble() ?: 0.0, w["winSetsLevel"] == true) })
        }
        fun nums(v: Any?): List<Double> = (v as? List<*>).orEmpty().map { (it as? Number)?.toDouble() ?: 0.0 }
        fun parseProgramEq(o: Map<String, Any?>): ProgramEq { val p = o.obj("programEq"); val g = nums(p["gains"])
            return ProgramEq(p["on"] == true, if (g.size == 10) g else List(10) { 0.0 }) }
        fun parseMeters(o: Map<String, Any?>): Meters { val m = o.obj("master")
            return Meters(o.bool("running"), o.list("strips").map { StripMeter(it.num("pk"), it.num("rms"), it.num("hold"), it.num("clip") > 0, it.num("gr"), it.num("gate"), it.num("in")) }, m.num("l"), m.num("r"), m.num("rms"), m.num("clip") > 0, o.obj("loudness").let { l -> Loudness(l.num("m", -120.0), l.num("s", -120.0), l.num("i", -120.0), l.num("lra"), l.num("tp", -120.0)) }, o.num("correlation", 1.0), o.num("xruns").toLong()) }
        /** slider 0..1 <-> fader dB: the console's law (-inf at 0, -60 at 0.25, -20 at 0.5, 0 dB at 0.75, +10 at 1) */
        fun posToDb(p: Double): Double = when { p <= 0 -> -120.0; p >= 1 -> 10.0; p >= 0.75 -> (p - 0.75) * 40; p >= 0.5 -> -20 + (p - 0.5) * 80; p >= 0.25 -> -60 + (p - 0.25) * 160; else -> -120 + p * 240 }
        fun dbToPos(db: Double): Double = when { db <= -120 -> 0.0; db >= 10 -> 1.0; db >= 0 -> 0.75 + db / 40; db >= -20 -> 0.5 + (db + 20) / 80; db >= -60 -> 0.25 + (db + 60) / 160; else -> (db + 120) / 240 }
        fun fmtDb(db: Double): String = if (db <= -119) "-∞" else (if (db > 0) "+" else "") + String.format("%.1f", db)
        fun lin2db(v: Double): Double = if (v > 1e-6) 20 * Math.log10(v) else -120.0
    }

    /** POST /api/control: {"op": ..., ...}. Returns the daemon's reply object; throws DaemonException with its message on refusal. */
    @Suppress("UNCHECKED_CAST") fun control(cmd: Map<String, Any?>): Map<String, Any?> = MiniJson.parse(request("POST", "/api/control", MiniJson.stringify(cmd))) as Map<String, Any?>
    /** FAST PATH for the live controls (faders, pan, mute, solo, trim, tone, EQ): the command goes as a text frame on the meter stream's
     *  open WebSocket when the console's service accepts that ("wsControl" in its state frames), so a move costs no new TCP connection over
     *  the Wi-Fi; otherwise (an older service, the stream down) it is the usual POST. The answer is not waited for on the fast path. */
    @Volatile internal var liveSocket: MiniWebSocket? = null
    fun push(cmd: Map<String, Any?>): Map<String, Any?> {
        val ws = liveSocket
        if (ws != null && runCatching { ws.sendText(MiniJson.stringify(cmd)) }.isSuccess) return mapOf("ok" to true, "via" to "socket")
        return control(cmd)
    }
    /** AUTO INPUT on the console: "" its microphone, "loopback:system" its computer's sound, "-" no input (nothing captured) */
    fun input(inDevice: String) = control(mapOf("op" to "input", "in" to inDevice))
    fun gain(strip: Int, db: Double) = push(mapOf("op" to "gain", "strip" to strip, "db" to db))
    fun pan(strip: Int, pan: Double) = push(mapOf("op" to "pan", "strip" to strip, "pan" to pan))
    fun mute(strip: Int, on: Boolean) = push(mapOf("op" to "mute", "strip" to strip, "on" to on))
    fun solo(strip: Int, on: Boolean) = push(mapOf("op" to "solo", "strip" to strip, "on" to on))
    fun name(strip: Int, name: String) = control(mapOf("op" to "name", "strip" to strip, "name" to name))
    fun master(db: Double) = push(mapOf("op" to "master", "db" to db))
    /** a console setting (the service checks each): "winVolume" 0..100, "winGain" true/false, "width" 0..2 ... */
    /** THE CONSOLE'S PLUG-INS while the service plays them: the racks (strip >= 0: with that strip's parameters), bypass, one parameter (0..1) */
    fun racks(strip: Int = -1): Map<String, Any?> = getObj(if (strip < 0) "/api/racks" else "/api/racks?strip=$strip")
    fun pluginBypass(strip: Int, slot: Int, on: Boolean) = control(mapOf("op" to "plugin", "strip" to strip, "slot" to slot, "bypass" to on))
    fun pluginParam(strip: Int, slot: Int, id: Long, value: Double) = control(mapOf("op" to "plugin", "strip" to strip, "slot" to slot, "param" to id, "value" to value))
    fun setting(key: String, value: Any) = control(mapOf("op" to "setting", "key" to key, "value" to value))
    /** the RTA: [src] -1 the master, 0.. a strip, null off. The console keeps it running for a while after each request. */
    fun rta(src: Int?) = control(mapOf("op" to "rta", "src" to (src ?: "off")))
    /** the RTA's latest spectrum (band centres and dBFS), or null while it has no sound yet */
    fun rtaNow(): Rta? = runCatching { val o = getObj("/api/rta"); val f = nums(o["f"]); val d = nums(o["db"])
        if (f.isEmpty() || f.size != d.size) null else Rta((o["src"] as? Number)?.toInt() ?: -1, f, d) }.getOrNull()
    /** the master program EQ (RTA EQ): 10 gains at the ISO octave centres */
    fun programEq(gains: List<Double>, on: Boolean) = control(mapOf("op" to "programEq", "gains" to gains, "on" to on))
    fun programEqNow(): ProgramEq = parseProgramEq(getObj("/api/state"))
    fun trim(strip: Int, db: Double) = push(mapOf("op" to "trim", "strip" to strip, "db" to db))
    fun tone(strip: Int, t: Tone) = push(mapOf("op" to "tone", "strip" to strip, "low" to t.low, "mid" to t.mid, "midHz" to t.midHz, "high" to t.high, "on" to t.on))
    fun polarity(strip: Int, on: Boolean) = control(mapOf("op" to "polarity", "strip" to strip, "on" to on))
    fun delay(strip: Int, ms: Double) = control(mapOf("op" to "delay", "strip" to strip, "ms" to ms))
    fun limiter(on: Boolean, ceilingDb: Double) = control(mapOf("op" to "limiter", "on" to on, "ceilingDb" to ceilingDb))
    fun busMute(bus: Int, on: Boolean) = control(mapOf("op" to "busMute", "bus" to bus, "on" to on))
    fun busLevel(bus: Int, db: Double) = control(mapOf("op" to "busLevel", "bus" to bus, "db" to db))
    fun busReturn(bus: Int, db: Double) = control(mapOf("op" to "busReturn", "bus" to bus, "db" to db))
    // ---- SAMPLE PADS: the console's 100 one-shots, played into its master
    data class Sample(val id: Int, val name: String, val category: String, val seconds: Double)
    fun samples(): List<Sample> = (getObj("/api/samples")["samples"] as? List<*>).orEmpty().mapNotNull { @Suppress("UNCHECKED_CAST") (it as? Map<String, Any?>)?.let { m -> Sample(m.int("id"), m.str("name"), m.str("category"), m.num("seconds")) } }
    fun sample(id: Int, gainDb: Double? = null) = control(buildMap { put("op", "sample"); put("id", id); if (gainDb != null) put("gainDb", gainDb) })
    fun samplesStop() = control(mapOf("op" to "sample", "stop" to true))
    fun samplesGain(db: Double) = control(mapOf("op" to "sample", "gainDb" to db))
    // 1000 CUSTOM PADS (the console's file, shared) and 1000 SOUND EFFECTS
    data class Pad(val n: Int, val source: String, val name: String, val gainDb: Double, val color: Int, val custom: Boolean) { val kind: String get() = when { source.isEmpty() -> "empty"; source.startsWith("sample:") -> "sample"; source.startsWith("sfx:") -> "sfx"; else -> "file" } }
    data class PadPage(val version: Int, val pads: List<Pad>)
    data class Sfx(val id: Int, val name: String, val family: String, val seconds: Double)
    fun pads(from: Int, to: Int): PadPage { val o = getObj("/api/pads?from=$from&to=$to")
        return PadPage(o.int("version"), (o["pads"] as? List<*>).orEmpty().mapNotNull { @Suppress("UNCHECKED_CAST") (it as? Map<String, Any?>)?.let { m -> Pad(m.int("n"), m.str("source"), m.str("name"), m.num("gainDb"), m.int("color"), m["custom"] == true) } }) }
    fun sfx(): List<Sfx> = (getObj("/api/sfx")["effects"] as? List<*>).orEmpty().mapNotNull { @Suppress("UNCHECKED_CAST") (it as? Map<String, Any?>)?.let { m -> Sfx(m.int("id"), m.str("name"), m.str("family"), m.num("seconds")) } }
    fun playPad(n: Int) = control(mapOf("op" to "pad", "n" to n))
    fun playSfx(id: Int) = control(mapOf("op" to "sfx", "id" to id))
    /** change a pad: any of source ("sample:N", "sfx:N", ""), name, gainDb, color; files are chosen on the computer */
    fun setPad(n: Int, source: String? = null, name: String? = null, gainDb: Double? = null, color: Int? = null) = control(buildMap { put("op", "padSet"); put("n", n)
        if (source != null) put("source", source); if (name != null) put("name", name); if (gainDb != null) put("gainDb", gainDb); if (color != null) put("color", color) })
    fun resetPad(n: Int) = control(mapOf("op" to "padReset", "n" to n))
    // ---- MASTERING: the master chain (on, low, air, thr, ratio, attack, release, makeup, drive, gain)
    fun mastering(values: Map<String, Any>) = control(mapOf("op" to "mastering") + values)
    fun masteringNow(): Map<String, Any?> = getObj("/api/state").obj("mastering")
    fun send(strip: Int, bus: Int, db: Double, pre: Boolean) = control(mapOf("op" to "send", "strip" to strip, "bus" to bus, "db" to db, "pre" to pre))
    fun eq(strip: Int, band: Int, type: String, freq: Double, q: Double, gainDb: Double) = push(mapOf("op" to "eq", "strip" to strip, "band" to band, "type" to type, "freq" to freq, "q" to q, "gainDb" to gainDb))
    fun compressor(strip: Int, on: Boolean, thresholdDb: Double, ratio: Double, kneeDb: Double, attackMs: Double, releaseMs: Double, makeupDb: Double) = control(mapOf("op" to "compressor", "strip" to strip, "on" to on, "thresholdDb" to thresholdDb, "ratio" to ratio, "kneeDb" to kneeDb, "attackMs" to attackMs, "releaseMs" to releaseMs, "makeupDb" to makeupDb))
    fun gate(strip: Int, on: Boolean, thresholdDb: Double, rangeDb: Double, attackMs: Double, holdMs: Double, releaseMs: Double) = control(mapOf("op" to "gate", "strip" to strip, "on" to on, "thresholdDb" to thresholdDb, "rangeDb" to rangeDb, "attackMs" to attackMs, "holdMs" to holdMs, "releaseMs" to releaseMs))
    fun monitor(source: Int, db: Double, dim: Boolean, mono: Boolean, mute: Boolean) = control(mapOf("op" to "monitor", "source" to source, "db" to db, "dim" to dim, "mono" to mono, "mute" to mute))
    fun open(backend: String, inDevice: String, outDevice: String, rate: Double, frames: Int, inChannels: Int, outChannels: Int) = control(mapOf("op" to "open", "backend" to backend, "in" to inDevice, "out" to outDevice, "rate" to rate, "frames" to frames, "inChannels" to inChannels, "outChannels" to outChannels))
    fun stop() = control(mapOf("op" to "stop"))
    /** the console's AUTO LEVEL target (LUFS): the SOUND SCORE's loudness target, as on the console */
    fun loudnessTarget(): Double = (getObj("/api/state").obj("settings")["autoTarget"] as? Number)?.toDouble() ?: -16.0
    fun routing(): Routing { val o = getObj("/api/routing")
        return Routing(nums(o["in"]).map { it.toInt() }, nums(o["out"]).map { it.toInt() },
            (o["sources"] as? List<*>).orEmpty().mapNotNull { @Suppress("UNCHECKED_CAST") (it as? Map<String, Any?>)?.let { m -> m.int("id") to m.str("name") } }, o.int("inputs"), o.int("outputs")) }
    fun inRoute(strip: Int, input: Int) = control(mapOf("op" to "inRoute", "strip" to strip, "input" to input))
    fun outRoute(out: Int, source: Int) = control(mapOf("op" to "outRoute", "out" to out, "source" to source))
    fun clearClips() = control(mapOf("op" to "clearClips"))
    /** RECORD on the console: the master to its recordings folder; format "flac" (24-bit), "wav24" or "wavf32". Returns the file's path there. */
    fun record(on: Boolean, format: String = "flac"): String = control(mapOf("op" to "record", "on" to on, "format" to format))["file"]?.toString().orEmpty()
    /** (recording, seconds so far, file) from the console's status */
    fun recordState(): Triple<Boolean, Double, String> { val o = getObj("/api/status"); return Triple(o["recording"] == true, (o["recordSeconds"] as? Number)?.toDouble() ?: 0.0, o["recordFile"]?.toString().orEmpty()) }

    /** The meter stream on a worker thread: onFrame about 20 times a second; reconnects after a drop until the returned handle is stopped.
     *  LIVE SYNC: onConsole gets the console state once on connect and again the moment any remote (or the console) changes it. */
    /** [onKicked]: the console disconnected this phone (CONNECTED PHONES → DISCONNECT or FORGET); the argument is how many seconds the
     *  console keeps this pairing out (0: none). The stream stops and does not reconnect by itself. */
    /** the name this phone shows in the console's CONNECTED PHONES (used when it links with the owner's token; a paired phone shows its pairing's name) */
    var clientName: String = ""
    fun meterStream(onFrame: (Meters) -> Unit, onState: ((Boolean, String?) -> Unit)? = null, onConsole: ((ConsoleState) -> Unit)? = null, onKicked: ((Int) -> Unit)? = null): MeterStream {
        val h = MeterStream()
        h.worker = thread(name = "amix-meters", isDaemon = true) {
            while (!h.stopped) {
                val ws = MiniWebSocket(ep.host, ep.port, "/ws/meters?token=" + URLEncoder.encode(ep.token, "UTF-8") + (if (clientName.isEmpty()) "" else "&client=" + URLEncoder.encode(clientName, "UTF-8")), ep.secure); h.socket = ws
                try {
                    ws.connect(); onState?.invoke(true, null)
                    while (!h.stopped) { val text = ws.receiveText() ?: break
                        @Suppress("UNCHECKED_CAST") val o = MiniJson.parse(text) as Map<String, Any?>
                        if (o["kicked"] == true) { h.stopped = true; onKicked?.invoke((o["holdSeconds"] as? Number)?.toInt() ?: 0); break }
                        if (o.containsKey("error")) { onState?.invoke(false, o.str("error")); h.stopped = true; break }
                        if (o.containsKey("state")) { if (o["wsControl"] == true) liveSocket = ws; onConsole?.invoke(parseState(o.obj("state"))); continue }
                        if (o.containsKey("ack")) continue   // the answer to a command sent on this socket (a refused one carries controlError)
                        onFrame(parseMeters(o)) }
                } catch (e: Exception) { if (!h.stopped) onState?.invoke(false, e.message ?: e.javaClass.simpleName) }
                if (liveSocket === ws) liveSocket = null   // the stream is down: commands go by HTTP again
                ws.close(); if (!h.stopped) { onState?.invoke(false, null); Thread.sleep(1000) }
            }
        }
        return h
    }
    class MeterStream { @Volatile var stopped = false; internal var socket: MiniWebSocket? = null; internal var worker: Thread? = null
        fun stop() { stopped = true; socket?.close(); worker?.join(1500) } }
    /** the live controls' socket path is in use (for tests and the link line) */
    fun fastPath(): Boolean = liveSocket != null
}
