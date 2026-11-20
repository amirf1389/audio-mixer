package com.amfb.amixos

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.shadows.AudioDeviceInfoBuilder

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class UsbAudioTest {
    private val ctx: Context = ApplicationProvider.getApplicationContext()
    private val am get() = ctx.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private fun dev(type: Int): AudioDeviceInfo = AudioDeviceInfoBuilder.newBuilder().setType(type).build()

    @Test fun listsOnlyUsbInterfacesInBothDirections() {
        val sh = shadowOf(am)
        sh.addOutputDevice(dev(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER), false)
        sh.addOutputDevice(dev(AudioDeviceInfo.TYPE_USB_DEVICE), false)
        sh.addInputDevice(dev(AudioDeviceInfo.TYPE_BUILTIN_MIC), false)
        sh.addInputDevice(dev(AudioDeviceInfo.TYPE_USB_DEVICE), false)
        sh.addInputDevice(dev(AudioDeviceInfo.TYPE_USB_HEADSET), false)
        val all = UsbAudio.list(ctx)
        assertEquals(3, all.size)
        assertTrue(all.all { it.type in UsbAudio.USB_TYPES })
        assertEquals(2, UsbAudio.inputs(ctx).size)
        assertEquals(1, UsbAudio.outputs(ctx).size)
        assertTrue(UsbAudio.inputs(ctx).all { it.isInput } && UsbAudio.outputs(ctx).none { it.isInput })
        assertTrue(all.all { it.name.isNotEmpty() })
        // a shadow device tells no channels or rates: Android's "any", never a crash
        assertTrue(all.all { it.channelCounts.isEmpty() && it.sampleRates.isEmpty() && it.maxChannels == 0 })
    }

    @Test fun noUsbDeviceGivesAnEmptyList() {
        shadowOf(am).addOutputDevice(dev(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER), false)
        assertTrue(UsbAudio.list(ctx).isEmpty())
    }

    @Test fun channelsAndRatesOfAMultiChannelInterface() {
        // an 8-in interface: counts 1, 2 and an index mask of 8 channels; 44.1 / 48 / 96 kHz
        val d = UsbAudioDevice(7, "Interface", AudioDeviceInfo.TYPE_USB_DEVICE, true, listOf(1, 2), listOf(0xFF), listOf(44100, 48000, 96000))
        assertEquals(8, d.maxChannels)
        assertEquals(8, d.channelsFor(0)); assertEquals(4, d.channelsFor(4)); assertEquals(8, d.channelsFor(16))
        assertTrue(d.supportsRate(48000)); assertFalse(d.supportsRate(22050))
        val any = UsbAudioDevice(3, "Any", AudioDeviceInfo.TYPE_USB_HEADSET, false, emptyList(), emptyList(), emptyList())
        assertEquals(0, any.maxChannels); assertEquals(2, any.channelsFor(0)); assertEquals(6, any.channelsFor(6)); assertTrue(any.supportsRate(192000))
    }

    @Test fun deviceChannelsMapToStripsInOrder() {
        assertArrayEquals(intArrayOf(0, 1, 2, 3), UsbAudio.stripMap(4, 8))
        assertArrayEquals(intArrayOf(0, 1, -1, -1), UsbAudio.stripMap(4, 2))
        assertEquals(0, UsbAudio.stripMap(0, 8).size)
    }
}
