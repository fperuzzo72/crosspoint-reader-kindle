# CMake toolchain file for the jailbroken Kindle (KT3 / 8th gen).
#
# Intended to be used from inside the container built by
# docker/toolchain.Dockerfile, where the cross compiler is already on PATH.
#
#   cmake -S cmake/kindle -B build/kindle/cmake \
#         -DCMAKE_TOOLCHAIN_FILE=../../cmake/kindle/toolchain.cmake
#
# The ABI here is measured, not chosen: see docs/kindle-port.md. Soft-float is
# not a preference, it is what the device's own binaries are, and a hard-float
# build fails at exec with a "not found" naming the binary rather than the
# missing interpreter.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(KINDLE_TRIPLE arm-kindlepw2-linux-gnueabi)

set(CMAKE_C_COMPILER ${KINDLE_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${KINDLE_TRIPLE}-g++)
set(CMAKE_AR ${KINDLE_TRIPLE}-gcc-ar)
set(CMAKE_RANLIB ${KINDLE_TRIPLE}-gcc-ranlib)
set(CMAKE_STRIP ${KINDLE_TRIPLE}-strip)

# Only look for libraries and headers in the toolchain's sysroot; a stray host
# header here would compile and then fail on the device.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
