package com.amfb.amixos

import android.content.Context
import android.content.Intent
import android.provider.Settings

/** BRAND PROFILE: which maker made this phone and how to run audio well on it, the way the desktop console's driver intelligence knows
 *  sound cards by name. Two parts:
 *   - the MAKER table: every major Android brand (and its sub-brands) with the one thing that most often stops audio on it, its own
 *     battery manager, and where the user turns it off. Brand-wide behaviour only, nothing model-specific is guessed.
 *   - the TUNING: the buffer headroom (in HAL bursts) the native AAudio loop asks for, taken from what the phone's audio HAL actually
 *     reported (AudioHal): no low-latency path or a long burst -> more headroom; a low-latency exclusive (MMAP) path -> the minimum. */
data class BrandProfile(val maker: String, val family: String, val batteryTip: String, val bursts: Int, val notes: List<String>) {
    companion object {
        private data class Maker(val re: Regex, val name: String, val family: String, val battery: String)
        private val makers = listOf(
            Maker(Regex("samsung", RegexOption.IGNORE_CASE), "Samsung", "Galaxy (One UI)", "Settings → Apps → AmF X1 → Battery → Unrestricted; also remove it from Sleeping apps (Battery → Background usage limits)."),
            Maker(Regex("xiaomi|redmi|poco", RegexOption.IGNORE_CASE), "Xiaomi", "Xiaomi / Redmi / POCO (HyperOS / MIUI)", "Settings → Apps → AmF X1 → Autostart ON and Battery saver → No restrictions."),
            Maker(Regex("huawei", RegexOption.IGNORE_CASE), "Huawei", "Huawei (EMUI / HarmonyOS)", "Settings → Battery → App launch → AmF X1 → Manage manually, all three switches on."),
            Maker(Regex("honor", RegexOption.IGNORE_CASE), "Honor", "Honor (MagicOS)", "Settings → Battery → App launch → AmF X1 → Manage manually, all switches on."),
            Maker(Regex("oneplus", RegexOption.IGNORE_CASE), "OnePlus", "OnePlus (OxygenOS)", "Settings → Battery → Battery optimisation → AmF X1 → Don't optimise; lock it in recent apps."),
            Maker(Regex("oppo", RegexOption.IGNORE_CASE), "OPPO", "OPPO (ColorOS)", "Settings → Battery → AmF X1 → Allow background activity; allow Auto launch."),
            Maker(Regex("realme", RegexOption.IGNORE_CASE), "realme", "realme (realme UI)", "Settings → Battery → AmF X1 → Allow background activity; allow Auto launch."),
            Maker(Regex("vivo|iqoo", RegexOption.IGNORE_CASE), "vivo", "vivo / iQOO (OriginOS / Funtouch)", "Settings → Battery → Background power consumption → AmF X1 → Allow."),
            Maker(Regex("google", RegexOption.IGNORE_CASE), "Google", "Pixel", "Settings → Apps → AmF X1 → App battery usage → Unrestricted."),
            Maker(Regex("motorola|lenovo", RegexOption.IGNORE_CASE), "Motorola", "Motorola / Lenovo", "Settings → Apps → AmF X1 → App battery usage → Unrestricted."),
            Maker(Regex("sony", RegexOption.IGNORE_CASE), "Sony", "Xperia", "Settings → Battery → Adaptive battery off for AmF X1 (STAMINA mode stops background audio)."),
            Maker(Regex("nokia|hmd", RegexOption.IGNORE_CASE), "Nokia / HMD", "Nokia (HMD)", "Settings → Apps → AmF X1 → Battery → Unrestricted."),
            Maker(Regex("asus", RegexOption.IGNORE_CASE), "ASUS", "ASUS (ZenUI / ROG)", "Settings → Battery → PowerMaster / Auto-start manager → allow AmF X1."),
            Maker(Regex("nothing", RegexOption.IGNORE_CASE), "Nothing", "Nothing Phone", "Settings → Apps → AmF X1 → Battery → Unrestricted."),
            Maker(Regex("tecno|infinix|itel", RegexOption.IGNORE_CASE), "Transsion", "TECNO / Infinix / itel (HiOS / XOS)", "Phone Master → App auto-start and Power saving → allow AmF X1."),
            Maker(Regex("zte|nubia", RegexOption.IGNORE_CASE), "ZTE", "ZTE / nubia", "Settings → Battery → AmF X1 → allow background running."),
            Maker(Regex("meizu", RegexOption.IGNORE_CASE), "Meizu", "Meizu (Flyme)", "Settings → Battery → AmF X1 → allow background running."),
            Maker(Regex("fairphone", RegexOption.IGNORE_CASE), "Fairphone", "Fairphone", "Settings → Apps → AmF X1 → Battery → Unrestricted."))

        /** the profile for this phone, from its HAL report (AudioHal.read) */
        fun of(hal: AudioHal): BrandProfile {
            val key = hal.manufacturer + " " + hal.brand
            val m = makers.firstOrNull { it.re.containsMatchIn(key) }
            val notes = mutableListOf<String>()
            // the buffer headroom, from what the HAL measured, not from the brand name
            val bursts = when {
                !hal.outOpened -> 4.also { notes += "AAudio did not open for the probe: the mixer starts with extra buffer." }
                !hal.outLowLatency -> 4.also { notes += "The HAL gave no low-latency output path: 4 bursts of buffer to avoid clicks." }
                hal.burstMs > 10 -> 3.also { notes += "Long HAL burst (" + String.format("%.1f", hal.burstMs) + " ms): 3 bursts of buffer." }
                hal.outExclusive -> 2.also { notes += "Exclusive (MMAP) low-latency output: the minimum buffer (2 bursts)." }
                else -> 2.also { notes += "Low-latency shared output: 2 bursts of buffer." }
            }
            if (hal.socMaker.contains("mediatek", true) && bursts < 3) notes += "MediaTek SoC: if you hear clicks, raise the buffer in Settings."
            if (!hal.unprocessedMic) notes += "No unprocessed microphone: Android's voice processing may colour the input."
            if (hal.usbHost) notes += "USB audio interfaces are supported (USB-C / OTG): pick yours on Devices & network."
            notes += "Bluetooth adds 100–300 ms of delay: use wired headphones or a USB interface to monitor live."
            return BrandProfile(m?.name ?: hal.manufacturer.replaceFirstChar { it.uppercase() }, m?.family ?: "Android", m?.battery ?: "Settings → Apps → AmF X1 → Battery → Unrestricted.", bursts, notes)
        }
        /** Android's own list of battery-optimised apps (no permission needed) */
        fun batterySettings(c: Context): Intent = Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    }
}
