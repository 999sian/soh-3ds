# Only the experimental DSP-capture build uses this adapter. The installed SDK
# archive is read, never modified. Its DSP service object remains unchanged
# apart from three reversible symbol renames, allowing the native backend to
# serialize APT transitions before any SDK automatic unload/reload.
function(soh3ds_attach_dsp_lifecycle target)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_filename_component(probe_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../research/dsp/synth-probe" ABSOLUTE)
    set(sdk_archive "${DEVKITPRO}/libctru/lib/libctru.a")
    set(sdk_object "${CMAKE_CURRENT_BINARY_DIR}/soh-dsp-sdk/dsp.o")
    add_custom_command(
        OUTPUT "${sdk_object}"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/soh-dsp-sdk/dsp.json"
        COMMAND "${Python3_EXECUTABLE}" "${probe_dir}/prepare_sdk_lifecycle.py"
            --devkitpro "${DEVKITPRO}" --output "${sdk_object}"
        DEPENDS "${sdk_archive}" "${probe_dir}/prepare_sdk_lifecycle.py"
        VERBATIM)
    set_source_files_properties("${sdk_object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
    target_sources(${target} PRIVATE "${sdk_object}" "${probe_dir}/sdk_lifecycle_bridge.c"
        "${probe_dir}/producer_gate_3ds.cpp" "${probe_dir}/sdk_ownership_3ds.c")
endfunction()

function(soh3ds_attach_dsp_runtime target)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_filename_component(probe_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../research/dsp/synth-probe" ABSOLUTE)
    set(firmware_dir "${CMAKE_CURRENT_BINARY_DIR}/soh-dsp-firmware")
    add_custom_target(${target}_dsp_firmware
        COMMAND "${Python3_EXECUTABLE}" "${probe_dir}/prepare_runtime_firmware.py"
            --mailbox-only --output "${firmware_dir}/mapped_firmware_component.h"
        BYPRODUCTS "${firmware_dir}/mapped_firmware_component.h"
        VERBATIM)
    add_dependencies(${target} ${target}_dsp_firmware)
    target_include_directories(${target} PRIVATE "${firmware_dir}")
    target_sources(${target} PRIVATE "${probe_dir}/native_audio_backend_3ds.cpp"
        "${probe_dir}/native_mixer_cpu.cpp")
endfunction()
