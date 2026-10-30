#!/usr/bin/env bash
# Install everything needed to build the Blender fork on Ubuntu 24.04 from
# distro packages (no precompiled Blender libraries). Tested 2026-10-30
# against Blender 5.2.3 with cmake/fork_linux_syslibs.cmake.
#
# Run as root or with sudo:  sudo ./bootstrap_ubuntu2404.sh
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update

# Toolchain. Blender 5.2 requires GCC >= 14 (or Clang >= 17).
apt-get install -y --no-install-recommends \
  build-essential gcc-14 g++-14 cmake ninja-build pkg-config patch git git-lfs lld

# Windowing / input / GPU
apt-get install -y --no-install-recommends \
  libx11-dev libxxf86vm-dev libxcursor-dev libxi-dev libxrandr-dev libxinerama-dev \
  libxkbcommon-dev libxrender-dev libxfixes-dev \
  libwayland-dev wayland-protocols libdecor-0-dev libdbus-1-dev \
  libegl-dev libgl-dev libgles-dev libepoxy-dev libspnav-dev

# Images, fonts, compression
apt-get install -y --no-install-recommends \
  libfreetype-dev libbrotli-dev libharfbuzz-dev libfribidi-dev \
  libjpeg-dev libpng-dev libtiff-dev libwebp-dev libopenjp2-7-dev \
  zlib1g-dev libzstd-dev libdeflate-dev \
  libimath-dev libopenexr-dev libopenimageio-dev openimageio-tools libopencv-core-dev \
  libopencolorio-dev libpystring-dev

# Core C++ libraries
apt-get install -y --no-install-recommends \
  libfmt-dev libtbb-dev libpugixml-dev libfftw3-dev libgmp-dev libpotrace-dev \
  libjemalloc-dev libeigen3-dev libceres-dev libgoogle-glog-dev libgflags-dev \
  libxml2-dev libexpat1-dev libsqlite3-dev

# Audio / video
apt-get install -y --no-install-recommends \
  libopenal-dev libsndfile1-dev librubberband-dev libpulse-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libavdevice-dev

# Python 3.13 (the version Blender 5.2 is written against) plus NumPy.
# Ubuntu 24.04's python3-numpy targets 3.12, so NumPy for 3.13 comes from pip.
apt-get install -y --no-install-recommends python3.13-dev python3.13-venv libpython3.13-dev
python3.13 -m pip install --quiet numpy

echo "Done. Next: scripts/build.sh"
