package com.amfb.amixos

import android.content.Context
import android.graphics.SurfaceTexture
import android.media.MediaPlayer
import android.net.Uri
import android.view.Surface
import android.view.TextureView

/**
 * A DECK'S PICTURE. A video file on a deck plays its sound through the deck's mixer channel (fader, effects, the recording); this
 * shows the picture beside it. The picture follows the sound: it starts, pauses and seeks with the deck, and is pulled back in step
 * when it drifts. It carries no sound of its own (its volume is zero), so the mixer is the only path to the output.
 */
class DeckVideo(private val ctx: Context, val view: TextureView) : TextureView.SurfaceTextureListener {
    enum class Step { NONE, START, PAUSE, SEEK }
    companion object {
        const val DRIFT_MS = 180L
        /** What the picture must do so it matches the deck's sound. */
        fun step(deckPlaying: Boolean, picturePlaying: Boolean, deckMs: Long, pictureMs: Long): Step = when {
            deckPlaying && !picturePlaying -> Step.START
            !deckPlaying && picturePlaying -> Step.PAUSE
            kotlin.math.abs(deckMs - pictureMs) > DRIFT_MS -> Step.SEEK
            else -> Step.NONE
        }
        /** The picture's height for a given width, from the video's own shape (16:9 until it is known). */
        fun heightFor(widthPx: Int, videoW: Int, videoH: Int): Int = if (videoW > 0 && videoH > 0) (widthPx.toLong() * videoH / videoW).toInt() else widthPx * 9 / 16
    }
    private var mp: MediaPlayer? = null; private var surface: Surface? = null; private var uri: Uri? = null; private var ready = false
    var onSize: ((Int, Int) -> Unit)? = null
    init { view.surfaceTextureListener = this; if (view.isAvailable) view.surfaceTexture?.let { surface = Surface(it) } }

    fun show(u: Uri) { if (u == uri && mp != null) return; close(); uri = u; open() }
    private fun open() {
        val u = uri ?: return; val s = surface ?: return
        runCatching {
            mp = MediaPlayer().apply {
                setDataSource(ctx, u); setSurface(s); setVolume(0f, 0f)
                setOnPreparedListener { ready = true; onSize?.invoke(it.videoWidth, it.videoHeight) }
                setOnErrorListener { _, _, _ -> ready = false; true }
                prepareAsync() }
        }.onFailure { mp = null; ready = false }
    }
    /** Called a few times a second with the deck's state. */
    fun follow(deckPlaying: Boolean, deckMs: Long) {
        val m = mp ?: return; if (!ready) return
        runCatching {
            when (step(deckPlaying, m.isPlaying, deckMs, m.currentPosition.toLong())) {
                Step.START -> { m.seekTo(deckMs, MediaPlayer.SEEK_CLOSEST); m.start() }
                Step.PAUSE -> { m.pause(); m.seekTo(deckMs, MediaPlayer.SEEK_CLOSEST) }
                Step.SEEK -> m.seekTo(deckMs, MediaPlayer.SEEK_CLOSEST)
                Step.NONE -> {}
            }
        }
    }
    fun close() { ready = false; runCatching { mp?.release() }; mp = null }

    override fun onSurfaceTextureAvailable(st: SurfaceTexture, w: Int, h: Int) { surface = Surface(st); if (mp == null) open() }
    override fun onSurfaceTextureSizeChanged(st: SurfaceTexture, w: Int, h: Int) {}
    override fun onSurfaceTextureDestroyed(st: SurfaceTexture): Boolean { close(); surface?.release(); surface = null; return true }
    override fun onSurfaceTextureUpdated(st: SurfaceTexture) {}
}
