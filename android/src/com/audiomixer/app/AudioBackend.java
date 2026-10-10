package com.audiomixer.app;

import java.util.Map;

/** What the bridge needs from the audio hardware. AndroidAudio implements it with AudioRecord / AudioTrack; the tests with a fake. No android.* classes here. */
interface AudioBackend {
    /** Body of GET /api/interfaces. */
    String interfacesJson();

    /** Devices as the bridge's PortAudio-shaped list, for GET /api/drivers: JSON array of {id,name,hostApi,inputs,outputs,sampleRate}. */
    String devicesJson();

    Input openInput(Map<String, Object> opts, Listener listener) throws Exception;

    Output openOutput(Map<String, Object> opts) throws Exception;

    interface Listener { void onData(byte[] pcm, int len); void onError(String message); }

    interface Stream {
        /** device, hostApi, sampleRate, channels, frameSize, latencyMs (what the "started" message reports). */
        Map<String, Object> info();
        void close();
    }

    interface Input extends Stream {}

    interface Output extends Stream { void write(byte[] pcm, int off, int len); }
}
