package com.amfb.amfx1.ui

import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import com.amfb.amfx1.ui.screens.HomeScreen
import com.amfb.amfx1.ui.screens.WebPageScreen

// The app has two kinds of screen: the native home screen, and the mixer pages (shown in a web view of the bridge).
// To add a screen: add a state value here, and a branch in the when().
@Composable
fun AppRoot() {
    var openTab by rememberSaveable { mutableStateOf<String?>(null) }

    if (openTab == null) {
        HomeScreen(onChoose = { tabId -> openTab = tabId })
    } else {
        BackHandler { openTab = null }
        WebPageScreen(tabId = openTab!!, onBack = { openTab = null })
    }
}
