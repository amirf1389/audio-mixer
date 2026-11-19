#!/bin/sh
# RELEASE BUILD: the Windows installer and zip, the Android APKs (AmF X1 and AmF X1 Mixer), the checksums, and the UNSIGNED update
# manifest of what was built (releases/update.pending.json). Usage:  QT_WIN_KIT=<Qt mingw_64 kit> sh scripts/build-release.sh
# The release reaches installed consoles when the vendor signs the manifest:  node scripts/make-update.js  (asks for the key PIN)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"; VER="$(tr -d ' \n' < native/amixos/version.txt)"
echo "== windows console $VER"
sh scripts/build-console-windows.sh "${QT_WIN_KIT:-}" dist/windows
cp "dist/windows/AmF-X1-$VER-windows-x64.zip" "dist/windows/AmF-X1-$VER-windows-x64.zip.sha256" releases/windows/
makensis -V2 -DVERSION="$VER" -DSRC="$ROOT/dist/windows/AmF-X1-$VER-windows-x64" -DOUT="$ROOT/releases/windows/AmF-X1-$VER-setup.exe" install/windows/amfx1.nsi
if [ -n "${AMFX1_SIGN_PFX:-}" ]; then node scripts/sign-windows.js "releases/windows/AmF-X1-$VER-setup.exe"; else echo "== the setup program is NOT SIGNED (no AMFX1_SIGN_PFX): Windows shows its protection screen for it"; fi
(cd releases/windows && sha256sum *.exe *.dll *.zip > SHA256SUMS.txt)
echo "== android"
(cd android && gradle --no-daemon -q assembleRelease)
B="$(tr -d ' \n' < android/build-number.txt)"
rm -f releases/android/AmF-X1-*-android.apk
cp android/app/build/outputs/apk/release/app-release*.apk "releases/android/AmF-X1-$VER-b$B-android.apk"
cp android/mixer/build/outputs/apk/release/mixer-release*.apk "releases/android/AmF-X1-Mixer-$VER-b$B-android.apk"
(cd releases/android && sha256sum *.apk *.aar > SHA256SUMS.txt)
ls -la releases/windows/*.exe releases/windows/*.zip releases/android/*.apk
echo "== update manifest (unsigned; the vendor signs it)"
node scripts/make-update.js --prepare
echo "== done"
