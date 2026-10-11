#!/bin/sh
# Audio Mixer PC mode (macOS / Linux): starts the local system server and opens the mixer.
# If Node.js 18+ is missing it is downloaded from nodejs.org (checksum verified, into ~/.local/share/audio-mixer, no administrator rights),
# and the native audio module (Audify) is installed once. AUDIO_MIXER_YES=1 skips the questions, AUDIO_MIXER_NO_NATIVE=1 skips the audio module.
cd "$(dirname "$0")" || exit 1
HERE=$(pwd)
# in the repository this file sits in linux/ and the app is one folder up; in an installed package they are the same folder
[ -f ./client/cli.js ] || { [ -f ../client/cli.js ] && cd ..; }
APP_DIR=$(pwd)
if [ -f "$HERE/ensure-node.sh" ]; then
  . "$HERE/ensure-node.sh"
  am_ensure_node || exit 1
  am_ensure_audio "$APP_DIR"
elif command -v node >/dev/null 2>&1; then
  NODE=node
else
  echo "Node.js is required. Download it from https://nodejs.org/ and run this file again."
  exit 1
fi
exec "$NODE" client/cli.js "$@"
