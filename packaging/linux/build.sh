#!/usr/bin/env bash
# Build the Linux packages for port/zeliard in a debian:12 container (glibc 2.36,
# so the result runs on anything newer):
#
#   build/zeliard-decomp_<ver>_amd64.deb
#   build/zeliard-decomp-<ver>-x86_64.AppImage
#
#   packaging/linux/build.sh [version]
#
# Version: the argument, else $ZELIARD_VERSION, else `git describe` (a leading
# v is dropped; a non-release describe becomes 0.0.0~<describe> so dpkg sorts it
# below any real release).  A tag build in CI passes the tag.
#
# Needs only docker and curl on the host.  No game data is read or packaged.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$REPO/build"
IMAGE="${BUILD_IMAGE:-debian:12}"
APPIMAGETOOL_URL="${APPIMAGETOOL_URL:-https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage}"

VERSION="${1:-${ZELIARD_VERSION:-}}"
if [ -z "$VERSION" ]; then
  VERSION="$(git -C "$REPO" describe --tags --always --dirty 2>/dev/null || echo unknown)"
fi
VERSION="${VERSION#v}"
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([-+~.][0-9A-Za-z.+~-]+)?$ ]]; then
  VERSION="0.0.0~$(echo "$VERSION" | tr -c '0-9A-Za-z.+\n' '.')"
fi
echo "version: $VERSION"

mkdir -p "$OUT/tools"
AIT="$OUT/tools/appimagetool-x86_64.AppImage"
if [ ! -x "$AIT" ]; then
  echo "fetching appimagetool"
  curl -fsSL -o "$AIT" "$APPIMAGETOOL_URL"
  chmod +x "$AIT"
fi

# Source goes in read-only; the container builds in its own tmpfs-like dir.
docker run --rm --memory "${BUILD_MEM:-3g}" --cpus "${BUILD_CPUS:-2}" \
  -e VERSION="$VERSION" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
  -v "$REPO:/src:ro" -v "$OUT:/out" \
  "$IMAGE" bash /src/packaging/linux/container-build.sh

ls -l "$OUT"/zeliard-decomp_"$VERSION"_amd64.deb "$OUT"/zeliard-decomp-"$VERSION"-x86_64.AppImage
