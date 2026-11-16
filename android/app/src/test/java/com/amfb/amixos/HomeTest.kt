package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** HOME's memory: RECENT pages, pinned tiles first, WHAT'S NEW from the release feed */
class HomeTest {
    @Test fun recentIsNewestFirstWithoutRepeats() {
        var l = emptyList<String>()
        for (id in listOf("A", "B", "C", "B", "D", "E")) l = Home.addRecent(l, id)
        assertEquals(listOf("E", "D", "B", "C"), l)
    }
    @Test fun pinnedTilesComeFirstInTheirOrder() {
        assertEquals(listOf("C", "E", "A", "B", "D"), Home.pinnedFirst(listOf("A", "B", "C", "D", "E"), { it }, setOf("E", "C")))
        assertEquals(listOf("A", "B"), Home.pinnedFirst(listOf("A", "B"), { it }, emptySet()))
    }
    @Test fun newsOfThisVersionFromTheFeed() {
        val feed = """{"features":[{"version":"1.24.0.0","title":"Release 1.24.0.0","items":["one","two"]},{"version":"1.23.0.1","title":"old","items":["x"]}]}"""
        val n = Home.newsFor(feed, "1.24.0.0 (build 2)")!!
        assertEquals("Release 1.24.0.0", n.title); assertEquals(listOf("one", "two"), n.items)
        assertNull(Home.newsFor(feed, "9.9.9.9")); assertNull(Home.newsFor("not json", "1.24.0.0"))
    }
    @Test fun theRealFeedHasThisVersion() {   // the feed bundled into the APK names the version being built
        val v = java.io.File("../../native/amixos/version.txt").takeIf { it.exists() }?.readText()?.trim() ?: java.io.File("../native/amixos/version.txt").readText().trim()
        val feed = (java.io.File("../../releases/features.json").takeIf { it.exists() } ?: java.io.File("../releases/features.json")).readText()
        org.junit.Assert.assertNotNull("releases/features.json has no entry for $v", Home.newsFor(feed, v))
    }
}
