#!/usr/bin/env bash
# Runs inside the debian:12 container started by build.sh.  /src is the repo
# (read-only), /out is the build/ directory.
set -euo pipefail
: "${VERSION:?}"
export DEBIAN_FRONTEND=noninteractive
echo force-unsafe-io > /etc/dpkg/dpkg.cfg.d/unsafe-io   # throwaway container: skip fsync
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential pkg-config libsdl2-dev \
  dpkg-dev file ca-certificates >/dev/null

# --- build ------------------------------------------------------------------
W=/tmp/w
rm -rf "$W"; mkdir -p "$W"
cp -a /src/port "$W/port"
rm -f "$W"/port/*.o "$W"/port/zeliard "$W"/port/local.mk
make -C "$W/port" -j"$(nproc)" zeliard >/dev/null
strip --strip-unneeded "$W/port/zeliard"
BIN="$W/port/zeliard"
P=/src/packaging/linux

# --- .deb -------------------------------------------------------------------
D="$W/deb/zeliard_${VERSION}_amd64"
install -d "$D/DEBIAN" "$D/usr/bin" "$D/usr/share/applications" \
  "$D/usr/share/icons/hicolor/256x256/apps" "$D/usr/share/icons/hicolor/scalable/apps" \
  "$D/usr/share/doc/zeliard"
install -m755 "$BIN" "$D/usr/bin/zeliard"
install -m644 "$P/zeliard.desktop" "$D/usr/share/applications/zeliard.desktop"
install -m644 "$P/zeliard.png" "$D/usr/share/icons/hicolor/256x256/apps/zeliard.png"
install -m644 "$P/zeliard.svg" "$D/usr/share/icons/hicolor/scalable/apps/zeliard.svg"
install -m644 /src/LICENSE "$D/usr/share/doc/zeliard/copyright"
install -m644 /src/port/README.md "$D/usr/share/doc/zeliard/README.md"
gzip -9n "$D/usr/share/doc/zeliard/README.md"
SDLDEP="libsdl2-2.0-0 (>= $(dpkg-query -W -f='${Version}' libsdl2-2.0-0 | sed 's/[+~].*//;s/-.*//'))"
SIZE=$(du -sk "$D/usr" | cut -f1)
cat > "$D/DEBIAN/control" <<EOF
Package: zeliard
Version: $VERSION
Section: games
Priority: optional
Architecture: amd64
Depends: libc6 (>= 2.34), $SDLDEP
Installed-Size: $SIZE
Maintainer: awkto <noreply@github.com>
Homepage: https://github.com/awkto/zeliard-decomp
Description: C port of the 1990 action RPG Zeliard
 An SDL2 re-implementation of Zeliard's engines that reads the original game
 files.  The game data is copyrighted and is NOT included: put your own copy
 in ~/.local/share/awkto/zeliard/game or run "zeliard --dir /path/to/files".
EOF
dpkg-deb --root-owner-group -Zxz -b "$D" "/out/zeliard_${VERSION}_amd64.deb" >/dev/null

# --- AppImage ---------------------------------------------------------------
A="$W/AppDir"
install -d "$A/usr/bin" "$A/usr/lib"
install -m755 "$BIN" "$A/usr/bin/zeliard"
# Bundle everything the binary links except the glibc family (and the loader),
# which must come from the host.  SDL2 dlopens X11/Wayland/audio itself, so
# those stay the host's, as for any SDL AppImage.
ldd "$BIN" | awk '/=> \//{print $3}' | grep -Ev '/(libc|libm|libdl|libpthread|librt|libresolv|libutil|libgcc_s|ld-linux[^/]*)\.so' \
  | while read -r so; do cp -L "$so" "$A/usr/lib/"; done
cat > "$A/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/zeliard" "$@"
EOF
chmod +x "$A/AppRun"
install -m644 "$P/zeliard.desktop" "$A/zeliard.desktop"
sed -i 's/^Exec=.*/Exec=zeliard/' "$A/zeliard.desktop"
install -m644 "$P/zeliard.png" "$A/zeliard.png"
ln -s zeliard.png "$A/.DirIcon"
ARCH=x86_64 /out/tools/appimagetool-x86_64.AppImage --appimage-extract-and-run \
  --no-appstream "$A" "/out/Zeliard-${VERSION}-x86_64.AppImage" >/dev/null 2>&1 \
  || ARCH=x86_64 /out/tools/appimagetool-x86_64.AppImage --appimage-extract-and-run --no-appstream "$A" "/out/Zeliard-${VERSION}-x86_64.AppImage"
chown "$HOST_UID:$HOST_GID" /out/zeliard_*.deb /out/Zeliard-*.AppImage
