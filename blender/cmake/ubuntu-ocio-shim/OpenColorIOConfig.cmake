# Minimal OpenColorIO package shim for Ubuntu 24.04's libopencolorio-dev.
# The distro installs OpenColorIOConfig.cmake flat into /usr/share/cmake, and
# that file computes its install prefix relative to its own location, which
# resolves to "/" and yields a bogus "/include" directory. This shim defines
# the same imported target with the real paths instead.
if(TARGET OpenColorIO::OpenColorIO)
  return()
endif()
find_path(_ocio_inc OpenColorIO/OpenColorIO.h PATHS /usr/include REQUIRED)
find_library(_ocio_lib NAMES OpenColorIO PATHS /usr/lib/x86_64-linux-gnu /usr/lib REQUIRED)
add_library(OpenColorIO::OpenColorIO SHARED IMPORTED)
set_target_properties(OpenColorIO::OpenColorIO PROPERTIES
  IMPORTED_LOCATION "${_ocio_lib}"
  INTERFACE_INCLUDE_DIRECTORIES "${_ocio_inc}"
)
# Values Blender's build reads from the real package.
set(OpenColorIO_FOUND TRUE)
set(OPENCOLORIO_FOUND TRUE)
set(OPENCOLORIO_INCLUDE_DIRS "${_ocio_inc}")
set(OPENCOLORIO_LIBRARIES "${_ocio_lib}")
