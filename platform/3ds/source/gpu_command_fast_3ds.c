#include <3ds/types.h>
#include <3ds/gpu/gpu.h>
#include <3ds/svc.h>

void __real_GPUCMD_Add(u32 header, const u32* param, u32 paramlength);

// Experimental link wrapper. Short register writes are common in Citro3D;
// avoid the general chunking/copy loop while preserving its command format.
// Only the soh_3ds_gpucmd target uses this; the shared SDK stays unchanged.
void __wrap_GPUCMD_Add(u32 header, const u32* param, u32 paramlength) {
    if (paramlength == 1) {
        const u32 offset = gpuCmdBufOffset;
        if (!gpuCmdBuf || offset > gpuCmdBufSize || gpuCmdBufSize - offset < 2) {
            svcBreak(USERBREAK_PANIC);
            return;
        }
        // Load before writing: param may point inside the destination buffer.
        const u32 value = param ? *param : 0;
        gpuCmdBuf[offset] = value;
        gpuCmdBuf[offset + 1] = header;
        gpuCmdBufOffset = offset + 2;
        return;
    }
    __real_GPUCMD_Add(header, param, paramlength);
}
