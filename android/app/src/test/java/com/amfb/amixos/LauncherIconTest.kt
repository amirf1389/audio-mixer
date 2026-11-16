package com.amfb.amixos

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Region
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File
import java.io.FileOutputStream

/** draws the launcher icon the way a phone's launcher does (background + foreground, masked to a squircle and a circle) for the
 *  screenshots, and so the build proves the vector drawables load */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [34])
class LauncherIconTest {
    @Test fun drawIcon() {
        val ctx = org.robolectric.RuntimeEnvironment.getApplication(); val out = File(System.getProperty("amfx.shots") ?: "screenshots-android").apply { mkdirs() }
        val bg = ctx.getDrawable(R.drawable.ic_launcher_background)!!; val fg = ctx.getDrawable(R.drawable.ic_launcher_foreground)!!
        val size = 432; val sheet = Bitmap.createBitmap(size * 2 + 60, size + 40, Bitmap.Config.ARGB_8888); val c = Canvas(sheet); c.drawColor(0xFF0B0F15.toInt())
        for ((i, round) in listOf(false, true).withIndex()) {
            val x = 20f + i * (size + 20); val clip = Path().apply { if (round) addCircle(x + size / 2f, 20f + size / 2f, size / 2f * 66f / 72f, Path.Direction.CW) else addRoundRect(RectF(x + size * 3f / 108, 20f + size * 3f / 108, x + size * 105f / 108, 20f + size * 105f / 108), size * 0.22f, size * 0.22f, Path.Direction.CW) }
            c.save(); c.clipPath(clip)
            bg.setBounds(x.toInt(), 20, x.toInt() + size, 20 + size); bg.draw(c); fg.setBounds(x.toInt(), 20, x.toInt() + size, 20 + size); fg.draw(c)
            c.restore()
        }
        FileOutputStream(File(out, "launcher-icon.png")).use { sheet.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }
}
