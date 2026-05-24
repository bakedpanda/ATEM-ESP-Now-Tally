FROM node:20-alpine
WORKDIR /app
COPY package*.json ./
RUN npm ci --omit=dev
COPY src/ ./src/
COPY views/ ./views/
COPY public/ ./public/
EXPOSE 8259
ENV NODE_ENV=production
CMD ["node", "src/index.js"]
