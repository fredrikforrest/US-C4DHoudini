#!/usr/bin/env bash
# Build and install the OpenImageIO release Blender pins.
#
# Ubuntu 24.04 ships OpenImageIO 2.4 whose headers carry a private copy of an
# old fmt; including them together with fmt 12 (which Blender needs) breaks the
# compile. Building the 3.1 release Blender targets, against the same external
# fmt, removes the clash and also matches the API Blender's image code expects.
# Version from build_files/build_environment/cmake/versions.cmake (OPENIMAGEIO_VERSION).
#
# Needs from apt: libopenexr-dev libimath-dev libtiff-dev libpng-dev libjpeg-dev
# libwebp-dev libopenjp2-7-dev libpugixml-dev libtbb-dev libdeflate-dev zlib1g-dev
# (installed by bootstrap_ubuntu2404.sh). robin-map is fetched by OIIO itself.
#
#   scripts/fetch_oiio.sh [install-prefix] [source-cache-dir] [fmt-prefix] [ocio-prefix]
set -euo pipefail
PREFIX="${1:-$HOME/blender-fork/deps/oiio}"
SRCDIR="${2:-$HOME/blender-fork/deps/src}"
FMT_PREFIX="${3:-$HOME/blender-fork/deps/fmt}"
OCIO_PREFIX="${4:-$HOME/blender-fork/deps/ocio}"
OIIO_VERSION=3.1.13.1

mkdir -p "$SRCDIR"
SRC="$SRCDIR/oiio-${OIIO_VERSION}"
if [ ! -d "$SRC/.git" ]; then
  git clone --depth 1 --branch "v${OIIO_VERSION}" https://github.com/AcademySoftwareFoundation/OpenImageIO.git "$SRC"
fi
BUILD="$SRCDIR/oiio-${OIIO_VERSION}-build"
cmake -G Ninja -S "$SRC" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_SHARED_LIBS=ON -DOIIO_BUILD_TOOLS=OFF -DOIIO_BUILD_TESTS=OFF -DBUILD_TESTING=OFF \
  -DUSE_PYTHON=OFF -DUSE_QT=OFF -DUSE_OPENCV=OFF -DUSE_FFMPEG=OFF -DUSE_FREETYPE=OFF \
  -DUSE_LIBHEIF=OFF -DUSE_GIF=OFF -DUSE_PTEX=OFF -DUSE_LIBRAW=OFF -DUSE_JXL=OFF -DUSE_DCMTK=OFF -DUSE_NUKE=OFF \
  -DUSE_TBB=ON -DUSE_WEBP=ON -DUSE_OPENJPEG=ON -DUSE_OPENCOLORIO=ON -DUSE_EXTERNAL_PUGIXML=ON \
  -DOIIO_INTERNALIZE_FMT=OFF -Dfmt_DIR="$FMT_PREFIX/lib/cmake/fmt" \
  -DOpenColorIO_DIR="$OCIO_PREFIX/lib/cmake/OpenColorIO" \
  -DOpenImageIO_BUILD_MISSING_DEPS=all
cmake --build "$BUILD" -j"$(nproc)"
cmake --install "$BUILD" > /dev/null
echo "OpenImageIO ${OIIO_VERSION} installed to $PREFIX"
echo "Pass -DOpenImageIO_DIR=$PREFIX/lib/cmake/OpenImageIO to Blender's CMake (scripts/build.sh does this)."
