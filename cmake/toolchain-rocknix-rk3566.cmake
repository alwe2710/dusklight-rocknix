# CMake toolchain file for cross-compiling Dusklight for ROCKNIX / Anbernic RG DS (RK3566/RK3568, aarch64).
#
# This does NOT use a generic aarch64-linux-gnu toolchain. It expects the actual
# cross toolchain + sysroot produced by the ROCKNIX distribution build, which is the
# only toolchain guaranteed to match the target image's glibc and kernel headers.
#
# Produce it first (from a checkout of https://github.com/ROCKNIX/distribution,
# on a host with full network access, NOT this sandbox):
#
#   make docker-shell
#   (inside the container:)
#   PROJECT=ROCKNIX DEVICE=RK3566 ARCH=aarch64 ./scripts/build_mt toolchain
#
# That produces (relative to the distribution checkout):
#   build.ROCKNIX-RK3566.aarch64/toolchain/bin/aarch64-rocknix-linux-gnu-{gcc,g++,...}
#   build.ROCKNIX-RK3566.aarch64/toolchain/aarch64-rocknix-linux-gnu/sysroot/
#
# Usage:
#   cmake --preset linux-default-relwithdebinfo \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rocknix-rk3566.cmake \
#     -DROCKNIX_TOOLCHAIN_ROOT=/path/to/distribution/build.ROCKNIX-RK3566.aarch64/toolchain \
#     -DAURORA_DAWN_PROVIDER=package
#
# ROCKNIX_TOOLCHAIN_ROOT may also be supplied via the environment instead of -D.

if(NOT ROCKNIX_TOOLCHAIN_ROOT)
  if(DEFINED ENV{ROCKNIX_TOOLCHAIN_ROOT})
    set(ROCKNIX_TOOLCHAIN_ROOT "$ENV{ROCKNIX_TOOLCHAIN_ROOT}")
  else()
    message(FATAL_ERROR
      "ROCKNIX_TOOLCHAIN_ROOT is not set. Point it at the 'toolchain' directory produced by "
      "'PROJECT=ROCKNIX DEVICE=RK3566 ARCH=aarch64 ./scripts/build_mt toolchain' in a "
      "ROCKNIX/distribution checkout, e.g. .../build.ROCKNIX-RK3566.aarch64/toolchain")
  endif()
endif()

set(ROCKNIX_TARGET_NAME "aarch64-rocknix-linux-gnu" CACHE STRING
  "ROCKNIX target triple (TARGET_GCC_ARCH-rocknix-linux-gnu\${TARGET_ABI}, see config/path in distribution)")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(_rocknix_bin "${ROCKNIX_TOOLCHAIN_ROOT}/bin")
set(_rocknix_sysroot "${ROCKNIX_TOOLCHAIN_ROOT}/${ROCKNIX_TARGET_NAME}/sysroot")

if(NOT EXISTS "${_rocknix_bin}/${ROCKNIX_TARGET_NAME}-gcc")
  message(FATAL_ERROR "Cross compiler not found: ${_rocknix_bin}/${ROCKNIX_TARGET_NAME}-gcc "
    "(check ROCKNIX_TOOLCHAIN_ROOT/ROCKNIX_TARGET_NAME)")
endif()

set(CMAKE_C_COMPILER "${_rocknix_bin}/${ROCKNIX_TARGET_NAME}-gcc")
set(CMAKE_CXX_COMPILER "${_rocknix_bin}/${ROCKNIX_TARGET_NAME}-g++")
set(CMAKE_AR "${_rocknix_bin}/${ROCKNIX_TARGET_NAME}-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_rocknix_bin}/${ROCKNIX_TARGET_NAME}-ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP "${_rocknix_bin}/${ROCKNIX_TARGET_NAME}-strip" CACHE FILEPATH "")

set(CMAKE_SYSROOT "${_rocknix_sysroot}")
set(CMAKE_FIND_ROOT_PATH "${_rocknix_sysroot}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Match TARGET_CPU/TARGET_CPU_FLAGS for RK3566 from
# projects/ROCKNIX/devices/RK3566/options in the distribution repo (Cortex-A55).
set(_rocknix_cpu_flags "-mcpu=cortex-a55+crc+crypto+fp+simd+rcpc")
set(CMAKE_C_FLAGS_INIT "${_rocknix_cpu_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_rocknix_cpu_flags}")

# So pkg-config resolves .pc files from the sysroot instead of the host.
set(ENV{PKG_CONFIG_LIBDIR} "${_rocknix_sysroot}/usr/lib/pkgconfig:${_rocknix_sysroot}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${_rocknix_sysroot}")
