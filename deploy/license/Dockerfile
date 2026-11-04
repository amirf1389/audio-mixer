# docker build -f deploy/license/Dockerfile -t audio-mixer-license .   (from the repository root)
FROM node:22-alpine
WORKDIR /app
COPY bridge/license-server ./bridge/license-server
COPY bridge/license.js bridge/license-public.json ./bridge/
USER node
EXPOSE 8787
ENV LICENSE_HOST=0.0.0.0 LICENSE_PORT=8787 LICENSE_DATA_FILE=/data/licenses.json
CMD ["node", "bridge/license-server/server.js"]
