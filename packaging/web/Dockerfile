# Self-hosted web build: the wasm port behind nginx.  No game data is built in.
# To serve the game files too, mount a zip of the game folder at
#   /usr/share/nginx/html/data/game.zip
# and the page installs it on first visit (index.html, serverGame()).
#   docker build -f packaging/web/Dockerfile -t zeliard-decomp-web .
FROM emscripten/emsdk:3.1.64 AS build
WORKDIR /src
COPY port port
COPY packaging/web packaging/web
RUN rm -f port/local.mk && ./packaging/web/build.sh

FROM nginx:1.27-alpine
LABEL org.opencontainers.image.title="zeliard-decomp-web" \
      org.opencontainers.image.description="Zeliard C port (decomp) in the browser; game files not included"
COPY --from=build /src/build/web/ /usr/share/nginx/html/
RUN mkdir -p /usr/share/nginx/html/data
EXPOSE 80
