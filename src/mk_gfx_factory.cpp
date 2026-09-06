// Factory hooks binding libultraship's backend selection to the vendored
// MK64-3DS renderer in platform/3ds/.
//
// The patched libultraship reaches the PICA200 backend through two extern
// factories (see patches/libultraship-3ds.patch, Window::Init) rather than
// including citro3d itself. src/pica/ defines them inline in its own
// translation units; the vendored backend cannot, because it is kept as a
// readable diff against upstream MK64-3DS and upstream has no such hook - it is
// driven directly by MK64's own game_runtime_3ds.cpp. So the hook lives here,
// outside platform/3ds/, which also keeps the vendored tree free of SoH glue.
//
// Both backends declare Fast::GfxRenderingAPICitro3D, so exactly one of
// gfx_citro3d and mk_gfx_citro3d may be linked. CMake's SOH3DS_USE_MK_RENDERER
// picks which, and this file is only compiled into the latter.

#include <cstdio>

#include "gfx_citro3d.h"
#include "gfx_window_manager_3ds.h"

namespace Fast {

GfxRenderingAPI* CreateCitro3DRenderingAPI() {
    std::fprintf(stderr, "soh-3ds gfx: backend=mk64-vendored (platform/3ds)\n");
    return new GfxRenderingAPICitro3D();
}

GfxWindowBackend* CreateCtrWindowBackend() {
    std::fprintf(stderr, "soh-3ds gfx: window=GfxWindowBackend3DS\n");
    return new GfxWindowBackend3DS();
}

} // namespace Fast
