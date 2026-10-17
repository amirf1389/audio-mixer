package com.audiomixer.app;

import java.io.IOException;

/** The bundled mixer page (assets/www): MiniBridge serves it on http://localhost:<port>/, so the page has a real origin (AudioWorklet modules, localStorage and the microphone need one; file:// pages have an opaque origin). No android.* classes. */
interface Assets {
    /** Bytes of a file below www/ (path like "/index.html"), or null when there is none. */
    byte[] read(String path) throws IOException;
}
