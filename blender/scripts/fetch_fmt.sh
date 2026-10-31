#!/usr/bin/env bash
# Build and install the {fmt} release Blender pins, as a static library.
#
# Blender 5.2 formats its own types through `format_as()` overloads, which fmt
# only understands from version 10. Ubuntu 24.04 ships fmt 9.1.
# Version from build_files/build_environment/cmake/versions.cmake (FMT_VERSION).
#
#   scripts/fetch_fmt.sh [install-prefix]   (default: $HOME/blender-fork/deps/fmt)
set -euo pipefail
PREFIX="${1:-$HOME/blender-fork/deps/fmt}"
FMT_VERSION=12.1.0
URL="https://github.com/fmtlib/fmt/releases/download/${FMT_VERSION}/fmt-${FMT_VERSION}.zip"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
curl -sSL --fail -o "$WORK/fmt.zip" "$URL"
python3 -I -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$WORK/fmt.zip" "$WORK"
cmake -S "$WORK/fmt-${FMT_VERSION}" -B "$WORK/build" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
  -DFMT_TEST=OFF -DFMT_DOC=OFF > /dev/null
cmake --build "$WORK/build" -j"$(nproc)" > /dev/null
cmake --install "$WORK/build" > /dev/null
echo "fmt ${FMT_VERSION} installed to $PREFIX"
