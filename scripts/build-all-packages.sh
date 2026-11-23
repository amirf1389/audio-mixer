#!/bin/sh
# Builds every package of AmF X1 that this Linux machine can build, into releases/:
#   releases/windows/AmF X1.exe (+ 32-bit), AudioMixerASIO64/32.dll        node scripts/build-native.js      (mingw-w64)
#   releases/windows/AmF-X1-<v>-windows-x64.zip                            scripts/build-console-windows.sh  (mingw-w64 + the Qt win64_mingw kit)
#   releases/windows/AmF-X1-<v>-setup.exe                                  makensis install/windows/amfx1.nsi (apt install nsis)
#   releases/android/AmF-X1-<v>-b<build>-android.apk                                gradle assembleRelease in android/ (ANDROID_HOME with platform 35, build-tools 35, NDK 26, CMake 3.22)
#   releases/linux/amixos-<v>-linux-<arch>.tar.gz                          scripts/package-linux.sh
# Nothing here is signed: Windows files with scripts/sign-windows.js, the APK with your keystore (android/app/build.gradle.kts signingConfigs).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; VER="$(tr -d ' \n' < "$ROOT/native/amixos/version.txt")"; cd "$ROOT"
mkdir -p releases/windows releases/android releases/linux dist/windows
node scripts/build-native.js --out releases/windows; rm -f releases/windows/AudioMixer-*-native-linux-*.tar.gz
sh scripts/build-console-windows.sh "${QT_WIN_KIT:-}" dist/windows; cp "dist/windows/AmF-X1-$VER-windows-x64.zip" "dist/windows/AmF-X1-$VER-windows-x64.zip.sha256" releases/windows/
command -v makensis >/dev/null && makensis -V2 -DVERSION="$VER" -DSRC="$ROOT/dist/windows/AmF-X1-$VER-windows-x64" -DOUT="$ROOT/releases/windows/AmF-X1-$VER-setup.exe" install/windows/amfx1.nsi
(cd releases/windows && sha256sum *.exe *.dll *.zip > SHA256SUMS.txt)
if [ -n "$ANDROID_HOME" ] && command -v gradle >/dev/null; then T=""; P=""; [ -n "${AMFX1_KEYSTORE:-}" ] || { T="-UNSIGNED-TEST"; P="-PallowDebugSigning=true"; }; (cd android && gradle --no-daemon -q assembleRelease $P) && rm -f releases/android/AmF-X1-*-android*.apk && cp android/app/build/outputs/apk/release/app-release*.apk "releases/android/AmF-X1-$VER-b$(tr -d ' \n' < android/build-number.txt)-android$T.apk" && (cd releases/android && sha256sum *.apk *.aar > SHA256SUMS.txt); fi   # the build number (android/build-number.txt: raise it for every APK given out) is in the file name and in the app
sh scripts/package-linux.sh "${BUILD_DIR:-$ROOT/build}"; cp "dist/amixos-$VER-linux-$(uname -m).tar.gz" "dist/amixos-$VER-linux-$(uname -m).tar.gz.sha256" releases/linux/
ls -la releases/windows releases/android releases/linux
