#!/usr/bin/env python3
"""Exercise timing guards, cleanup and production Graph_Update scope ordering."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
body = r'''
#include "fast/backends/game_profile_3ds.h"
#include <assert.h>
bool gSoh3dsRenderProfileEnabled = false;
unsigned begins = 0, ends = 0;
uint64_t Soh3dsGameProfileBegin(unsigned section) {
    assert(section == SOH3DS_GAME_ACTOR_UPDATE); ++begins; return 99;
}
void Soh3dsGameProfileEnd(unsigned section, uint64_t start) {
    assert(section == SOH3DS_GAME_ACTOR_UPDATE && start == 99); ++ends;
}
static int work(int early) {
    SOH3DS_GAME_PROFILE_SCOPE(scope, SOH3DS_GAME_ACTOR_UPDATE);
    if (early) return 4;
    return 5;
}
int main(void) {
    for (int i = 0; i < 100000; ++i) assert(work(i & 1) == ((i & 1) ? 4 : 5));
    assert(begins == 0 && ends == 0);
#ifdef __3DS__
    gSoh3dsRenderProfileEnabled = true;
    work(1); work(0);
    assert(begins == 2 && ends == 2);
    { SOH3DS_GAME_PROFILE_SCOPE(scope, SOH3DS_GAME_ACTOR_UPDATE);
      gSoh3dsRenderProfileEnabled = false; }
    assert(begins == 3 && ends == 3);
#endif
}
'''
missing = r'''
#include "fast/backends/game_profile_3ds.h"
int main(void) { SOH3DS_GAME_PROFILE_SCOPE(scope, SOH3DS_GAME_UPDATE); }
'''

# Compile the actual entry through the first gameplay operation: moving its
# scope above the debugger return must fail, as must losing the normal scope.
# Keep RunFrame's production call ordering, including its C/3DS timing guards.
graph = (root / 'third_party/shipwright/soh/src/code/graph.c').read_text()
entry_begin = graph.index('void Graph_Update(GraphicsContext* gfxCtx, GameState* gameState) {')
entry_end = graph.index('    OPEN_DISPS(gfxCtx);', entry_begin)
frame_update = graph.index('            Graph_Update(', entry_end)
frame_begin = graph.rindex('#ifdef __3DS__', entry_end, frame_update)
submit = '            Graph_ProcessGfxCommands(runFrameContext.gfxCtx.workBuffer);'
frame_end = graph.index(submit, frame_update) + len(submit)
graph_ordering = r'''
#include "fast/backends/game_profile_3ds.h"
#include <assert.h>
typedef unsigned u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef struct { int* workBuffer; } GraphicsContext;
typedef struct { int unk_A0; } GameState;
static int displayList, gPadMgr;
static struct { GraphicsContext gfxCtx; } runFrameContext = { { &displayList } };
static GameState state, *gGameState = &state;
bool gSoh3dsRenderProfileEnabled = false;
static bool debugging;
static unsigned begins, ends, updates, renders;
uint64_t Soh3dsGameProfileBegin(unsigned section) {
    assert(section == SOH3DS_GAME_UPDATE);
    ++begins;
    return 99;
}
void Soh3dsGameProfileEnd(unsigned section, uint64_t start) {
    assert(section == SOH3DS_GAME_UPDATE && start == 99);
    ++ends;
}
static bool GfxDebuggerIsDebugging(void) { return debugging; }
static bool GfxDebuggerIsDebuggingRequested(void) { return false; }
static void GfxDebuggerDebugDisplayList(int* buffer) { (void)buffer; assert(0); }
static void Graph_ProcessGfxCommands(int* buffer) {
    assert(buffer == &displayList);
    assert(begins == ends && "graphics submission must have no open update token");
    if (debugging) assert(begins == 0 && "debugger bypass must start no update token");
    ++renders;
}
static void Graph_InitTHGA(GraphicsContext* gfxCtx) {
    assert(gfxCtx == &runFrameContext.gfxCtx && state.unk_A0 == 0);
#ifdef __3DS__
    if (gSoh3dsRenderProfileEnabled) assert(begins == 1 && ends == 0);
    else assert(begins == 0 && ends == 0);
#else
    assert(begins == 0 && ends == 0);
#endif
    ++updates;
}
static void Graph_StartFrame(void) {}
static void PadMgr_ThreadEntry(int* pad) { assert(pad == &gPadMgr); }
#ifdef __3DS__
u64 gSoh3dsTickPhaseTicks[3];
s64 svcGetSystemTick(void) { static s64 ticks; return ++ticks; }
#endif
''' + graph[entry_begin:entry_end] + r'''
    // The rest of gameplay is outside this entry/control-flow regression.
    (void)problem;
}
static void RunFrameEntry(void) {
''' + graph[frame_begin:frame_end] + r'''
}
int main(void) {
    for (int enabled = 0; enabled < 2; ++enabled) {
        gSoh3dsRenderProfileEnabled = enabled;
        for (int debug = 0; debug < 2; ++debug) {
            debugging = debug;
            begins = ends = updates = renders = 0;
            state.unk_A0 = 7;
            RunFrameEntry();
            if (debugging) {
                // The debugger renders inside Graph_Update, then again at
                // RunFrame's existing outer submission, with no game update.
                assert(renders == 2 && updates == 0 && state.unk_A0 == 7);
                assert(begins == 0 && ends == 0);
            } else {
                assert(renders == 1 && updates == 1 && state.unk_A0 == 0);
#ifdef __3DS__
                assert(begins == (unsigned)enabled && ends == (unsigned)enabled);
#else
                assert(begins == 0 && ends == 0);
#endif
            }
        }
    }
}
'''
with tempfile.TemporaryDirectory(prefix='soh-game-profile-') as directory:
    p = Path(directory)
    for compiler, extension, standard in [('cc', 'c', 'c11'), ('c++', 'cpp', 'c++20')]:
        for flags in [[], ['-D__3DS__']]:
            for name, source in [('hooks', body), ('missing', missing), ('graph-ordering', graph_ordering)]:
                f = p / (name + '.' + extension)
                f.write_text(source)
                subprocess.run([compiler, '-std=' + standard, '-O2', '-Wall', '-Werror', *flags,
                                '-I' + str(root / 'third_party/libultraship/include'),
                                str(f), '-o', str(p / 'test')], check=True)
                subprocess.run([str(p / 'test')], check=True)
                print(f'{standard} {"3DS" if flags else "portable"} {name}: pass', flush=True)
print('C/C++ game profiling: 100,000 disabled calls, early exits, toggles, absent hooks and non-3DS pass')
print('Production Graph_Update/RunFrame entry: debugger bypass has no token; normal update closes before rendering')
