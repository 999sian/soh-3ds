#=================== Top-Level ===================
file(GLOB Source_Files__TopLevel RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.cpp")

source_group("" FILES ${Source_Files__TopLevel})
target_sources(libultraship PRIVATE ${Source_Files__TopLevel})

if(N3DS AND SOH3DS_TRIANGLE_RUN_REUSE)
    set_property(SOURCE interpreter.cpp TARGET_DIRECTORY libultraship
        APPEND PROPERTY COMPILE_DEFINITIONS SOH3DS_TRIANGLE_RUN_REUSE=1)
endif()

if(N3DS AND SOH3DS_ARM11_VERTEX_ASM)
    target_sources(libultraship PRIVATE vertex_transform_arm11.S)
    target_compile_definitions(libultraship PRIVATE SOH3DS_ARM11_VERTEX_ASM=1)
endif()

if(N3DS AND SOH3DS_ARM11_TRIANGLE_EMIT)
    target_sources(libultraship PRIVATE triangle_emit_arm11.S)
    set_property(SOURCE interpreter.cpp TARGET_DIRECTORY libultraship
        APPEND PROPERTY COMPILE_DEFINITIONS SOH3DS_ARM11_TRIANGLE_EMIT=1)
endif()

if(N3DS AND SOH3DS_COMPACT_VERTEX_STREAM)
    set_property(SOURCE interpreter.cpp TARGET_DIRECTORY libultraship
        APPEND PROPERTY COMPILE_DEFINITIONS SOH3DS_COMPACT_VERTEX_STREAM=1)
endif()

#=================== Debug ===================

file(GLOB Source_Files__Debug RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "debug/*.cpp")

source_group("debug" FILES ${Source_Files__Debug})
target_sources(libultraship PRIVATE ${Source_Files__Debug})

#=================== Resource ===================

file(GLOB Source_Files__Resource__Types RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "resource/type/*.h" "resource/type/*.cpp")
source_group("resource/type" FILES ${Source_Files__Resource__Types})

file(GLOB Source_Files__Resource__Factories RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "resource/factory/*.h" "resource/factory/*.cpp")
source_group("resource/factory" FILES ${Source_Files__Resource__Factories})

target_sources(libultraship PRIVATE ${Source_Files__Resource} ${Source_Files__Resource__Types} ${Source_Files__Resource__Factories} ${Source_Files__Resource__Archive})

#=================== Graphic ===================

file(GLOB Source_Files__Graphic RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "backends/*.cpp")

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "graphic/Fast3D/backends/gfx_dxgi*")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "graphic/Fast3D/backends/gfx_direct3d*")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "graphic/Fast3D/backends/dxsdk/*")
endif()

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "graphic/Fast3D/backends/gfx_metal*")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "graphic/Fast3D/backends/gfx_opengl*")
endif()


# SoH-3DS: PICA200 has no OpenGL and the 3DS has no dlfcn/GLES2 headers, so the
# OpenGL backend cannot compile. The 3DS supplies gfx_citro3d instead.
if(N3DS)
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "gfx_opengl")
    list(FILTER Source_Files__Graphic EXCLUDE REGEX "gfx_sdl2")
endif()

source_group("graphic" FILES ${Source_Files__Graphic})
target_sources(libultraship PRIVATE ${Source_Files__Graphic})
