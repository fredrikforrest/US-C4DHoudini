#!/usr/bin/env bash
# Configure and build the Blender fork against Ubuntu 24.04 system libraries.
#
#   scripts/build.sh [blender-source-dir] [build-dir]
#
# Defaults assume the layout used while preparing this fork:
#   <workspace>/blender   Blender source checkout (your fork)
#   <workspace>/build     out-of-tree build directory
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${1:-$HOME/blender-fork/blender}"
BUILD="${2:-$HOME/blender-fork/build}"
JOBS="${JOBS:-$(nproc)}"
# Libraries Ubuntu 24.04 ships too old, built by scripts/fetch_*.sh on first use.
DEPS="${DEPS:-$HOME/blender-fork/deps}"
EIGEN_PREFIX="${EIGEN_PREFIX:-$DEPS/eigen}"   # Eigen 3.4.0 -> pinned 5.x snapshot
FMT_PREFIX="${FMT_PREFIX:-$DEPS/fmt}"         # fmt 9.1 -> 12.1 (format_as support)
OCIO_PREFIX="${OCIO_PREFIX:-$DEPS/ocio}"      # OpenColorIO 2.1.3 -> 2.5.0
OIIO_PREFIX="${OIIO_PREFIX:-$DEPS/oiio}"      # OpenImageIO 2.4 -> 3.1.13.1
[ -f "$EIGEN_PREFIX/share/eigen3/cmake/Eigen3Config.cmake" ] || "$HERE/fetch_eigen.sh" "$EIGEN_PREFIX"
[ -f "$FMT_PREFIX/lib/cmake/fmt/fmt-config.cmake" ]          || "$HERE/fetch_fmt.sh" "$FMT_PREFIX"
[ -f "$OCIO_PREFIX/lib/cmake/OpenColorIO/OpenColorIOConfig.cmake" ] || "$HERE/fetch_ocio.sh" "$OCIO_PREFIX" "$DEPS/src"
[ -f "$OIIO_PREFIX/lib/cmake/OpenImageIO/OpenImageIOConfig.cmake" ] || "$HERE/fetch_oiio.sh" "$OIIO_PREFIX" "$DEPS/src" "$FMT_PREFIX" "$OCIO_PREFIX"

NUMPY_INC="$(python3.13 -c 'import numpy; print(numpy.get_include())')"
NUMPY_PATH="$(dirname "$(dirname "$NUMPY_INC")")"   # .../site-packages/numpy/_core/include -> site-packages

mkdir -p "$BUILD"
cmake -G Ninja \
  -C "$HERE/../cmake/fork_linux_syslibs.cmake" \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DPYTHON_NUMPY_PATH="$(dirname "$NUMPY_PATH")" \
  -DPYTHON_NUMPY_INCLUDE_DIRS="$NUMPY_INC" \
  -DEigen3_DIR="$EIGEN_PREFIX/share/eigen3/cmake" \
  -Dfmt_DIR="$FMT_PREFIX/lib/cmake/fmt" \
  -DOpenColorIO_DIR="$OCIO_PREFIX/lib/cmake/OpenColorIO" \
  -DOpenImageIO_DIR="$OIIO_PREFIX/lib/cmake/OpenImageIO" \
  -DCMAKE_PREFIX_PATH="$EIGEN_PREFIX;$FMT_PREFIX;$OCIO_PREFIX;$OIIO_PREFIX" \
  -S "$SRC" -B "$BUILD"

ninja -C "$BUILD" -j"$JOBS"
echo
echo "Built: $BUILD/bin/blender"
echo "OpenColorIO and OpenImageIO are shared libraries outside the system path; run with:"
echo "  LD_LIBRARY_PATH=$OCIO_PREFIX/lib:$OIIO_PREFIX/lib $BUILD/bin/blender"
LD_LIBRARY_PATH="$OCIO_PREFIX/lib:$OIIO_PREFIX/lib" "$BUILD/bin/blender" --version
