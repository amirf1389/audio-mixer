package com.amfb.amfx1

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import com.amfb.amfx1.ui.AppRoot
import com.amfb.amfx1.ui.theme.AmFX1Theme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            // Light by default, like the desktop app. The phone's dark setting is not followed unless the line below is changed.
            AmFX1Theme(dark = false) {
                AppRoot()
            }
        }
    }
}
