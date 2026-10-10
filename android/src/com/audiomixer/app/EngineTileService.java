package com.audiomixer.app;

import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

/**
 * Quick Settings tile "Mixer engine": one tap starts or stops the background audio engine (EngineService) from the notification shade, without opening the app.
 * The tile is lit while the engine runs. If Android refuses to start the foreground service from the tile, the app is opened instead (it starts the engine itself).
 */
public class EngineTileService extends TileService {
    @Override public void onTileAdded() { refresh(); }

    @Override public void onStartListening() { refresh(); }

    @Override public void onClick() {
        if (EngineState.running) stopEngine(); else startEngine();
        refresh();
    }

    private void startEngine() {
        Intent i = new Intent(this, EngineService.class);
        try {
            if (Build.VERSION.SDK_INT >= 26) Context.class.getMethod("startForegroundService", Intent.class).invoke(this, i);   // API 26, reached by reflection (API 23 android.jar)
            else startService(i);
            EngineState.running = true;
        } catch (Throwable e) {
            try { startActivityAndCollapse(new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); } catch (Throwable ignored) { /* nothing more to try */ }
        }
    }

    private void stopEngine() {
        try { startService(new Intent(this, EngineService.class).setAction(EngineService.ACTION_STOP)); } catch (Throwable e) { /* already stopped */ }
        EngineState.running = false;
    }

    /** Label, state and icon from what the engine is doing now. */
    private void refresh() {
        Tile t = getQsTile();
        if (t == null) return;
        boolean on = EngineState.running;
        t.setState(on ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        t.setLabel("Mixer engine");
        if (Build.VERSION.SDK_INT >= 29) t.setSubtitle(on ? (EngineService.port > 0 ? "Running :" + EngineService.port : "Starting") : "Stopped");
        t.setIcon(glyph());
        t.updateTile();
    }

    /** Three faders in white on transparent (a tile icon is tinted by the system): drawn here, so the app needs no extra resource. */
    static Icon glyph() {
        Bitmap b = Bitmap.createBitmap(64, 64, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(b);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(0xFFFFFFFF);
        float[] x = { 16f, 32f, 48f }, cap = { 40f, 24f, 34f };
        for (int k = 0; k < 3; k++) {
            p.setStrokeWidth(4f);
            c.drawLine(x[k], 10f, x[k], 54f, p);                                        // the fader track
            c.drawRoundRect(x[k] - 9f, cap[k] - 4f, x[k] + 9f, cap[k] + 4f, 3f, 3f, p);   // the cap
        }
        return Icon.createWithBitmap(b);
    }
}
