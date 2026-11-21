# One image for both servers (licence server + OTA server); docker-compose.yml runs it twice with different commands.
# Build context is the repository root:  docker compose -f deploy/portal/docker-compose.yml build
# No packages are installed: the servers use Node.js only. No key or secret is ever copied into the image.
FROM node:22.22.0-alpine3.22
ENV NODE_ENV=production
WORKDIR /app
COPY bridge/license.js bridge/edition.js bridge/diagnostics.js bridge/update.js bridge/security.js bridge/osdetect.js bridge/mail.js bridge/license-public.json bridge/package.json ./bridge/
COPY bridge/license-server ./bridge/license-server
COPY bridge/portal-auth ./bridge/portal-auth
COPY ota-server ./ota-server
COPY management/customer ./management/customer
COPY management/inline-page.js ./management/
COPY deploy/portal/start.js deploy/portal/package.json ./deploy/portal/
COPY deploy/portal/tools ./deploy/portal/tools
# the data folders exist in the image owned by the node user, so new named volumes start with the right owner
RUN rm -f bridge/license-server/*.test.js bridge/license-server/test.js \
 && mkdir -p /data/licence /data/staff /data/ota /data/site-releases \
 && chown -R node:node /data && chmod 700 /data/licence /data/staff /data/ota
USER node
EXPOSE 8787 8780
CMD ["node", "bridge/license-server/server.js"]
