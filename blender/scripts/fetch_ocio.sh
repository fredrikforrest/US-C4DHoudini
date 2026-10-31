#!/usr/bin/env bash
# Build and install the OpenColorIO release Blender pins.
#
# Blender 5.2 static-asserts OpenColorIO >= 2.3 (GPU shader binder) and uses
# 2.5 APIs behind version guards. Ubuntu 24.04 ships 2.1.3.
# Version from build_files/build_environment/cmake/versions.cmake (OPENCOLORIO_VERSION).
#
# Needs from apt: libyaml-cpp-dev libexpat1-dev libpystring-dev libimath-dev zlib1g-dev
# (all installed by bootstrap_ubuntu2404.sh). minizip-ng is not packaged by
# Ubuntu, so OCIO's build fetches and builds it itself (OCIO_INSTALL_EXT_PACKAGES=MISSING).
#
#   scripts/fetch_ocio.sh [install-prefix] [source-cache-dir]
set -euo pipefail
PREFIX="${1:-$HOME/blender-fork/deps/ocio}"
SRCDIR="${2:-$HOME/blender-fork/deps/src}"
OCIO_VERSION=2.5.0

mkdir -p "$SRCDIR"
SRC="$SRCDIR/ocio-${OCIO_VERSION}"
if [ ! -d "$SRC/.git" ]; then
  git clone --depth 1 --branch "v${OCIO_VERSION}" https://github.com/AcademySoftwareFoundation/OpenColorIO.git "$SRC"
fi
BUILD="$SRCDIR/ocio-${OCIO_VERSION}-build"
cmake -G Ninja -S "$SRC" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DOCIO_BUILD_APPS=OFF -DOCIO_BUILD_TESTS=OFF -DOCIO_BUILD_GPU_TESTS=OFF \
  -DOCIO_BUILD_PYTHON=OFF -DOCIO_BUILD_DOCS=OFF -DOCIO_BUILD_OPENFX=OFF \
  -DOCIO_INSTALL_EXT_PACKAGES=MISSING -DOCIO_USE_SOVERSION=ON
cmake --build "$BUILD" -j"$(nproc)"
cmake --install "$BUILD" > /dev/null
echo "OpenColorIO ${OCIO_VERSION} installed to $PREFIX"
echo "Pass -DOpenColorIO_DIR=$PREFIX/lib/cmake/OpenColorIO to Blender's CMake (scripts/build.sh does this)."
