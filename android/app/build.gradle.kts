plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.amfb.amixos"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.amfb.amixos"
        minSdk = 26
        targetSdk = 35
        // from the version (a.b.c.d -> a * 1 000 000 + b * 10 000 + c * 100 + d): every release is an upgrade of the one before
        versionCode = rootProject.file("../native/amixos/version.txt").readText().trim().split(".").map { it.toIntOrNull() ?: 0 }.let { v -> (v.getOrElse(0) { 0 }) * 1_000_000 + (v.getOrElse(1) { 0 }) * 10_000 + (v.getOrElse(2) { 0 }) * 100 + (v.getOrElse(3) { 0 }) }
        versionName = rootProject.file("../native/amixos/version.txt").readText().trim()
    }
    // RELEASE SIGNING: an APK signed with Android's public debug certificate is the main reason Play Protect warns about an app. A release
    // is signed with AMFB Audio's own key, read at build time from the environment (never stored in the repository):
    //   AMFX1_KEYSTORE (path to the .jks), AMFX1_KEYSTORE_PASSWORD, AMFX1_KEY_ALIAS, AMFX1_KEY_PASSWORD
    // Without them a release still builds, debug-signed, for local testing only: never hand that one out. See docs/android-play-protect.md.
    val releaseKeystore = System.getenv("AMFX1_KEYSTORE")?.takeIf { it.isNotBlank() && file(it).exists() }
    signingConfigs {
        if (releaseKeystore != null) create("release") {
            storeFile = file(releaseKeystore); storePassword = System.getenv("AMFX1_KEYSTORE_PASSWORD")
            keyAlias = System.getenv("AMFX1_KEY_ALIAS"); keyPassword = System.getenv("AMFX1_KEY_PASSWORD")
            enableV2Signing = true; enableV3Signing = true
        }
    }
    buildTypes {
        release { isMinifyEnabled = false; signingConfig = if (releaseKeystore != null) signingConfigs.getByName("release") else signingConfigs.getByName("debug") }
    }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { viewBinding = true }
    // SCREENSHOTS without a phone: Robolectric draws the real activities on the JVM (native graphics) into PNGs, see src/test
    testOptions { unitTests { isIncludeAndroidResources = true; all { it.systemProperty("robolectric.graphicsMode", "NATIVE"); it.systemProperty("amfx.shots", rootProject.file("../screenshots-android").absolutePath); it.systemProperty("amfx.daemon", System.getenv("AMFX_DAEMON") ?: "")
        // the Android runtime jar Robolectric needs: from android/.robolectric when it is there (git-ignored; fetched once by hand where Maven rate-limits), else downloaded
        val rd = rootProject.file(".robolectric"); if (rd.isDirectory) { it.systemProperty("robolectric.offline", "true"); it.systemProperty("robolectric.dependency.dir", rd.absolutePath) } } } }
}

dependencies {
    // the AmF X1 SDK (engine over JNI, AAudio loop, HAL report, brand profile): its own module so other apps can use it as an AAR
    implementation(project(":amix-sdk"))
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    // MIXDOWN: Media3 Transformer (Apache 2.0) decodes the picked files, mixes them with its AudioMixer (DefaultAudioMixer) and writes AAC
    implementation("androidx.media3:media3-transformer:1.4.1")
    implementation("androidx.media3:media3-common:1.4.1")
    // SCAN QR: Google's code scanner runs inside Play services and returns only the scanned text (no camera permission for this app)
    implementation("com.google.android.gms:play-services-code-scanner:16.1.0")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.robolectric:robolectric:4.14.1")
    testImplementation("androidx.test:core:1.6.1")
}
