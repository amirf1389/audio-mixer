package com.audiomixer.app;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.provider.Settings;

import java.util.ArrayList;
import java.util.List;

/** The app's own facts for GET /api/app, and the switch of the optional "now playing" access (the notification-listener component is shipped switched off). */
final class AndroidAppInfo {
    private final Context ctx;

    AndroidAppInfo(Context ctx) { this.ctx = ctx.getApplicationContext(); }

    private ComponentName listener() { return new ComponentName(ctx, NowPlayingListener.class); }

    private boolean listenerEnabled() {
        int s = ctx.getPackageManager().getComponentEnabledSetting(listener());
        return s == PackageManager.COMPONENT_ENABLED_STATE_ENABLED;       // the manifest says disabled: DEFAULT means off
    }

    private boolean listenerAccess() {
        try { String l = Settings.Secure.getString(ctx.getContentResolver(), "enabled_notification_listeners"); return l != null && l.contains(ctx.getPackageName() + "/"); } catch (Exception e) { return false; }
    }

    String json() {
        try {
            PackageManager pm = ctx.getPackageManager();
            PackageInfo pi = pm.getPackageInfo(ctx.getPackageName(), PackageManager.GET_SIGNATURES | PackageManager.GET_PERMISSIONS);
            ApplicationInfo ai = ctx.getApplicationInfo();
            List<AppInfo.Perm> perms = new ArrayList<AppInfo.Perm>();
            if (pi.requestedPermissions != null) for (int i = 0; i < pi.requestedPermissions.length; i++) {
                boolean granted = pi.requestedPermissionsFlags != null && (pi.requestedPermissionsFlags[i] & PackageInfo.REQUESTED_PERMISSION_GRANTED) != 0;
                perms.add(new AppInfo.Perm(pi.requestedPermissions[i], granted));
            }
            String cert = pi.signatures != null && pi.signatures.length > 0 ? AppInfo.fingerprint(pi.signatures[0].toByteArray()) : "";
            String installer = null; try { installer = pm.getInstallerPackageName(ctx.getPackageName()); } catch (Exception e) { /* unknown */ }
            return AppInfo.toJson(ctx.getPackageName(), pi.versionName == null ? "" : pi.versionName, pi.versionCode, ai.targetSdkVersion, cert, installer, (ai.flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0,
                perms, listenerEnabled(), listenerAccess());
        } catch (Exception e) { return "{\"ok\":false,\"error\":" + Json.str(String.valueOf(e.getMessage())) + "}"; }
    }

    /** Switches the notification-listener component on (and opens the system screen where the person grants the access) or off again. */
    String setNowPlaying(boolean on) {
        try {
            ctx.getPackageManager().setComponentEnabledSetting(listener(), on ? PackageManager.COMPONENT_ENABLED_STATE_ENABLED : PackageManager.COMPONENT_ENABLED_STATE_DEFAULT, PackageManager.DONT_KILL_APP);
            boolean opened = false;
            if (on) { try { ctx.startActivity(new Intent("android.settings.ACTION_NOTIFICATION_LISTENER_SETTINGS").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); opened = true; } catch (Exception e) { /* no such screen */ } }
            return "{\"ok\":true,\"enabled\":" + on + ",\"settingsOpened\":" + opened + "}";
        } catch (Exception e) { return "{\"ok\":false,\"error\":" + Json.str(String.valueOf(e.getMessage())) + "}"; }
    }
}
