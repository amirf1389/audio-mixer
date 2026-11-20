package com.amfb.amixos

/** AUDIO PATH: which audio API runs the mixer. AUTO picks the best one the phone has; the others force one (with the Java path behind
 *  it, so there is always sound). Stored by the app as [name]. */
enum class AudioPathChoice(val label: String) {
    AUTO("AUTO"), AAUDIO("AAUDIO"), OPENSL("OPENSL ES"), JAVA("JAVA");
    companion object {
        /** a stored value back; anything unknown is AUTO */
        fun parse(s: String?): AudioPathChoice = entries.firstOrNull { it.name.equals(s?.trim(), ignoreCase = true) } ?: AUTO
    }
}

/** The path choice as pure functions (no Android calls), so it is unit-tested. Paths: "aaudio", "opensl", "java". */
object AudioPaths {
    const val AAUDIO = "aaudio"; const val OPENSL = "opensl"; const val JAVA = "java"
    /** AAudio exists from Android 8 (API 26) */
    const val AAUDIO_MIN_API = 26

    /** the paths to try, in order. AUTO: AAudio on API >= 26, then OpenSL ES, then Java; below 26 OpenSL ES, then Java. A forced
     *  AAUDIO below API 26 cannot run and goes straight to Java. */
    fun order(api: Int, choice: AudioPathChoice): List<String> = when (choice) {
        AudioPathChoice.AUTO -> if (api >= AAUDIO_MIN_API) listOf(AAUDIO, OPENSL, JAVA) else listOf(OPENSL, JAVA)
        AudioPathChoice.AAUDIO -> if (api >= AAUDIO_MIN_API) listOf(AAUDIO, JAVA) else listOf(JAVA)
        AudioPathChoice.OPENSL -> listOf(OPENSL, JAVA)
        AudioPathChoice.JAVA -> listOf(JAVA)
    }

    /** the path that runs: the first in [order] that did not fail to open. Java never fails here (it is the last resort). */
    fun choose(api: Int, choice: AudioPathChoice, aaudioFailed: Boolean = false, openSlFailed: Boolean = false): String =
        order(api, choice).first { !(it == AAUDIO && aaudioFailed) && !(it == OPENSL && openSlFailed) }

    /** the label the LOW LATENCY report shows */
    fun describe(path: String): String = when (path) {
        AAUDIO -> "AAudio (native)"; OPENSL -> "OpenSL ES (native)"; JAVA -> "AudioTrack / AudioRecord (Java fallback)"; else -> "stopped"
    }
}
