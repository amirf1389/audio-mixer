#!/usr/bin/env bash
# Runs the security audit (static, offline-capable) and its unit tests. Exit status: the audit's (1 = a HIGH finding, 2 = the audit failed).
#   scripts/security/audit.sh [audit.js options]        e.g. --offline, --deep, --compile-db build, --fail-on MEDIUM
#   AUDIT_SKIP_TESTS=1 scripts/security/audit.sh         skip the checker's own unit tests
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
command -v node >/dev/null 2>&1 || { echo "node (22+) is needed for the security audit" >&2; exit 2; }
if [ "${AUDIT_SKIP_TESTS:-0}" != "1" ]; then
  node --test "$HERE/test/audit.test.js" > /dev/null 2>&1 || { echo "the security checker's unit tests fail: node --test scripts/security/test/audit.test.js" >&2; exit 2; }
fi
cd "$ROOT" && exec node "$HERE/audit.js" "$@"
