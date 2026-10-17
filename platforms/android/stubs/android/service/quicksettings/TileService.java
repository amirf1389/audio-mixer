package android.service.quicksettings;

import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.IBinder;

/** Compile-time stub of the framework class (API 24). NOT packaged: the real class of the phone is used at run time. Signatures as in the Android SDK. */
public class TileService extends Service {
    public static final String ACTION_QS_TILE = "android.service.quicksettings.action.QS_TILE";
    public void onTileAdded() {}
    public void onTileRemoved() {}
    public void onStartListening() {}
    public void onStopListening() {}
    public void onClick() {}
    public final Tile getQsTile() { return null; }
    public final boolean isLocked() { return false; }
    public final boolean isSecure() { return false; }
    public final void unlockAndRun(Runnable runnable) {}
    public final void startActivityAndCollapse(Intent intent) {}
    public static final void requestListeningState(Context context, ComponentName component) {}
    @Override public IBinder onBind(Intent intent) { return null; }
}
