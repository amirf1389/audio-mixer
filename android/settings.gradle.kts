// A mix OS for Android: a Kotlin app over the engine's C SDK (JNI). Open this folder in Android Studio (Hedgehog or newer) or run
// ./gradlew assembleRelease with ANDROID_HOME set; the NDK (r26+) and CMake 3.22 come from the SDK manager.
pluginManagement { repositories { google(); mavenCentral(); gradlePluginPortal() } }
dependencyResolutionManagement { repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS); repositories { google(); mavenCentral() } }
rootProject.name = "amixos"
include(":app", ":amix-sdk", ":mixer")
