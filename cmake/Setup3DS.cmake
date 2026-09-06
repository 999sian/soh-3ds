# PC-free first-run setup libraries and RomFS staging for the 3DS game target.
# Included from the N3DS/SoH section of the root CMakeLists after LUS_DIR,
# LUS_BUILD, SOH_DIR and PORTLIBS have been established.

if(NOT N3DS)
    return()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SOH3DS_SETUP_METADATA_DIR "${SOH_DIR}/assets/yml")
set(SOH3DS_SETUP_SUPPORT_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/shipwright/soh.o2r")
set(SOH3DS_SETUP_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/setup-romfs")
set(SOH3DS_SETUP_ROMFS_STAMP "${CMAKE_CURRENT_BINARY_DIR}/setup-romfs.stamp")

# Ship's root project owns the runtime version. Read that declaration instead of
# maintaining a second version constant in this port.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/shipwright/CMakeLists.txt"
    _soh3ds_ship_project_line REGEX "^project\\(Ship VERSION [0-9]+\\.[0-9]+\\.[0-9]+")
string(REGEX MATCH "VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)" _soh3ds_ship_version_match
    "${_soh3ds_ship_project_line}")
set(SOH3DS_SHIP_VERSION "${CMAKE_MATCH_1}")
if(NOT SOH3DS_SHIP_VERSION)
    message(FATAL_ERROR "Could not derive Ship version for PC-free setup packaging")
endif()
message(STATUS "SoH 3DS setup archive version: ${SOH3DS_SHIP_VERSION}")

set(SOH3DS_SETUP_GENERATED_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/setup-generated")
file(MAKE_DIRECTORY "${SOH3DS_SETUP_GENERATED_INCLUDE}")
file(GENERATE OUTPUT "${SOH3DS_SETUP_GENERATED_INCLUDE}/setup_version.h" CONTENT
    "#pragma once\n#define SOH3DS_SETUP_VERSION \"${SOH3DS_SHIP_VERSION}\"\n")

# CONFIGURE_DEPENDS makes adding or removing an allowlisted YAML file rerun
# configure; listing every file in DEPENDS then makes content changes restage.
file(GLOB_RECURSE SOH3DS_SETUP_METADATA CONFIGURE_DEPENDS
    "${SOH3DS_SETUP_METADATA_DIR}/*.yml")
add_custom_command(
    OUTPUT "${SOH3DS_SETUP_ROMFS_STAMP}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare-setup-romfs.py"
        --metadata-dir "${SOH3DS_SETUP_METADATA_DIR}"
        --support-archive "${SOH3DS_SETUP_SUPPORT_ARCHIVE}"
        --output-dir "${SOH3DS_SETUP_ROMFS_DIR}"
        --expected-version "${SOH3DS_SHIP_VERSION}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${SOH3DS_SETUP_ROMFS_STAMP}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare-setup-romfs.py"
        "${SOH3DS_SETUP_SUPPORT_ARCHIVE}"
        ${SOH3DS_SETUP_METADATA}
    COMMENT "Staging allowlisted Torch metadata and SoH support archive"
    VERBATIM)
add_custom_target(soh3ds_setup_romfs DEPENDS "${SOH3DS_SETUP_ROMFS_STAMP}")

# Build the same pinned yaml-cpp source already fetched by Ship's host build.
# Cross-compiling it here avoids a network fetch and keeps host Torch untouched.
set(SOH3DS_TORCH_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/shipwright/build-host/_deps")
set(SOH3DS_YAML_SOURCE "${SOH3DS_TORCH_DEPS}/yaml-cpp-src")
if(NOT EXISTS "${SOH3DS_YAML_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR
        "Missing ${SOH3DS_YAML_SOURCE}; configure the Ship host build once before the 3DS build")
endif()
if(NOT TARGET yaml-cpp)
    set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
    set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${SOH3DS_YAML_SOURCE}"
        "${CMAKE_CURRENT_BINARY_DIR}/setup-yaml-cpp" EXCLUDE_FROM_ALL)
endif()

set(SOH3DS_TORCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/shipwright/torch")
file(GLOB_RECURSE SOH3DS_TORCH_SOURCES CONFIGURE_DEPENDS
    "${SOH3DS_TORCH_DIR}/src/*.cpp")
list(FILTER SOH3DS_TORCH_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")
list(FILTER SOH3DS_TORCH_SOURCES EXCLUDE REGEX "/src/ui/")
list(FILTER SOH3DS_TORCH_SOURCES EXCLUDE REGEX
    "/src/factories/(bk64|fzerox|mario_artist|mk64|naudio|pm64|sf64|sm64)/")

# LUS exports the same global StringHelper and CRC64 implementations. Leaving
# Torch's copies out prevents duplicate symbols while its own headers retain the
# compatible interface used by these sources.
list(FILTER SOH3DS_TORCH_SOURCES EXCLUDE REGEX "/src/utils/StringHelper\\.cpp$")

file(GLOB SOH3DS_BINARYTOOLS_SOURCES CONFIGURE_DEPENDS
    "${SOH3DS_TORCH_DIR}/lib/binarytools/*.cpp")
set(SOH3DS_TORCH_C_SOURCES
    "${SOH3DS_TORCH_DIR}/lib/bk_zip/bk_unzip.cpp"
    "${SOH3DS_TORCH_DIR}/lib/libmio0/mio0.c"
    "${SOH3DS_TORCH_DIR}/lib/libmio0/tkmk00.c"
    "${SOH3DS_TORCH_DIR}/lib/libmio0/utils.c"
    "${SOH3DS_TORCH_DIR}/lib/libyay0/yay0.c"
    "${SOH3DS_TORCH_DIR}/lib/libyay0/yay1.c"
    "${SOH3DS_TORCH_DIR}/lib/libyaz0/yaz0.c"
    "${SOH3DS_TORCH_DIR}/lib/n64graphics/n64graphics.c"
    "${SOH3DS_TORCH_DIR}/lib/n64graphics/stb_image_write.c")

add_library(soh3ds_torch_oot STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/source/torch_companion_instance.cpp"
    ${SOH3DS_TORCH_SOURCES}
    ${SOH3DS_BINARYTOOLS_SOURCES}
    ${SOH3DS_TORCH_C_SOURCES})
target_include_directories(soh3ds_torch_oot PUBLIC
    "${SOH3DS_TORCH_DIR}"
    "${SOH3DS_TORCH_DIR}/src"
    "${SOH3DS_TORCH_DIR}/lib"
    "${SOH3DS_TORCH_DIR}/lib/n64graphics"
    "${SOH3DS_YAML_SOURCE}/include"
    "${PORTLIBS}/include"
    "${LUS_BUILD}/_deps/imgui-src")
target_compile_definitions(soh3ds_torch_oot PRIVATE
    OOT_SUPPORT
    PORT_VERSION_ENDIANNESS
    THROW_ON_UNKNOWN_TYPE
    EXCLUDE_MPQ_SUPPORT
    NO_MPQ
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_compile_options(soh3ds_torch_oot PRIVATE -w -fpermissive -Os)
target_link_libraries(soh3ds_torch_oot PUBLIC yaml-cpp)
set_target_properties(soh3ds_torch_oot PROPERTIES
    CXX_STANDARD 20
    EXCLUDE_FROM_ALL TRUE)

add_library(soh3ds_setup_core STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/setup/setup_core.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/setup/setup_bundle.cpp")
target_include_directories(soh3ds_setup_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${PORTLIBS}/include" "${DEVKITPRO}/libctru/include")
target_compile_options(soh3ds_setup_core PRIVATE -w -fpermissive -Os)
set_target_properties(soh3ds_setup_core PROPERTIES CXX_STANDARD 20 EXCLUDE_FROM_ALL TRUE)

function(soh3ds_attach_pc_free_setup target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Cannot attach PC-free setup to missing target ${target}")
    endif()
    target_sources("${target}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/setup/setup_3ds.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/setup/torch_adapter.cpp")
    target_include_directories("${target}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${SOH3DS_SETUP_GENERATED_INCLUDE}")
    add_dependencies("${target}" soh3ds_setup_romfs)
    # A metadata-only change must regenerate the POST_BUILD 3DSX/CIA too.
    set_property(TARGET "${target}" APPEND PROPERTY LINK_DEPENDS "${SOH3DS_SETUP_ROMFS_STAMP}")
endfunction()

function(soh3ds_add_pc_free_artifacts target)
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${DEVKITPRO}/tools/bin/3dsxtool"
            "$<TARGET_FILE:${target}>" "$<TARGET_FILE:${target}>.3dsx"
            "--smdh=${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/cia/icon.icn"
            "--romfs=${SOH3DS_SETUP_ROMFS_DIR}"
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/make-cia.sh"
            "$<TARGET_FILE:${target}>" "${CMAKE_CURRENT_BINARY_DIR}/${target}.cia"
            "${SOH3DS_SETUP_ROMFS_DIR}"
        COMMENT "Packaging ${target}.3dsx and ${target}.cia with setup RomFS"
        VERBATIM)
endfunction()
