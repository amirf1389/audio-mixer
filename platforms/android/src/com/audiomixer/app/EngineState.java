package com.audiomixer.app;

import android.content.ComponentName;
import android.content.Context;
import android.service.quicksettings.TileService;

/** Whether the background engine service is running, and a way to tell the Quick Settings tile when that changes. Set by EngineService (Java and Kotlin twin). */
public final class EngineState {
    private EngineState() {}

    /** True from EngineService.onStartCommand until onDestroy. */
    public static volatile boolean running;

    /** Asks Android to let the tile refresh itself now (API 24). Harmless when the tile is not added or the call is not available. */
    public static void changed(Context ctx) {
        try { TileService.requestListeningState(ctx, new ComponentName(ctx, EngineTileService.class)); } catch (Throwable e) { /* no tile / old system */ }
    }
}
