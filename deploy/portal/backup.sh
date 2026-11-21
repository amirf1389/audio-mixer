#!/bin/sh
# Backs up everything the portal keeps (Docker install), into backups/<date>/ (0700):
#   staff.db           the staff / CRM database (consistent copy via /admin/system/backup; the CRM inside stays encrypted)
#   licence.tgz        licence records, audit log, website + presets state
#   ota-meta.tgz       OTA manifests (current + history), file index, stats, audit log, licence requests
#   ota-releases.tgz   the release files (only with --with-releases: large, and you have them on your PC anyway)
# NOT included on purpose: .env and secrets/. Keep one offline copy of those (password manager / encrypted USB): without
# secrets/licence-data.key the customer details in the licence records cannot be read, without PORTAL_CRM_SECRET the CRM cannot.
#   sh backup.sh [--with-releases]       e.g. daily from cron:  15 3 * * * cd /opt/audio-mixer/deploy/portal && sh backup.sh
set -eu
umask 077
cd "$(dirname "$0")"
out="backups/$(date -u +%Y-%m-%dT%H%M%SZ)"; mkdir -p "$out"
docker compose exec -T licence node deploy/portal/tools/staff-backup.js > "$out/staff.db"
docker compose exec -T licence tar -C /data -czf - licence > "$out/licence.tgz"
docker compose exec -T ota tar -C /data -czf - --exclude=ota/releases ota > "$out/ota-meta.tgz"
if [ "${1:-}" = "--with-releases" ]; then docker compose exec -T ota tar -C /data -czf - ota/releases > "$out/ota-releases.tgz"; fi
( cd "$out" && sha256sum ./* > SHA256SUMS )
echo "backup written to $out"
# keep the last 30
ls -1d backups/*/ 2>/dev/null | sort | head -n -30 | xargs -r rm -rf
