// AmF X1 MIXER: the phone's own mixer as a separate app (package com.amfb.amixos.mixer, installs beside AmF X1). It is built from the
// same sources as :app (nothing is copied); only the manifest differs: it starts in the mixer, has no console remote, and its bar
// is MIXER · PLAYERS · TOOLS · SETTINGS (see Nav.standalone).
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// release signing (see android { } below): the keystore from the environment, or an explicit opt-in to a debug-signed TEST build
val releaseKeystorePath: String? = System.getenv("AMFX1_KEYSTORE")?.takeIf { it.isNotBlank() && file(it).exists() }
val allowDebugSigning = (findProperty("allowDebugSigning") as String?) == "true"
tasks.matching { it.name == "packageRelease" }.configureEach {
    doFirst {
        if (releaseKeystorePath == null && !allowDebugSigning) throw GradleException("release build refused: no release keystore (set AMFX1_KEYSTORE, AMFX1_KEYSTORE_PASSWORD, AMFX1_KEY_ALIAS, AMFX1_KEY_PASSWORD). For a local test APK only, add -PallowDebugSigning=true (debug-signed, named *-UNSIGNED-TEST.apk; never publish it).")
    }
}

android {
    namespace = "com.amfb.amixos"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.amfb.amixos.mixer"
        minSdk = 26
        targetSdk = 35
        val build = rootProject.file("build-number.txt").readText().trim().toIntOrNull() ?: 0
        versionCode = rootProject.file("../native/amixos/version.txt").readText().trim().split(".").map { it.toIntOrNull() ?: 0 }.let { v -> (v.getOrElse(0) { 0 }) * 1_000_000 + (v.getOrElse(1) { 0 }) * 10_000 + (v.getOrElse(2) { 0 }) * 100 + (v.getOrElse(3) { 0 }) } * 100 + build
        versionName = rootProject.file("../native/amixos/version.txt").readText().trim() + " (build " + build + ")"
    }
    // RELEASE SIGNING: the same rule as :app (the key comes from the environment, never from the repository)
    val releaseKeystore = releaseKeystorePath
    signingConfigs {
        if (releaseKeystore != null) create("release") {
            storeFile = file(releaseKeystore); storePassword = System.getenv("AMFX1_KEYSTORE_PASSWORD")
            keyAlias = System.getenv("AMFX1_KEY_ALIAS"); keyPassword = System.getenv("AMFX1_KEY_PASSWORD")
            enableV2Signing = true; enableV3Signing = true
        }
    }
    // No release keystore: the release build FAILS (packageRelease below), unless -PallowDebugSigning=true asks for a local TEST APK, which is
    // then signed with the public debug key and named *-UNSIGNED-TEST.apk. Never hand such an APK out; store / public APKs need your own key.
    val releaseSigning = when {
        releaseKeystore != null -> signingConfigs.getByName("release")
        allowDebugSigning -> signingConfigs.getByName("debug")
        else -> null
    }
    if (releaseKeystore == null && allowDebugSigning) applicationVariants.all {
        if (buildType.name == "release") outputs.all { (this as com.android.build.gradle.internal.api.BaseVariantOutputImpl).outputFileName = "${project.name}-release-UNSIGNED-TEST.apk" }
    }
    buildTypes {
        release { isMinifyEnabled = false; signingConfig = releaseSigning }
    }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { viewBinding = true }
    sourceSets["main"].java.srcDir("../app/src/main/java")
    sourceSets["main"].res.srcDir("../app/src/main/res")
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/feed"))
}

dependencies {
    implementation(project(":amix-sdk"))
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.webkit:webkit:1.11.0")   // WebView mute for the PLAYERS channel
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.media3:media3-transformer:1.4.1")
    implementation("androidx.media3:media3-common:1.4.1")
    implementation("com.google.android.gms:play-services-code-scanner:16.1.0")
}

val copyReleaseFeed = tasks.register<Copy>("copyReleaseFeed") { from(rootProject.file("../releases/features.json")); into(layout.buildDirectory.dir("generated/feed")) }
tasks.named("preBuild") { dependsOn(copyReleaseFeed) }
