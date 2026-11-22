FROM node:latest
WORKDIR /app
COPY . .
ENV API_TOKEN=abcdef123456
CMD ["node", "server.js"]
