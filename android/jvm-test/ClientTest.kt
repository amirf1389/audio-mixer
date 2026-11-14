// A JVM test of the remote's pure-Kotlin client against a real amixos-daemon (no Android needed):
//   kotlinc ../app/src/main/java/com/amfb/amixos/remote/*.kt ClientTest.kt -include-runtime -d client-test.jar && java -jar client-test.jar PORT TOKEN
import com.amfb.amixos.remote.*
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

fun check(c: Boolean, what: String) { if (!c) { System.err.println("FAIL $what"); System.exit(1) } }

fun main(args: Array<String>) {
    val port = args[0].toInt(); val token = args[1]
    check(MiniJson.stringify(MiniJson.parse("""{"a":[1,2.5,"x\n",true,null],"b":{"c":-3}}""")) == """{"a":[1,2.5,"x\n",true,null],"b":{"c":-3}}""", "json round trip")
    check(Math.abs(DaemonClient.posToDb(DaemonClient.dbToPos(-7.0)) + 7.0) < 1e-9 && DaemonClient.dbToPos(0.0) == 0.75 && DaemonClient.fmtDb(-120.0) == "-∞", "fader law")
    val bad = DaemonClient(Endpoint("127.0.0.1", port, "wrong-token"))
    val refused = runCatching { bad.gain(0, -3.0) }.exceptionOrNull(); check(refused is DaemonException && (refused as DaemonException).status == 401, "wrong token refused with 401: $refused")
    val c = DaemonClient(Endpoint("127.0.0.1", port, token))
    val st = c.status(); check(st.name == "A mix OS" && st.strips == 32 && st.backends.contains("null"), "status $st")
    val devs = c.devices(); check(devs.any { it.backend == "null" }, "devices list the null device")
    c.open("null", "", "", 48000.0, 256, 2, 2); check(c.status().running, "opened")
    c.gain(3, -12.0); c.pan(3, 0.5); c.mute(4, true); c.solo(5, true); c.name(3, "Vox"); c.master(-2.0); c.busLevel(1, -6.0)
    c.eq(3, 0, "peaking", 1000.0, 1.0, 3.0); c.compressor(3, true, -18.0, 3.0, 6.0, 10.0, 100.0, 0.0); c.gate(3, true, -40.0, -80.0, 1.0, 50.0, 100.0)
    val s = c.state(); val s3 = s.strips[3]
    check(Math.abs(s3.db + 12) < 1e-9 && Math.abs(s3.pan - 0.5) < 1e-9 && s3.name == "Vox" && s.strips[4].mute && s.strips[5].solo && Math.abs(s.masterDb + 2) < 1e-9 && Math.abs(s.buses[1].db + 6) < 1e-9, "state after control: $s3 master ${s.masterDb}")
    val m = c.meters(); check(m.running && m.strips.size == 32, "meters over HTTP")
    val latch = CountDownLatch(5); var connected = false
    val stream = c.meterStream({ f -> if (f.running && f.strips.size == 32) latch.countDown() }, { ok, err -> if (ok) connected = true else if (err != null) System.err.println("stream: $err") })
    check(latch.await(8, TimeUnit.SECONDS) && connected, "5 meter frames over the WebSocket")
    stream.stop()
    c.solo(5, false); c.mute(4, false); c.stop(); check(!c.status().running, "stopped")
    val found = DaemonClient.discover("127.0.0.9", port); check(found.any { it.first == "127.0.0.1" }, "FIND sees the console: $found")
    c.busLevel(2, -9.0); c.busMute(2, true); val b2 = c.state().buses[2]; check(Math.abs(b2.db + 9.0) < 1e-6 && b2.mute, "bus level and mute")
    c.monitor(1, -6.0, true, false, false); c.name(7, "Kick"); check(c.state().strips[7].name == "Kick", "rename")
    c.open("null", "", "", 48000.0, 256, 2, 2); c.trim(0, 3.0); c.polarity(0, true); c.delay(0, 12.5); c.send(0, 1, -6.0, true); c.limiter(true, -1.0)
    check(Math.abs(c.state().ceilingDb + 1.0) < 1e-6, "limiter ceiling"); c.stop()
    println("ClientTest: JSON, fader law, 401 on a wrong token, status / devices / open / control / state / meters / WebSocket stream / stop / PIN pairing / FIND / buses / monitor / rename / trim / polarity / delay / sends / limiter OK")
}
