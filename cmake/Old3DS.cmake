# Share game libraries, but keep the ROM extractor out of the old-model process.
get_target_property(_old_sources soh_3ds SOURCES)
list(FILTER _old_sources EXCLUDE REGEX "/src/setup/(setup_3ds|torch_adapter)\\.cpp$")
add_executable(soh_3ds_old ${_old_sources} src/setup/setup_prebuilt_3ds.cpp src/compat3ds/compact_zip.cpp)
if(SOH3DS_DSP_CAPTURE)
    add_dependencies(soh_3ds_old soh_3ds_dsp_firmware)
endif()
foreach(_prop INCLUDE_DIRECTORIES COMPILE_OPTIONS LINK_LIBRARIES LINK_OPTIONS)
    get_target_property(_value soh_3ds ${_prop})
    if(_value)
        if(_prop STREQUAL "LINK_LIBRARIES")
            list(REMOVE_ITEM _value soh3ds_torch_oot)
        endif()
        set_property(TARGET soh_3ds_old PROPERTY ${_prop} "${_value}")
    endif()
endforeach()
set_target_properties(soh_3ds_old PROPERTIES CXX_STANDARD 20 EXCLUDE_FROM_ALL TRUE)

# Generate from the canonical permissions so the two profiles cannot drift.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/cia/app.rsf" _old_rsf)
option(SOH3DS_OLD_CPU80 "Experiment: permit an 80 percent core-1 CPU request in the Old CIA" OFF)
if(SOH3DS_OLD_CPU80)
    # Luma PM uses bit 7 for multi scheduling and bits 0-6 as the maximum
    # requested core-1 allowance. Keep the scheduling bit, raise 30 to 80.
    # OTRAudio_Init already requests 80 first and checks the APT result.
    string(REPLACE "MaxCpu                        : 0x9E" "MaxCpu                        : 0xD0" _old_rsf "${_old_rsf}")
endif()
option(SOH3DS_OLD_COMPRESS "Compress the old-model CIA executable" ON)
if(NOT SOH3DS_OLD_COMPRESS)
    string(REPLACE "EnableCompress          : true" "EnableCompress          : false" _old_rsf "${_old_rsf}")
endif()
# Keep the canonical 80 MiB mode: the 96 MiB layout crashes retail Old HOME Menu.
string(REPLACE "SystemModeExt                 : 124MB" "SystemModeExt                 : 178MB" _old_rsf "${_old_rsf}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/old3ds.rsf" CONTENT "${_old_rsf}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/old3ds-romfs")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/old3ds-romfs/README.txt"
    "Copy compatible soh.o2r and oot.o2r or oot-mq.o2r into /3ds/soh on the SD card.\n")
set_property(TARGET soh_3ds_old APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/old3ds.rsf")
add_custom_command(TARGET soh_3ds_old POST_BUILD
    COMMAND "${DEVKITPRO}/tools/bin/makerom" -f cia
        -o "${CMAKE_CURRENT_BINARY_DIR}/soh_3ds_old.cia" -elf "$<TARGET_FILE:soh_3ds_old>"
        -rsf "${CMAKE_CURRENT_BINARY_DIR}/old3ds.rsf"
        -icon "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/cia/icon.icn"
        -banner "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/cia/banner.bnr"
        "-DROMFS_ROOT=${CMAKE_CURRENT_BINARY_DIR}/old3ds-romfs" -target t -exefslogo
    COMMENT "Packaging original 3DS build (pre-extracted archives required)"
    VERBATIM)
