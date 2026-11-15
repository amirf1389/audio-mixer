// AmF X1 SDK for Android (AAR): the engine's C SDK over JNI, the native AAudio loop with device selection, the audio HAL report and the
// brand profile. A library other apps can use: it asks for no permission of its own (the host app adds RECORD_AUDIO if it opens the
// microphone) and reads only Android's public APIs. Built with: gradle :amix-sdk:assembleRelease -> amix-sdk/build/outputs/aar/
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.amfb.amixos.sdk"
    compileSdk = 35
    defaultConfig {
        minSdk = 26
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild { cmake { cppFlags += listOf("-std=c++20", "-fexceptions", "-frtti"); arguments += listOf("-DANDROID_STL=c++_shared") } }
        consumerProguardFiles("consumer-rules.pro")
        aarMetadata { minCompileSdk = 26 }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" } }
    buildTypes { release { isMinifyEnabled = false } }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
}
