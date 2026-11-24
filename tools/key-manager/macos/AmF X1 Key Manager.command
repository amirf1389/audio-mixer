#!/bin/sh
# Double-click in Finder: starts the AmF X1 Key Manager from this repository (needs Node.js 18+ from nodejs.org).
# It opens its own window (Chrome / Edge app mode, else your browser) and stops when that window closes.
# For a stand-alone .app (no Node needed afterwards): sh tools/key-manager/macos/make-app.sh
cd "$(dirname "$0")/../../.." || exit 1
command -v node >/dev/null 2>&1 || { echo "Node.js is not installed: get it from https://nodejs.org, then double-click this file again."; read -r _; exit 1; }
exec node tools/key-manager/main.js "$@"
