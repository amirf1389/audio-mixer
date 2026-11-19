package com.amfb.amixos

import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.widget.Button
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/** AUDIO CALCULATOR: works offline, nothing is sent. Six tools; results update as you type.
 *  DELAY (tempo to note lengths) · ALIGN (distance to delay, temperature) · dB (ratios, adding sources) · FREQUENCY (wavelength, note)
 *  · LATENCY (buffer to milliseconds, round trip) · STORAGE (recording size). */
class CalculatorActivity : AppCompatActivity() {
    private lateinit var body: LinearLayout
    private val accent = Color.parseColor("#FFB590"); private val cyan = Color.parseColor("#7FD8E8"); private val dim = Color.parseColor("#C2C7CF")
    private val tools = listOf("DELAY", "ALIGN", "dB", "FREQUENCY", "LATENCY", "STORAGE")
    private val tabs = ArrayList<Button>()
    var current = 0; private set

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        root.addView(TextView(this).apply { text = getString(R.string.calc_title); setTextColor(accent); textSize = 20f; setTypeface(null, Typeface.BOLD) })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        tools.forEachIndexed { i, t -> row.addView(Button(this).apply { text = t; isAllCaps = false; setOnClickListener { show(i) } }.also { tabs.add(it) }) }
        root.addView(HorizontalScrollView(this).apply { addView(row) })
        body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, 16, 0, 0) }
        root.addView(body)
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(Color.parseColor("#111418")); addView(root) }, null))
        show(intent.getIntExtra("tool", 0))
    }

    fun show(i: Int) {
        current = i; body.removeAllViews(); tabs.forEachIndexed { k, b -> b.setTextColor(if (k == i) accent else Color.WHITE) }
        when (i) {
            0 -> { val bpm = field("Tempo (BPM)", "120"); val out = result()
                calc(bpm) { val b = num(bpm); if (b == null || b <= 0) "—" else AudioMath.noteTimes(b).joinToString("\n") { (n, ms) -> String.format("%-12s %8.1f ms   %6.2f Hz", n, ms, 1000 / ms) } }(out)
                note("Echo and delay times that sit on the beat. Hz is the same length as a modulation rate (chorus, tremolo).") }
            1 -> { val d = field("Distance (m)", "3.4"); val t = field("Air temperature (°C)", "20"); val out = result()
                calc(d, t) { val m = num(d) ?: return@calc "—"; val c = num(t) ?: 20.0
                    String.format("Delay   %.2f ms\nSpeed of sound   %.1f m/s\nSamples at 48 kHz   %.0f", AudioMath.distanceToMs(m, c), AudioMath.speedOfSound(c), AudioMath.distanceToMs(m, c) * 48) }(out)
                note("Delay the nearer speaker by this much so both arrive together (delay towers, front fills). Set it on the channel page: DELAY.") }
            2 -> { val db = field("Level change (dB)", "6"); val list = field("Add sources (dB, comma separated)", "90, 90, 87"); val out = result()
                calc(db, list) { val v = num(db); val parts = list.text.split(',').mapNotNull { it.trim().toDoubleOrNull() }
                    (if (v == null) "" else String.format("%+.1f dB = ×%.3f voltage, ×%.3f power\n", v, AudioMath.dbToVoltageRatio(v), AudioMath.dbToPowerRatio(v))) +
                        (if (parts.isEmpty()) "" else String.format("Together (unrelated sources): %.1f dB\nTwo equal in phase: +6.0 dB · unrelated: +3.0 dB", AudioMath.sumIncoherent(parts))) }(out)
                note("+6 dB is twice the voltage, +10 dB sounds about twice as loud, +3 dB is twice the power.") }
            3 -> { val hz = field("Frequency (Hz)", "440"); val t = field("Air temperature (°C)", "20"); val out = result()
                calc(hz, t) { val f = num(hz); if (f == null || f <= 0) return@calc "—"; val c = num(t) ?: 20.0; val (n, cents) = AudioMath.noteOf(f)
                    String.format("Wavelength   %.3f m  (half: %.3f m, quarter: %.3f m)\nPeriod   %.3f ms\nNote   %s %+d cents", AudioMath.wavelength(f, c), AudioMath.wavelength(f, c) / 2, AudioMath.wavelength(f, c) / 4, 1000 / f, n, cents) }(out)
                note("A quarter wavelength is the distance at which a reflection cancels that frequency (comb filtering, bass traps).") }
            4 -> { val fr = field("Buffer (frames)", "256"); val sr = field("Sample rate (Hz)", "48000"); val out = result()
                calc(fr, sr) { val f = num(fr) ?: return@calc "—"; val s = num(sr) ?: return@calc "—"; if (s <= 0) return@calc "—"; val one = AudioMath.latencyMs(f.toInt(), s)
                    String.format("One buffer   %.2f ms\nIn + out (round trip, at least)   %.2f ms\nDistance it equals   %.2f m of air", one, one * 2, AudioMath.msToDistance(one * 2)) }(out)
                note("Below about 10 ms round trip, players do not feel the delay. Each metre from a speaker adds about 2.9 ms.") }
            else -> { val sr = field("Sample rate (Hz)", "48000"); val bits = field("Bits", "24"); val ch = field("Channels", "32"); val min = field("Minutes", "60"); val out = result()
                calc(sr, bits, ch, min) { val r = num(sr); val b = num(bits); val c = num(ch); val m = num(min); if (r == null || b == null || c == null || m == null) return@calc "—"
                    val mb = AudioMath.pcmMegabytes(r, b.toInt(), c.toInt(), m)
                    String.format("Size   %.0f MB  (%.2f GB)\nData rate   %.2f MB/s", mb, mb / 1000, mb / (m * 60)) }(out)
                note("Uncompressed WAV / multitrack recording. FLAC is usually 50-60 % of this.") }
        }
    }
    private fun num(e: EditText) = e.text.toString().replace(',', '.').trim().toDoubleOrNull()
    private fun field(label: String, value: String): EditText {
        body.addView(TextView(this).apply { text = label; setTextColor(dim); setPadding(0, 12, 0, 0) })
        return EditText(this).apply { setText(value); setTextColor(Color.WHITE); textSize = 20f; inputType = InputType.TYPE_CLASS_TEXT }.also { body.addView(it) }
    }
    private fun result() = TextView(this).apply { setTextColor(cyan); typeface = Typeface.MONOSPACE; textSize = 15f; setPadding(0, 20, 0, 8) }.also { body.addView(it) }
    private fun note(t: String) = body.addView(TextView(this).apply { text = t; setTextColor(dim); textSize = 12f; setPadding(0, 12, 0, 0) })
    /** Runs f now and again whenever one of the fields changes. */
    private fun calc(vararg fs: EditText, f: () -> String): (TextView) -> Unit = { out ->
        val run = { out.text = runCatching(f).getOrDefault("—") }
        fs.forEach { it.addTextChangedListener(object : TextWatcher { override fun afterTextChanged(s: Editable?) { run() }; override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}; override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {} }) }
        run() }
}
