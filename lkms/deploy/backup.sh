#!/bin/sh
# Daily backup of the licence database:  ./backup.sh /path/to/backups      (run it from cron on the server)
# Keeps 30 days. Copy the folder to another machine as well; a backup on the same disk is not a backup.
# Back up .env separately (password manager): without LKMS_KEY_PEPPER a restored database cannot recognise any key.
set -eu
OUT="${1:?usage: backup.sh BACKUP_FOLDER}"
cd "$(dirname "$0")"
mkdir -p "$OUT"
FILE="$OUT/lkms-$(date -u +%Y%m%d-%H%M%S).sql.gz"
docker compose exec -T db pg_dump -U lkms -d lkms --no-owner | gzip -9 > "$FILE.part"
[ -s "$FILE.part" ] || { echo "backup failed: empty dump" >&2; rm -f "$FILE.part"; exit 1; }
mv "$FILE.part" "$FILE"
chmod 600 "$FILE"
find "$OUT" -name 'lkms-*.sql.gz' -mtime +30 -delete
echo "backup written: $FILE"
