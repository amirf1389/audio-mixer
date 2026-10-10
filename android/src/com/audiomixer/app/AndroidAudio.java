package com.audiomixer.app;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.Build;
import android.os.Process;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

/** The phone's audio hardware for MiniBridge: every input / output Android lists (built-in, USB audio class interfaces, Bluetooth, wired) through AudioRecord and AudioTrack. */
final class AndroidAudio implements AudioBackend {
    private final Context ctx;
    private final AudioManager am;

    AndroidAudio(Context ctx) { this.ctx = ctx.getApplicationContext(); this.am = (AudioManager) this.ctx.getSystemService(Context.AUDIO_SERVICE); }

    private List<Interfaces.Dev> devices() {
        List<Interfaces.Dev> out = new ArrayList<Interfaces.Dev>();
        for (AudioDeviceInfo d : am.getDevices(AudioManager.GET_DEVICES_ALL)) {
            int ch = Interfaces.maxChannels(d.getChannelCounts(), d.getChannelIndexMasks()), rate = 0;               // a multi-channel USB interface lists its channels as index masks
            for (int r : d.getSampleRates()) rate = Math.max(rate, r);
            if (ch == 0) ch = d.getType() == AudioDeviceInfo.TYPE_BUILTIN_MIC || d.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO ? 1 : 2;
            out.add(new Interfaces.Dev(d.getId(), String.valueOf(d.getProductName()), d.getType(), d.isSource(), d.isSink(), ch, rate > 0 && rate <= 192000 ? rate : 48000));
        }
        return out;
    }

    public String interfacesJson() { return Interfaces.toJson(devices()); }

    public String devicesJson() { return Interfaces.devicesJson(devices()); }

    private int nativeProp(String name) { try { return Integer.parseInt(String.valueOf(am.getProperty(name))); } catch (Exception e) { return 0; } }

    /** What the hardware itself does best: the output sample rate and burst size Android reports, low latency / pro audio / USB host features. */
    public String nativeJson() {
        PackageManager pm = ctx.getPackageManager();
        return Interfaces.nativeJson(nativeProp(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE), nativeProp(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER),
            pm.hasSystemFeature("android.hardware.audio.low_latency"), pm.hasSystemFeature("android.hardware.audio.pro"), pm.hasSystemFeature("android.hardware.usb.host"));
    }

    private AudioDeviceInfo find(int id, boolean source) {
        if (id < 0) return null;
        for (AudioDeviceInfo d : am.getDevices(source ? AudioManager.GET_DEVICES_INPUTS : AudioManager.GET_DEVICES_OUTPUTS)) if (d.getId() == id) return d;
        return null;
    }

    private static int clamp(int v, int lo, int hi) { return Math.max(lo, Math.min(hi, v)); }

    private int frames(Map<String, Object> o, int rate) {
        return Interfaces.autoFrames(o.get("frameSize"), rate, nativeProp(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE), nativeProp(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER));   // "auto": two bursts of the device's own buffer
    }

    private static Map<String, Object> info(String device, String api, int rate, int ch, int frames) {
        Map<String, Object> m = new HashMap<String, Object>();
        m.put("device", device); m.put("hostApi", api); m.put("sampleRate", rate); m.put("channels", ch); m.put("frameSize", frames);
        m.put("latencyMs", Math.round(frames * 2.0 / rate * 10000) / 10.0);
        return m;
    }

    public Input openInput(Map<String, Object> o, final Listener l) throws Exception {
        if (ctx.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED)
            throw new Exception("the microphone permission is missing: allow it for Audio Mixer in the Android settings");
        int rate = clamp(Json.intOf(o, "sampleRate", 48000), 8000, 192000);
        AudioDeviceInfo dev = find(Json.intOf(o, "deviceId", -1), true);
        int maxCh = 2;
        if (dev != null) { int m = Interfaces.maxChannels(dev.getChannelCounts(), dev.getChannelIndexMasks()); if (m > 0) maxCh = m; }
        final int ch = clamp(Json.intOf(o, "channels", 2), 1, Math.min(maxCh, 8));
        final int fr = frames(o, rate);
        AudioFormat.Builder fb = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(rate);
        if (ch <= 2) fb.setChannelMask(ch == 1 ? AudioFormat.CHANNEL_IN_MONO : AudioFormat.CHANNEL_IN_STEREO); else fb.setChannelIndexMask((1 << ch) - 1);     // multi-channel USB interfaces
        int min = ch <= 2 ? AudioRecord.getMinBufferSize(rate, ch == 1 ? AudioFormat.CHANNEL_IN_MONO : AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_16BIT) : 0;
        final int bytes = fr * ch * 2;
        int src = MediaRecorder.AudioSource.MIC;
        if ("true".equals(am.getProperty("android.media.property.SUPPORT_AUDIO_SOURCE_UNPROCESSED"))) src = 9;           // UNPROCESSED: no gain control or noise suppression, as a mixer input should be
        final AudioRecord rec;
        try { rec = new AudioRecord.Builder().setAudioSource(src).setAudioFormat(fb.build()).setBufferSizeInBytes(Math.max(min, bytes * 4)).build(); }
        catch (Exception e) { throw new Exception("could not open the input: " + e.getMessage()); }
        if (rec.getState() != AudioRecord.STATE_INITIALIZED) { rec.release(); throw new Exception("could not open the input (another app may be using it)"); }
        if (dev != null) rec.setPreferredDevice(dev);
        rec.startRecording();
        final boolean[] on = { true };
        Thread t = new Thread(new Runnable() { public void run() {
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
            byte[] buf = new byte[bytes];
            while (on[0]) {
                int n = rec.read(buf, 0, buf.length);
                if (n > 0) l.onData(buf, n);
                else if (n < 0) { if (on[0]) l.onError("the input stopped (error " + n + ")"); break; }
            }
        } }, "mixer-capture");
        t.start();
        final Map<String, Object> inf = info(dev != null ? String.valueOf(dev.getProductName()) : "default microphone", "Android (AudioRecord)", rec.getSampleRate(), ch, fr);
        return new Input() {
            public Map<String, Object> info() { return inf; }
            public void close() { on[0] = false; try { rec.stop(); } catch (Exception e) { /* stopped */ } rec.release(); }
        };
    }

    public Output openOutput(Map<String, Object> o) throws Exception {
        int rate = clamp(Json.intOf(o, "sampleRate", 48000), 8000, 192000);
        AudioDeviceInfo dev = find(Json.intOf(o, "deviceId", -1), false);
        final int ch = clamp(Json.intOf(o, "channels", 2), 1, 8);
        final int fr = frames(o, rate);
        AudioFormat.Builder fb = new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(rate);
        int mask = ch == 1 ? AudioFormat.CHANNEL_OUT_MONO : AudioFormat.CHANNEL_OUT_STEREO;
        if (ch <= 2) fb.setChannelMask(mask); else fb.setChannelIndexMask((1 << ch) - 1);
        int min = ch <= 2 ? AudioTrack.getMinBufferSize(rate, mask, AudioFormat.ENCODING_PCM_16BIT) : 0;
        AudioTrack.Builder tb = new AudioTrack.Builder()
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setAudioFormat(fb.build()).setBufferSizeInBytes(Math.max(min, fr * ch * 2 * 4)).setTransferMode(AudioTrack.MODE_STREAM);
        if (Build.VERSION.SDK_INT >= 26) { try { tb.getClass().getMethod("setPerformanceMode", int.class).invoke(tb, 1); } catch (Throwable e) { /* low latency is optional */ } }      // PERFORMANCE_MODE_LOW_LATENCY
        final AudioTrack track;
        try { track = tb.build(); } catch (Exception e) { throw new Exception("could not open the output: " + e.getMessage()); }
        if (track.getState() != AudioTrack.STATE_INITIALIZED) { track.release(); throw new Exception("could not open the output"); }
        if (dev != null) track.setPreferredDevice(dev);
        track.play();
        final BlockingQueue<byte[]> q = new ArrayBlockingQueue<byte[]>(24);                // a few blocks: when the page is faster than the device the oldest are dropped, delay stays bounded
        final boolean[] on = { true };
        Thread t = new Thread(new Runnable() { public void run() {
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);
            while (on[0]) { try { byte[] b = q.take(); if (b.length == 0) break; track.write(b, 0, b.length); } catch (InterruptedException e) { break; } }
        } }, "mixer-playback");
        t.start();
        final Map<String, Object> inf = info(dev != null ? String.valueOf(dev.getProductName()) : "default speaker", "Android (AudioTrack)", track.getSampleRate(), ch, fr);
        return new Output() {
            public Map<String, Object> info() { return inf; }
            public void write(byte[] p, int off, int len) {
                byte[] c = new byte[len]; System.arraycopy(p, off, c, 0, len);
                while (!q.offer(c)) q.poll();
            }
            public void close() { on[0] = false; q.clear(); q.offer(new byte[0]); try { track.stop(); } catch (Exception e) { /* stopped */ } track.release(); }
        };
    }
}
