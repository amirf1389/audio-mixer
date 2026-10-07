#!/bin/sh
# One-command setup of nginx rate limiting + ban protection for the Audio Mixer proxy.
# Run from a terminal on: Linux / WSL, Android (Termux), macOS, iOS (iSH / a-Shell), Windows (Git Bash).
#   sh deploy/nginx/setup.sh              install + configure
#   sh deploy/nginx/setup.sh --dry-run    only print what would be done
# Override detection with SETUP_PLATFORM=linux|wsl|macos|termux|ios|windows
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
DRY=0; [ "${1:-}" = "--dry-run" ] && DRY=1

detect() {
  [ -n "${SETUP_PLATFORM:-}" ] && { echo "$SETUP_PLATFORM"; return; }
  if [ -n "${TERMUX_VERSION:-}" ] || [ -d /data/data/com.termux ]; then echo termux
  elif [ -e /proc/ish ]; then echo ios
  else
    case "$(uname -s)" in
      Darwin) echo macos ;;
      MINGW*|MSYS*|CYGWIN*) echo windows ;;
      *) if grep -qi microsoft /proc/version 2>/dev/null; then echo wsl; else echo linux; fi ;;
    esac
  fi
}
PLAT=$(detect)

SUDO=""
if [ "$(id -u 2>/dev/null || echo 0)" != 0 ] && command -v sudo >/dev/null 2>&1 && [ "$PLAT" != termux ] && [ "$PLAT" != windows ]; then SUDO="sudo"; fi
run() { if [ "$DRY" = 1 ]; then echo "+ $*"; else "$@"; fi; }
note() { printf '\n== %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

pkg_install() {   # pkg_install <packages...> for Linux package managers
  if have apt-get; then run $SUDO apt-get update && run $SUDO apt-get install -y "$@"
  elif have dnf; then run $SUDO dnf install -y "$@"
  elif have yum; then run $SUDO yum install -y "$@"
  elif have pacman; then run $SUDO pacman -S --noconfirm "$@"
  elif have zypper; then run $SUDO zypper --non-interactive install "$@"
  elif have apk; then run $SUDO apk add "$@"
  else echo "No supported package manager found; install: $*" >&2; exit 1; fi
}

# Installs a copy of audio-mixer.conf with this platform's nginx paths. It is NOT included automatically:
# it still has the example domain/certificate (CHANGE ME lines) and would stop nginx from starting if loaded as-is.
install_conf() {  # install_conf <nginx conf dir> <log dir>
  esc() { printf '%s' "$1" | sed 's/[\\&|]/\\&/g'; }
  run $SUDO mkdir -p "$1"
  if [ "$DRY" = 1 ]; then echo "+ install audio-mixer.conf -> $1/audio-mixer.conf (paths: $1, $2)"; return; fi
  sed -e "s|/etc/nginx/|$(esc "$1")/|g" -e "s|/var/log/nginx/|$(esc "$2")/|g" "$HERE/audio-mixer.conf" | $SUDO tee "$1/audio-mixer.conf" >/dev/null
}

install_guard() {  # install_guard <dir for guard.sh> <nginx conf dir> <log dir>
  run $SUDO mkdir -p "$1" "$2" "$3"
  run $SUDO cp "$HERE/guard.sh" "$1/audio-mixer-guard"
  run $SUDO chmod 755 "$1/audio-mixer-guard"
  run $SUDO sh -c ": >> '$2/mixer_banned.conf'"
  install_conf "$2" "$3"
}

echo "Platform: $PLAT$( [ "$DRY" = 1 ] && echo '  (dry run)')"

case "$PLAT" in
  linux|wsl)
    note "Installing nginx + fail2ban"
    pkg_install nginx fail2ban
    note "Installing the fail2ban filter and jails"
    run $SUDO cp "$HERE/fail2ban/filter.d/audio-mixer-bridge.conf" /etc/fail2ban/filter.d/
    run $SUDO cp "$HERE/fail2ban/jail.d/audio-mixer.local" /etc/fail2ban/jail.d/
    run $SUDO mkdir -p /var/log/nginx
    run $SUDO sh -c ": >> /var/log/nginx/audio-mixer.access.log"
    install_conf /etc/nginx /var/log/nginx
    if [ -d /run/systemd/system ]; then
      run $SUDO systemctl enable --now fail2ban
      run $SUDO systemctl restart fail2ban
      note "Check"; echo "sudo fail2ban-client status audio-mixer-bridge"
    else
      note "No systemd here (typical for WSL1/containers): using the portable guard instead of fail2ban"
      install_guard /usr/local/bin /etc/nginx /var/log/nginx
      echo "Start it:  sudo audio-mixer-guard run &"
    fi ;;
  macos)
    note "Installing nginx (Homebrew)"
    have brew || { echo "Install Homebrew first: https://brew.sh" >&2; [ "$DRY" = 1 ] || exit 1; }
    run brew install nginx
    P=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
    note "fail2ban does not run on macOS: installing the portable guard"
    install_guard "$P/bin" "$P/etc/nginx" "$P/var/log/nginx"
    echo "Start it:  sudo $P/bin/audio-mixer-guard run &" ;;
  termux)
    note "Installing nginx (Termux)"
    run pkg install -y nginx
    P=${PREFIX:-/data/data/com.termux/files/usr}
    note "fail2ban needs root + a firewall, so Termux uses the portable guard"
    install_guard "$P/bin" "$P/etc/nginx" "$P/var/log/nginx"
    echo "Keep the phone awake:  termux-wake-lock"
    echo "Start it:  nohup audio-mixer-guard run >/dev/null 2>&1 &" ;;
  ios)
    note "Installing nginx (iSH / Alpine)"
    run apk add nginx
    note "fail2ban is not usable in iSH: installing the portable guard"
    install_guard /usr/local/bin /etc/nginx /var/log/nginx
    echo "Start it:  audio-mixer-guard run &"
    echo "iOS suspends apps in the background: the guard only watches while iSH stays open in the foreground." ;;
  windows)
    note "Windows (Git Bash)"
    echo "fail2ban is Linux-only. Two options:"
    echo "  1) Best: install WSL2 (wsl --install), open the Linux terminal and run this script there."
    echo "  2) Native nginx for Windows (https://nginx.org/en/download.html) + the portable guard from Git Bash:"
    echo "       export GUARD_NGINX_DIR=/c/nginx/conf GUARD_LOG_DIR=/c/nginx/logs"
    echo "       export GUARD_RELOAD='/c/nginx/nginx.exe -p /c/nginx -s reload'"
    echo "       sh $HERE/guard.sh run"
    echo "     and in your nginx.conf use:  access_log logs/audio-mixer.access.log;  include mixer_banned*.conf;" ;;
  *) echo "Unknown platform '$PLAT'" >&2; exit 1 ;;
esac

note "Next"
echo "1. Edit the CHANGE ME lines (domain, certificate paths, root) in the installed audio-mixer.conf (see the path above),"
echo "   then include it from the http { } block of your nginx.conf:  include <that path>;   and run: nginx -t && nginx -s reload"
echo "2. Rate limits are already in that file: /api and /ws 10 req/s (burst 20 / 5), page 20 req/s, 40 connections per IP; excess gets HTTP 429."
