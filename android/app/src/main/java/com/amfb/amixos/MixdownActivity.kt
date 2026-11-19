package com.amfb.amixos

import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.OptIn
import androidx.appcompat.app.AppCompatActivity
import androidx.media3.common.MediaItem
import androidx.media3.common.MimeTypes
import androidx.media3.common.audio.ChannelMixingAudioProcessor
import androidx.media3.common.audio.ChannelMixingMatrix
import androidx.media3.common.util.UnstableApi
import androidx.media3.transformer.Composition
import androidx.media3.transformer.DefaultAudioMixer
import androidx.media3.transformer.EditedMediaItem
import androidx.media3.transformer.EditedMediaItemSequence
import androidx.media3.transformer.Effects
import androidx.media3.transformer.ExportException
import androidx.media3.transformer.ExportResult
import androidx.media3.transformer.ProgressHolder
import androidx.media3.transformer.Transformer
import java.io.File

/** MIXDOWN: several audio files (MP3, M4A / AAC, WAV, FLAC, OGG ... whatever the phone decodes) played together and exported as one
 *  AAC (.m4a) file. Each file is its own sequence in a Media3 Composition; Media3's AudioMixer (DefaultAudioMixer) adds them up, and a
 *  channel-mixing processor sets each one's volume (mono files go to both sides). Nothing leaves the phone: the result is saved where
 *  the user picks. */
@OptIn(UnstableApi::class)
class MixdownActivity : FitActivity() {
    private data class Track(val uri: Uri, val name: String, var volume: Float = 1f)
    private val tracks = mutableListOf<Track>()
    private lateinit var list: LinearLayout
    private lateinit var status: TextView
    private lateinit var exportBtn: Button
    private var transformer: Transformer? = null
    private var out: File? = null
    private val ui = Handler(Looper.getMainLooper())

    private val pick = registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        for (u in uris) tracks += Track(u, nameOf(u))
        render()
    }
    private val save = registerForActivityResult(ActivityResultContracts.CreateDocument("audio/mp4")) { dest ->
        val src = out ?: return@registerForActivityResult
        if (dest == null) { status.text = getString(R.string.mix_not_saved); return@registerForActivityResult }
        runCatching { contentResolver.openOutputStream(dest)!!.use { o -> src.inputStream().use { it.copyTo(o) } } }
            .onSuccess { status.text = getString(R.string.mix_saved) }.onFailure { status.text = getString(R.string.mix_save_failed) + " " + it.message }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 40, 32, 32) }
        box.addView(TextView(this).apply { text = getString(R.string.mix_title); setTextColor(getColor(R.color.accent)); textSize = 22f; typeface = Typeface.DEFAULT_BOLD; letterSpacing = 0.12f })
        box.addView(TextView(this).apply { text = getString(R.string.mix_intro); setTextColor(getColor(R.color.dim)); textSize = 12f; setPadding(0, 6, 0, 20) })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        row.addView(Button(this).apply { text = getString(R.string.mix_add); setOnClickListener { pick.launch(arrayOf("audio/*")) } })
        exportBtn = Button(this).apply { text = getString(R.string.mix_export); setOnClickListener { export() } }
        row.addView(exportBtn)
        box.addView(row)
        status = TextView(this).apply { setTextColor(getColor(R.color.cyan)); textSize = 12f; setPadding(0, 16, 0, 16); typeface = Typeface.MONOSPACE }
        box.addView(status)
        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        box.addView(list)
        setContentView(Nav.wrap(this, ScrollView(this).apply { addView(box) }, null))
        render()
    }

    private fun render() {
        list.removeAllViews()
        if (tracks.isEmpty()) list.addView(TextView(this).apply { text = getString(R.string.mix_empty); setTextColor(getColor(R.color.dim)); textSize = 13f })
        tracks.forEachIndexed { i, t ->
            val card = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.card_bg); setPadding(28, 20, 28, 20)
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 0, 0, 16) } }
            val head = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
            head.addView(TextView(this).apply { text = "${i + 1}  ${t.name}"; setTextColor(Color.WHITE); textSize = 14f; typeface = Typeface.DEFAULT_BOLD; maxLines = 1
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) })
            head.addView(Button(this).apply { text = "✕"; setOnClickListener { tracks.removeAt(i); render() } })
            card.addView(head)
            val vol = TextView(this).apply { setTextColor(getColor(R.color.dim)); textSize = 11f; typeface = Typeface.MONOSPACE }
            fun showVol() { vol.text = getString(R.string.mix_volume) + " " + (t.volume * 100).toInt() + " %" }
            showVol()
            card.addView(vol)
            card.addView(SeekBar(this).apply { max = 150; progress = (t.volume * 100).toInt()
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(s: SeekBar?, p: Int, fromUser: Boolean) { t.volume = p / 100f; showVol() }
                    override fun onStartTrackingTouch(s: SeekBar?) {}
                    override fun onStopTrackingTouch(s: SeekBar?) {} }) })
            list.addView(card)
        }
        exportBtn.isEnabled = tracks.isNotEmpty() && transformer == null; exportBtn.alpha = if (exportBtn.isEnabled) 1f else 0.4f
    }

    // every track is its own sequence: Media3 mixes parallel sequences with the AudioMixer given to the Transformer
    private fun export() {
        if (tracks.isEmpty() || transformer != null) return
        val sequences = tracks.map { t ->
            val gain = ChannelMixingAudioProcessor().apply {
                putChannelMixingMatrix(ChannelMixingMatrix.create(1, 2).scaleBy(t.volume))   // mono: to both sides
                putChannelMixingMatrix(ChannelMixingMatrix.create(2, 2).scaleBy(t.volume))
            }
            EditedMediaItemSequence(EditedMediaItem.Builder(MediaItem.fromUri(t.uri)).setRemoveVideo(true).setEffects(Effects(listOf(gain), listOf())).build())
        }
        val file = File(cacheDir, "amfx1-mixdown.m4a").also { it.delete() }
        out = null
        val tr = Transformer.Builder(this)
            .setAudioMimeType(MimeTypes.AUDIO_AAC)
            .setAudioMixerFactory(DefaultAudioMixer.Factory())
            .addListener(object : Transformer.Listener {
                override fun onCompleted(composition: Composition, exportResult: ExportResult) {
                    transformer = null; out = file
                    status.text = getString(R.string.mix_done, file.length() / 1024, exportResult.durationMs / 1000.0)
                    render(); save.launch("AmF X1 mixdown.m4a")
                }
                override fun onError(composition: Composition, exportResult: ExportResult, exportException: ExportException) {
                    transformer = null; status.text = getString(R.string.mix_failed) + " " + (exportException.message ?: exportException.errorCodeName); render()
                }
            }).build()
        transformer = tr
        tr.start(Composition.Builder(sequences).build(), file.absolutePath)
        render()
        val holder = ProgressHolder()
        ui.post(object : Runnable { override fun run() {
            val t = transformer ?: return
            if (t.getProgress(holder) == Transformer.PROGRESS_STATE_AVAILABLE) status.text = getString(R.string.mix_progress, holder.progress)
            ui.postDelayed(this, 250) } })
    }

    private fun nameOf(u: Uri): String = runCatching {
        contentResolver.query(u, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
    }.getOrNull() ?: (u.lastPathSegment ?: "audio")

    override fun onDestroy() { transformer?.cancel(); transformer = null; ui.removeCallbacksAndMessages(null); super.onDestroy() }
}
