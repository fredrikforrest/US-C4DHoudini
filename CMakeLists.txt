cmake_minimum_required(VERSION 3.16)
project(US_C4DHoudini CXX)

# Builds the host-neutral core (hapi_core), the command line probe and the
# unit tests. The Cinema 4D plugin itself is built with Maxon's project tool
# from src/c4d_plugin (see README.md); it links hapi_core's sources directly.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# HAPI headers are not vendored (they are SideFX's). Point at a Houdini
# install's toolkit/include, or at the copy inside HoudiniEngineForUnreal.
set(HAPI_INCLUDE_DIR "$ENV{HFS}/toolkit/include" CACHE PATH "Directory containing HAPI/HAPI.h")
if(NOT EXISTS "${HAPI_INCLUDE_DIR}/HAPI/HAPI.h")
  message(FATAL_ERROR "HAPI/HAPI.h not found under HAPI_INCLUDE_DIR='${HAPI_INCLUDE_DIR}'. "
                      "Set -DHAPI_INCLUDE_DIR=<HFS>/toolkit/include")
endif()

add_library(hapi_core STATIC
  src/hapi_core/HapiLibrary.cpp
  src/hapi_core/HapiSession.cpp
  src/hapi_core/HapiParms.cpp
  src/hapi_core/HapiAsset.cpp
  src/hapi_core/HapiGeometry.cpp)
target_include_directories(hapi_core PUBLIC src/hapi_core "${HAPI_INCLUDE_DIR}")
target_compile_definitions(hapi_core PUBLIC HAPI_SUPPRESS_DEPRECATIONS)
if(NOT WIN32)
  target_link_libraries(hapi_core PUBLIC ${CMAKE_DL_LIBS} pthread)
endif()
if(MSVC)
  target_compile_options(hapi_core PRIVATE /W4)
else()
  target_compile_options(hapi_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(hapi_probe src/hapi_probe/main.cpp)
target_link_libraries(hapi_probe PRIVATE hapi_core)

enable_testing()
add_executable(test_geomath tests/test_geomath.cpp)
target_link_libraries(test_geomath PRIVATE hapi_core)
add_test(NAME geomath COMMAND test_geomath)

# A fake libHAPIL exporting a handful of symbols; proves the dynamic loader
# and the generated table work without a Houdini install.
add_library(mock_hapil SHARED tests/mock_hapil.cpp)
target_include_directories(mock_hapil PRIVATE "${HAPI_INCLUDE_DIR}")
target_compile_definitions(mock_hapil PRIVATE HAPI_EXPORTS HAPI_SUPPRESS_DEPRECATIONS)
set_target_properties(mock_hapil PROPERTIES OUTPUT_NAME "HAPIL_mock")
add_executable(test_loader tests/test_loader.cpp)
target_link_libraries(test_loader PRIVATE hapi_core)
add_test(NAME loader COMMAND test_loader $<TARGET_FILE:mock_hapil>)
