#!/bin/sh
# AmF X1 license key generator: double-click (macOS) or run (Linux) to open it in your browser (this computer only). Needs Node.js 18+.
cd "$(dirname "$0")/.." || exit 1
command -v node >/dev/null 2>&1 || { echo "Node.js is required: https://nodejs.org/"; exit 1; }
exec node scripts/license-generator.js --open "$@"
