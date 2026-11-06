#!/usr/bin/env bash
# Cross-build the Windows release zip from Linux with mingw-w64:
#
#   build/win/zeliard-<ver>-windows-x64.zip   (zeliard.exe, SDL2.dll, README.txt, LICENSE)
#
#   packaging/windows/build.sh [version]
#
# Needs: x86_64-w64-mingw32-gcc (apt: gcc-mingw-w64-x86-64), make, curl, zip,
# sha256sum, objdump.  SDL2 is the pinned mingw dev release from libsdl.org's
# GitHub releases, downloaded once into build/win/dl and checksum-verified.
# The build happens in a scratch copy of port/, so a Linux build tree is never
# disturbed.  No game data is read or packaged.
#
# Save sync (#55/#57): libcurl is NOT bundled; the Windows build links
# net_winhttp.c (WinHTTP is part of Windows, so no extra DLL).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="${OUT:-$REPO/build/win}"
TRIPLE=x86_64-w64-mingw32
CC="${CC:-$TRIPLE-gcc}"

SDL_VER=2.30.9
SDL_TGZ="SDL2-devel-$SDL_VER-mingw.tar.gz"
SDL_URL="${SDL_URL:-https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/$SDL_TGZ}"
SDL_SHA256=b188d165dc4a0372042bff3d0c18e9bb90772581e92a7b8b47b521b47714f75c

VERSION="${1:-${ZELIARD_VERSION:-}}"
[ -n "$VERSION" ] || VERSION="$(git -C "$REPO" describe --tags --always --dirty 2>/dev/null || echo unknown)"
VERSION="${VERSION#v}"
VERSION="$(echo "$VERSION" | tr -c '0-9A-Za-z.+~\n-' '.')"
echo "version: $VERSION"

mkdir -p "$OUT/dl"
if [ ! -f "$OUT/dl/$SDL_TGZ" ]; then
  echo "fetching $SDL_TGZ"
  curl -fsSL -o "$OUT/dl/$SDL_TGZ" "$SDL_URL"
fi
echo "$SDL_SHA256  $OUT/dl/$SDL_TGZ" | sha256sum -c -
rm -rf "$OUT/sdl"; mkdir -p "$OUT/sdl"
tar -xzf "$OUT/dl/$SDL_TGZ" -C "$OUT/sdl"
SDL="$OUT/sdl/SDL2-$SDL_VER/$TRIPLE"

# build in a scratch copy of port/ (objects are per-platform)
rm -rf "$OUT/port"; mkdir -p "$OUT/port"
tar -C "$REPO/port" --exclude='*.o' --exclude=local.mk \
    --exclude=zeliard -cf - . | tar -C "$OUT/port" -xf -
rm -f "$OUT/port/local.mk"

make -C "$OUT/port" -j"$(nproc)" zeliard CC="$CC" \
  SDL_CFLAGS="-I$SDL/include/SDL2 -Dmain=SDL_main" \
  SDL_LIBS="-L$SDL/lib -lmingw32 -lSDL2main -lSDL2 -static-libgcc" \
  CURL_CFLAGS= CURL_LIBS= WINHTTP=1
EXE="$OUT/port/zeliard.exe"
[ -f "$EXE" ] || { echo "no zeliard.exe produced" >&2; exit 1; }
"$TRIPLE-strip" "$EXE"

# sanity: only Windows system DLLs plus the bundled SDL2.dll
BAD=$("$TRIPLE-objdump" -p "$EXE" | awk '/DLL Name:/{print tolower($3)}' \
  | grep -Evx 'sdl2\.dll|kernel32\.dll|msvcrt\.dll|user32\.dll|gdi32\.dll|shell32\.dll|advapi32\.dll|ole32\.dll|oleaut32\.dll|winmm\.dll|imm32\.dll|setupapi\.dll|version\.dll|ws2_32\.dll|winhttp\.dll|api-ms-win-crt-.*\.dll|ucrtbase\.dll' || true)
if [ -n "$BAD" ]; then echo "unexpected DLL dependencies:" >&2; echo "$BAD" >&2; exit 1; fi
"$TRIPLE-objdump" -p "$EXE" | grep "DLL Name"

# stage and zip
STAGE="$OUT/stage/zeliard-$VERSION-windows-x64"
rm -rf "$OUT/stage"; mkdir -p "$STAGE"
cp "$EXE" "$STAGE/zeliard.exe"
cp "$SDL/bin/SDL2.dll" "$STAGE/"
cp "$REPO/LICENSE" "$STAGE/LICENSE"
cp "$SDL/../LICENSE.txt" "$STAGE/SDL2-LICENSE.txt"
sed "s/@VERSION@/$VERSION/" "$HERE/README.txt" | sed 's/$/\r/' > "$STAGE/README.txt"
ZIP="$OUT/zeliard-$VERSION-windows-x64.zip"
rm -f "$ZIP"
(cd "$OUT/stage" && zip -qr "$ZIP" "zeliard-$VERSION-windows-x64")
unzip -l "$ZIP"
echo "$ZIP"
