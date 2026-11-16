package com.amfb.amixos

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.amfb.amixos.remote.DaemonClient
import java.net.Inet4Address
import java.net.Inet6Address
import java.net.NetworkInterface
import java.util.concurrent.Executors

/** DEVICES & NETWORK: what this phone really has.
 *  AUDIO INTERFACES: every input and output Android reports (USB audio interface, wired headset, Bluetooth, the built-in speaker and
 *  microphones) with its channels and rates; the one picked is what the phone mixer opens through AAudio (Android's low-latency path:
 *  the role ASIO plays on Windows).
 *  NETWORK: every network interface with its real IPv4 / IPv6 addresses, type, state and MTU, the gateway and DNS of the active network,
 *  and FIND CONSOLES, which asks every local network the phone is on for an AmF X1 console and saves the one tapped for the remote. */
class DevicesActivity : AppCompatActivity() {
    private lateinit var box: LinearLayout
    private lateinit var found: LinearLayout
    private lateinit var findStatus: TextView
    private val io = Executors.newSingleThreadExecutor()
    private val ui = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 40, 32, 32) }
        setContentView(Nav.wrap(this, ScrollView(this).apply { addView(box) }, null))
        render()
    }
    override fun onResume() { super.onResume(); render() }
    override fun onDestroy() { io.shutdownNow(); super.onDestroy() }

    private fun head(t: String, sub: String) {
        box.addView(TextView(this).apply { text = t; setTextColor(getColor(R.color.accent)); textSize = 18f; typeface = Typeface.DEFAULT_BOLD; letterSpacing = 0.12f; setPadding(0, 24, 0, 2) })
        box.addView(TextView(this).apply { text = sub; setTextColor(getColor(R.color.dim)); textSize = 12f; setPadding(0, 0, 0, 14) })
    }
    private fun card(): LinearLayout = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.card_bg); setPadding(28, 20, 28, 20)
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 0, 0, 14) } }
    private fun line(parent: LinearLayout, t: String, col: Int = Color.WHITE, size: Float = 13f, bold: Boolean = false, mono: Boolean = false) =
        parent.addView(TextView(this).apply { text = t; setTextColor(col); textSize = size; typeface = if (mono) Typeface.MONOSPACE else if (bold) Typeface.DEFAULT_BOLD else Typeface.DEFAULT })

    private fun render() {
        box.removeAllViews()
        box.addView(TextView(this).apply { text = getString(R.string.dev_title); setTextColor(getColor(R.color.accent)); textSize = 22f; typeface = Typeface.DEFAULT_BOLD; letterSpacing = 0.12f })
        audioSection(); halSection(); networkSection()
    }

    // ---- AUDIO HAL + BRAND PROFILE (from the AmF X1 SDK): read once, the probe opens and closes an AAudio stream
    private val hal by lazy { AudioHal.read(this) }
    private fun halSection() {
        head(getString(R.string.dev_hal), getString(R.string.dev_hal_sub))
        val c = card(); for (l in hal.lines()) line(c, l, Color.WHITE, 12f, mono = true); box.addView(c)
        val p = BrandProfile.of(hal)
        head(getString(R.string.dev_brand), getString(R.string.dev_brand_sub))
        val b = card()
        line(b, p.maker + " · " + p.family, Color.WHITE, 15f, bold = true)
        line(b, getString(R.string.dev_brand_buffer, p.bursts), getColor(R.color.cyan), 12f, mono = true)
        for (n in p.notes) line(b, "• " + n, getColor(R.color.dim), 12f)
        line(b, getString(R.string.dev_brand_battery), getColor(R.color.accent), 12f, bold = true)
        line(b, p.batteryTip, Color.WHITE, 12f)
        b.addView(Button(this).apply { text = getString(R.string.dev_brand_open); setOnClickListener { runCatching { startActivity(BrandProfile.batterySettings(this@DevicesActivity)) } } })
        box.addView(b)
    }

    // ---- AUDIO INTERFACES
    private fun audioSection() {
        head(getString(R.string.dev_audio), getString(R.string.dev_audio_sub))
        val am = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val outSel = Prefs.localOutDevice(this); val inSel = Prefs.localInDevice(this)
        for ((title, flag, isOut) in listOf(Triple(getString(R.string.dev_outputs), AudioManager.GET_DEVICES_OUTPUTS, true), Triple(getString(R.string.dev_inputs), AudioManager.GET_DEVICES_INPUTS, false))) {
            line(box, title, getColor(R.color.cyan), 11f, mono = true)
            val c = card()
            val sel = if (isOut) outSel else inSel
            fun pickRow(id: Int, name: String, detail: String) {
                val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, 6, 0, 6); isClickable = true
                    setOnClickListener { Prefs.local(this@DevicesActivity).edit().putInt(if (isOut) "outDevice" else "inDevice", id).apply(); render() } }
                row.addView(TextView(this).apply { text = if (id == sel) "●" else "○"; setTextColor(if (id == sel) getColor(R.color.accent) else getColor(R.color.dim)); textSize = 16f; setPadding(0, 0, 20, 0) })
                val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) }
                col.addView(TextView(this).apply { text = name; setTextColor(Color.WHITE); textSize = 14f; typeface = Typeface.DEFAULT_BOLD })
                if (detail.isNotEmpty()) col.addView(TextView(this).apply { text = detail; setTextColor(getColor(R.color.dim)); textSize = 11f; typeface = Typeface.MONOSPACE })
                row.addView(col); c.addView(row)
            }
            pickRow(0, getString(R.string.dev_system_route), getString(R.string.dev_system_route_sub))
            for (d in am.getDevices(flag)) {
                if (isOut && d.type == AudioDeviceInfo.TYPE_TELEPHONY) continue
                val ch = d.channelCounts.distinct().sorted(); val rates = d.sampleRates.distinct().sorted()
                val detail = typeName(d.type) + " · " + (if (ch.isEmpty()) getString(R.string.dev_any_ch) else ch.joinToString("/") + " ch") + " · " +
                    (if (rates.isEmpty()) getString(R.string.dev_any_rate) else rates.joinToString("/") { (it / 1000.0).let { k -> if (k % 1.0 == 0.0) "${k.toInt()}k" else "${k}k" } })
                pickRow(d.id, (d.productName?.toString()?.takeIf { it.isNotBlank() } ?: typeName(d.type)), detail)
            }
            box.addView(c)
        }
        line(box, getString(R.string.dev_audio_note), getColor(R.color.dim), 11f)
    }

    // ---- NETWORK
    private fun networkSection() {
        head(getString(R.string.dev_network), getString(R.string.dev_network_sub))
        val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val active = cm.activeNetwork; val lp = active?.let { cm.getLinkProperties(it) }; val caps = active?.let { cm.getNetworkCapabilities(it) }
        val act = card()
        line(act, getString(R.string.dev_active), getColor(R.color.cyan), 11f, mono = true)
        if (lp == null) line(act, getString(R.string.dev_no_network), getColor(R.color.dim))
        else {
            val kind = when { caps == null -> "?"; caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> "Wi-Fi"; caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) -> "Ethernet"
                caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> getString(R.string.dev_mobile); caps.hasTransport(NetworkCapabilities.TRANSPORT_VPN) -> "VPN"; else -> "?" }
            line(act, kind + " · " + (lp.interfaceName ?: ""), Color.WHITE, 15f, bold = true)
            line(act, getString(R.string.dev_gateway) + " " + lp.routes.filter { it.isDefaultRoute && it.gateway != null }.mapNotNull { it.gateway?.hostAddress }.distinct().joinToString(", ").ifEmpty { "—" }, getColor(R.color.dim), 12f, mono = true)
            line(act, "DNS " + lp.dnsServers.mapNotNull { it.hostAddress }.joinToString(", ").ifEmpty { "—" }, getColor(R.color.dim), 12f, mono = true)
        }
        box.addView(act)
        val ifs = runCatching { NetworkInterface.getNetworkInterfaces().toList() }.getOrDefault(emptyList()).filter { it.inetAddresses.toList().isNotEmpty() }.sortedBy { if (it.isLoopback) 1 else 0 }
        for (n in ifs) {
            val c = card()
            val v4 = n.inetAddresses.toList().filterIsInstance<Inet4Address>().mapNotNull { it.hostAddress }
            val v6 = n.inetAddresses.toList().filterIsInstance<Inet6Address>().mapNotNull { it.hostAddress?.substringBefore('%') }
            line(c, n.name + " · " + ifType(n.name, n.isLoopback), Color.WHITE, 14f, bold = true)
            line(c, (if (n.isUp) "● UP" else "○ DOWN") + " · MTU " + runCatching { n.mtu }.getOrDefault(0) + (if (n.isVirtual) " · virtual" else ""), if (n.isUp) Color.parseColor("#34D399") else getColor(R.color.dim), 11f, mono = true)
            for (a in v4) line(c, "IPv4  $a", getColor(R.color.cyan), 13f, mono = true)
            for (a in v6) line(c, "IPv6  $a", getColor(R.color.dim), 11f, mono = true)
            box.addView(c)
        }
        // FIND CONSOLES: every IPv4 network the phone is on (Wi-Fi, Ethernet, hotspot), not only the first
        line(box, getString(R.string.dev_find_title), getColor(R.color.cyan), 11f, mono = true)
        val fc = card()
        findStatus = TextView(this).apply { text = getString(R.string.dev_find_sub); setTextColor(getColor(R.color.dim)); textSize = 12f }
        fc.addView(findStatus)
        fc.addView(Button(this).apply { text = getString(R.string.dev_find); setOnClickListener { find(ifs) } })
        found = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; fc.addView(found)
        box.addView(fc)
    }
    private fun ifType(name: String, loop: Boolean): String = when {
        loop -> "LOOPBACK"; name.startsWith("wlan") -> "WI-FI"; name.startsWith("eth") -> "ETHERNET"; name.startsWith("rmnet") || name.startsWith("ccmni") || name.startsWith("v4-rmnet") -> getString(R.string.dev_mobile).uppercase()
        name.startsWith("ap") || name.startsWith("swlan") || name.startsWith("softap") -> "HOTSPOT"; name.startsWith("tun") || name.startsWith("ppp") || name.startsWith("ipsec") -> "VPN"
        name.startsWith("p2p") -> "WI-FI DIRECT"; name.startsWith("rndis") || name.startsWith("usb") -> "USB TETHER"; else -> "OTHER" }

    private fun find(ifs: List<NetworkInterface>) {
        val nets = ifs.filter { it.isUp && !it.isLoopback && !it.name.startsWith("rmnet") && !it.name.startsWith("ccmni") }
            .flatMap { n -> n.inetAddresses.toList().filterIsInstance<Inet4Address>().mapNotNull { it.hostAddress } }.distinctBy { it.substringBeforeLast('.') }
        if (nets.isEmpty()) { findStatus.text = getString(R.string.no_wifi); return }
        val port = Prefs.defaultPort(this)
        findStatus.text = getString(R.string.dev_finding, nets.joinToString(", ") { it.substringBeforeLast('.') + ".0/24" }, port); found.removeAllViews()
        io.execute {
            val hits = nets.flatMap { DaemonClient.discover(it, port) }.distinctBy { it.first }
            ui.post {
                findStatus.text = if (hits.isEmpty()) getString(R.string.dev_found_none) else getString(R.string.dev_found_n, hits.size)
                for ((host, label) in hits) found.addView(Button(this).apply { text = "$host  ·  $label"; isAllCaps = false
                    setOnClickListener { Prefs.remote(this@DevicesActivity).edit().putString("host", host).putString("port", port.toString()).apply(); findStatus.text = getString(R.string.dev_saved, host) } })
            }
        }
    }
    companion object {
        fun typeName(t: Int): String = when (t) {
            AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> "BUILT-IN SPEAKER"; AudioDeviceInfo.TYPE_BUILTIN_EARPIECE -> "EARPIECE"; AudioDeviceInfo.TYPE_BUILTIN_MIC -> "BUILT-IN MIC"
            AudioDeviceInfo.TYPE_WIRED_HEADSET -> "WIRED HEADSET"; AudioDeviceInfo.TYPE_WIRED_HEADPHONES -> "WIRED HEADPHONES"; AudioDeviceInfo.TYPE_LINE_ANALOG -> "LINE (ANALOG)"
            AudioDeviceInfo.TYPE_LINE_DIGITAL -> "LINE (DIGITAL)"; AudioDeviceInfo.TYPE_USB_DEVICE -> "USB AUDIO INTERFACE"; AudioDeviceInfo.TYPE_USB_HEADSET -> "USB HEADSET"
            AudioDeviceInfo.TYPE_USB_ACCESSORY -> "USB ACCESSORY"; AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> "BLUETOOTH (MUSIC)"; AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "BLUETOOTH (CALL)"
            AudioDeviceInfo.TYPE_HDMI -> "HDMI"; AudioDeviceInfo.TYPE_HDMI_ARC -> "HDMI ARC"; AudioDeviceInfo.TYPE_DOCK -> "DOCK"; AudioDeviceInfo.TYPE_AUX_LINE -> "AUX LINE"
            AudioDeviceInfo.TYPE_REMOTE_SUBMIX -> "REMOTE SUBMIX"; AudioDeviceInfo.TYPE_TELEPHONY -> "TELEPHONY"; AudioDeviceInfo.TYPE_FM_TUNER -> "FM TUNER"
            else -> if (android.os.Build.VERSION.SDK_INT >= 31 && t == AudioDeviceInfo.TYPE_BLE_HEADSET) "BLUETOOTH LE" else "TYPE $t" }
    }
}
