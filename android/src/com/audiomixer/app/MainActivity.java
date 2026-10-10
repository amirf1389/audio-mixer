package com.audiomixer.app;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.webkit.PermissionRequest;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

/** Full-screen WebView that runs the bundled mixer page (assets/www/index.html). Nothing else is loaded into the WebView: other links open in the browser. */
public class MainActivity extends Activity {
    private static final String START_URL = "file:///android_asset/www/index.html";
    private static final int REQ_AUDIO = 7, REQ_ENGINE_AUDIO = 8;
    private WebView web;
    private PermissionRequest pendingMic;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);   // a mixer on stage must not go to sleep
        web = new WebView(this);
        web.setBackgroundColor(Color.BLACK);
        setContentView(web);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);                          // localStorage: scenes, settings, license key
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(true);                            // the bundled pages only
        s.setAllowContentAccess(false);
        s.setAllowFileAccessFromFileURLs(false);
        s.setAllowUniversalAccessFromFileURLs(false);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setUserAgentString(s.getUserAgentString() + " AudioMixerAndroid/" + version());
        web.setWebViewClient(new WebViewClient() {
            // (API 24+; no @Override because the build compiles against the API 23 android.jar from the distribution packages)
            public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
                Uri u = r.getUrl();
                if ("file".equals(u.getScheme()) && u.toString().startsWith("file:///android_asset/")) return false;
                if ("http".equals(u.getScheme()) && ("localhost".equals(u.getHost()) || "127.0.0.1".equals(u.getHost())) && u.getPort() == EngineService.port) return false;   // the engine's own page
                try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (Exception e) { /* no app can open it */ }
                return true;                                   // never navigate the mixer away from the bundled page
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onPermissionRequest(final PermissionRequest request) {
                boolean wantsMic = false;
                for (String r : request.getResources()) if (PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(r)) wantsMic = true;
                if (!wantsMic) { request.deny(); return; }       // no camera, no MIDI sysex ...
                if (Build.VERSION.SDK_INT < 23 || checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                    request.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE });
                } else {
                    pendingMic = request;
                    requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, REQ_AUDIO);
                }
            }
        });
        startEngine();
        loadMixer(state);
    }

    private int attempts;

    /**
     * The mixer page comes from the engine's own server (http://localhost:<port>/): a page from file:// has an opaque origin, where AudioWorklet modules
     * cannot be loaded. Waits briefly for the engine; without it (it could not start) the bundled file is the fallback.
     */
    private void loadMixer(final Bundle state) {
        final int port = EngineService.port;
        if (port > 0) {
            web.getSettings().setAllowFileAccess(false);        // nothing is read from files while the page comes from the engine
            if (state != null && web.restoreState(state) != null) return;
            web.loadUrl("http://localhost:" + port + "/index.html");
        } else if (++attempts < 25) {
            web.postDelayed(new Runnable() { public void run() { if (web != null) loadMixer(state); } }, 120);
        } else {
            web.getSettings().setAllowFileAccess(true);
            if (state != null) web.restoreState(state); else web.loadUrl(START_URL);
        }
    }

    /** The background engine (EngineService): audio interfaces for the page and audio that keeps running when the app is in the background. */
    private void startEngine() {
        if (Build.VERSION.SDK_INT >= 23 && checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, REQ_ENGINE_AUDIO);
        Intent i = new Intent(this, EngineService.class);
        try {
            if (Build.VERSION.SDK_INT >= 26) Context.class.getMethod("startForegroundService", Intent.class).invoke(this, i);   // API 26, reached by reflection (API 23 android.jar)
            else startService(i);
        } catch (Exception e) { /* the page still works without the engine */ }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        if (code != REQ_AUDIO || pendingMic == null) return;
        PermissionRequest r = pendingMic; pendingMic = null;
        if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) r.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE });
        else r.deny();
    }

    private String version() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; } catch (Exception e) { return "0"; }
    }

    @Override
    public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) web.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override protected void onSaveInstanceState(Bundle out) { super.onSaveInstanceState(out); web.saveState(out); }
    @Override protected void onPause() { super.onPause(); web.onPause(); }
    @Override protected void onResume() { super.onResume(); web.onResume(); }
    @Override public void onBackPressed() { if (web.canGoBack()) web.goBack(); else moveTaskToBack(true); }
    @Override protected void onDestroy() { if (web != null) { web.destroy(); web = null; } super.onDestroy(); }
}
