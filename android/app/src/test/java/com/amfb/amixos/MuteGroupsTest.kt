package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Test

class MuteGroupsTest {
    @Test fun parseAndFormat() {
        assertEquals(setOf(2, 3, 7), MuteGroups.parse("7, 3,2,x,-1,3")); assertEquals("2,3,7", MuteGroups.format(setOf(7, 2, 3))); assertEquals(emptySet<Int>(), MuteGroups.parse(null))
        assertEquals("CH 3, 4, 8", MuteGroups.label(setOf(7, 2, 3))); assertEquals("—", MuteGroups.label(emptySet()))
    }
    @Test fun plan() {
        val g = listOf(setOf(0, 1, 2), setOf(2, 3), emptySet())
        assertEquals(listOf(0 to true, 1 to true, 2 to true), MuteGroups.plan(g, listOf(false, false, false), 0, true))
        // group 2 is still active and holds strip 2: switching group 1 off leaves strip 2 muted
        assertEquals(listOf(0 to false, 1 to false), MuteGroups.plan(g, listOf(true, true, false), 0, false))
        assertEquals(listOf(0 to false, 1 to false, 2 to false), MuteGroups.plan(g, listOf(true, false, false), 0, false))
        assertEquals(emptyList<Pair<Int, Boolean>>(), MuteGroups.plan(g, listOf(false, false, false), 2, true))
    }
}
