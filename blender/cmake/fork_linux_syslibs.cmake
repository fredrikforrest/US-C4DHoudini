# Fork build configuration: Blender 5.2.x on Ubuntu 24.04 using distro packages.
#
# Why this exists: Blender's official builds use precompiled libraries from
# projects.blender.org (git submodule lib/linux_x64). When that host is not
# reachable, or when you want to build against system libraries, this config
# turns off every feature whose dependency Ubuntu 24.04 cannot satisfy and
# keeps everything else on.
#
# Usage:
#   cmake -G Ninja -C <this file> -S <blender source> -B <build dir>
#
# Feature status with Ubuntu 24.04 packages (see ../README.md for details):
#   ON : UI, geometry/shader/compositor nodes, EEVEE, Workbench, animation,
#        sculpt/paint, VSE, OBJ/PLY/STL/FBX/glTF-core IO, FFmpeg video, audio,
#        fluids (mantaflow), ocean, remesh, Freestyle, Bullet, motion tracking
#   OFF: Cycles (needs OpenImageIO 3.x/Embree 4.4/OSL 1.15), OpenSubdiv,
#        OpenVDB (apt has v10, Blender wants v13), USD/Hydra/Alembic/MaterialX,
#        Vulkan backend (shaderc too old), Manifold boolean, Haru PDF export,
#        OpenXR (SDK too old), OpenImageDenoise, Draco/meshoptimizer bridges,
#        SDL (Blender uses SDL3), LLVM.

set(WITH_LIBS_PRECOMPILED    OFF CACHE BOOL "" FORCE)
# Ubuntu's OpenColorIO CMake package is broken (it resolves its include dir to
# "/include"), so point CMake at the shim next to this file instead.
set(OpenColorIO_DIR          ${CMAKE_CURRENT_LIST_DIR}/ubuntu-ocio-shim CACHE PATH "" FORCE)
set(WITH_STRICT_BUILD_OPTIONS OFF CACHE BOOL "" FORCE)
set(CMAKE_BUILD_TYPE         Release CACHE STRING "" FORCE)

# Python: use the system interpreter; do not bundle one.
set(PYTHON_VERSION           3.13 CACHE STRING "" FORCE)
set(WITH_PYTHON_INSTALL      OFF CACHE BOOL "" FORCE)
set(WITH_PYTHON_INSTALL_NUMPY OFF CACHE BOOL "" FORCE)
set(WITH_PYTHON_INSTALL_ZSTANDARD OFF CACHE BOOL "" FORCE)

# Render engines / GPU backends
set(WITH_CYCLES              OFF CACHE BOOL "" FORCE)
set(WITH_CYCLES_OSL          OFF CACHE BOOL "" FORCE)
set(WITH_CYCLES_EMBREE       OFF CACHE BOOL "" FORCE)
set(WITH_CYCLES_PATH_GUIDING OFF CACHE BOOL "" FORCE)
set(WITH_CYCLES_DEVICE_OPTIX OFF CACHE BOOL "" FORCE)
set(WITH_CYCLES_DEVICE_CUDA  OFF CACHE BOOL "" FORCE)
set(WITH_CYCLES_DEVICE_HIP   OFF CACHE BOOL "" FORCE)
set(WITH_CYCLES_DEVICE_ONEAPI OFF CACHE BOOL "" FORCE)
set(WITH_VULKAN_BACKEND      OFF CACHE BOOL "" FORCE)
set(WITH_OPENGL_BACKEND      ON  CACHE BOOL "" FORCE)
set(WITH_OPENIMAGEDENOISE    OFF CACHE BOOL "" FORCE)
set(WITH_LLVM                OFF CACHE BOOL "" FORCE)

# Libraries Ubuntu 24.04 does not ship (or ships too old)
set(WITH_OPENSUBDIV          OFF CACHE BOOL "" FORCE)
set(WITH_OPENVDB             OFF CACHE BOOL "" FORCE)
set(WITH_OPENVDB_BLOSC       OFF CACHE BOOL "" FORCE)
set(WITH_NANOVDB             OFF CACHE BOOL "" FORCE)
set(WITH_USD                 OFF CACHE BOOL "" FORCE)
set(WITH_HYDRA               OFF CACHE BOOL "" FORCE)
set(WITH_ALEMBIC             OFF CACHE BOOL "" FORCE)
set(WITH_MATERIALX           OFF CACHE BOOL "" FORCE)
set(WITH_MANIFOLD            OFF CACHE BOOL "" FORCE)
set(WITH_HARU                OFF CACHE BOOL "" FORCE)
set(WITH_XR_OPENXR           OFF CACHE BOOL "" FORCE)
set(WITH_DRACO               OFF CACHE BOOL "" FORCE)
set(WITH_MESHOPTIMIZER       OFF CACHE BOOL "" FORCE)
set(WITH_SDL                 OFF CACHE BOOL "" FORCE)
set(WITH_SDL_AUDIO           OFF CACHE BOOL "" FORCE)
set(WITH_JACK                OFF CACHE BOOL "" FORCE)

# Everything below is available from apt and stays ON
set(WITH_AUDASPACE           ON  CACHE BOOL "" FORCE)
set(WITH_OPENAL              ON  CACHE BOOL "" FORCE)
set(WITH_CODEC_SNDFILE       ON  CACHE BOOL "" FORCE)
set(WITH_CODEC_FFMPEG        ON  CACHE BOOL "" FORCE)
set(WITH_FFTW3               ON  CACHE BOOL "" FORCE)
set(WITH_GMP                 ON  CACHE BOOL "" FORCE)
set(WITH_PUGIXML             ON  CACHE BOOL "" FORCE)
set(WITH_POTRACE             ON  CACHE BOOL "" FORCE)
set(WITH_IMAGE_WEBP          ON  CACHE BOOL "" FORCE)
set(WITH_IMAGE_OPENJPEG      ON  CACHE BOOL "" FORCE)
set(WITH_IMAGE_CINEON        ON  CACHE BOOL "" FORCE)
set(WITH_INPUT_NDOF          ON  CACHE BOOL "" FORCE)
set(WITH_INTERNATIONAL       ON  CACHE BOOL "" FORCE)
set(WITH_MOD_FLUID           ON  CACHE BOOL "" FORCE)
set(WITH_MOD_OCEANSIM        ON  CACHE BOOL "" FORCE)
set(WITH_MOD_REMESH          ON  CACHE BOOL "" FORCE)
set(WITH_FREESTYLE           ON  CACHE BOOL "" FORCE)
set(WITH_BULLET              ON  CACHE BOOL "" FORCE)
set(WITH_LIBMV               ON  CACHE BOOL "" FORCE)
set(WITH_QUADRIFLOW          ON  CACHE BOOL "" FORCE)
set(WITH_IO_WAVEFRONT_OBJ    ON  CACHE BOOL "" FORCE)
set(WITH_IO_PLY              ON  CACHE BOOL "" FORCE)
set(WITH_IO_STL              ON  CACHE BOOL "" FORCE)
set(WITH_IO_FBX              ON  CACHE BOOL "" FORCE)
set(WITH_IO_GREASE_PENCIL    ON  CACHE BOOL "" FORCE)
set(WITH_TBB                 ON  CACHE BOOL "" FORCE)
set(WITH_BUILDINFO           ON  CACHE BOOL "" FORCE)
set(WITH_GHOST_X11           ON  CACHE BOOL "" FORCE)
set(WITH_GHOST_WAYLAND       ON  CACHE BOOL "" FORCE)
set(WITH_INSTALL_PORTABLE    ON  CACHE BOOL "" FORCE)

# Faster builds
set(WITH_UNITY_BUILD         ON  CACHE BOOL "" FORCE)
set(WITH_LINKER_LLD          ON  CACHE BOOL "" FORCE)
