# AmF X1 · Android UI pack (Jetpack Compose)

A ready-to-open Android Studio project with the native Compose screens. Use it as the place to edit the Android look, then copy the finished screens into the app when they're ready.

**Status: written but NOT compiled.** The pack was written without an Android SDK or Google's Maven access, so build it in Android Studio and report any errors.

## Open it
1. Android Studio (Koala or newer) → **File → Open** → select this `compose-ui` folder.
2. Let Gradle sync. Run **app** on a phone or emulator (Android 7.0 / API 24 or newer).

## What to edit (one file each)
| You want to change | Edit |
|---|---|
| Product name, company, version label, home-screen choices | `app/src/main/java/com/amfb/amfx1/config/AppConfig.kt` |
| Every colour (light and dark) | `app/src/main/java/com/amfb/amfx1/ui/theme/Theme.kt` |
| Size and look of the big home-screen cards | `app/src/main/java/com/amfb/amfx1/ui/components/HomeTile.kt` |
| The home screen layout | `app/src/main/java/com/amfb/amfx1/ui/screens/HomeScreen.kt` |
| Which screen opens (the navigation) | `app/src/main/java/com/amfb/amfx1/ui/AppRoot.kt` |
| The mixer page view and its Back button | `app/src/main/java/com/amfb/amfx1/ui/screens/WebPageScreen.kt` |
| App name, permissions | `app/src/main/AndroidManifest.xml`, `app/src/main/res/values/strings.xml` |
| Version numbers and library versions | `app/build.gradle.kts` (and `build.gradle.kts` for the Kotlin and Android plugins) |

## Things to check first
- **The mixer address.** `AppConfig.BRIDGE_URL` is `http://127.0.0.1:8765/`, the desktop bridge's port. On the phone, the engine runs inside the Android app (the Java files in `platforms/android/src`), so the address must point to that engine's web server. Confirm the port before relying on the mixer pages.
- **Tab names.** The web view calls `window.switchTab('<id>')` on the page. The ids in `AppConfig.HOME_TILES` must match the page's `window.navTabs` ids in `index.html`.
- **Light by default.** `MainActivity.kt` sets `dark = false`. Change it to follow the phone's setting if you want.

## Add a new home-screen choice
Add one line to `AppConfig.HOME_TILES` with a title, a short plain description and the tab id. Nothing else needs to change.

## Gradle wrapper
`gradlew`, `gradlew.bat` and `gradle/wrapper/` are copied from `platforms/android/studio`, so the wrapper version matches the existing project.
