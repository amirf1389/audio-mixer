#!/bin/sh
# Prepares this folder for the first start: .env with fresh random secrets (0600), secrets/licence-data.key (0600), data folders.
# Safe to run again: existing secrets are kept, only empty ones are filled. Secrets are never printed.
#   sh init.sh            (Docker: run it as root, or with sudo, so the files can be handed to the containers' user 1000)
set -eu
umask 077
cd "$(dirname "$0")"

rand() {   # 48 random bytes, base64url (64 characters)
  if command -v node >/dev/null 2>&1; then node -e "process.stdout.write(require('node:crypto').randomBytes(48).toString('base64url'))"
  elif command -v openssl >/dev/null 2>&1; then openssl rand -base64 48 | tr '+/' '-_' | tr -d '=\n'
  else echo "need node or openssl to make secrets" >&2; exit 1; fi
}

[ -f .env ] || cp .env.example .env
chmod 600 .env
for k in LICENSE_ADMIN_TOKEN OTA_ADMIN_TOKEN PORTAL_JWT_SECRET PORTAL_CRM_SECRET; do
  if grep -q "^$k=\$" .env; then
    v=$(rand); tmp=$(mktemp .env.XXXXXX)
    awk -v k="$k" -v v="$v" 'BEGIN{FS=OFS="="} $1==k && $2=="" {print k "=" v; next} {print}' .env > "$tmp"
    chmod 600 "$tmp"; mv "$tmp" .env
    echo "made $k"
  else
    echo "kept $k"
  fi
done

mkdir -p secrets data/licence data/staff data/ota site-releases backups
chmod 700 secrets data backups
[ -s secrets/licence-data.key ] || { rand > secrets/licence-data.key; echo "made secrets/licence-data.key"; }
chmod 600 secrets/licence-data.key
if [ -f secrets/vendor-private.pem ]; then chmod 600 secrets/vendor-private.pem; else
  echo "NEXT: copy your vendor private key to $(pwd)/secrets/vendor-private.pem (scp from your PC), then run this again."; fi

# the containers run as user 1000 (node): give it the key files (read-only mounts keep the 0600 mode)
if [ "$(id -u)" = 0 ]; then chown -R 1000:1000 secrets data site-releases; echo "files handed to uid 1000 for Docker"; fi
echo "done. Now edit the domains in .env (PUBLIC_DOMAIN, ADMIN_DOMAIN, OTA_ADMIN_DOMAIN, ACME_EMAIL, ADMIN_ALLOW_IPS)."
