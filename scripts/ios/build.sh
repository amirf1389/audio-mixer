#!/bin/bash
# AmF X1 Remote for iOS (platforms/ios, SwiftUI): xcodegen -> xcodebuild.
#   scripts/ios/build.sh             simulator build (no signing)
#   AMIX_TEAM_ID=ABCDE12345 scripts/ios/build.sh   archive + export a signed .ipa (automatic signing; the team's certificates in your keychain)
# AMIX_EXPORT_METHOD: development (default) | ad-hoc | app-store-connect. Nothing secret is read from or written to the repository.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; P="$ROOT/platforms/ios"; OUT="${AMIX_OUT_DIR:-$ROOT/dist/ios}"
[ "$(uname)" = Darwin ] || { echo "this script runs on macOS only" >&2; exit 2; }
command -v xcodegen >/dev/null || { echo "install XcodeGen first: brew install xcodegen" >&2; exit 2; }
(cd "$P" && xcodegen generate --quiet)
mkdir -p "$OUT"
if [ -z "${AMIX_TEAM_ID:-}" ]; then
  xcodebuild -project "$P/AmFX1Remote.xcodeproj" -scheme AmFX1Remote -configuration Debug -sdk iphonesimulator \
    -destination 'generic/platform=iOS Simulator' -derivedDataPath "$OUT/dd" CODE_SIGNING_ALLOWED=NO build
  echo "NOTE: AMIX_TEAM_ID not set: simulator build only (unsigned): $OUT/dd/Build/Products/Debug-iphonesimulator/AmFX1Remote.app"
  exit 0
fi
xcodebuild -project "$P/AmFX1Remote.xcodeproj" -scheme AmFX1Remote -configuration Release -destination 'generic/platform=iOS' \
  -archivePath "$OUT/AmFX1Remote.xcarchive" DEVELOPMENT_TEAM="$AMIX_TEAM_ID" -allowProvisioningUpdates archive
PL="$(mktemp -t exportopts).plist"
cat > "$PL" <<PLIST
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>method</key><string>${AMIX_EXPORT_METHOD:-development}</string><key>teamID</key><string>$AMIX_TEAM_ID</string><key>signingStyle</key><string>automatic</string></dict></plist>
PLIST
xcodebuild -exportArchive -archivePath "$OUT/AmFX1Remote.xcarchive" -exportPath "$OUT" -exportOptionsPlist "$PL" -allowProvisioningUpdates
rm -f "$PL"; echo "signed: $OUT/AmFX1Remote.ipa"
