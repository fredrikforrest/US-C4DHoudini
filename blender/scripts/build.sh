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
# Eigen snapshot installed by scripts/fetch_eigen.sh (Ubuntu's Eigen 3.4.0 is too old).
EIGEN_PREFIX="${EIGEN_PREFIX:-$HOME/blender-fork/deps/eigen}"
if [ ! -f "$EIGEN_PREFIX/share/eigen3/cmake/Eigen3Config.cmake" ]; then
  "$HERE/fetch_eigen.sh" "$EIGEN_PREFIX"
fi

NUMPY_INC="$(python3.13 -c 'import numpy; print(numpy.get_include())')"
NUMPY_PATH="$(dirname "$(dirname "$NUMPY_INC")")"   # .../site-packages/numpy/_core/include -> site-packages

mkdir -p "$BUILD"
cmake -G Ninja \
  -C "$HERE/../cmake/fork_linux_syslibs.cmake" \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DPYTHON_NUMPY_PATH="$(dirname "$NUMPY_PATH")" \
  -DPYTHON_NUMPY_INCLUDE_DIRS="$NUMPY_INC" \
  -DEigen3_DIR="$EIGEN_PREFIX/share/eigen3/cmake" \
  -DCMAKE_PREFIX_PATH="$EIGEN_PREFIX" \
  -S "$SRC" -B "$BUILD"

ninja -C "$BUILD" -j"$JOBS"
echo
echo "Built: $BUILD/bin/blender"
"$BUILD/bin/blender" --version
