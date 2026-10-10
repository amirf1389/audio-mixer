# docker build -f deploy/ota/Dockerfile -t audio-mixer-ota .   (from the repository root)
FROM node:22-alpine
WORKDIR /app
COPY ota-server ./ota-server
COPY bridge/update.js bridge/license.js bridge/license-public.json bridge/security.js bridge/osdetect.js ./bridge/
USER node
EXPOSE 8780
ENV OTA_HOST=0.0.0.0 OTA_DATA=/data
CMD ["node", "ota-server/server.js"]
