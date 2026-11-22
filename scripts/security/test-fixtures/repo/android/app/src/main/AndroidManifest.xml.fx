<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-permission android:name="android.permission.INTERNET" />
    <application android:label="x" android:debuggable="true" android:allowBackup="true" android:usesCleartextTraffic="true">
        <activity android:name=".Main" android:exported="true">
            <intent-filter><action android:name="android.intent.action.MAIN" /><category android:name="android.intent.category.LAUNCHER" /></intent-filter>
        </activity>
        <service android:name=".Sync" android:exported="true" />
        <provider android:name=".Files" android:authorities="x.files" android:exported="true" />
    </application>
</manifest>
