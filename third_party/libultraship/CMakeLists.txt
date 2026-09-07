cmake_minimum_required(VERSION 3.24.0)

# Enable /Z7 (embedded debug info) instead of /Zi (shared PDB) on MSVC so that
# sccache can cache object files without racing over a shared .pdb file.
# CMP0141 was introduced in CMake 3.25, so guard it for older versions.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

option(NON_PORTABLE "Build a non-portable version" OFF)
option(DISABLE_DLL_LOADER "Disable dynamic library loading support" OFF)
option(ENABLE_SCRIPTING "Enable TCC-based C mod compilation and scripting system" OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    option(SIGN_LIBRARY "Enable xcode signing" OFF)
    option(BUNDLE_ID "Bundle ID for xcode signing" "com.example.libultraship")
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
endif()

project(libultraship LANGUAGES C CXX)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
    set(CMAKE_OBJC_FLAGS "${CMAKE_OBJC_FLAGS} -fobjc-arc")
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	# Default to embedded debug info (/Z7) so sccache can cache object files
	# without multiple CL.EXE processes racing over a shared .pdb file.
	if(POLICY CMP0141)
		set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:Embedded>")
	endif()
endif()

include(cmake/Utils.cmake)
set(ADDITIONAL_LIB_INCLUDES "")

# ========= Configuration Options =========
option(INCLUDE_MPQ_SUPPORT "Enable StormLib and MPQ archive support" OFF)
# This is a preprocessor symbol, not a boolean: option() turns the default
# into OFF and makes XML display lists use the wrong command encoding.
set(GBI_UCODE "F3DEX_GBI_2" CACHE STRING "Specify the GBI ucode version")
set_property(CACHE GBI_UCODE PROPERTY STRINGS F3DEX_GBI_2 F3DEX_GBI F3DLP_GBI F3D_OLD)
if(NOT GBI_UCODE MATCHES "^(F3DEX_GBI_2|F3DEX_GBI|F3DLP_GBI|F3D_OLD)$")
    message(FATAL_ERROR "Invalid GBI_UCODE '${GBI_UCODE}'. Set -DGBI_UCODE:STRING=F3DEX_GBI_2, F3DEX_GBI, F3DLP_GBI, or F3D_OLD.")
endif()

# =========== Dependencies =============
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/dependencies/windows-vcpkg.cmake)
endif()

include(cmake/dependencies/common.cmake)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
	include(cmake/dependencies/android.cmake)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(cmake/dependencies/mac.cmake)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    include(cmake/dependencies/ios.cmake)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/dependencies/linux.cmake)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
    include(cmake/dependencies/openbsd.cmake)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/dependencies/windows.cmake)
endif()

# === Platform Specific Configuration ===
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    include(cmake/ios-toolchain-populate.cmake)
endif()

# =========== Sources =============
add_subdirectory("src")

# =========== Tests =============
option(LUS_BUILD_TESTS "Build unit tests" OFF)
if(LUS_BUILD_TESTS)
    enable_testing()
    add_subdirectory("tests")
endif()
