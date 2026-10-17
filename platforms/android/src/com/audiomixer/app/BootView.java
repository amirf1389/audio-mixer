package com.audiomixer.app;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.view.View;

/** Start-up screen drawn over the page while the engine starts and the page loads: power LED (red, amber, green), the logo spelling in, the OS boot lines
 *  ticking to OK and a progress bar, like the console's power-on sequence (the page plays its own right after). */
final class BootView extends View {
    private static final String[] LINES = { "TITAN OS", "DSP CORE", "AUDIO ENGINE", "I/O", "FADERS", "CONSOLE" };
    private static final String LOGO = "AUDIO MIXER";
    private final long t0 = System.currentTimeMillis();
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private boolean running = true;

    BootView(Context c) {
        super(c);
        setBackgroundColor(Color.BLACK);
        setClickable(true);                                  // swallows touches while it is shown
        p.setTypeface(Typeface.MONOSPACE);
    }

    void stop() { running = false; }

    @Override
    protected void onDraw(Canvas c) {
        float e = (System.currentTimeMillis() - t0) / 1000f, d = getResources().getDisplayMetrics().density;
        int w = getWidth(), h = getHeight(), cx = w / 2;
        float y = h / 2f - 120 * d;
        // power LED
        int led = e < 0.5f ? Color.rgb(239, 68, 68) : e < 1.0f ? Color.rgb(245, 158, 11) : Color.rgb(34, 197, 94);
        p.setStyle(Paint.Style.FILL);
        p.setColor(Color.argb(60, Color.red(led), Color.green(led), Color.blue(led)));
        c.drawCircle(cx, y, 13 * d, p);
        p.setColor(led);
        c.drawCircle(cx, y, 7 * d, p);
        // logo, letter by letter
        int shown = Math.max(0, Math.min(LOGO.length(), (int) ((e - 0.5f) / 0.06f)));
        p.setTextSize(26 * d); p.setFakeBoldText(true); p.setColor(Color.WHITE);
        float step = 19 * d, x0 = cx - LOGO.length() * step / 2f;
        for (int i = 0; i < shown; i++) c.drawText(String.valueOf(LOGO.charAt(i)), x0 + i * step, y + 50 * d, p);
        p.setFakeBoldText(false); p.setTextSize(9 * d); p.setColor(Color.rgb(100, 116, 139));
        c.drawText("T I T A N   S T A G E", cx - 52 * d, y + 70 * d, p);
        // boot lines
        p.setTextSize(11 * d);
        for (int i = 0; i < LINES.length; i++) {
            if (e < 0.9f + i * 0.22f) break;
            float ly = y + 98 * d + i * 16 * d;
            p.setColor(Color.rgb(148, 163, 184)); c.drawText(LINES[i], cx - 110 * d, ly, p);
            p.setColor(Color.rgb(34, 197, 94)); c.drawText("OK", cx + 90 * d, ly, p);
        }
        // progress bar
        float bar = Math.min(1f, e / 2.2f);
        float by = y + 98 * d + LINES.length * 16 * d + 8 * d;
        p.setColor(Color.rgb(15, 23, 42)); c.drawRect(cx - 110 * d, by, cx + 110 * d, by + 3 * d, p);
        p.setColor(Color.rgb(34, 197, 94)); c.drawRect(cx - 110 * d, by, cx - 110 * d + 220 * d * bar, by + 3 * d, p);
        if (running) postInvalidateOnAnimation();
    }
}
