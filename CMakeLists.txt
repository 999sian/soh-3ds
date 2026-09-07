# Two build modes.
#
# Host (default) — portable logic + its tests. No toolchain, no hardware:
#   cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
#
# 3DS — cross-compile, produces a .3dsx:
#   export DEVKITPRO=$HOME/dkp-root/opt/devkitpro
#   cmake -B build-3ds -DCMAKE_TOOLCHAIN_FILE=cmake/3DS.cmake
#   cmake --build build-3ds
#
# ponytail: not a project scaffold. When this code moves into
# libultraship/src/fast/backends/ these targets go with it and this file is deleted.

cmake_minimum_required(VERSION 3.20)
project(soh3ds CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# --- portable logic, builds for both host and 3DS ----------------------------
add_library(pica_combiner STATIC src/pica/combiner_lower.cpp)
target_include_directories(pica_combiner PUBLIC src)
target_compile_options(pica_combiner PRIVATE -Wall -Wextra)

# Skip service calls that 3DS emulators leave unimplemented, so the same source
# runs under Azahar and on hardware. Currently: osSetSpeedupEnable, which goes
# through ptm:sysm ConfigureNew3DSCPU and never returns under Azahar.
option(SOH3DS_EMULATOR_SAFE "Skip emulator-unimplemented service calls" OFF)

set(N3DS_BUILD ON CACHE BOOL "building for 3DS" FORCE)
if(N3DS)
    # --- Phase 0 link smoke test ---------------------------------------------
    #
    # Proves libctru + citro3d + the from-source SDL2 all link into one 3DS
    # binary, and that both render targets and the SDL subsystems LUS depends on
    # initialise. This is the dependency-stack half of SPEC.md gate A2.
    #
    # SDL2 is NOT available as a devkitPro package (no `3ds-sdl2` exists — only
    # `3ds-sdl` 1.2). Build it from libsdl-org/SDL@SDL2 and install into
    # portlibs/3ds, with HAVE_SIGACTION forced off:
    #
    #   cmake -B b -DCMAKE_TOOLCHAIN_FILE=<this repo>/cmake/3DS.cmake \
    #             -DCMAKE_BUILD_TYPE=Release -DHAVE_SIGACTION=0
    #   cmake --build b -j && cmake --install b --prefix $DEVKITPRO/portlibs/3ds
    #
    # Without -DHAVE_SIGACTION=0 the link fails on undefined `sigaction`: newlib
    # declares the prototype but libc.a has no implementation, so SDL2's
    # check_symbol_exists misdetects it.
    set(PORTLIBS "${DEVKITPRO}/portlibs/3ds")

    find_library(LIB_SDL2     NAMES SDL2     PATHS "${PORTLIBS}/lib" REQUIRED)
    find_library(LIB_SDL2MAIN NAMES SDL2main PATHS "${PORTLIBS}/lib" REQUIRED)
    find_library(LIB_CITRO3D  NAMES citro3d  PATHS "${DEVKITPRO}/libctru/lib" REQUIRED)
    find_library(LIB_CTRU     NAMES ctru     PATHS "${DEVKITPRO}/libctru/lib" REQUIRED)

    add_executable(smoke_3ds tests/smoke_3ds.c)
    target_include_directories(smoke_3ds PRIVATE
        "${DEVKITPRO}/libctru/include" "${PORTLIBS}/include" "${PORTLIBS}/include/SDL2")
    target_link_libraries(smoke_3ds PRIVATE
        ${LIB_SDL2MAIN} ${LIB_SDL2} ${LIB_CITRO3D} ${LIB_CTRU} m)
    target_compile_options(smoke_3ds PRIVATE -Wall -Wextra)
    if(SOH3DS_EMULATOR_SAFE)
        target_compile_definitions(smoke_3ds PRIVATE SOH3DS_EMULATOR_SAFE)
    endif()

    # 3dsxtool turns the ELF into a loadable .3dsx. devkitPro's own 3ds-cmake
    # package provides ctr_create_3dsx; this is the manual equivalent.
    add_custom_command(TARGET smoke_3ds POST_BUILD
        COMMAND "${DEVKITPRO}/tools/bin/3dsxtool" $<TARGET_FILE:smoke_3ds> $<TARGET_FILE:smoke_3ds>.3dsx
        COMMENT "3dsxtool -> smoke_3ds.3dsx")

    set(LUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libultraship")
    set(LUS_BUILD "${LUS_DIR}/build-3ds")

    # --- PICA vertex shader ---------------------------------------------------
    # picasso assembles .v.pica -> .shbin; bin2s turns it into a linkable object
    # exposing fast3d_shbin / fast3d_shbin_size.
    # bin2s lives in devkitPro's general-tools, which is not installed;
    # scripts/bin2c.py emits an equivalent C array instead.
    set(SHBIN "${CMAKE_CURRENT_BINARY_DIR}/fast3d.shbin")
    set(SHBIN_S "${CMAKE_CURRENT_BINARY_DIR}/fast3d_shbin.c")
    add_custom_command(
        OUTPUT "${SHBIN_S}"
        COMMAND "${DEVKITPRO}/tools/bin/picasso" -o "${SHBIN}" "${CMAKE_CURRENT_SOURCE_DIR}/src/pica/fast3d.v.pica"
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bin2c.py" "${SHBIN}" "${SHBIN_S}" fast3d_shbin
        DEPENDS src/pica/fast3d.v.pica scripts/bin2c.py
        COMMENT "picasso + bin2c -> fast3d_shbin")

    # --- Fast3D PICA200 backend -----------------------------------------------
    add_library(gfx_citro3d STATIC src/pica/gfx_citro3d.cpp src/pica/gfx_ctr_window.cpp src/pica/combiner_lower.cpp "${SHBIN_S}")
    target_include_directories(gfx_citro3d PUBLIC src
        "${LUS_DIR}/include" "${LUS_DIR}/src"
        "${LUS_BUILD}/_deps/threadpool-src/include"
        "${LUS_BUILD}/_deps/imgui-src"
        "${LUS_BUILD}/_deps/stb"
        "${DEVKITPRO}/libctru/include" "${PORTLIBS}/include" "${PORTLIBS}/include/SDL2")
    target_compile_options(gfx_citro3d PRIVATE -Wall -Wextra)

    # --- Renderer selection ---------------------------------------------------
    # src/pica/ and platform/3ds/ both define Fast::GfxRenderingAPICitro3D, so
    # exactly one may be linked into soh_3ds. platform/3ds is the vendored
    # MK64-3DS backend - proven on real hardware, but its licence is not
    # permissive (platform/3ds/PROVENANCE.md); src/pica is the from-scratch
    # replacement that has to take over before any public release. Hence a
    # switch rather than a deletion of the losing side.
    #
    # Default ON, i.e. platform/3ds: it renders visibly better than src/pica on
    # real use - it is 2043 lines against 1152, and the difference is a texture
    # cache with LRU recycling, frame interpolation, adaptive resolution scaling
    # and a set of device-discovered workarounds.
    #
    # Two things this default costs, both recorded so they are not rediscovered:
    #   - platform/3ds declares no licence (platform/3ds/PROVENANCE.md), so a
    #     build made this way cannot be published. src/pica exists to replace it
    #     before any release, and -DSOH3DS_USE_MK_RENDERER=OFF selects it.
    #   - the two backends are not feature-equivalent. src/pica carries the
    #     combiner fold for commuting varying inputs, the NextPow2 overflow fix
    #     and the citro2d bottom screen; switching backends silently changes
    #     which of those are present. Only the absence of a backend's own log
    #     strings reveals it, so check those after any flip.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/source/gfx_citro3d.cpp")
        option(SOH3DS_USE_MK_RENDERER "Render with the vendored MK64-3DS backend" ON)
    else()
        set(SOH3DS_USE_MK_RENDERER OFF)
    endif()
    if(SOH3DS_USE_MK_RENDERER)
        set(SOH3DS_GFX_LIB mk_gfx_citro3d)
    else()
        set(SOH3DS_GFX_LIB gfx_citro3d)
    endif()
    message(STATUS "soh_3ds renderer: ${SOH3DS_GFX_LIB}")
    # --- Phase 0 acceptance test ---------------------------------------------
    #
    # Links the real libultraship (third_party/libultraship, with
    # patches/libultraship-3ds.patch applied) and mounts an .o2r on hardware.
    set(LUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libultraship")
    set(LUS_BUILD "${LUS_DIR}/build-3ds")
    if(EXISTS "${LUS_BUILD}/src/libultraship.a")
        add_executable(phase0_3ds tests/phase0_3ds.cpp)
        # libultraship's public headers pull in a few of its FetchContent deps
        # (BS_thread_pool, ImGui, nlohmann), so their source dirs have to be on
        # the include path for anything that includes ship/Context.h.
        target_include_directories(phase0_3ds PRIVATE
            "${LUS_DIR}/include" "${LUS_DIR}/src"
            "${LUS_BUILD}/_deps/threadpool-src/include"
            "${LUS_BUILD}/_deps/imgui-src"
            "${LUS_BUILD}/_deps/stb"
            "${DEVKITPRO}/libctru/include" "${PORTLIBS}/include" "${PORTLIBS}/include/SDL2")
        target_link_libraries(phase0_3ds PRIVATE
            "${LUS_BUILD}/src/libultraship.a"
            "${LUS_BUILD}/libImGui.a"
            "${LUS_BUILD}/_deps/prism-build/libprism.a"
            "${LUS_BUILD}/libstb.a"
            "${LUS_BUILD}/libmonocypher.a"
            "${PORTLIBS}/lib/libzip.a" "${PORTLIBS}/lib/libspdlog.a"
            "${PORTLIBS}/lib/libtinyxml2.a" "${PORTLIBS}/lib/libzlibstatic.a"
            ${LIB_SDL2MAIN} ${LIB_SDL2} ${LIB_CITRO3D} ${LIB_CTRU} m)
        target_compile_options(phase0_3ds PRIVATE -Wall)
        if(SOH3DS_EMULATOR_SAFE)
            target_compile_definitions(phase0_3ds PRIVATE SOH3DS_EMULATOR_SAFE)
        endif()
        add_custom_command(TARGET phase0_3ds POST_BUILD
            COMMAND "${DEVKITPRO}/tools/bin/3dsxtool" $<TARGET_FILE:phase0_3ds> $<TARGET_FILE:phase0_3ds>.3dsx
            COMMENT "3dsxtool -> phase0_3ds.3dsx")

        # --- Offscreen orientation probe ---------------------------------------
        # Determines, on hardware, whether render-to-texture targets need the
        # same rotation the physical screens do. Its result is what decides the
        # matrix StartDrawToFramebuffer binds - not an assumption.
        add_executable(orient_3ds tests/orient_3ds.cpp)
        target_include_directories(orient_3ds PRIVATE tests)
        target_link_libraries(orient_3ds PRIVATE
            gfx_citro3d
            "${LUS_BUILD}/src/libultraship.a"
            "${LUS_BUILD}/libImGui.a"
            "${LUS_BUILD}/_deps/prism-build/libprism.a"
            "${LUS_BUILD}/libstb.a"
            "${LUS_BUILD}/libmonocypher.a"
            "${PORTLIBS}/lib/libzip.a" "${PORTLIBS}/lib/libspdlog.a"
            "${PORTLIBS}/lib/libtinyxml2.a" "${PORTLIBS}/lib/libzlibstatic.a"
            ${LIB_SDL2MAIN} ${LIB_SDL2} ${LIB_CITRO3D} ${LIB_CTRU} m)
        target_compile_options(orient_3ds PRIVATE -Wall -Wextra)
        add_custom_command(TARGET orient_3ds POST_BUILD
            COMMAND "${DEVKITPRO}/tools/bin/3dsxtool" $<TARGET_FILE:orient_3ds> $<TARGET_FILE:orient_3ds>.3dsx
            COMMENT "3dsxtool -> orient_3ds.3dsx")

        # --- Phase 2: real OoT display lists ------------------------------------
        add_executable(phase2_3ds tests/phase2_3ds.cpp)
        target_include_directories(phase2_3ds PRIVATE tests src
            "${LUS_DIR}/include" "${LUS_DIR}/src"
            "${LUS_BUILD}/_deps/threadpool-src/include"
            "${LUS_BUILD}/_deps/imgui-src"
            "${LUS_BUILD}/_deps/stb"
            "${DEVKITPRO}/libctru/include" "${PORTLIBS}/include" "${PORTLIBS}/include/SDL2")
        target_link_libraries(phase2_3ds PRIVATE
            gfx_citro3d
            "${LUS_BUILD}/src/libultraship.a"
            "${LUS_BUILD}/libImGui.a"
            "${LUS_BUILD}/_deps/prism-build/libprism.a"
            "${LUS_BUILD}/libstb.a"
            "${LUS_BUILD}/libmonocypher.a"
            "${PORTLIBS}/lib/libzip.a" "${PORTLIBS}/lib/libspdlog.a"
            "${PORTLIBS}/lib/libtinyxml2.a" "${PORTLIBS}/lib/libzlibstatic.a"
            ${LIB_SDL2MAIN} ${LIB_SDL2} ${LIB_CITRO3D} ${LIB_CTRU} m)
        target_compile_options(phase2_3ds PRIVATE -Wall)
        add_custom_command(TARGET phase2_3ds POST_BUILD
            COMMAND "${DEVKITPRO}/tools/bin/3dsxtool" $<TARGET_FILE:phase2_3ds> $<TARGET_FILE:phase2_3ds>.3dsx
            COMMENT "3dsxtool -> phase2_3ds.3dsx")

        # --- Phase 1 render test ----------------------------------------------
        # Links gfx_citro3d into a real executable and verifies rendered pixels.
        # A static library nobody links is not evidence the backend works.
        # It needs libultraship for gfx_cc_get_features - the backend decodes
        # real Fast3D shader ids, it does not reimplement that decode.
        add_executable(phase1_3ds tests/phase1_3ds.cpp)
        target_include_directories(phase1_3ds PRIVATE tests)
        target_link_libraries(phase1_3ds PRIVATE
            gfx_citro3d
            "${LUS_BUILD}/src/libultraship.a"
            "${LUS_BUILD}/libImGui.a"
            "${LUS_BUILD}/_deps/prism-build/libprism.a"
            "${LUS_BUILD}/libstb.a"
            "${LUS_BUILD}/libmonocypher.a"
            "${PORTLIBS}/lib/libzip.a" "${PORTLIBS}/lib/libspdlog.a"
            "${PORTLIBS}/lib/libtinyxml2.a" "${PORTLIBS}/lib/libzlibstatic.a"
            ${LIB_SDL2MAIN} ${LIB_SDL2} ${LIB_CITRO3D} ${LIB_CTRU} m)
        target_compile_options(phase1_3ds PRIVATE -Wall -Wextra)
        add_custom_command(TARGET phase1_3ds POST_BUILD
            COMMAND "${DEVKITPRO}/tools/bin/3dsxtool" $<TARGET_FILE:phase1_3ds> $<TARGET_FILE:phase1_3ds>.3dsx
            COMMENT "3dsxtool -> phase1_3ds.3dsx")
    else()
        message(STATUS "phase0_3ds/phase1_3ds skipped: build third_party/libultraship first (see patches/README.md)")
    endif()

else()
    add_executable(test_combiner tests/test_combiner.cpp)
    target_link_libraries(test_combiner PRIVATE pica_combiner)
    target_compile_options(test_combiner PRIVATE -Wall -Wextra)

    enable_testing()
    add_test(NAME combiner_lowering COMMAND test_combiner)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME setup_packaging COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_setup_packaging.py)
    add_test(NAME setup_bundle COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_setup_bundle.py)
    add_test(NAME setup_core COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_setup_core.py)
    add_test(NAME setup_torch_zip COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/setup_torch_zip.py)
    add_executable(test_interpolation_recording tests/interpolation_recording_test.cpp)
    target_include_directories(test_interpolation_recording PRIVATE tests/interpolation_stubs)
    add_test(NAME interpolation_retention COMMAND test_interpolation_recording)
    add_test(NAME frame_pacing COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/frame_pacing_test.py)
    add_test(NAME cvar_string COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cvar_string_test.py)
    add_test(NAME cvar_concurrency COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cvar_concurrency_test.py)
    add_test(NAME renderer_profile COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/renderer_profile_test.py)
    add_test(NAME frame_trace COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/frame_trace_test.py)
    add_test(NAME heartbeat_capture COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/heartbeat_capture_test.py)
    add_test(NAME debug_logging COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/debug_logging_test.py)
    add_test(NAME combiner_key COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/combiner_key_test.py)
    add_test(NAME triangle_pair COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/triangle_pair_test.py)
    add_test(NAME packed_vertex COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/packed_vertex_test.py)
    add_test(NAME gpu_command COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/gpu_command_test.py)
    add_test(NAME save_transactions COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/save_transaction_test.sh)
    add_test(NAME settings_commit COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/settings_commit_test.py)
    add_test(NAME enhancement_link COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/enhancement_link_test.py)
    add_test(NAME xml_display_list_ucode COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/xml_display_list_ucode_test.py)
    add_test(NAME mirrored_world COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mirrored_world_test.py)
    add_test(NAME cheat_controls_3ds COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cheat_controls_3ds_test.py)
    add_test(NAME archive_memory_3ds COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/archive_memory_3ds_test.py)
    add_test(NAME noclip_collision COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/noclip_collision_test.py)
    add_test(NAME infinite_ammo COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/infinite_ammo_test.py)
    add_test(NAME sun_depth_coordinates COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/sun_depth_test.py)
    add_test(NAME framebuffer_sampling COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/framebuffer_sampling_test.py)
    add_test(NAME randomizer_entropy COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/randomizer_entropy_test.py)
    add_test(NAME randomizer_timer COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/randomizer_timer_test.py)
    add_test(NAME randomizer_boot_settings_3ds COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/randomizer_boot_settings_3ds_test.py)
    add_test(NAME randomizer_settings_3ds COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/randomizer_settings_3ds_test.py)
    add_test(NAME randomizer_reachability_filter COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/randomizer_reachability_filter_test.py)
    add_test(NAME settings_font COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/settings_font_test.py)
    add_test(NAME quick_items_3ds COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/quick_items_3ds_test.py)
    add_test(NAME controls_3ds COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/controls_3ds_test.py)
    add_test(NAME tracker_3ds COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tracker_3ds_test.py)
    add_test(NAME randomizer_menu_3ds COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/randomizer_menu_3ds_test.py)
    add_test(NAME glow_depth COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/glow_depth_test.py)
    foreach(regression sampler depth timing)
        add_test(NAME renderer_${regression} COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/renderer_regression.py ${regression})
    endforeach()
    add_test(NAME audio_safety COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_safety_test.py)
    add_test(NAME sleep_3ds COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/sleep_3ds_test.py)
endif()

# ============================================================================
# SoH game code for 3DS
# ============================================================================
# The decomp (soh/src, ~409k lines) and the enhancement layer (soh/soh, ~151k).
# Built as its own target so failures are measured against real compile commands
# rather than a hand-rolled probe: the include order and definitions here are
# the ones the port will actually ship with.
if(N3DS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/shipwright/soh/src")
    set(SOH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/shipwright/soh")

    file(GLOB_RECURSE SOH_DECOMP_SOURCES "${SOH_DIR}/src/*.c")
    # Mirror soh/CMakeLists.txt's own exclusion list exactly (lines 189-204
    # upstream) rather than deriving one. These are the files the port replaces:
    # libultraship supplies the OS/IO/rmon layers, gu_pc.c supplies the matrix
    # helpers, stubs.c supplies an empty gDmaDataTable, and newlib supplies libc.
    # Guessing at this list produced a string of duplicate-symbol link errors.
    list(FILTER SOH_DECOMP_SOURCES EXCLUDE REGEX "src/dmadata/")
    list(FILTER SOH_DECOMP_SOURCES EXCLUDE REGEX "src/elf_message/")
    list(FILTER SOH_DECOMP_SOURCES EXCLUDE REGEX "src/libultra/io/")
    list(FILTER SOH_DECOMP_SOURCES EXCLUDE REGEX "src/libultra/libc/")
    list(FILTER SOH_DECOMP_SOURCES EXCLUDE REGEX "src/libultra/os/")
    list(FILTER SOH_DECOMP_SOURCES EXCLUDE REGEX "src/libultra/rmon/")
    # ...then add back the two upstream keeps.
    list(APPEND SOH_DECOMP_SOURCES "${SOH_DIR}/src/libultra/libc/sprintf.c")
    foreach(_gu cosf lookat lookathil perspective position sinf sqrtf us2dex)
        list(REMOVE_ITEM SOH_DECOMP_SOURCES "${SOH_DIR}/src/libultra/gu/${_gu}.c")
    endforeach()

    list(LENGTH SOH_DECOMP_SOURCES SOH_DECOMP_COUNT)
    message(STATUS "soh decomp: ${SOH_DECOMP_COUNT} translation units")

    add_library(soh_decomp STATIC ${SOH_DECOMP_SOURCES})
    target_include_directories(soh_decomp PRIVATE
        "${SOH_DIR}/include" "${SOH_DIR}" "${SOH_DIR}/src" "${SOH_DIR}/assets"
        "${LUS_DIR}/include" "${LUS_DIR}/src"
        "${DEVKITPRO}/libctru/include" "${PORTLIBS}/include")
    # SoH defines its CVar name prefixes and log levels as compile definitions
    # in CMake/soh-cvars.cmake and soh/CMakeLists.txt. Without them the decomp
    # sees CVAR_ENHANCEMENT("x") expand to garbage and reports thousands of
    # syntax errors that look like a porting problem and are not one.
    set(SW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/shipwright")
    include("${SW_DIR}/CMake/soh-cvars.cmake")
    # lus-cvars.cmake ends with a relative include of libultraship/cmake/cvars.cmake,
    # resolved against SoH's own root. Read it directly and satisfy that include
    # from our libultraship rather than restructuring their tree.
    file(READ "${SW_DIR}/CMake/lus-cvars.cmake" _lus_cvars)
    string(REPLACE "include(\"libultraship/cmake/cvars.cmake\")" "" _lus_cvars "${_lus_cvars}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/lus-cvars-local.cmake" "${_lus_cvars}")
    include("${CMAKE_CURRENT_BINARY_DIR}/lus-cvars-local.cmake")
    include("${LUS_DIR}/cmake/cvars.cmake")
    target_compile_definitions(soh_decomp PRIVATE
        F3DEX_GBI_2 NDEBUG INCLUDE_GAME_PRINTF
        LOG_LEVEL_GAME_PRINTS=6) # SPDLOG_LEVEL_OFF, as every console build uses
    # The decomp is machine-generated from N64 code and freely mixes pointers
    # with uintptr_t/u32. GCC 14 promoted -Wincompatible-pointer-types and
    # -Wint-conversion from warnings to errors by default, and devkitARM here is
    # GCC 16 - so code that builds fine on SoH's supported toolchains fails
    # here for a reason that has nothing to do with the 3DS. -fpermissive puts
    # them back to warnings; -w then silences the noise.
    # -Os, not -O0. Without an explicit -O the decomp compiled unoptimised and
    # .text came out at 27 MB - resident in the same 96 MB APPLICATION region as
    # the heap, which is why the randomizer's tables ran out of memory. Size is
    # the right axis to optimise: every MB of code is a MB the game cannot use.
    target_compile_options(soh_decomp PRIVATE -w -fpermissive -Os)
    set_target_properties(soh_decomp PROPERTIES C_STANDARD 11 EXCLUDE_FROM_ALL TRUE)
    # SoH-3DS: the per-tick game logic is the other half of the frame budget
    # (hardware loop60 = 8-18 ms per tick in play, vs ~15 ms render). The
    # oot-PSP port compiles hot files at -O3 over a -O2 base; here the files
    # that dominate a tick - actor update, collision, background check,
    # matrix, skeleton animation, camera, lighting, room/scene draw - get -O2
    # while the bulk (overlays, text, menus) stays -Os for the text budget.
    # These ~14 files are ~0.3 MB of the 17.8 MB text, so the size cost is
    # bounded regardless of the per-file growth.
    set(SOH3DS_HOT_DECOMP
        z_actor z_collision_check z_bgcheck sys_matrix z_skelanime z_skin_matrix
        z_camera z_lights z_play z_room z_scene z_view z_rcp z_effect_soft_sprite
        z_eff_shield_particle z_en_item00 z_quake z_player_lib)
    foreach(_hot ${SOH3DS_HOT_DECOMP})
        set_source_files_properties("${SOH_DIR}/src/code/${_hot}.c" PROPERTIES COMPILE_OPTIONS "-O2")
    endforeach()
    # Link's own actor overlay too.
    set_source_files_properties("${SOH_DIR}/src/overlays/actors/ovl_player_actor/z_player.c"
                                PROPERTIES COMPILE_OPTIONS "-O2")


    # --- SoH enhancement layer (soh/soh) ------------------------------------
    # C++: ImGui menus, randomizer, save editor, cvar system. Unlike MK64-3DS,
    # which stubs ImGui out entirely, this port needs it: the menu system is a
    # main reason to run SoH rather than an OoT decomp, and the bottom screen is
    # the natural place to put it.
    file(GLOB_RECURSE SOH_ENH_SOURCES "${SOH_DIR}/soh/*.cpp" "${SOH_DIR}/soh/*.c")
    # Network/Anchor is SoH's multiplayer sync and needs SDL2_net, which
    # devkitPro does not package for 3DS. Extractor/ runs torch on-device; we
    # extract on the host instead. Both are out of scope for a first boot and
    # neither is load-bearing for single-player.
    list(FILTER SOH_ENH_SOURCES EXCLUDE REGEX "/soh/Network/")
    list(FILTER SOH_ENH_SOURCES EXCLUDE REGEX "/soh/Extractor/")
    # Speech synthesis: SAPI is Windows-only and ESpeak is an optional desktop
    # dependency. Upstream gates ESpeak behind a CMake option already.
    list(FILTER SOH_ENH_SOURCES EXCLUDE REGEX "/speechsynthesizer/")
    list(LENGTH SOH_ENH_SOURCES SOH_ENH_COUNT)
    message(STATUS "soh enhancement layer: ${SOH_ENH_COUNT} translation units")

    add_library(soh_enhancement STATIC ${SOH_ENH_SOURCES})
    target_include_directories(soh_enhancement PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${SOH_DIR}/include" "${SOH_DIR}" "${SOH_DIR}/src" "${SOH_DIR}/assets"
        "${SOH_DIR}/soh"
        "${LUS_DIR}/include" "${LUS_DIR}/src"
        "${LUS_BUILD}/_deps/threadpool-src/include"
        "${LUS_BUILD}/_deps/imgui-src"
        "${LUS_BUILD}/_deps/stb"
        "${LUS_BUILD}/_deps/nlohmann_json-src/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dr_libs"
        # Shadow headers for things devkitPro does not package. Placed last so a
        # real implementation, if one ever appears, wins.
        "${CMAKE_CURRENT_SOURCE_DIR}/src/compat3ds"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libogg/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvorbis/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opus/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opusfile/include"
        "${DEVKITPRO}/libctru/include" "${PORTLIBS}/include" "${PORTLIBS}/include/SDL2")
    target_compile_definitions(soh_enhancement PRIVATE
        F3DEX_GBI_2 NDEBUG INCLUDE_GAME_PRINTF LOG_LEVEL_GAME_PRINTS=6)
    target_compile_options(soh_enhancement PRIVATE -w -fpermissive -Os)
    # Generation spends most of its time evaluating reachability and inventory.
    # Optimize these small hot files for speed; retain -Os for the wider port.
    foreach(_rando_hot 3drando/fill logic location_access)
        set_source_files_properties("${SOH_DIR}/soh/Enhancements/randomizer/${_rando_hot}.cpp"
                                    PROPERTIES COMPILE_OPTIONS "-O2")
    endforeach()
    set_target_properties(soh_enhancement PROPERTIES CXX_STANDARD 20 EXCLUDE_FROM_ALL TRUE)
    # --- PC-free first-run setup --------------------------------------------
    include(cmake/Setup3DS.cmake)

    # --- The game itself ----------------------------------------------------
    # main() lives in the decomp (soh/src/code/main.c) and calls InitOTR() from
    # the enhancement layer, which brings up libultraship's window and renderer.
    # SDL2main is deliberately NOT linked: it defines its own main() and would
    # collide with the decomp's.
    # soh_hud_bridge.c is the game side of the bottom-screen HUD: it is the only
    # place that sees both the decomp headers and the plain struct src/pica/
    # reads, which keeps libultra's types out of the renderer.
    add_executable(soh_3ds tests/soh_3ds_main.cpp
        src/compat3ds/logging_3ds.cpp
        src/compat3ds/sleep_3ds.cpp
        src/compat3ds/excluded_stubs.cpp
        src/compat3ds/network_stubs.cpp)
    soh3ds_attach_pc_free_setup(soh_3ds)
    target_include_directories(soh_3ds PRIVATE
        src tests src/compat3ds
        "${LUS_BUILD}/_deps/imgui-src"
        "${LUS_BUILD}/_deps/imgui-src/backends"
        "${LUS_BUILD}/_deps/nlohmann_json-src/include"
        "${LUS_BUILD}/_deps/threadpool-src/include"
        "${SOH_DIR}/include" "${SOH_DIR}" "${SOH_DIR}/src" "${SOH_DIR}/soh"
        "${LUS_DIR}/include" "${LUS_DIR}/src"
        "${DEVKITPRO}/libctru/include" "${PORTLIBS}/include" "${PORTLIBS}/include/SDL2")
    target_link_libraries(soh_3ds PRIVATE
        # Everything in this group calls into everything else: the decomp and the
        # enhancement layer are mutually recursive, and libultraship reaches the
        # PICA200 backend through factory hooks that live in the renderer library.
        -Wl,--start-group
        # Many enhancements register only through a static constructor. Keep
        # their objects even when nothing directly references a function in
        # them, or the menu exposes switches whose hooks never reach the game.
        -Wl,--whole-archive soh_enhancement -Wl,--no-whole-archive
        soh_decomp ${SOH3DS_GFX_LIB}
        soh3ds_setup_core soh3ds_torch_oot
        "${LUS_BUILD}/src/libultraship.a"
        -Wl,--end-group
        "${LUS_BUILD}/libImGui.a"
        "${LUS_BUILD}/_deps/prism-build/libprism.a"
        "${LUS_BUILD}/libstb.a"
        "${LUS_BUILD}/libmonocypher.a"
        "${PORTLIBS}/lib/libzip.a" "${PORTLIBS}/lib/libspdlog.a"
        "${PORTLIBS}/lib/libtinyxml2.a" "${PORTLIBS}/lib/libzlibstatic.a"
        ${LIB_SDL2} ${LIB_CITRO3D} ${LIB_CTRU} m)
    target_compile_options(soh_3ds PRIVATE -w -fpermissive -Os)
    # SoH-3DS: std::thread on this toolchain gets a 32 KiB heap stack (libctru
    # __syscall_thread_create default) - overflowed by SaveManager's worker
    # (std::filesystem + nlohmann; reproducible data abort at a prologue push).
    # Wrap the one choke point every pthread/std::thread/BS::thread_pool routes
    # through and raise the floor; wrapper in tests/soh_3ds_main.cpp.
    target_link_options(soh_3ds PRIVATE -Wl,--wrap=__syscall_thread_create -Wl,--wrap=threadCreate)
    set_target_properties(soh_3ds PROPERTIES CXX_STANDARD 20 EXCLUDE_FROM_ALL TRUE)
    soh3ds_add_pc_free_artifacts(soh_3ds)

    # Isolate the command-writer experiment, sharing the exact baseline game
    # libraries and SDK. Build explicitly with --target soh_3ds_gpucmd.
    get_target_property(SOH_MAIN_SOURCES soh_3ds SOURCES)
    add_executable(soh_3ds_gpucmd ${SOH_MAIN_SOURCES}
        platform/3ds/source/gpu_command_fast_3ds.c)
    foreach(prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES LINK_OPTIONS)
        get_target_property(SOH_MAIN_PROP soh_3ds ${prop})
        if(SOH_MAIN_PROP)
            set_property(TARGET soh_3ds_gpucmd PROPERTY ${prop} "${SOH_MAIN_PROP}")
        endif()
    endforeach()
    set_source_files_properties(platform/3ds/source/gpu_command_fast_3ds.c
        PROPERTIES COMPILE_OPTIONS -O3)
    target_link_options(soh_3ds_gpucmd PRIVATE -Wl,--wrap=GPUCMD_Add
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/soh_3ds_gpucmd.map")
    set_target_properties(soh_3ds_gpucmd PROPERTIES CXX_STANDARD 20 EXCLUDE_FROM_ALL TRUE)
    add_dependencies(soh_3ds_gpucmd soh3ds_setup_romfs)
    set_property(TARGET soh_3ds_gpucmd APPEND PROPERTY LINK_DEPENDS "${SOH3DS_SETUP_ROMFS_STAMP}")
    soh3ds_add_pc_free_artifacts(soh_3ds_gpucmd)
endif()


# ============================================================================
# MK64-3DS renderer (vendored)
# ============================================================================
# platform/3ds/ is adapted from EstebanPdN/mario-kart-64-3ds - the only
# Fast3D-on-PICA200 port over libultraship proven on real hardware. See
# platform/3ds/PROVENANCE.md for the licence position, which is not permissive.
if(N3DS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/source/gfx_citro3d.cpp")
    set(MK_SHBIN "${CMAKE_CURRENT_BINARY_DIR}/fast3d_passthrough.shbin")
    set(MK_SHBIN_C "${CMAKE_CURRENT_BINARY_DIR}/fast3d_passthrough_shbin.c")
    add_custom_command(
        OUTPUT "${MK_SHBIN_C}"
        COMMAND "${DEVKITPRO}/tools/bin/picasso" -o "${MK_SHBIN}"
                "${CMAKE_CURRENT_SOURCE_DIR}/platform/3ds/shaders/fast3d_passthrough.v.pica"
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bin2c.py" "${MK_SHBIN}" "${MK_SHBIN_C}"
                fast3d_passthrough_shbin
        DEPENDS platform/3ds/shaders/fast3d_passthrough.v.pica scripts/bin2c.py
        COMMENT "picasso + bin2c -> fast3d_passthrough_shbin")

    add_library(mk_gfx_citro3d STATIC
        platform/3ds/source/gfx_citro3d.cpp
        platform/3ds/source/gfx_window_manager_3ds.cpp
        src/mk_gfx_factory.cpp
        "${MK_SHBIN_C}")
    target_include_directories(mk_gfx_citro3d PUBLIC src
        platform/3ds/include
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${LUS_DIR}/include" "${LUS_DIR}/src"
        "${LUS_BUILD}/_deps/imgui-src"
        "${LUS_BUILD}/_deps/threadpool-src/include"
        "${LUS_BUILD}/_deps/nlohmann_json-src/include"
        "${LUS_BUILD}/_deps/stb"
        "${DEVKITPRO}/libctru/include" "${PORTLIBS}/include" "${PORTLIBS}/include/SDL2")
    target_compile_options(mk_gfx_citro3d PRIVATE
        -O3 -fomit-frame-pointer -fno-strict-aliasing -w)
    # C++20, not 17: the SoH-3DS window backend includes fast/Fast3dGui.h to
    # bring up ImGui, and that pulls in fast/interpreter.h, which uses C++20
    # defaulted operator== and the spaceship operator. soh_3ds is C++20 too.
    set_target_properties(mk_gfx_citro3d PROPERTIES CXX_STANDARD 20 EXCLUDE_FROM_ALL TRUE)
endif()
