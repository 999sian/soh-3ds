#!/usr/bin/env python3
"""Verify the production wall-check preflight initializes outputs when bypassed."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / 'third_party/shipwright/soh/src/code/z_bgcheck.c').read_text()
start = src.index('s32 BgCheck_CheckWallImpl(')
end = src.index('    dx = posNext->x - posPrev->x;', start)
# The collision solver after this preflight is intentionally not exercised:
# No Clip must return before touching that solver or collision geometry.
preflight = src[start:end] + '\n    return result;\n}\n'
code = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef int32_t s32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef float f32;
typedef struct { float x,y,z; } Vec3f;
typedef struct { int unused; } StaticLookup;
typedef struct { int unused; } CollisionPoly;
typedef struct { StaticLookup* lookupTbl; } CollisionContext;
typedef struct { int unused; } Actor;
enum { BGCHECK_SCENE=50, VB_PERFORM_WALL_COLLISION_CHECK=1 };
static bool enabled;
static bool GameInteractor_Should(int event, bool fallback, Actor* actor) {
    (void)event; (void)fallback; (void)actor; return enabled;
}
''' + preflight + r'''
int main(void) {
    CollisionPoly oldWall;
    Vec3f next={12.5f, 99.0f, -42.0f}, prev={10,98,-41};
    CollisionContext context={NULL};
    for (int run=0; run<2; ++run) {
        enabled=run!=0;
        Vec3f result={-999,-999,-999};
        CollisionPoly* wall=&oldWall;
        s32 owner=7;
        assert(!BgCheck_CheckWallImpl(&context,0,&result,&next,&prev,10,&wall,&owner,NULL,10,0));
        assert(wall==NULL);
        assert(owner==BGCHECK_SCENE);
        assert(result.x==next.x && result.y==next.y && result.z==next.z);
    }
    // An aliasing result/next vector must also remain intact.
    enabled=false;
    CollisionPoly* wall=&oldWall;
    s32 owner=7;
    assert(!BgCheck_CheckWallImpl(&context,0,&next,&next,&prev,10,&wall,&owner,NULL,10,0));
    assert(next.x==12.5f && next.y==99.0f && next.z==-42.0f);
    assert(wall==NULL && owner==BGCHECK_SCENE);
    puts("No Clip bypass clears stale wall outputs and preserves the destination position");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-noclip-collision-') as directory:
    temp=Path(directory)
    (temp/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined','-no-pie',str(temp/'test.c'),'-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test')],check=True)
