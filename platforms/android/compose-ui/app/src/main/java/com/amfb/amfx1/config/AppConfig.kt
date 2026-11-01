package com.amfb.amfx1.config

// EDIT HERE: the product name, the version shown on the home screen, and the pages.
object AppConfig {
    const val PRODUCT_NAME = "AmF X1"
    const val COMPANY = "AMFB Audio"
    const val VERSION_LABEL = "Version 0.1 Alpha"

    // The mixer page the app opens. On the phone the engine runs inside the app, so this is the local bridge address.
    const val BRIDGE_URL = "http://127.0.0.1:8765/"

    // Home-screen choices: title, plain-language description, and the bridge tab id (see index.html, window.navTabs).
    val HOME_TILES = listOf(
        HomeChoice("Mix my sound", "Set the volume and balance for each microphone, instrument or source.", "mixer"),
        HomeChoice("Use my sound card", "Choose the device that plays your sound, and check that it works.", "asio"),
        HomeChoice("Tone and effects", "Make voices and music clearer, warmer or brighter.", "dsp"),
        HomeChoice("Check the levels", "See how loud each sound is, so nothing distorts.", "meter"),
        HomeChoice("Connect devices", "Plug in microphones, speakers and other sources.", "io"),
        HomeChoice("Settings and help", "Your account, license, updates and the device.", "sys"),
    )
}

data class HomeChoice(val title: String, val description: String, val tabId: String)
