#!/usr/bin/env bash
# Web build of the C port with Emscripten (ASYNCIFY, SDL2 port, no libcurl):
#
#   packaging/web/build.sh        # run with emcc/emmake on PATH (CI uses the emsdk container)
#
# Output: build/web/{index.html,zeliard.js,zeliard.wasm}.  Code only: no game
# data is read or shipped; the player picks their own folder in the page.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$REPO/build/web"
cd "$REPO/port"
rm -f *.o zeliard.js zeliard.wasm
# SDL comes from -sUSE_SDL=2; net_fetch.c is the network backend; local.mk is ignored
# CFLAGS goes in the environment: a command-line CFLAGS would also override the
# Makefile's per-target `CFLAGS +=` (miniz's include path)
CFLAGS="-std=c11 -O2 -Wall -Wno-unused-parameter" emmake make zeliard.js CC=emcc ${WEB_ASYNC:+WEB_ASYNC="$WEB_ASYNC"} \
  SDL_CFLAGS="-sUSE_SDL=2" SDL_LIBS="-sUSE_SDL=2" CURL_CFLAGS= CURL_LIBS=
rm -rf "$OUT"; mkdir -p "$OUT"
cp zeliard.js zeliard.wasm "$OUT/"
cp "$HERE/index.html" "$HERE/keys.js" "$OUT/"
ls -l "$OUT"
