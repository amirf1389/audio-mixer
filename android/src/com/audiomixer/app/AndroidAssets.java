package com.audiomixer.app;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

/** assets/www of the APK for MiniBridge. */
final class AndroidAssets implements Assets {
    private final Context ctx;

    AndroidAssets(Context ctx) { this.ctx = ctx.getApplicationContext(); }

    public byte[] read(String path) throws IOException {
        InputStream in;
        try { in = ctx.getAssets().open("www" + path); } catch (IOException e) { return null; }      // not there (or a folder)
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream(Math.max(8192, in.available()));
            byte[] buf = new byte[16384]; int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        } finally { in.close(); }
    }
}
