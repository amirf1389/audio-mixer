#!/bin/sh
# Audio Mixer PC mode (macOS / Linux): starts the local system server and opens the mixer. Needs Node.js 18+.
cd "$(dirname "$0")" || exit 1
if ! command -v node >/dev/null 2>&1; then
  echo "Node.js is required. Download it from https://nodejs.org/ and run this file again."
  exit 1
fi
exec node client/cli.js "$@"
