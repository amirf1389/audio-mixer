package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** the brand profile: maker detection from Build and the buffer headroom from what the HAL probe reported */
class BrandProfileTest {
    private fun hal(maker: String, brand: String, opened: Boolean = true, low: Boolean = true, excl: Boolean = true, burst: Int = 96, rate: Int = 48000, soc: String = "") =
        AudioHal(maker, brand, "M", "d", soc, "", "15", 35, 48000, 192, true, true, true, true, true, opened, low, excl, burst, rate, burst * 4, true, low, excl, burst, rate)

    @Test fun makers() {
        assertEquals("Samsung", BrandProfile.of(hal("samsung", "samsung")).maker)
        assertEquals("Xiaomi", BrandProfile.of(hal("Xiaomi", "POCO")).maker)
        assertEquals("Xiaomi", BrandProfile.of(hal("Xiaomi", "Redmi")).maker)
        assertEquals("Google", BrandProfile.of(hal("Google", "google")).maker)
        assertEquals("OnePlus", BrandProfile.of(hal("OnePlus", "OnePlus")).maker)
        assertEquals("Huawei", BrandProfile.of(hal("HUAWEI", "HUAWEI")).maker)
        assertEquals("Honor", BrandProfile.of(hal("HONOR", "HONOR")).maker)
        assertEquals("OPPO", BrandProfile.of(hal("OPPO", "OPPO")).maker)
        assertEquals("Transsion", BrandProfile.of(hal("TECNO MOBILE LIMITED", "TECNO")).maker)
        assertEquals("Acme", BrandProfile.of(hal("acme", "acme")).maker)   // unknown makers keep their own name
    }
    @Test fun buffer() {
        assertEquals(2, BrandProfile.of(hal("Google", "google")).bursts)                    // MMAP low latency: the minimum
        assertEquals(2, BrandProfile.of(hal("Google", "google", excl = false)).bursts)      // shared low latency
        assertEquals(3, BrandProfile.of(hal("x", "x", burst = 960)).bursts)                  // 20 ms burst
        assertEquals(4, BrandProfile.of(hal("x", "x", low = false)).bursts)                 // no low-latency path
        assertEquals(4, BrandProfile.of(hal("x", "x", opened = false)).bursts)              // probe failed
        assertTrue(BrandProfile.of(hal("x", "x", soc = "Mediatek")).notes.any { it.contains("MediaTek") })
    }
}
