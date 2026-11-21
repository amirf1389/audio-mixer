#!/bin/sh
# AmF X1 license key generator: double-click (macOS) or run (Linux) to open it in your browser (this computer only).
# AUTO NODE.JS: if Node.js 18+ is missing, it offers to install Node.js LTS (macOS: Homebrew; Linux: apt / dnf / pacman / zypper), then starts
# the generator through npm (npm run license:generator). Nothing is installed without asking; the private key never leaves this computer.
cd "$(dirname "$0")/.." || exit 1
node_ok() { command -v node >/dev/null 2>&1 && [ "$(node -p "process.versions.node.split('.')[0]")" -ge 18 ] 2>/dev/null; }
if ! node_ok; then
  echo "Node.js 18 or newer is needed to run the key generator."
  if [ "$(uname)" = Darwin ] && command -v brew >/dev/null 2>&1; then cmd="brew install node"
  elif command -v apt-get >/dev/null 2>&1; then cmd="sudo apt-get install -y nodejs npm"
  elif command -v dnf >/dev/null 2>&1; then cmd="sudo dnf install -y nodejs npm"
  elif command -v pacman >/dev/null 2>&1; then cmd="sudo pacman -S --noconfirm nodejs npm"
  elif command -v zypper >/dev/null 2>&1; then cmd="sudo zypper install -y nodejs npm"
  else echo "Install Node.js LTS from https://nodejs.org/ and run this again."; exit 1; fi
  printf "Install it now with: %s ? [y/N] " "$cmd"; read -r a
  case "$a" in y|Y|yes|YES) sh -c "$cmd" || exit 1 ;; *) echo "Not installed."; exit 1 ;; esac
  node_ok || { echo "Node.js is still older than 18 or not found: install the LTS from https://nodejs.org/"; exit 1; }
fi
if command -v npm >/dev/null 2>&1; then exec npm run --silent license:generator -- "$@"; fi
exec node scripts/license-generator.js --open "$@"
