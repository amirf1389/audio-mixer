package com.amfb.amixos

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import java.nio.ByteOrder
import kotlin.concurrent.thread

/** A DECK of the standalone phone mixer: one song file (MP3, AAC / M4A, FLAC, Opus, Vorbis, WAV ... whatever Android decodes) played on a
 *  pair of mixer strips. Android's own decoder (MediaExtractor + MediaCodec) turns it into float stereo on a worker thread; the engine
 *  resamples it to the mixer's rate (r8brain) and the audio callback takes it from a lock-free buffer. PAUSE stops the deck where it is
 *  (the buffer is kept, so PLAY carries on without a gap); STOP and SEEK drop what is buffered. Nothing is downloaded: the user picks a
 *  file on the phone. */
class DeckPlayer(private val ctx: Context, private val engine: Engine, val index: Int, private val stripL: Int, private val mixRate: () -> Double) {
    interface Listener { fun changed(deck: DeckPlayer) }
    var listener: Listener? = null
    @Volatile var title: String = ""; private set
    @Volatile var durationUs: Long = 0; private set
    @Volatile var playing = false; private set
    @Volatile var loop = false
    @Volatile var error: String = ""; private set
    @Volatile var hasVideo = false; private set          // the file also carries a picture (the VIDEO button shows it)
    val source: Uri? get() = uri
    @Volatile private var uri: Uri? = null
    @Volatile private var startUs = 0L                 // where the frames now being played began (after a seek or a loop)
    @Volatile private var playedAtStart = 0L
    @Volatile private var feeding = false
    private var worker: Thread? = null
    private val lock = Object()
    @Volatile private var seekTo = -1L

    val loaded: Boolean get() = uri != null
    /** where the deck is, in microseconds (what the mixer has really played, not what was decoded) */
    val positionUs: Long get() { val rate = mixRate(); if (rate <= 0) return startUs
        val p = startUs + ((engine.deckPlayed(index) - playedAtStart) * 1_000_000.0 / rate).toLong(); return if (durationUs > 0) p.coerceIn(0, durationUs) else p }

    fun load(u: Uri, name: String) {
        stop(); uri = u; title = name; error = ""; durationUs = 0; hasVideo = false
        runCatching { MediaExtractor().apply { setDataSource(ctx, u, null) }.also { ex -> hasVideo = (0 until ex.trackCount).any { ex.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true }; trackOf(ex)?.let { durationUs = ex.getTrackFormat(it).let { f -> if (f.containsKey(MediaFormat.KEY_DURATION)) f.getLong(MediaFormat.KEY_DURATION) else 0L } }; ex.release() } }
            .onFailure { error = it.message ?: "cannot open"; uri = null }
        listener?.changed(this)
    }
    fun play() {
        if (uri == null || playing) return
        playing = true; engine.deckRoute(index, stripL)
        if (!feeding) startFeeding(startUs)
        listener?.changed(this)
    }
    fun pause() { if (!playing) return; playing = false; engine.deckRoute(index, -1); listener?.changed(this) }   // the buffer stays: PLAY resumes exactly here
    fun stop() {
        playing = false; engine.deckRoute(index, -1); stopFeeding(); engine.deckFlush(index); startUs = 0; playedAtStart = engine.deckPlayed(index)
        listener?.changed(this)
    }
    fun seek(us: Long) {
        if (uri == null) return
        val target = if (durationUs > 0) us.coerceIn(0, durationUs) else maxOf(0, us)
        if (feeding) { seekTo = target; synchronized(lock) { lock.notifyAll() } } else { startUs = target; playedAtStart = engine.deckPlayed(index) }
        listener?.changed(this)
    }
    fun release() { stop(); uri = null }

    private fun trackOf(ex: MediaExtractor): Int? { for (i in 0 until ex.trackCount) if (ex.getTrackFormat(i).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true) return i; return null }
    private fun stopFeeding() { feeding = false; synchronized(lock) { lock.notifyAll() }; worker?.join(1500); worker = null }

    private fun startFeeding(fromUs: Long) {
        val u = uri ?: return; feeding = true
        worker = thread(name = "amix-deck-$index", isDaemon = true) {
            var ex: MediaExtractor? = null; var codec: MediaCodec? = null
            try {
                ex = MediaExtractor().apply { setDataSource(ctx, u, null) }
                val track = trackOf(ex) ?: throw IllegalStateException("no audio in this file")
                ex.selectTrack(track); val fmt = ex.getTrackFormat(track)
                codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME)!!)
                fmt.setInteger(MediaFormat.KEY_PCM_ENCODING, AudioFormat.ENCODING_PCM_FLOAT)   // float where the decoder can; 16-bit otherwise
                codec.configure(fmt, null, null, 0); codec.start()
                var srcRate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE).toDouble(); var ch = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT); var isFloat = false
                fun restartAt(us: Long) { ex.seekTo(us, MediaExtractor.SEEK_TO_CLOSEST_SYNC); codec.flush(); engine.deckFlush(index); startUs = maxOf(0, ex.sampleTime.takeIf { it >= 0 } ?: us); playedAtStart = engine.deckPlayed(index) }
                if (fromUs > 0) restartAt(fromUs) else { engine.deckFlush(index); startUs = 0; playedAtStart = engine.deckPlayed(index) }
                val info = MediaCodec.BufferInfo(); var inputDone = false; var stereo = FloatArray(0)
                val ahead = 0.5   // seconds kept decoded ahead of the mixer
                while (feeding) {
                    val s = seekTo; if (s >= 0) { seekTo = -1; restartAt(s); inputDone = false }
                    // enough waiting: let the mixer take some (the native side also moves leftovers into its buffer here)
                    if (engine.deckPush(index, null, 0, srcRate, mixRate()) > mixRate() * ahead) { synchronized(lock) { lock.wait(10) }; continue }
                    if (!inputDone) { val ii = codec.dequeueInputBuffer(5000)
                        if (ii >= 0) { val buf = codec.getInputBuffer(ii)!!; val n = ex.readSampleData(buf, 0)
                            if (n < 0) { codec.queueInputBuffer(ii, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inputDone = true }
                            else { codec.queueInputBuffer(ii, 0, n, ex.sampleTime, 0); ex.advance() } } }
                    val oi = codec.dequeueOutputBuffer(info, 5000)
                    if (oi == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) { val of = codec.outputFormat; srcRate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE).toDouble(); ch = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                        isFloat = of.containsKey(MediaFormat.KEY_PCM_ENCODING) && of.getInteger(MediaFormat.KEY_PCM_ENCODING) == AudioFormat.ENCODING_PCM_FLOAT; continue }
                    if (oi >= 0) {
                        val out = codec.getOutputBuffer(oi)!!; out.order(ByteOrder.nativeOrder()); out.position(info.offset); out.limit(info.offset + info.size)
                        val samples = if (isFloat) info.size / 4 else info.size / 2; val frames = if (ch > 0) samples / ch else 0
                        if (frames > 0) {
                            if (stereo.size < 2 * frames) stereo = FloatArray(2 * frames)
                            for (f in 0 until frames) {   // to stereo: mono doubled, more than two channels: the first two (front left / right)
                                val l: Float; val r: Float
                                if (isFloat) { val fb = out.asFloatBuffer(); l = fb.get(f * ch); r = if (ch > 1) fb.get(f * ch + 1) else l }
                                else { val sb = out.asShortBuffer(); l = sb.get(f * ch) / 32768f; r = if (ch > 1) sb.get(f * ch + 1) / 32768f else l }
                                stereo[2 * f] = l; stereo[2 * f + 1] = r }
                            engine.deckPush(index, stereo, frames, srcRate, mixRate())
                        }
                        codec.releaseOutputBuffer(oi, false)
                        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) {
                            if (loop) { restartAtLoop(ex, codec); inputDone = false }
                            else { engine.deckPush(index, FloatArray(2 * 4096), 4096, srcRate, mixRate())   // the resampler's last few ms pushed out by a little silence
                                while (feeding && engine.deckPush(index, null, 0, srcRate, mixRate()) > 0) Thread.sleep(20)   // play out what is buffered
                                feeding = false; playing = false; engine.deckRoute(index, -1); startUs = 0; playedAtStart = engine.deckPlayed(index); listener?.changed(this) } }
                    }
                }
            } catch (e: Exception) { error = e.message ?: e.javaClass.simpleName; playing = false; engine.deckRoute(index, -1); listener?.changed(this) }
            finally { runCatching { codec?.stop() }; runCatching { codec?.release() }; runCatching { ex?.release() }; feeding = false }
        }
    }
    // LOOP: back to the start without dropping what is still buffered (the end of the song plays out, then the start follows)
    private fun restartAtLoop(ex: MediaExtractor, codec: MediaCodec) {
        ex.seekTo(0, MediaExtractor.SEEK_TO_CLOSEST_SYNC); codec.flush()
        val buffered = engine.deckPush(index, null, 0, 0.0, mixRate()); val rate = mixRate()
        // the position wraps when the buffered tail has played
        startUs = -(if (rate > 0) (buffered * 1_000_000.0 / rate).toLong() else 0L); playedAtStart = engine.deckPlayed(index)
    }

    companion object {
        /** "1:05" from microseconds */
        fun clock(us: Long): String { val s = maxOf(0L, us / 1_000_000); return "%d:%02d".format(s / 60, s % 60) }
    }
}
