package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** QUICK CHAINS of the phone's own mixer: the lists, and how a chain is written into a rack */
class LocalChainsTest {
    /** the engine's rack effects (fx::catalog() in engine/cpp/app/fx/fx.cpp): a chain may only name these */
    private val catalog: Set<String> by lazy {
        val f = listOf("../../engine/cpp/app/fx/fx.cpp", "../engine/cpp/app/fx/fx.cpp").map { java.io.File(it) }.first { it.exists() }
        val line = f.readLines().first { it.contains("catalog()") && it.contains("PLATE REVERB") }
        Regex("\\{\"([a-z0-9]+)\", \"").findAll(line).map { it.groupValues[1] }.toSet()
    }
    @Test fun everyChainNamesRealEffectsAndFitsTheRack() {
        assertTrue(catalog.size >= 51 && "automiceq" in catalog && "pingpong" in catalog && "peakremover" in catalog)
        assertEquals(LocalChains.chains.size, LocalChains.chains.map { it.id }.toSet().size)
        for (c in LocalChains.chains) { assertTrue(c.id, c.slots.size <= Engine.MAX_INSERTS); assertTrue(c.title.isNotBlank() && c.sub.isNotBlank())
            for (f in c.slots) { assertTrue("${c.id}: ${f.kind} is not a rack effect", f.kind in catalog); for (v in f.params.values) assertTrue(v in 0.0..1.0) } }
        assertTrue(LocalChains.byId("clear")!!.slots.isEmpty()); assertEquals(null, LocalChains.byId("nope"))
    }
    @Test fun aChainBecomesARackAndItsKnobs() {
        val k = LocalChains.byId("karaoke")!!
        assertEquals("rider|doubler|pingpong|hall", LocalChains.rackString(k, 4))
        assertEquals("rider|doubler", LocalChains.rackString(k, 2))                       // a shorter rack takes the start of the chain
        assertEquals("clean|rider|deesser|", LocalChains.rackString(LocalChains.byId("voice")!!, 4))
        assertEquals("|||", LocalChains.rackString(LocalChains.byId("clear")!!, 4))
        assertEquals("peakremover|rider||", LocalChains.rackString(LocalChains.byId("nopeaks")!!, 4))
        val p = LocalChains.paramString(k.slots[2])
        assertTrue(p.contains("left=0.1351") && p.contains("right=0.2095") && p.contains("spread=1.0000") && p.split(';').size == 6)
        assertEquals("", LocalChains.paramString(LocalChains.Fx("hall")))
        assertEquals("x=1.0000", LocalChains.paramString(LocalChains.Fx("a", mapOf("x" to 7.0))))   // out of range: held to 0 .. 1
    }
}
