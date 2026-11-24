#!/bin/sh
# Builds "AmF X1 Key Manager.app" ON A MAC (codesign is needed, so this cannot run on Linux or Windows).
#   sh tools/key-manager/macos/make-app.sh            -> releases/macos/AmF-X1-Key-Manager-<version>-macos-<arm64|x64>.zip (+ .sha256)
# Needs: Node.js of the version you build with (nodejs.org; the official darwin binary of that SAME version is downloaded and checked against
# the signed SHASUMS256), Xcode command line tools (codesign, ditto) and network access for npx postject. The app is signed ad hoc ("-"):
# on another Mac, open it the first time with right-click -> Open (or sign / notarise it with your Developer ID).
set -eu
cd "$(dirname "$0")/../../.."
[ "$(uname -s)" = Darwin ] || { echo "make-app.sh runs on macOS only (codesign)."; exit 1; }
case "$(uname -m)" in arm64) T=darwin-arm64 ;; x86_64) T=darwin-x64 ;; *) echo "unsupported CPU $(uname -m)"; exit 1 ;; esac
exec node tools/key-manager/build.js --target "${1:-$T}"
