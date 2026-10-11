# Audio Mixer: finds Node.js 18+ or installs it (official build from nodejs.org, checksum verified, no administrator rights).
# Sourced by start-pc-mode.sh, the Linux launcher and the macOS app. Sets NODE (and puts it on PATH when it installs a private copy).
#   AUDIO_MIXER_NODE=/path/to/node   use this Node.js
#   AUDIO_MIXER_YES=1                do not ask before downloading
#   AUDIO_MIXER_NODE_MAJOR=22        Node.js line to install (an LTS line; default 22)
#   AUDIO_MIXER_HOME=<dir>           where the private copy and the audio module live (default ~/.local/share/audio-mixer)
AM_MIN_NODE=18
AM_HOME="${AUDIO_MIXER_HOME:-$HOME/.local/share/audio-mixer}"
AM_NODE_DIR="$AM_HOME/node"
AM_EXTRA_NODES="${AM_EXTRA_NODES-/opt/homebrew/bin/node /usr/local/bin/node}"   # places a GUI launcher may not have on its PATH

am_node_ok() {   # $1 = node binary: runs and is version 18 or newer
  [ -n "$1" ] && command -v "$1" >/dev/null 2>&1 || return 1
  v=$("$1" -p 'process.versions.node.split(".")[0]' 2>/dev/null) || return 1
  [ "$v" -ge "$AM_MIN_NODE" ] 2>/dev/null
}

am_fetch() {     # $1 = url, $2 = output file
  if command -v curl >/dev/null 2>&1; then curl -fsSL "$1" -o "$2"
  elif command -v wget >/dev/null 2>&1; then wget -q "$1" -O "$2"
  else return 1; fi
}

am_sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
  else openssl dgst -sha256 "$1" 2>/dev/null | sed 's/^.*= //'; fi
}

am_ask() {       # $1 = question; yes by default, AUDIO_MIXER_YES=1 never asks; a graphical prompt on macOS without a terminal
  [ "${AUDIO_MIXER_YES:-}" = "1" ] && return 0
  if [ -t 0 ]; then printf '%s [Y/n] ' "$1"; read -r a; case "$a" in n|N|no|NO) return 1;; esac; return 0; fi
  if [ "$(uname -s)" = "Darwin" ] && command -v osascript >/dev/null 2>&1; then
    osascript -e "display dialog \"$1\" buttons {\"Cancel\",\"Install\"} default button \"Install\" with icon note" >/dev/null 2>&1; return $?
  fi
  return 0
}

am_install_node() {
  os=$(uname -s); arch=$(uname -m)
  case "$os" in Linux) p=linux;; Darwin) p=darwin;; *) echo "Automatic Node.js install is not supported on $os: https://nodejs.org/" >&2; return 1;; esac
  case "$arch" in x86_64|amd64) a=x64;; aarch64|arm64) a=arm64;; armv7l) a=armv7l;; *) echo "No official Node.js build for $arch: https://nodejs.org/" >&2; return 1;; esac
  major="${AUDIO_MIXER_NODE_MAJOR:-22}"
  base="https://nodejs.org/dist/latest-v${major}.x"
  am_ask "Node.js $AM_MIN_NODE or newer is needed and was not found. Download the official Node.js ${major} (about 30 MB) from nodejs.org into $AM_NODE_DIR?" || { echo "Node.js is required: https://nodejs.org/" >&2; return 1; }
  tmp=$(mktemp -d 2>/dev/null || echo "${TMPDIR:-/tmp}/audio-mixer-node.$$"); mkdir -p "$tmp"
  echo "Downloading the Node.js ${major} checksum list ..."
  am_fetch "$base/SHASUMS256.txt" "$tmp/SHASUMS256.txt" || { echo "Cannot reach nodejs.org (offline?). Install Node.js from https://nodejs.org/" >&2; rm -rf "$tmp"; return 1; }
  line=$(grep -E " node-v[0-9.]+-$p-$a\.tar\.gz\$" "$tmp/SHASUMS256.txt" | head -n 1)
  want=${line%% *}; file=${line##* }
  [ -n "$want" ] && [ -n "$file" ] || { echo "No Node.js ${major} build for $p-$a was found at nodejs.org." >&2; rm -rf "$tmp"; return 1; }
  echo "Downloading $file ..."
  am_fetch "$base/$file" "$tmp/$file" || { echo "Download failed." >&2; rm -rf "$tmp"; return 1; }
  got=$(am_sha256 "$tmp/$file")
  if [ "$got" != "$want" ]; then echo "The download does not match nodejs.org's checksum (expected $want, got $got). Nothing was installed." >&2; rm -rf "$tmp"; return 1; fi
  rm -rf "$AM_NODE_DIR"; mkdir -p "$AM_NODE_DIR"
  tar -xzf "$tmp/$file" -C "$AM_NODE_DIR" --strip-components=1 || { echo "Could not unpack Node.js." >&2; rm -rf "$tmp" "$AM_NODE_DIR"; return 1; }
  rm -rf "$tmp"
  echo "Node.js installed in $AM_NODE_DIR (checksum verified)."
}

am_ensure_node() {
  NODE=""
  for c in "${AUDIO_MIXER_NODE:-}" node "$AM_NODE_DIR/bin/node" $AM_EXTRA_NODES; do
    if am_node_ok "$c"; then NODE="$c"; break; fi
  done
  if [ -z "$NODE" ]; then
    am_install_node || return 1
    am_node_ok "$AM_NODE_DIR/bin/node" || return 1
    NODE="$AM_NODE_DIR/bin/node"
  fi
  case "$NODE" in */*) PATH="$(dirname "$NODE"):$PATH"; export PATH;; esac
  # native audio module (Audify, prebuilt binary, no compiler) in a user folder; the app folder may be read-only
  export NODE_PATH="$AM_HOME/modules/node_modules${NODE_PATH:+:$NODE_PATH}"
  return 0
}

# Native audio module (Audify = RtAudio: ALSA / JACK / PulseAudio / Core Audio; prebuilt binary, no compiler) installed once into the user's folder.
# $1 = the app folder (checked first, so a bundled module is never installed twice). Never fatal: without it the mixer runs in web-audio mode.
am_ensure_audio() {
  [ "${AUDIO_MIXER_NO_NATIVE:-}" = "1" ] && return 0
  [ -d "$1/bridge/node_modules/audify" ] && return 0
  [ -d "$AM_HOME/modules/node_modules/audify" ] && return 0
  command -v npm >/dev/null 2>&1 || { echo "npm not found: skipping the native audio module (the mixer still works with browser audio)." >&2; return 0; }
  am_ask "Install the native audio module (Audify, a prebuilt download of about 1 MB) for ASIO-style low-latency audio?" || return 0
  mkdir -p "$AM_HOME/modules" || return 0
  [ -f "$AM_HOME/modules/package.json" ] || echo '{"name":"audio-mixer-modules","private":true}' > "$AM_HOME/modules/package.json"
  if (cd "$AM_HOME/modules" && npm install --no-audit --no-fund audify); then echo "Native audio module installed."; else echo "Could not install the native audio module; continuing with browser audio." >&2; fi
  return 0
}

# Start-up screen: opens boot.html in the browser at once (the first start can spend a minute on Node.js and the audio module). The page switches to
# the mixer when the server answers, so the launcher must not open a second window: AM_SPLASH_ARGS holds "--no-open" when the screen was opened.
#   am_splash "$APP" "$@"     (arguments of the launcher: --no-open and --port are honoured; AUDIO_MIXER_NO_SPLASH=1 turns it off)
am_splash() {
  AM_SPLASH_ARGS=""
  app="$1"; shift
  case "${1:-}" in ''|-*) ;; start) ;; *) return 0 ;; esac   # a command (doctor, drivers, npm ...) is not a start: no start-up screen
  [ -z "${AUDIO_MIXER_NO_SPLASH:-}" ] && [ -f "$app/boot.html" ] || return 0
  port=8765; prev=""
  for a in "$@"; do
    [ "$a" = "--no-open" ] && return 0
    [ "$prev" = "--port" ] && port="$a"
    case "$a" in --port=*) port="${a#--port=}" ;; esac
    prev="$a"
  done
  case "$port" in ''|*[!0-9]*) port=8765 ;; esac
  url="file://$(printf '%s' "$app/boot.html" | sed 's/ /%20/g')#$port"
  if [ "$(uname)" = "Darwin" ]; then open "$url" >/dev/null 2>&1 && AM_SPLASH_ARGS="--no-open"
  elif [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && command -v xdg-open >/dev/null 2>&1; then xdg-open "$url" >/dev/null 2>&1 && AM_SPLASH_ARGS="--no-open"
  fi
  return 0
}
