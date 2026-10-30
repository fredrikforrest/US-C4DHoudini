#!/usr/bin/env bash
# Fetch and install the Eigen snapshot Blender pins (header-only).
#
# Ubuntu 24.04 ships Eigen 3.4.0, but Blender 5.2's motion-tracking code
# (intern/libmv) uses the templated `jacobiSvd<Options>()` API that only exists
# in Eigen >= 3.4.90 / 5.x. The commit, URL and SHA-256 below are copied from
# build_files/build_environment/cmake/versions.cmake in the Blender tree.
#
#   scripts/fetch_eigen.sh [install-prefix]   (default: $HOME/blender-fork/deps/eigen)
set -euo pipefail
PREFIX="${1:-$HOME/blender-fork/deps/eigen}"
EIGEN_COMMIT=8a1083e9bf41b91fdea6546681f806154efdc25a
EIGEN_SHA256=cc28a84fdec496c6777596350ea805519bf10f717d21044ae6ba3dd562183a26
URL="https://gitlab.com/libeigen/eigen/-/archive/${EIGEN_COMMIT}/eigen-${EIGEN_COMMIT}.tar.gz"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
curl -sSL --fail -o "$WORK/eigen.tar.gz" "$URL"
echo "${EIGEN_SHA256}  $WORK/eigen.tar.gz" | sha256sum -c -
mkdir -p "$WORK/src" && tar -xzf "$WORK/eigen.tar.gz" -C "$WORK/src" --strip-components=1
cmake -S "$WORK/src" -B "$WORK/build" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DEIGEN_BUILD_DOC=OFF -DEIGEN_BUILD_TESTING=OFF -DBUILD_TESTING=OFF -DEIGEN_BUILD_BLAS=OFF -DEIGEN_BUILD_LAPACK=OFF -DEIGEN_BUILD_DEMOS=OFF > /dev/null
cmake --install "$WORK/build" > /dev/null
echo "Eigen installed to $PREFIX"
echo "Pass -DEigen3_DIR=$PREFIX/share/eigen3/cmake to CMake (scripts/build.sh does this when EIGEN_PREFIX is set)."
