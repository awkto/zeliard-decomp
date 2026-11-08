# Self-hosted web build: the wasm port behind nginx.  No game data is built in.
# Needs build/web/ from packaging/web/build.sh first (CI: the web job's artifact).
# To serve the game files too, mount a zip of the game folder at
#   /usr/share/nginx/html/data/game.zip
# and the page installs it on first visit (index.html, serverGame()).
#   packaging/web/build.sh && docker build -f packaging/web/Dockerfile -t zeliard-decomp-web .
FROM nginx:1.27-alpine
LABEL org.opencontainers.image.title="zeliard-decomp-web" \
      org.opencontainers.image.description="Zeliard C port (decomp) in the browser; game files not included"
COPY build/web/ /usr/share/nginx/html/
RUN mkdir -p /usr/share/nginx/html/data
EXPOSE 80
