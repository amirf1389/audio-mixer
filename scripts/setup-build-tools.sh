#!/usr/bin/env bash
# Installs what the Windows installers need on a Debian / Ubuntu machine (also used inside WSL on Windows):
#   git                  the source checkout
#   Node.js 18+          npm run build, npm run build:installers
#   MinGW-w64 (i686, x86_64)  the setup program stub (scripts/build-exe.js)
#   wixl + msitools      the .msi packages (scripts/build-msi.js)
#   osslsigncode         signs the .exe and .msi (scripts/sign.js)
# Usage:  bash scripts/setup-build-tools.sh
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This script needs apt (Debian / Ubuntu / WSL Ubuntu). On macOS, use Homebrew: brew install git node msitools osslsigncode" >&2
  exit 1
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

$SUDO apt-get update
$SUDO apt-get install -y git ca-certificates curl gnupg \
  gcc-mingw-w64-i686 gcc-mingw-w64-x86-64 wixl msitools osslsigncode

# Node.js 20 from NodeSource when the distribution's Node is missing or older than 18
need_node=1
if command -v node >/dev/null 2>&1; then
  major=$(node -p 'process.versions.node.split(".")[0]')
  if [ "$major" -ge 18 ]; then need_node=0; fi
fi
if [ "$need_node" -eq 1 ]; then
  curl -fsSL https://deb.nodesource.com/setup_20.x | $SUDO bash -
  $SUDO apt-get install -y nodejs
fi

echo
echo "Checking the tools:"
fail=0
check() { if command -v "$1" >/dev/null 2>&1; then printf '  %-22s %s\n' "$1" "$("$@" 2>&1 | head -1)"; else echo "  $1 MISSING"; fail=1; fi; }
check git --version
check node --version
check npm --version
check x86_64-w64-mingw32-gcc --version
check i686-w64-mingw32-gcc --version
check wixl --version
check osslsigncode --version
[ "$fail" -eq 0 ] || { echo "Some tools are missing. Fix the errors above and run this script again." >&2; exit 1; }
echo
echo "All build tools are installed. Next: npm run build && npm run build:installers"
