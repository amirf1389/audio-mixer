package com.audiomixer.app;

import android.service.notification.NotificationListenerService;

/** Does nothing itself. Android lets an app read the other apps' media sessions (Spotify, YouTube Music, ...) only when it has a notification listener the user switched on:
 *  Settings > Notifications > Device & app notifications > Audio Mixer. AndroidNowPlaying then asks MediaSessionManager with this component. No notification is read. */
public final class NowPlayingListener extends NotificationListenerService {}
