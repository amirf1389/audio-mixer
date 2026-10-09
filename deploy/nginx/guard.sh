#!/bin/sh
# Audio Mixer proxy guard: a portable, fail2ban-style banner for nginx. Plain POSIX sh + awk, no packages.
# Use it where fail2ban does not exist (macOS, Android/Termux, iOS/iSH, Windows Git Bash) or as a fallback.
# It follows the proxy access log, counts failed/blocked bridge requests per client IP and, past a threshold,
# adds `deny <ip>;` to a file nginx includes, then reloads nginx. Bans expire by themselves.
#
#   guard.sh run            follow the log forever (run as root / the user that can reload nginx)
#   guard.sh feed < log     process an existing log once (testing, replay)
#   guard.sh list           show active bans
#   guard.sh unban <ip>     remove a ban
#   guard.sh prune          drop expired bans
#
# Settings (environment): GUARD_MAXRETRY=10  GUARD_FINDTIME=600  GUARD_BANTIME=3600  GUARD_IGNORE="127.0.0.1 ::1"
#   GUARD_LOG  GUARD_BANFILE  GUARD_RELOAD="nginx -s reload"  GUARD_STATE
set -eu

if [ -n "${GUARD_NGINX_DIR:-}" ]; then ND=$GUARD_NGINX_DIR; LOGD=${GUARD_LOG_DIR:-/var/log/nginx}
elif [ -n "${PREFIX:-}" ] && [ -d "${PREFIX}/etc/nginx" ]; then ND=$PREFIX/etc/nginx; LOGD=$PREFIX/var/log/nginx   # Termux
elif [ -d /opt/homebrew/etc/nginx ]; then ND=/opt/homebrew/etc/nginx; LOGD=/opt/homebrew/var/log/nginx           # macOS arm
elif [ -d /usr/local/etc/nginx ]; then ND=/usr/local/etc/nginx; LOGD=/usr/local/var/log/nginx                    # macOS intel / BSD
else ND=/etc/nginx; LOGD=/var/log/nginx; fi

LOG=${GUARD_LOG:-$LOGD/audio-mixer.access.log}
BANFILE=${GUARD_BANFILE:-$ND/mixer_banned.conf}
MAXRETRY=${GUARD_MAXRETRY:-10}
FINDTIME=${GUARD_FINDTIME:-600}
BANTIME=${GUARD_BANTIME:-3600}
IGNORE=${GUARD_IGNORE-"127.0.0.1 ::1"}   # set GUARD_IGNORE="" to ignore nobody
RELOAD=${GUARD_RELOAD:-"nginx -s reload"}
STATE=${GUARD_STATE:-${TMPDIR:-/tmp}/audio-mixer-guard}

say() { printf '%s guard: %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"; }
# Only plain IPv4 / IPv6 literals may ever reach the nginx config.
valid_ip() { [ "${#1}" -le 45 ] && printf '%s' "$1" | grep -Eq '^([0-9]{1,3}\.){3}[0-9]{1,3}$|^[0-9a-fA-F:]{2,39}$'; }
ignored() { for i in $IGNORE; do [ "$i" = "$1" ] && return 0; done; return 1; }

lock() { n=0; until mkdir "$STATE/lock" 2>/dev/null; do n=$((n + 1)); [ "$n" -gt 10 ] && { rmdir "$STATE/lock" 2>/dev/null || true; }; sleep 1; done; }
unlock() { rmdir "$STATE/lock" 2>/dev/null || true; }
reload() { sh -c "$RELOAD" >/dev/null 2>&1 || say "WARNING: '$RELOAD' failed; bans apply after the next reload"; }

ensure() { mkdir -p "$STATE"; [ -f "$BANFILE" ] || : > "$BANFILE"; }

banned() { grep -q "^deny $1;" "$BANFILE" 2>/dev/null; }

ban() {
  exp=$(( $(date +%s) + BANTIME ))
  lock; { cat "$BANFILE"; printf 'deny %s; # until %s\n' "$1" "$exp"; } > "$BANFILE.tmp" && mv "$BANFILE.tmp" "$BANFILE"; unlock
  reload; say "BANNED $1 for ${BANTIME}s"
}

prune() {
  ensure; now=$(date +%s)
  lock
  awk -v now="$now" '{ if ($4 == "until" && $5 + 0 <= now) next; print }' "$BANFILE" > "$BANFILE.tmp"
  if cmp -s "$BANFILE" "$BANFILE.tmp"; then rm -f "$BANFILE.tmp"; changed=0; else mv "$BANFILE.tmp" "$BANFILE"; changed=1; fi
  unlock
  [ "$changed" = 1 ] && { reload; say "expired bans removed"; }
  return 0
}

record() {
  ip=$1
  valid_ip "$ip" || return 0
  ignored "$ip" && return 0
  banned "$ip" && return 0
  now=$(date +%s); f="$STATE/$(printf '%s' "$ip" | tr ':' '_')"
  echo "$now" >> "$f"
  awk -v c=$((now - FINDTIME)) '$1 >= c' "$f" > "$f.t" && mv "$f.t" "$f"
  [ "$(wc -l < "$f")" -ge "$MAXRETRY" ] && { ban "$ip"; rm -f "$f"; }
  return 0
}

# Failed / blocked bridge requests (401 bad login, 403 not allowed, 429 rate limited) or scanners probing hidden
# paths (404 on /.git, /bridge/, /deploy/ ...). Pure `case` matching on the whole combined-log line, no awk, so it
# works on busybox / macOS / Git Bash and is not fooled by spaces in the login name. The client IP is field 1.
feed() {
  ensure
  while IFS= read -r line; do
    case $line in
      *\"[A-Z]*\ /api/*\ HTTP/*\"\ 401\ *|*\"[A-Z]*\ /api/*\ HTTP/*\"\ 403\ *|*\"[A-Z]*\ /api/*\ HTTP/*\"\ 429\ *|\
      *\"[A-Z]*\ /ws/*\ HTTP/*\"\ 401\ *|*\"[A-Z]*\ /ws/*\ HTTP/*\"\ 403\ *|*\"[A-Z]*\ /ws/*\ HTTP/*\"\ 429\ *|\
      *\"[A-Z]*\ /.*\ HTTP/*\"\ 404\ *|*\"[A-Z]*\ /bridge/*\ HTTP/*\"\ 404\ *|*\"[A-Z]*\ /deploy/*\ HTTP/*\"\ 404\ *)
        record "${line%% *}" ;;
    esac
  done
}

case "${1:-}" in
  run)
    ensure
    say "watching $LOG (ban after $MAXRETRY hits in ${FINDTIME}s, for ${BANTIME}s); bans file: $BANFILE"
    rm -f "$STATE/pipe"; mkfifo "$STATE/pipe"
    ( while sleep 60; do prune; done ) &
    PR=$!
    tail -F -n 0 "$LOG" > "$STATE/pipe" 2>/dev/null &
    TP=$!
    trap 'kill "$PR" "$TP" 2>/dev/null; rm -f "$STATE/pipe"; exit 0' INT TERM EXIT
    feed < "$STATE/pipe"
    ;;
  feed) feed ;;
  list) ensure; grep '^deny' "$BANFILE" || echo "no active bans" ;;
  unban)
    [ -n "${2:-}" ] && valid_ip "$2" || { echo "usage: $0 unban <ip>" >&2; exit 2; }
    ensure; lock; grep -v "^deny $2;" "$BANFILE" > "$BANFILE.tmp" || true; mv "$BANFILE.tmp" "$BANFILE"; unlock
    reload; say "unbanned $2" ;;
  prune) prune ;;
  *) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
