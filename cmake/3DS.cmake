# Minimal devkitARM / 3DS CMake toolchain file.
#
# devkitPro's own `3ds-cmake` package provides this, but it is not installed here
# (present in the package db, not downloaded). This is a stand-in with just enough
# to cross-compile and link 3DS static libraries and ELFs.
#
#   cmake -B build-3ds -DCMAKE_TOOLCHAIN_FILE=cmake/3DS.cmake
#
# Replace with devkitPro's official file once `3ds-cmake` is installed — it also
# supplies ctr_add_shader_library / ctr_create_3dsx helpers that this does not.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(N3DS TRUE)
set(NINTENDO_3DS TRUE)

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not set. e.g. export DEVKITPRO=$HOME/dkp-root/opt/devkitpro")
endif()
set(DEVKITPRO $ENV{DEVKITPRO})
set(DEVKITARM "${DEVKITPRO}/devkitARM")

set(TOOL_PREFIX "${DEVKITARM}/bin/arm-none-eabi-")
set(CMAKE_C_COMPILER   "${TOOL_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${TOOL_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${TOOL_PREFIX}gcc")
set(CMAKE_AR           "${TOOL_PREFIX}ar"     CACHE FILEPATH "")
set(CMAKE_RANLIB       "${TOOL_PREFIX}ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP        "${TOOL_PREFIX}strip"  CACHE FILEPATH "")

# ARM11 (MPCore, ARMv6K, VFPv2 hard-float). -mword-relocations is required by the
# 3DS loader. -ffp-contract=off and -fno-fast-math are NOT optional for this
# project: the N64's R4300/RSP have no FMA, and contracting mul+add changes
# gameplay-visible results.
set(ARCH_FLAGS "-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations")
set(FP_FLAGS   "-fno-fast-math -ffp-contract=off")
# -D_GNU_SOURCE exposes newlib's POSIX declarations (fileno, isatty, fsync,
# strdup...). Without it a dependency compiled in strict-ISO mode sees them
# hidden behind feature-test macros and fails with "not declared in this scope",
# even though libc.a exports them. spdlog hits this immediately.
set(CMAKE_C_FLAGS_INIT   "${ARCH_FLAGS} ${FP_FLAGS} -ffunction-sections -fdata-sections -D_GNU_SOURCE -D__3DS__ -D_3DS -DARM11")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_ASM_FLAGS_INIT "${ARCH_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${ARCH_FLAGS} -specs=${DEVKITARM}/arm-none-eabi/lib/3dsx.specs -Wl,--gc-sections")

include_directories(SYSTEM "${DEVKITPRO}/libctru/include")
link_directories("${DEVKITPRO}/libctru/lib")

set(CMAKE_FIND_ROOT_PATH "${DEVKITARM}" "${DEVKITARM}/arm-none-eabi" "${DEVKITPRO}/libctru" "${DEVKITPRO}/portlibs/3ds")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# try_compile MUST produce a real executable, not a static library.
#
# With CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY, check_function_exists and
# check_symbol_exists never reach the linker, so every probe succeeds and
# dependencies conclude that newlib implements things it does not: sigaction,
# the whole C11 Annex K family (memcpy_s, strncpy_s, strerror_s, ...), even
# macOS-only clonefile. The build then fails much later with
# implicit-declaration or undefined-reference errors that look like the
# dependency's fault.
#
# CMAKE_*_STANDARD_LIBRARIES is appended at the END of every link line, which is
# what static libctru needs (put it in LINKER_FLAGS and it lands before the
# objects, so nothing resolves). This also makes CMake's own compiler-ID test
# link, since 3dsx_crt0 pulls initSystem/__system_argc/__system_argv from ctru.
set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
set(CMAKE_C_STANDARD_LIBRARIES   "-lctru -lm" CACHE STRING "")
set(CMAKE_CXX_STANDARD_LIBRARIES "-lctru -lm" CACHE STRING "")
set(CMAKE_REQUIRED_LIBRARIES ctru)

# No shared libraries on 3DS.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
