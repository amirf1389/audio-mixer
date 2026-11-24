package com.amfb.amixos

import androidx.test.core.app.ApplicationProvider
import android.content.Context
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** MANAGE LICENCE ONLINE: the licence site link built from the server setting, the machine code and the plan. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class LicenceLinkTest {
    @Test fun buildsTheSiteLinkWithMachineAndEdition() {
        assertEquals("https://licence.example.org/licence/?machine=A1B2-C3D4-E5F6-0718-293A&edition=pro", LicenceLink.url("https://licence.example.org/", " a1b2-c3d4-e5f6-0718-293a ", "PRO"))
        assertEquals("https://licence.example.org/portal/licence/?machine=A1B2-C3D4-E5F6-0718-293A", LicenceLink.url("https://licence.example.org/portal", "A1B2-C3D4-E5F6-0718-293A", ""))
        assertEquals("https://licence.example.org/licence/", LicenceLink.url("https://licence.example.org", null, null))
        assertEquals("http://127.0.0.1:8787/licence/?edition=basic", LicenceLink.url("http://127.0.0.1:8787", "short", "basic"))
    }
    @Test fun nothingElseGetsIntoTheLink() {
        assertEquals("https://licence.example.org/licence/?machine=A1B2XY1C3D4", LicenceLink.url("https://licence.example.org", "A1B2<x>&y=1#C3D4", "pro&admin=1"))
        assertNull(LicenceLink.url("http://licence.example.org", "A1B2-C3D4-E5F6-0718-293A", "pro"))
        assertNull(LicenceLink.url("https://licence.example.org/?x=1", "A1B2-C3D4-E5F6-0718-293A", "pro"))
        assertNull(LicenceLink.url("https://user@licence.example.org", "A1B2-C3D4-E5F6-0718-293A", "pro"))
        assertNull(LicenceLink.url("javascript:alert(1)", "A1B2-C3D4-E5F6-0718-293A", "pro"))
        assertNull(LicenceLink.url("", "A1B2-C3D4-E5F6-0718-293A", "pro"))
    }
    @Test fun usesTheDefaultPortalAndTheEngineLicence() {
        val c = ApplicationProvider.getApplicationContext<Context>()
        Prefs.local(c).edit().remove(LicenceLink.KEY).commit()
        assertEquals("https://licence.amirfakhribadri.my/licence/?machine=0A1B-2C3D-4E5F-6071-8293&edition=master",
            LicenceLink.forLicence(c, """{"state":"active","plan":"master","machine":"0A1B-2C3D-4E5F-6071-8293"}"""))
        Prefs.local(c).edit().putString(LicenceLink.KEY, "https://portal.example.net").commit()
        assertEquals("https://portal.example.net/licence/", LicenceLink.forLicence(c, "not json"))
        Prefs.local(c).edit().remove(LicenceLink.KEY).commit()
    }
}
