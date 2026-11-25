#!/bin/bash
# AmF X1 for macOS: configure, build (universal arm64 + x86_64), macdeployqt, sign, notarize, .dmg.
#   QT_DIR=~/Qt/6.8.3/macos scripts/macos/build.sh
# Optional environment (never stored in the repository):
#   AMIX_SIGN_IDENTITY   "Developer ID Application: NAME (TEAMID)"  - from your login keychain; unset = unsigned build
#   AMIX_NOTARY_PROFILE  a notarytool keychain profile (xcrun notarytool store-credentials NAME ...); unset = not notarized
#   AMIX_ARCHS           default "arm64;x86_64"
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${AMIX_BUILD_DIR:-$ROOT/build/macos}"
OUT="${AMIX_OUT_DIR:-$ROOT/dist/macos}"
ARCHS="${AMIX_ARCHS:-arm64;x86_64}"
VERSION="$(tr -d '[:space:]' < "$ROOT/native/amixos/version.txt")"
[ "$(uname)" = Darwin ] || { echo "this script runs on macOS only" >&2; exit 2; }
QT_DIR="${QT_DIR:-$(dirname "$(dirname "$(command -v qmake 2>/dev/null || echo /x/x)")")}"
[ -x "$QT_DIR/bin/macdeployqt" ] || { echo "set QT_DIR to a Qt 6 macOS kit (the folder with bin/macdeployqt)" >&2; exit 2; }

cmake -S "$ROOT/native/amixos" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build "$BUILD" --target amixos -j"$(sysctl -n hw.ncpu)"
APP="$BUILD/AmF X1.app"
[ -d "$APP" ] || { echo "no bundle at $APP" >&2; exit 1; }
"$QT_DIR/bin/macdeployqt" "$APP" -qmldir="$ROOT/native/amixos/qml" -always-overwrite
for f in "$BUILD"/engine/libamix_soxr*.dylib "$BUILD"/libamix_soxr*.dylib; do [ -f "$f" ] && cp -f "$f" "$APP/Contents/Frameworks/"; done

ENT="$ROOT/native/amixos/mac/amixos.entitlements"
SIGNED=0
if [ -n "${AMIX_SIGN_IDENTITY:-}" ]; then
  # inside out: every library and plug-in, then the app with the hardened runtime and its entitlements
  find "$APP/Contents" \( -name "*.dylib" -o -name "*.so" -o -name "*.framework" \) -print0 | xargs -0 -I{} codesign --force --timestamp --options runtime --sign "$AMIX_SIGN_IDENTITY" "{}"
  codesign --force --timestamp --options runtime --entitlements "$ENT" --sign "$AMIX_SIGN_IDENTITY" "$APP"
  codesign --verify --deep --strict --verbose=2 "$APP"
  SIGNED=1
else
  codesign --force --deep --sign - "$APP"   # ad-hoc: runs on this Mac (Apple Silicon needs a signature), Gatekeeper warns elsewhere
  echo "NOTE: AMIX_SIGN_IDENTITY is not set: the app is UNSIGNED (ad-hoc) and will not be notarized."
fi

mkdir -p "$OUT"
DMG="$OUT/AmF-X1-$VERSION-macos.dmg"
STAGE="$(mktemp -d)"; cp -R "$APP" "$STAGE/"; ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"; hdiutil create -volname "AmF X1" -srcfolder "$STAGE" -ov -format UDZO "$DMG"; rm -rf "$STAGE"
[ "$SIGNED" = 1 ] && codesign --force --timestamp --sign "$AMIX_SIGN_IDENTITY" "$DMG"

if [ "$SIGNED" = 1 ] && [ -n "${AMIX_NOTARY_PROFILE:-}" ]; then
  xcrun notarytool submit "$DMG" --keychain-profile "$AMIX_NOTARY_PROFILE" --wait
  xcrun stapler staple "$DMG"
  echo "signed, notarized and stapled: $DMG"
elif [ "$SIGNED" = 1 ]; then
  echo "signed but NOT notarized (AMIX_NOTARY_PROFILE not set): $DMG"
else
  echo "UNSIGNED build: $DMG"
fi
