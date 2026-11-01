package com.amfb.amfx1.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// EDIT HERE: every colour of the app. Light is the default, like the desktop app.
object AmfColors {
    val Background = Color(0xFFF1F4F8)
    val Card = Color(0xFFFFFFFF)
    val Ink = Color(0xFF0F172A)
    val Muted = Color(0xFF475569)
    val Line = Color(0xFFD7DEE8)
    val Accent = Color(0xFFB45309)     // amber for light mode
    val AccentBright = Color(0xFFF59E0B)
}

private val LightScheme = lightColorScheme(
    primary = AmfColors.Accent,
    onPrimary = Color.White,
    background = AmfColors.Background,
    onBackground = AmfColors.Ink,
    surface = AmfColors.Card,
    onSurface = AmfColors.Ink,
    surfaceVariant = AmfColors.Background,
    onSurfaceVariant = AmfColors.Muted,
    outline = AmfColors.Line,
)

private val DarkScheme = darkColorScheme(
    primary = Color(0xFFFBBF24),
    onPrimary = Color(0xFF111827),
    background = Color(0xFF0A0D13),
    onBackground = Color(0xFFF1F5F9),
    surface = Color(0xFF0F141C),
    onSurface = Color(0xFFF1F5F9),
    surfaceVariant = Color(0xFF0A0D13),
    onSurfaceVariant = Color(0xFFCBD5E1),
    outline = Color(0x24FFFFFF),
)

@Composable
fun AmFX1Theme(dark: Boolean = false, content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (dark) DarkScheme else LightScheme, content = content)
}
