#!/bin/sh
# Build the oracle DOSBox-X: upstream dosbox-x-v2026.08.02 + oracle-dosbox-x.patch only.
# usage: tools/harness/build-oracle.sh [BUILD_DIR]   (default ~/.cache/zel-oracle/dosbox-x)
#   INSTALL_DEPS=1  apt-get the Debian build dependencies first (needs root)
# Result: BUILD_DIR/src/dosbox-x, which tools/oracle.sh finds by default.
# Heavy: run it on a claude-test droplet, not on pro.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DIR=${1:-$HOME/.cache/zel-oracle/dosbox-x}
TAG=dosbox-x-v2026.08.02
if [ "${INSTALL_DEPS:-0}" = 1 ]; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq --no-install-recommends build-essential autoconf automake libtool \
    pkg-config git ca-certificates libsdl2-dev libsdl2-net-dev libpng-dev zlib1g-dev libgl-dev \
    libncursesw5-dev ccache
fi
if [ ! -d "$DIR/.git" ]; then
  mkdir -p "$(dirname "$DIR")"
  git clone -q --depth 1 --branch "$TAG" https://github.com/joncampbell123/dosbox-x "$DIR"
fi
cd "$DIR"
git checkout -q -- . && git clean -qfd src/misc   # back to the pristine tag sources
git apply --whitespace=nowarn "$HERE/oracle-dosbox-x.patch"
CC=gcc; CXX=g++
command -v ccache >/dev/null && { CC="ccache gcc"; CXX="ccache g++"; }
[ -f configure ] || ./autogen.sh >/dev/null 2>&1
[ -f Makefile ] || CC="$CC" CXX="$CXX" ./configure --enable-sdl2 > configure.log 2>&1
make -j"$(nproc)" > make.log 2>&1 || { tail -30 make.log; exit 1; }
echo "built $DIR/src/dosbox-x"
