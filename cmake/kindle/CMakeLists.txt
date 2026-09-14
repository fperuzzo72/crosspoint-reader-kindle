cmake_minimum_required(VERSION 3.16)
project(crosspoint_kindle CXX)

# The Kindle build. PlatformIO only knows how to talk to ESP32s, so this target
# needs its own entry point.
#
# What it builds today is the platform layer and the on-device tools, not the
# reader: at the time of writing about three quarters of the tree cross-compiles
# (run tools/kindle/census.sh for the current number), and the remainder is
# mostly networking and unvendored third-party headers. This file is the thing
# that grows as that number does, and it is deliberately structured so adding
# sources is the only change needed.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  # Size matters more than speed here: the device has 256 MB and a single
  # ~1 GHz core, and nothing in the reader is compute-bound. The one hot path
  # measured so far, the 1bpp framebuffer expansion, costs 3-5 ms.
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(KINDLE_DIR "${REPO_ROOT}/lib/hal/kindle")
set(BUILD_DIR "${REPO_ROOT}/build/kindle")

# --- FBInk ------------------------------------------------------------------
# Not vendored: it tracks Amazon's firmware quirks and is pinned by
# tools/kindle/build.sh. Only the ioctls and the per-model quirk table are used
# from it; pixels go into an mmap of /dev/fb0 directly.
set(FBINK_DIR "${BUILD_DIR}/FBInk")
find_library(FBINK_LIB NAMES fbink PATHS "${FBINK_DIR}/Release" NO_DEFAULT_PATH)
if(NOT FBINK_LIB)
  message(FATAL_ERROR
    "libfbink.a not found under ${FBINK_DIR}/Release.\n"
    "Run tools/kindle/build.sh once first: it clones FBInk at its pinned tag "
    "and builds the static library.")
endif()

# --- platform layer ---------------------------------------------------------
# Everything that makes the rest of the tree believe it is still on an Arduino.
add_library(crosspoint_kindle_platform STATIC
  ${KINDLE_DIR}/ArduinoString.cpp
  ${KINDLE_DIR}/ArduinoPlatform.cpp
  ${KINDLE_DIR}/ArduinoShim.cpp
  ${KINDLE_DIR}/SdFatPosix.cpp
  ${KINDLE_DIR}/NetworkPosix.cpp
  ${KINDLE_DIR}/CryptoPosix.cpp
  ${KINDLE_DIR}/WebServerPosix.cpp
  ${KINDLE_DIR}/KindleGrayExpand.cpp
  ${KINDLE_DIR}/KindleFrameBuffer.cpp
  ${KINDLE_DIR}/KindleTouchClassifier.cpp
  ${KINDLE_DIR}/KindleTouchDevice.cpp
)

target_include_directories(crosspoint_kindle_platform PUBLIC
  # arduino-shim goes FIRST so that `#include <Arduino.h>` inside the SDK
  # resolves here, which is what keeps this port from having to edit the SDK.
  ${KINDLE_DIR}/arduino-shim
  ${KINDLE_DIR}
  ${FBINK_DIR}
  ${FBINK_DIR}/libunibreak/src
  ${BUILD_DIR}/thirdparty
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/src
)

# platformio.ini injects this from its [crosspoint] section; a build outside
# PlatformIO has to supply it or the version string is simply absent.
if(NOT CROSSPOINT_VERSION)
  set(CROSSPOINT_VERSION "kindle-dev")
endif()

target_compile_definitions(crosspoint_kindle_platform PUBLIC
  FREEINK_DEVICE_KINDLE=1
  CROSSPOINT_VERSION="${CROSSPOINT_VERSION}"
)
target_compile_options(crosspoint_kindle_platform PRIVATE -Wall -Wextra)

# clock_gettime lives in librt in glibc before 2.17, and this target predates
# that by a decade. Without it the link fails on a symbol the header declares
# perfectly happily.
target_link_libraries(crosspoint_kindle_platform PUBLIC ${FBINK_LIB} rt pthread)

# --- the reader itself ------------------------------------------------------
# Not buildable yet: see tools/kindle/census.sh for what still does not compile
# and docs/kindle-port.md for what it means. The target exists so that finishing
# the port is a matter of the sources below compiling, not of inventing a build.
# Enable with -DCROSSPOINT_BUILD_READER=ON once the census reaches 100%.
option(CROSSPOINT_BUILD_READER "Link the reader binary (incomplete)" OFF)

if(CROSSPOINT_BUILD_READER)
  file(GLOB_RECURSE READER_SOURCES
    ${REPO_ROOT}/src/*.cpp
    ${REPO_ROOT}/lib/*/*.cpp
    ${REPO_ROOT}/lib/*/*/*.cpp
  )
  # Third-party trees carry their own build rules and their own C dialects.
  list(FILTER READER_SOURCES EXCLUDE REGEX "/(expat|miniz|uzlib)/")

  add_executable(crosspoint
    ${READER_SOURCES}
    ${REPO_ROOT}/tools/kindle/main_kindle.cpp
  )
  target_link_libraries(crosspoint PRIVATE crosspoint_kindle_platform)
endif()

# --- on-device tools --------------------------------------------------------
# Each has a matching scriptlet in tools/kindle/; see docs/kindle-build.md.
function(add_kindle_tool name)
  add_executable(${name} ${REPO_ROOT}/tools/kindle/${name}.cpp)
  target_link_libraries(${name} PRIVATE crosspoint_kindle_platform)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
endfunction()

add_kindle_tool(smoketest)
add_kindle_tool(touchtest)

# inputprobe talks only to evdev: no FBInk, no platform layer.
add_executable(inputprobe ${REPO_ROOT}/tools/kindle/inputprobe.cpp)
target_compile_options(inputprobe PRIVATE -Wall -Wextra)
