package com.amfb.amixos

/** 1.27 BANK SELECTOR of the remote: the console's layers as keys above the faders (CH 1-16, CH 17-32, CH 33-40 when the console has
 *  them, ALL CH). A bank is a range of strip ids; ALL shows every strip. Kept pure so the unit tests check the ranges. */
object RemoteBanks {
    const val ALL = -1
    /** the keys for a console of [count] strips: (bank, label) */
    fun keys(count: Int): List<Pair<Int, String>> {
        val l = mutableListOf<Pair<Int, String>>()
        var b = 0
        while (b * 16 < count && b < 3) { val from = b * 16 + 1; val to = minOf(count, if (b == 2) 40 else (b + 1) * 16); l += b to "CH $from-$to"; b++ }
        l += ALL to "ALL CH"
        return l
    }
    fun inBank(bank: Int, strip: Int): Boolean = bank == ALL || (strip >= bank * 16 && strip < (if (bank == 2) 40 else (bank + 1) * 16))
}

/** 1.27 remote commands for the MUTE GROUPS held by the console's service and its MONITOR section: every value bounded here (the service
 *  checks them again). */
object RemoteOps {
    fun muteGroup(group: Int, mute: Boolean): Map<String, Any?> = mapOf("op" to "muteGroup", "group" to group.coerceIn(1, 8), "mute" to mute)
    fun assign(group: Int, strips: Collection<Int>, buses: Collection<Int>): Map<String, Any?> =
        mapOf("op" to "muteGroup", "group" to group.coerceIn(1, 8), "strips" to strips.filter { it >= 0 }.distinct().sorted(), "buses" to buses.filter { it in 0 until 16 }.distinct().sorted())
    fun talkback(on: Boolean, db: Double, dest: Int): Map<String, Any?> = mapOf("op" to "talkback", "on" to on, "db" to (if (db.isFinite()) db else -10.0).coerceIn(-60.0, 10.0), "dest" to dest.coerceIn(0, 16))
    fun monitorOut(out: Int): Map<String, Any?> = mapOf("op" to "monitor", "out" to out.coerceIn(0, 2))
    fun phones(db: Double): Map<String, Any?> = mapOf("op" to "monitor", "phonesDb" to (if (db.isFinite()) db else 0.0).coerceIn(-60.0, 10.0))
    val OUT_LABELS = listOf("A", "B", "A+B")
    fun destLabel(d: Int): String = if (d <= 0) "MAIN" else "B$d"
}
