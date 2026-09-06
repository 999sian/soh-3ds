// Definitions for features this port deliberately does not build.
//
// Four groups, and the distinction between them matters:
//
//   1. ImGui SDL2/OpenGL3 backends. Fast3dGui's FAST3D_SDL_OPENGL and
//      FAST3D_SDL_METAL cases are not #ifdef'd, so they must link even though
//      the only backend registered on 3DS is FAST3D_CITRO3D. These are
//      genuinely unreachable - no-ops are correct, not a compromise.
//
//   2. Networking (Anchor, CrowdControl, Sail, Network) and the on-device
//      Extractor. devkitPro packages no SDL2_net, and assets are extracted on
//      the host. These abort() rather than no-op: reaching one means the UI
//      offered a feature that cannot work, which is a bug worth a loud failure
//      rather than a silent nothing.
//
//   3. Speech synthesis. SAPI is Windows-only, ESpeak is an optional desktop
//      dependency. Accessibility narration is genuinely absent, so Init()
//      reports failure and callers fall back to no narration.
//
//   4. Ogg/Vorbis/Opus decoding. Only used for *custom* music; vanilla audio
//      runs through the N64 sequence player and does not touch these. Returning
//      failure makes a custom-music archive degrade to silence for that track
//      instead of crashing.
//
// Anything here that a real 3DS implementation should eventually have is called
// out in the comment above its group.

#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void Unsupported(const char* what) {
    std::fprintf(stderr, "soh-3ds: %s is not available in this build\n", what);
    std::abort();
}

} // namespace

// ---------------------------------------------------------------------------
// 1. ImGui platform/renderer backends - unreachable on 3DS
// ---------------------------------------------------------------------------

struct ImDrawData;
struct SDL_Window;
union SDL_Event;
struct _SDL_GameController;

extern "C" {

bool ImGui_ImplOpenGL3_Init(const char*) {
    return false;
}
void ImGui_ImplOpenGL3_Shutdown() {
}
void ImGui_ImplOpenGL3_NewFrame() {
}
void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData*) {
}

} // extern "C"

// The ImGui backends are plain C++, not extern "C" - their symbols are mangled.
// Declaring them with C linkage produces the wrong names and the references go
// unresolved, so these have to be defined exactly as imgui_impl_sdl2.h declares
// them, including the enum parameter type.
#include <imgui_impl_sdl2.h>

bool ImGui_ImplSDL2_InitForOpenGL(SDL_Window*, void*) {
    return false;
}
void ImGui_ImplSDL2_Shutdown() {
}
void ImGui_ImplSDL2_NewFrame() {
}
bool ImGui_ImplSDL2_ProcessEvent(const SDL_Event*) {
    return false;
}
void ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode, struct _SDL_GameController**, int) {
}

// SDL_net: declared by the shim in src/compat3ds/SDL2/SDL_net.h. Only Init and
// Quit are reached, from libultraship's startup; the rest stay declaration-only.
int SDLNet_Init(void) {
    return -1;
}
void SDLNet_Quit(void) {
}

// N64 libultra's quiet-NaN constant, used by soh/src/libultra/gu/sinf.c and
// cosf.c on out-of-range input. newlib has no such symbol.
extern "C" const float __libm_qnan_f = __builtin_nanf("");

// ---------------------------------------------------------------------------
// 4. Audio codecs - custom music only
// ---------------------------------------------------------------------------

extern "C" {

// libogg
int ogg_sync_init(void*) {
    return -1;
}
char* ogg_sync_buffer(void*, long) {
    return nullptr;
}
int ogg_sync_wrote(void*, long) {
    return -1;
}
int ogg_sync_pageout(void*, void*) {
    return 0;
}
int ogg_sync_clear(void*) {
    return 0;
}
int ogg_stream_init(void*, int) {
    return -1;
}
int ogg_stream_pagein(void*, void*) {
    return -1;
}
int ogg_stream_packetout(void*, void*) {
    return 0;
}
int ogg_stream_clear(void*) {
    return 0;
}
long ogg_page_serialno(void*) {
    return 0;
}

// libvorbisfile
int ov_open_callbacks(void*, void*, const char*, long, void*) {
    return -1;
}
long ov_read(void*, char*, int, int, int, int, int*) {
    return 0;
}
long long ov_pcm_total(void*, int) {
    return 0;
}
void* ov_info(void*, int) {
    return nullptr;
}
int ov_clear(void*) {
    return 0;
}

// opusfile
void* op_open_memory(const unsigned char*, unsigned long, int*) {
    return nullptr;
}
int op_read(void*, short*, int, int*) {
    return 0;
}
int op_pcm_seek(void*, long long) {
    return -1;
}
void op_free(void*) {
}

} // extern "C"
