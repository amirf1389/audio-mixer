#!/bin/sh
# Compiles the remote's pure-Kotlin client with kotlinc and tests it against a freshly started amixos-daemon (build dir as $1, default ../../build).
set -e
cd "$(dirname "$0")"; BUILD="${1:-../../build}"; OUT="${TMPDIR:-/tmp}/amix-client-test"; mkdir -p "$OUT"
kotlinc ../app/src/main/java/com/amfb/amixos/remote/*.kt ClientTest.kt -include-runtime -d "$OUT/client-test.jar" 2>&1 | grep -v "warning\|Xverify" || true
"$BUILD/amixos-daemon" --port 18765 --token smoke-token-123 > "$OUT/daemon.log" 2>&1 & DP=$!
sleep 1; java -jar "$OUT/client-test.jar" 18765 smoke-token-123; RC=$?
kill $DP 2>/dev/null || true; exit $RC
