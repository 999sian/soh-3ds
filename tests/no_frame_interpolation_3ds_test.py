#!/usr/bin/env python3
"""One recorder executable: Old bypasses hooks; New still records/interpolates."""
from pathlib import Path
import argparse
import subprocess
import tempfile
import re

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / 'third_party/shipwright/soh'
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path)
args = parser.parse_args()
prefix = (ROOT / 'tests/interpolation_recording_test.cpp').read_text().split('// These walkers run only in the test')[0]
prefix = prefix.replace('#include "../third_party/shipwright/soh/soh/frame_interpolation.cpp"',
                        '#include "' + str(GAME / 'soh/frame_interpolation.cpp') + '"')
# Recorder internals are visible only to this translation unit. Actual game
# call-site guards are compiled in separate C/C++ translation units below.
startup = (ROOT / 'tests/soh_3ds_main.cpp').read_text()
model_globals = re.search(r'static bool sUsesOldProfile = true;\nbool gSoh3dsFrameInterpolationEnabled = false;', startup).group()
model_query = startup.split('static void SohCtrEarlyLogInit() {', 1)[1].split('    Soh3dsConfigureDebugOutput();', 1)[0]
code = '#include "' + str(ROOT / 'platform/3ds/include/model_profile_3ds.h') + '"\n'
code += 'extern "C" {\n' + model_globals + '\n}\n'
code += prefix + r'''
static bool querySucceeded, queriedNew;
#define R_SUCCEEDED(result) ((result) == 0)
int APT_CheckNew3DS(bool* isNew) { *isNew = queriedNew; return querySucceeded ? 0 : -1; }
void DetectModel(bool succeeded, bool isNew) {
    querySucceeded = succeeded; queriedNew = isNew;
''' + model_query + r'''
}
extern "C" void ExerciseHooks(void);
extern "C" void ExerciseCHooks(void);
extern "C" void ExerciseNew(void);
int main() {
    assert(allocatedBytes == 0 && allocationCalls == 0);
    OTRGlobals globals; OTRGlobals::Instance = &globals;
    DetectModel(false, true); // Failed query conservatively selects Old.
    assert(sUsesOldProfile && !gSoh3dsFrameInterpolationEnabled);
    DetectModel(true, false);
    assert(sUsesOldProfile && !gSoh3dsFrameInterpolationEnabled);
    const auto initialGeneration = record_generation;
    for (int i = 0; i < 100000; ++i) { ExerciseHooks(); ExerciseCHooks(); }
    assert(allocatedBytes == 0 && allocationCalls == 0);
    assert(record_generation == initialGeneration && camera_epoch == 0 && previous_camera_epoch == 0);
    assert(!is_recording && current_path.empty());
    assert(!current_recording.root_path && !previous_recording.root_path);
    puts("PASS: Old: 100000 ticks, zero recording allocations, hook arguments and bookkeeping");
    DetectModel(true, true);
    assert(!sUsesOldProfile && gSoh3dsFrameInterpolationEnabled);
    ExerciseNew();
    assert(allocationCalls > 0 && record_generation > 0 && !is_recording);
    puts("PASS: same executable: New recording, matrix midpoint, camera epoch and scene reset");
}
'''
hooks = r'''
#include <assert.h>
#include <stdint.h>
#include "soh/frame_interpolation.h"
#include "include/macros.h"
#ifdef __cplusplus
extern "C"
#endif
void HOOK_ENTRY(void) {
    int evaluated = 0;
    FrameInterpolation_StartRecord();
    FrameInterpolation_RecordOpenChild((const void*)(uintptr_t)++evaluated, 0);
    FrameInterpolation_RecordMatrixPush();
    FrameInterpolation_RecordMatrixTranslate(++evaluated, 2, 3, 0);
    FrameInterpolation_RecordMatrixMtxFToMtx((MtxF*)(uintptr_t)++evaluated, 0);
    FrameInterpolation_RecordMatrixPop();
    FrameInterpolation_RecordCloseChild();
    FrameInterpolation_DontInterpolateCamera();
    assert(FrameInterpolation_GetCameraEpoch() == 0);
    FrameInterpolation_StopRecord();
    FrameInterpolation_ShrinkRecording();
    OPEN_DISPS(0); CLOSE_DISPS(0);
    assert(evaluated == 0);
}
'''
new = r'''
extern "C" void ExerciseNew(void) {
    int evaluated = 0;
    Mtx destination{};
    for (int frame = 0; frame < 2; ++frame) {
        FrameInterpolation_StartRecord();
        FrameInterpolation_RecordOpenChild(&destination, 0);
        MtxF matrix{}; matrix.mf[0][0] = float(2 + 4 * frame);
        FrameInterpolation_RecordMatrixMtxFToMtx(&matrix, &destination);
        FrameInterpolation_RecordCloseChild();
        // Call-site argument evaluation and OPEN_DISPS must still run on New.
        FrameInterpolation_RecordOpenChild(nullptr, ++evaluated);
        FrameInterpolation_RecordCloseChild();
        OPEN_DISPS(nullptr); CLOSE_DISPS(nullptr);
        FrameInterpolation_StopRecord();
    }
    assert(evaluated == 2);
    auto values = FrameInterpolation_Interpolate(0.5f);
    assert(values.at(&destination).mf[0][0] == 4.0f);
    int epoch = FrameInterpolation_GetCameraEpoch();
    FrameInterpolation_DontInterpolateCamera();
    assert(FrameInterpolation_GetCameraEpoch() == epoch + 1);
    FrameInterpolation_ShrinkRecording();
}
'''
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    profiler = p / 'fast/backends/gfx_profile_3ds.h'
    profiler.parent.mkdir(parents=True)
    profiler.write_text('enum class Soh3dsProfileSection {Interpolate}; struct Soh3dsProfileScope {Soh3dsProfileScope(Soh3dsProfileSection){}};')
    endian = p / 'ship/utils/binarytools/endianness.h'
    endian.parent.mkdir(parents=True)
    endian.write_text('')
    math = p / 'include/z64math.h'
    math.parent.mkdir(parents=True)
    types = (ROOT / 'tests/interpolation_stubs/include/z64math.h').read_text().replace('<cstdint>', '<stdint.h>')
    types = re.sub(r'using (\w+) = (\w+);', r'typedef \2 \1;', types)
    types = re.sub(r'struct (\w+) \{([^}]+)\};', r'typedef struct \1 {\2} \1;', types)
    math.write_text(types)
    # OPEN_DISPS release expansion needs the pointer type, no engine context.
    (p / 'types.h').write_text('typedef struct GraphicsContext GraphicsContext;')
    (p / 'recorder.cpp').write_text(code)
    (p / 'hooks.cpp').write_text(hooks.replace('HOOK_ENTRY', 'ExerciseHooks') + new)
    (p / 'hooks.c').write_text(hooks.replace('HOOK_ENTRY', 'ExerciseCHooks'))
    includes = ['-I' + str(p), '-I' + str(ROOT / 'tests/interpolation_stubs'), '-I' + str(GAME)]
    flags = ['-O2', '-D__3DS__', '-include', str(p / 'types.h'), *includes]
    # Use release OPEN_DISPS without disabling the runtime assertions.
    for f in ['hooks.cpp', 'hooks.c']:
        text = (p / f).read_text().replace('#include "include/macros.h"', '#define NDEBUG\n#include "include/macros.h"\n#undef NDEBUG')
        (p / f).write_text(text)
    subprocess.run(['cc', '-std=c11', *flags, '-c', str(p / 'hooks.c'), '-o', str(p / 'hooks.o')], check=True)
    subprocess.run(['c++', '-std=c++20', *flags, str(p / 'recorder.cpp'), str(p / 'hooks.cpp'), str(p / 'hooks.o'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        for name in ['recorder.cpp', 'hooks.cpp', 'hooks.c']:
            (args.output / name).write_text((p / name).read_text())
