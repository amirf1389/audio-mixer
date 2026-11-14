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
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild { cmake { cppFlags += listOf("-std=c++20", "-fexceptions", "-frtti"); arguments += listOf("-DANDROID_STL=c++_shared") } }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" } }
    buildTypes {
        release { isMinifyEnabled = false; signingConfig = signingConfigs.getByName("debug") }   // replace with your own keystore for the Play Store / sideload builds
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
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.robolectric:robolectric:4.14.1")
    testImplementation("androidx.test:core:1.6.1")
}
