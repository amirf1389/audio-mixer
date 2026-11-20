package com.amfb.amixos

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager

/** One USB audio interface as Android reports it (one entry per direction: Android lists the inputs and the outputs of a device apart).
 *  [channelCounts] / [channelIndexMasks] / [sampleRates] empty = the device takes any (Android's convention). */
data class UsbAudioDevice(
    val id: Int,
    val name: String,
    val type: Int,
    val isInput: Boolean,
    val channelCounts: List<Int>,
    val channelIndexMasks: List<Int>,
    val sampleRates: List<Int>,
) {
    /** the most channels the device offers: the highest count or the widest index mask (8 for an 8-in interface), 0 = not told (any) */
    val maxChannels: Int get() = maxOf(channelCounts.maxOrNull() ?: 0, channelIndexMasks.maxOfOrNull { Integer.bitCount(it) } ?: 0)
    /** whether [rate] is offered (an empty list = any rate) */
    fun supportsRate(rate: Int): Boolean = sampleRates.isEmpty() || rate in sampleRates
    /** what to ask the device for: [wanted] (0 = all it has) capped to what it offers */
    fun channelsFor(wanted: Int): Int = when { maxChannels == 0 -> if (wanted > 0) wanted else 2; wanted <= 0 -> maxChannels; else -> minOf(wanted, maxChannels) }
}

/** USB AUDIO INTERFACES for the native AAudio loop ([Engine.startUsb]): list them with their channels and rates, pick one, open N channels. */
object UsbAudio {
    val USB_TYPES = setOf(AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_HEADSET)

    /** every USB audio input and output now attached */
    fun list(context: Context): List<UsbAudioDevice> {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        // asked per direction: the direction is the query's (a device of both directions appears once as input, once as output)
        return fromDevices(am.getDevices(AudioManager.GET_DEVICES_INPUTS).toList(), true) + fromDevices(am.getDevices(AudioManager.GET_DEVICES_OUTPUTS).toList(), false)
    }
    fun inputs(context: Context) = list(context).filter { it.isInput }
    fun outputs(context: Context) = list(context).filter { !it.isInput }

    /** the USB ones of [devices], described (no other device types) */
    fun fromDevices(devices: List<AudioDeviceInfo>, isInput: Boolean): List<UsbAudioDevice> = devices.filter { it.type in USB_TYPES }.map { describe(it, isInput) }

    fun describe(d: AudioDeviceInfo, isInput: Boolean = d.isSource): UsbAudioDevice = UsbAudioDevice(
        id = d.id,
        name = runCatching { d.productName?.toString() }.getOrNull().orEmpty().ifEmpty { if (d.type == AudioDeviceInfo.TYPE_USB_HEADSET) "USB headset" else "USB audio" },
        type = d.type,
        isInput = isInput,
        channelCounts = runCatching { d.channelCounts.toList() }.getOrDefault(emptyList()).filter { it > 0 }.distinct().sorted(),
        channelIndexMasks = runCatching { d.channelIndexMasks.toList() }.getOrDefault(emptyList()).filter { it != 0 }.distinct(),
        sampleRates = runCatching { d.sampleRates.toList() }.getOrDefault(emptyList()).filter { it > 0 }.distinct().sorted(),
    )

    /** device channel k -> strip k, for the first [strips] channels; -1 where a channel has no strip (the native loop does the same) */
    fun stripMap(channels: Int, strips: Int): IntArray = IntArray(maxOf(channels, 0)) { if (it < strips) it else -1 }
}
