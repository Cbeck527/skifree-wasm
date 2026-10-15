# Toolchains and npm inputs are pinned by the flake and package lock.
FROM nixos/nix:2.31.2@sha256:29fc5fe207f159ceb0143c25c19c774062fee02ce5eda118f3067547b3054894 AS build
RUN printf '%s\n' 'experimental-features = nix-command flakes' 'sandbox = false' >> /etc/nix/nix.conf
ENV EM_CACHE=/tmp/emscripten-cache
WORKDIR /app
COPY flake.nix flake.lock package.json package-lock.json ./
RUN nix develop .#build --command true
COPY Makefile ./
COPY src/ src/
COPY tools/gen_assets.py tools/gen_assets.py
COPY web/ web/
RUN nix develop .#build --command make web server

FROM node:24-bookworm-slim@sha256:a5f4366e834695d5f363b920c104953a535d3a9864d1bdf74566a0ebe01daee5 AS dependencies
WORKDIR /app
COPY package.json package-lock.json ./
# Do not copy Nix-store symlinks or install the Railway development SDK here.
RUN npm ci --omit=dev --ignore-scripts --no-audit --no-fund

FROM node:24-bookworm-slim@sha256:a5f4366e834695d5f363b920c104953a535d3a9864d1bdf74566a0ebe01daee5 AS runtime
ENV NODE_ENV=production
WORKDIR /app
COPY --from=dependencies --chown=node:node /app/node_modules ./node_modules
COPY --chown=node:node package.json LICENSE ./
COPY --chown=node:node server/server.mjs server/server.mjs
COPY --from=build --chown=node:node /app/build/web/ build/web/
COPY --from=build --chown=node:node /app/build/server/ build/server/
USER node
# Railway supplies PORT; 8000 is only the server's local default.
EXPOSE 8000
CMD ["node", "server/server.mjs"]
