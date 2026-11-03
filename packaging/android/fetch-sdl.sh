#!/usr/bin/env bash
# Fetch the pinned SDL2 release source into packaging/android/sdl/ and copy its
# Java glue (SDLActivity & co) into the app.  The version matches the Windows
# build (packaging/windows/build.sh); the checksum is of the official
# libsdl-org release tarball.  Needs: curl, sha256sum, tar.
set -euo pipefail
cd "$(dirname "$0")"
SDL_VER=2.30.9
SDL_SHA256=24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4
TGZ="SDL2-$SDL_VER.tar.gz"
mkdir -p sdl
[ -f "sdl/$TGZ" ] || curl -fsSL -o "sdl/$TGZ" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/$TGZ"
echo "$SDL_SHA256  sdl/$TGZ" | sha256sum -c -
rm -rf "sdl/SDL2-$SDL_VER" sdl/src
tar xzf "sdl/$TGZ" -C sdl
mv "sdl/SDL2-$SDL_VER" sdl/src
JAVA=app/src/main/java/org/libsdl/app
rm -rf "$JAVA"; mkdir -p "$JAVA"
cp sdl/src/android-project/app/src/main/java/org/libsdl/app/*.java "$JAVA/"
echo "SDL $SDL_VER ready in packaging/android/sdl/src"
